# candleLight_fw_hpm_port 编译指导

本仓库是基于 HPM SDK 与 CherryUSB 的 candleLight / gs_usb 兼容 USB ⇄ CAN-FD 应用程序，目标平台为 HPMicro HPM5321/HPM5361。本文档说明如何配置环境并使用 CMake preset 完成编译。

> English version: [README_build.md](./README_build.md). 工程说明： [README.md](./README.md)（英文）/ [README_zh.md](./README_zh.md)（中文）。

---

## 1. 前置准备（必要步骤）

编译之前，**必须**具备以下三项：

1. HPM SDK 源码；
2. RISC-V 交叉编译工具链，且环境变量正确指向它；
3. `candleLight_fw/` 子模块已检出（本工程会编译 candleLight 的原始源码）。

> 工具链的**具体版本可以自行选择**，不强制与示例一致。但 SDK、工具链与子模块三者缺一不可。

### 1.1 获取 HPM SDK

```bash
git clone https://github.com/hpmicro/hpm_sdk.git ~/Code/SDK/hpm_sdk
```

### 1.2 获取 RISC-V 工具链

本工程使用 `riscv-none-elf` 工具链（由 preset 中的 `CUSTOM_TARGET_TRIPLET` 指定）。可从 xpack 下载对应版本：

```bash
# 示例：xpack riscv-none-elf-gcc 15.2.0-1
# 解压到 ~/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1
```

工具链目录应至少包含 `bin/riscv-none-elf-gcc` 等可执行文件。

### 1.3 检出子模块

candleLight 原始源码以 git 子模块形式保存。若 `candleLight_fw/` 为空：

```bash
git submodule update --init --recursive
```

---

## 2. 环境变量设置

`CMakePresets.json` 中**刻意不含任何与本机强相关的内容** —— 没有绝对编译器路径，也没有 `environment` 块。SDK 与工具链通过环境变量定位，从而保持 preset 简短、可复用（与 boot 工程采用同一套做法）。

将以下内容加入 `~/.bashrc`（或对应 shell 的配置文件），然后 `source ~/.bashrc` 使其生效：

```bash
export PATH="$HOME/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"
export HPM_SDK_BASE="$HOME/Code/SDK/hpm_sdk"
export GNURISCV_TOOLCHAIN_PATH="$HOME/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1"
```

| 变量 | 作用 |
| --- | --- |
| `PATH` | 让 `cmake` / `ninja` 以及 `riscv-none-elf-*` 编译器可被直接调用。 |
| `HPM_SDK_BASE` | `CMakeLists.txt` 通过 `find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})` 定位 SDK。 |
| `GNURISCV_TOOLCHAIN_PATH` | HPM SDK 内部据此寻找 RISC-V 工具链。 |

验证环境：

```bash
echo $HPM_SDK_BASE
echo $GNURISCV_TOOLCHAIN_PATH
which riscv-none-elf-gcc cmake ninja
```

一次干净配置应能同时报出两者，例如：

```
-- Found toolchain: gnu (/home/you/Code/tools/xpack-riscv-none-elf-gcc-15.2.0-1)
-- hpm_sdk: 1.13.0 (/home/you/Code/SDK/hpm_sdk)
```

---

## 3. 使用 CMake Preset 编译（推荐）

请在**仓库根目录**执行以下命令。

### 3.1 配置并构建

```bash
# HPM5321 USB2CAN dongle
cmake --preset hpm5321_usb2can-dfu
cmake --build --preset hpm5321_usb2can-dfu

# HSCanT
cmake --preset hscant-dfu
cmake --build --preset hscant-dfu
```

预设默认是 Release；每块板子同时提供 Debug 变体：

```bash
cmake --preset hscant-dfu-debug
cmake --build --preset hscant-dfu-debug
```

或直接用 `ninja` 指定 jobs：

```bash
ninja -C build/hscant-dfu -j8
```

如需强制一次全新配置（丢弃 CMake cache，保留源码）—— 切换工具链后很有用：

```bash
cmake --preset hscant-dfu --fresh     # 需要 CMake >= 3.24
```

### 3.2 支持的 Board 与 Preset

| Board | Release preset | Debug preset | `BOARD` |
| --- | --- | --- | --- |
| HPM5321 USB2CAN dongle | `hpm5321_usb2can-dfu` | `hpm5321_usb2can-dfu-debug` | `hpm5321_usb2can` |
| HSCanT | `hscant-dfu` | `hscant-dfu-debug` | `hscant` |

Release 预设继承隐藏的 `base`（`HPM_BUILD_TYPE=flash_dfu`、`CMAKE_BUILD_TYPE=Release`）；`-debug` 预设继承 `base-debug`。查看全部 preset：

```bash
cmake --list-presets
```

### 3.3 `flash_dfu` 的含义

这是**应用程序镜像**，不是独立固件：

- 链接在 `0x80020000`，并带 `.dfu_signature`；
- 由位于 `0x80000000` 的引导程序 [`hpm_dfu_boot`](../hpm_dfu_boot) 启动。

必须先烧录 bootloader。`HPM_BUILD_TYPE=flash_xip`（独立运行于 `0x80000000`）在本工程**没有任何 preset 使用** —— 本 APP 只作为 DFU 镜像构建。

---

## 4. 编译产物

```
build/<preset>/output/candle_usb2canfd.elf
build/<preset>/output/candle_usb2canfd.hex
build/<preset>/output/candle_usb2canfd.bin
```

