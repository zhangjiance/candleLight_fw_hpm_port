# candleLight_fw_hpm_port Build Guide

This repository is a candleLight / gs_usb compatible USB ⇄ CAN-FD application for HPMicro HPM5321/HPM5361, built on the HPM SDK and CherryUSB's USB device stack. This document explains how to set up the environment and build with CMake presets.

> 中文版见 [README_build_zh.md](./README_build_zh.md)。Project overview: [README.md](./README.md) (English) / [README_zh.md](./README_zh.md) (中文).

---

## 1. Prerequisites (required)

Before building you **must** have all three of the following:

1. The HPM SDK source tree;
2. The RISC-V cross-compilation toolchain, with the environment variables pointing at it;
3. The `candleLight_fw/` submodule checked out (this project builds candleLight's original sources).

> The exact toolchain version is up to you — it is not pinned. But the SDK, the toolchain and the submodule are all indispensable.

### 1.1 Get the HPM SDK

```bash
git clone https://github.com/hpmicro/hpm_sdk.git ~/Code/SDK/hpm_sdk
```

### 1.2 Get the RISC-V toolchain

The presets use the `riscv-none-elf` toolchain (`CUSTOM_TARGET_TRIPLET`). Download a matching version, e.g. from xpack:

```bash
# Example: xpack riscv-none-elf-gcc 15.2.0-1
# Extract to ~/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1
```

The directory must contain `bin/riscv-none-elf-gcc` and the other executables.

### 1.3 Check out the submodule

The original candleLight sources are kept as a git submodule. If `candleLight_fw/` is empty:

```bash
git submodule update --init --recursive
```

---

## 2. Environment Variables

`CMakePresets.json` deliberately contains **nothing machine specific** — no absolute compiler paths, no `environment` block. The SDK and the toolchain are located through environment variables, which keeps the presets short and portable (the same approach as the bootloader project).

Add the following to `~/.bashrc` (or your shell's config file), then `source ~/.bashrc`:

```bash
export PATH="$HOME/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"
export HPM_SDK_BASE="$HOME/Code/SDK/hpm_sdk"
export GNURISCV_TOOLCHAIN_PATH="$HOME/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1"
```

| Variable | Purpose |
| --- | --- |
| `PATH` | Makes `cmake` / `ninja` and the `riscv-none-elf-*` compilers directly callable. |
| `HPM_SDK_BASE` | `CMakeLists.txt` locates the SDK via `find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})`. |
| `GNURISCV_TOOLCHAIN_PATH` | Used internally by the HPM SDK to find the RISC-V toolchain. |

Verify:

```bash
echo $HPM_SDK_BASE
echo $GNURISCV_TOOLCHAIN_PATH
which riscv-none-elf-gcc cmake ninja
```

A clean configure then reports both, e.g.:

```
-- Found toolchain: gnu (/home/you/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1)
-- hpm_sdk: 1.13.0 (/home/you/Code/SDK/hpm_sdk)
```

---

## 3. Build with CMake Presets (recommended)

Run the commands **from the repository root**.

### 3.1 Configure and build

```bash
# DFU image (Release) - pairs with the hpm_dfu_boot bootloader
cmake --preset hscant-dfu
cmake --build --preset hscant-dfu

# UF2 image (Release) - pairs with the device's stock UF2/MSC boot
cmake --preset hscant-uf2
cmake --build --preset hscant-uf2
```

Presets are Release by default; each flavour also has a Debug variant:

```bash
cmake --preset hscant-uf2-debug
cmake --build --preset hscant-uf2-debug
```

Or drive `ninja` directly:

```bash
ninja -C build/hscant-uf2 -j8
```

To force a clean configure (drops the CMake cache but keeps the sources) — handy after switching toolchains:

```bash
cmake --preset hscant-uf2 --fresh     # needs CMake >= 3.24
```

### 3.2 Supported Boards and Presets

| Board | DFU release | DFU debug | UF2 release | UF2 debug | `BOARD` |
| --- | --- | --- | --- | --- | --- |
| HSCanT | `hscant-dfu` | `hscant-dfu-debug` | `hscant-uf2` | `hscant-uf2-debug` | `hscant` |
| HSCanT-Lite | — (not yet tested) | — | — | — | `hscant-lite` |

The `hscant-dfu` presets inherit the hidden `base` preset (`HPM_BUILD_TYPE=flash_dfu`); the `hscant-uf2` presets inherit `base-uf2` (`HPM_BUILD_TYPE=flash_uf2`). The `-debug` variants inherit `base-debug` / `base-uf2-debug`. List them with:

```bash
cmake --list-presets
```

> **HSCanT-Lite is not yet tested**, so it intentionally has **no preset**. The `boards/hscant-lite/` package is in place, but building it also needs a `BOARD_hscant_lite` block in `candleLight_fw/include/config.h` (an untouched submodule); once that is added, configure it with `-DBOARD=hscant-lite -DHPM_BUILD_TYPE=flash_dfu`.

### 3.3 Boot image flavours

Both flavours are **application images** (not standalone firmware), linked at `0x80020000` behind a 4-byte signature and started by a bootloader at `0x80000000`. They differ in the signature and in how the application requests the bootloader:

- **`flash_dfu`** — carries a `.dfu_signature` and is started by [`hpm_dfu_boot`](https://github.com/zhangjiance/hpm_dfu_boot.git). The application requests DFU mode through a retention-register magic (`src/dfu_hpm.c`).
- **`flash_uf2`** — carries a `.uf2_signature` (`BOARD_UF2_SIGNATURE = 0x0A4D5048`) and is started by a UF2/MSC bootloader (the stock one shipped on the device). The build also emits `<APP_NAME>.uf2` (family `0x0A4D5048`, base `0x80020000`) through `tools/uf2conv.py`, ready to be dropped onto the exposed MSC volume. The application requests the bootloader by setting `keep_bootloader` in the shared `bl_setting` block (`src/bl_setting.c`) and re-entering the ROM boot flow (`ROM_API_TABLE_ROOT->run_bootloader`).

Each flavour pairs with a different bootloader:

| Preset flavour | Bootloader |
| --- | --- |
| `hscant-dfu` | built from [`zhangjiance/hpm_dfu_boot`](https://github.com/zhangjiance/hpm_dfu_boot.git) |
| `hscant-uf2` | the HSCanT device's original, closed-source UF2/MSC boot — a derivative of CherryDAP's [HSLink-Pro bootloader](https://github.com/cherry-embedded/CherryDAP/tree/master/projects/HSLink-Pro/bootloader). No boot sources are needed to stay compatible with it; the recovered contract is documented in the [main README](README.md#bootloaders-and-image-compatibility). |

`HPM_BUILD_TYPE=flash_xip` (standalone at `0x80000000`) is **not** used by any preset here.

---

## 4. Build Artifacts

```
build/<preset>/output/candle_usb2canfd.elf
build/<preset>/output/candle_usb2canfd.hex
build/<preset>/output/candle_usb2canfd.bin
build/<preset>/output/candle_usb2canfd.uf2   # flash_uf2 presets only
```

- `.elf` — for debugging / OpenOCD flashing;
- `.hex` — Intel HEX, for flashing tools;
- `.bin` — raw image, **this is what you feed to `dfu-util -D`**;
- `.uf2` — UF2 image, **this is what you drop onto the bootloader's MSC volume**.

Key build configuration (see `CMakeLists.txt`):

- Architecture `rv32imac_zicsr_zifencei`, ABI `ilp32`;
- `_flash_size=1M`, `_noncacheable_size=256K`;
- `-DBOARD_<name> -DSTM32G4` — `STM32G4` is only used as a **selector** for candleLight's FDCAN code paths (`can.h`, `device.h`); no STM32 HAL is compiled;
- `sdk_ld_options("--specs=nano.specs")` plus `HPM_SDK_LD_NO_NANO_SPECS=1`, so the SDK does not force `-u _printf_float` / `-u _scanf_float`;
- `version.h` is generated at configure time with `GIT_HASH` (candleLight's `config.h` needs it);
- `-DBL_SETTING_APP_VERSION=x.y.z` (default `0.0.0`) is the application version published to the bootloader through the shared `bl_setting` block; the release workflow passes the git tag, locally you can pass it yourself (`v1.0.9` and `1.1.0-rc1` are accepted, the tag without its `v` is what ends up in the block);
- Release builds add `-DNDEBUG`.

### 4.1 A note on `board.h` and the generated include directory

The build creates `build/<preset>/candle_include/`, containing **symlinks to every header in `candleLight_fw/include/` except `board.h`**. That is deliberate: `board.h` must resolve to the board package (`boards/<BOARD>/board.h`), while candleLight's own board abstraction is reachable as `board_layout.h`. Do not "fix" this by renaming files.

---

## 5. Manual Build without Presets

```bash
cmake -B build/manual \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBOARD=hscant \
  -DHPM_BUILD_TYPE=flash_dfu \
  -DCUSTOM_TARGET_TRIPLET=riscv-none-elf \
  -DCMAKE_TOOLCHAIN_FILE=$HPM_SDK_BASE/cmake/toolchain/riscv_none_elf_gcc.cmake

cmake --build build/manual -j8
```

`BOARD` is mandatory — `CMakeLists.txt` aborts with `BOARD is not set. Use: cmake --preset <preset>` otherwise. The `HPM_SDK_BASE` / `GNURISCV_TOOLCHAIN_PATH` environment variables from Section 2.2 are required for a manual build.

---

## 6. Adding a New Board

Nothing in `port/` or `src/` has to change. Three steps:

1. Create `boards/<name>/` with at least `board.h`, `board.c` and `pinmux.c`. The required macros and functions are listed and enforced in **`port/board_contract.h`** (`BOARD_CAN_COUNT`, `BOARD_CAN_INSTANCES`, `BOARD_CAN_CLOCKS`, `BOARD_USB_BASE`, `BOARD_USB_IRQn`, `BOARD_BGPR`, `BOARD_HAS_SYS_LED`, and `board_init()` / `board_init_usb()` / `board_delay_ms()` / `init_can_pins()`). `board_init_can_clock()` has a weak default in `src/port_board.c`.
2. Add a `BOARD_<name>` block to `candleLight_fw/include/config.h` — USB strings, `NUM_CAN_CHANNEL`, `CAN_CLOCK_SPEED`, `CONFIG_CANFD` (and `TERM_Pin` if the board really has a switchable terminator).
3. Add a preset in `CMakePresets.json` with `-DBOARD=<name>`.

`BOARD_CAN_COUNT` and `NUM_CAN_CHANNEL` should agree.

---

## 7. Flashing and Verifying

1. Flash the bootloader (`hpm_dfu_boot`) with a debugger to `0x80000000`.
2. Enter DFU mode: hold the boot pin at power-on, or ask the running application to detach.
3. Write the application:

```bash
# -d takes vendor[:product]; the bootloader's PID is not pinned, so match on the
# HPMicro VID (0x34b7) only -- this keeps working across PID changes
dfu-util.exe -d 34b7 -a 0 -s 0x80020000:leave -D build/hscant-dfu/output/candle_usb2canfd.bin
```

4. On Linux the device should appear as a `gs_usb` interface and `candump` / `cansend` should work on the new `canN` netdevices. On Windows no driver is needed (WCID → WinUSB).

See [README.md](./README.md) for the current hardware-validation status before assuming CAN forwarding works.

---

## 8. FAQ

- **`BOARD is not set. Use: cmake --preset <preset>`**
  No preset was selected, or `-DBOARD=<board>` was not passed for a manual build.

- **`HPM SDK not found` / `find_package(hpm-sdk) failed`**
  `HPM_SDK_BASE` (preset or shell) does not point at an SDK root.

- **`riscv-none-elf-gcc` not found**
  The toolchain `bin` directory is not on `PATH`, or `GNURISCV_TOOLCHAIN_PATH` does not point at the toolchain root. The presets contain no compiler paths on purpose, so this is always an environment problem, never a preset problem.

- **The submodule directory is empty / `can.h` not found**
  Run `git submodule update --init --recursive`.

- **The IDE shows a wall of `'hpm_soc.h' file not found` / `undeclared identifier` errors, but `cmake --build` succeeds**
  The editor is not using the cross-compile include paths. `CMAKE_EXPORT_COMPILE_COMMANDS` is already `ON`, so point your language server at `build/<preset>/compile_commands.json` (e.g. a `.clangd` file with `CompileFlags: { CompilationDatabase: build/<preset> }`). These are false positives.