- `.elf`：用于调试 / OpenOCD 烧录；
- `.hex`：Intel HEX 格式，便于烧录工具写入；
- `.bin`：裸镜像，**就是喂给 `dfu-util -D` 的文件**。

工程配置要点（见 `CMakeLists.txt`）：

- 架构 `rv32imac_zicsr_zifencei`，ABI `ilp32`；
- `_flash_size=1M`、`_noncacheable_size=256K`；
- `-DBOARD_<name> -DSTM32G4` —— `STM32G4` 仅作为**选择开关**，用于启用 candleLight 的 FDCAN 代码路径（`can.h`、`device.h`），并不编译任何 STM32 HAL；
- `sdk_ld_options("--specs=nano.specs")` 配合 `HPM_SDK_LD_NO_NANO_SPECS=1`，避免 SDK 强制 `-u _printf_float` / `-u _scanf_float`；
- 配置阶段生成 `version.h`（含 `GIT_HASH`），candleLight 的 `config.h` 需要它；
- Release 构建额外添加 `-DNDEBUG`。

### 4.1 关于 `board.h` 与自动生成的 include 目录

构建时会生成 `build/<preset>/candle_include/`，里面是 **`candleLight_fw/include/` 下除 `board.h` 之外所有头文件的符号链接**。这是刻意为之：`board.h` 必须解析到板级包（`boards/<BOARD>/board.h`），而 candleLight 自己的板抽象则通过 `board_layout.h` 访问。**不要**试图通过改名来"修好"它。

---

## 5. 不使用 Preset 的手动编译

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

`BOARD` 是必填项 —— 否则 `CMakeLists.txt` 会以 `BOARD is not set. Use: cmake --preset <preset>` 直接终止。手动编译同样需要第 2.2 节中的 `HPM_SDK_BASE` / `GNURISCV_TOOLCHAIN_PATH` 环境变量。

---

## 6. 新增一块板子

`port/` 与 `src/` 中的代码无需任何改动。共三步：

1. 新建 `boards/<name>/`，至少包含 `board.h`、`board.c`、`pinmux.c`。所需宏与函数在 **`port/board_contract.h`** 中列出并在编译期强制检查（`BOARD_CAN_COUNT`、`BOARD_CAN_INSTANCES`、`BOARD_CAN_CLOCKS`、`BOARD_USB_BASE`、`BOARD_USB_IRQn`、`BOARD_BGPR`、`BOARD_HAS_SYS_LED`，以及 `board_init()` / `board_init_usb()` / `board_delay_ms()` / `init_can_pins()`）。`board_init_can_clock()` 在 `src/port_board.c` 中有弱符号默认实现。
2. 在 `candleLight_fw/include/config.h` 中增加 `BOARD_<name>` 块 —— USB 字符串、`NUM_CAN_CHANNEL`、`CAN_CLOCK_SPEED`、`CONFIG_CANFD`（若该板确实有可切换的终端电阻，再加 `TERM_Pin`）。
3. 在 `CMakePresets.json` 中增加一个带 `-DBOARD=<name>` 的预设。

`BOARD_CAN_COUNT` 与 `NUM_CAN_CHANNEL` 应保持一致。

---

## 7. 烧录与验证

1. 用调试器把 bootloader（`hpm_dfu_boot`）烧到 `0x80000000`。
2. 进入 DFU 模式：上电时按住 Boot 引脚，或让运行中的 APP 主动 detach。
3. 写入应用程序：

```bash
# -d 的形式为 vendor[:product]；bootloader 的 PID 并未固定，因此只匹配
# HPMicro 的 VID（0x34b7）即可 —— PID 变更时命令无需修改
dfu-util.exe -d 34b7 -a 0 -s 0x80020000:leave -D build/hscant-dfu/output/candle_usb2canfd.bin
```

4. Linux 下设备应作为 `gs_usb` 接口出现，`candump` / `cansend` 可在新增的 `canN` 网卡上使用；Windows 下无需任何驱动（WCID → WinUSB）。

在假定 CAN 转发可用之前，请先看 [README_zh.md](./README_zh.md) 中的硬件验证状态表。

---

## 8. 常见问题

- **`BOARD is not set. Use: cmake --preset <preset>`**
  未选用任何 preset，或手动编译时未传 `-DBOARD=<board>`。

- **`HPM SDK not found` / `find_package(hpm-sdk) failed`**
  `HPM_SDK_BASE`（preset 或 shell 中）没有指向 SDK 根目录。

- **找不到 `riscv-none-elf-gcc`**
  工具链 `bin` 目录未加入 `PATH`，或 `GNURISCV_TOOLCHAIN_PATH` 未指向工具链根目录。preset 中刻意不含任何编译器路径，因此这永远是环境问题，而非预设问题。

- **子模块目录为空 / 找不到 `can.h`**
  执行 `git submodule update --init --recursive`。

- **IDE 报一片 `'hpm_soc.h' file not found` / `undeclared identifier`，但 `cmake --build` 正常通过**
  编辑器没有加载交叉编译的 include 路径。preset 中 `CMAKE_EXPORT_COMPILE_COMMANDS` 已为 `ON`，让语言服务器指向 `build/<preset>/compile_commands.json` 即可（例如放一个 `.clangd`，内容为 `CompileFlags: { CompilationDatabase: build/<preset> }`）。这些都是误报。
