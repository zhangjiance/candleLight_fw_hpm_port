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

### 1.4 DFU bootloader（烧录需要，编译不需要）

DFU 镜像是被引导程序启动的应用程序，配套的 bootloader 来自
[`zhangjiance/hpm_dfu_boot`](https://github.com/zhangjiance/hpm_dfu_boot.git)，
需要先烧写到 `0x80000000`：

```bash
git clone https://github.com/zhangjiance/hpm_dfu_boot.git
```

---

## 2. 环境变量设置

`CMakePresets.json` 中**刻意不含任何与本机强相关的内容** —— 没有绝对编译器路径，也没有 `environment` 块。SDK 与工具链通过环境变量定位，从而保持 preset 简短、可复用（与 [boot 工程](https://github.com/zhangjiance/hpm_dfu_boot.git)采用同一套做法）。

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
# DFU 镜像（Release）—— 配合 hpm_dfu_boot 引导程序
cmake --preset hscant-dfu
cmake --build --preset hscant-dfu

# UF2 镜像（Release）—— 配合设备原版 UF2/MSC boot
cmake --preset hscant-uf2
cmake --build --preset hscant-uf2

# HSCanT-Lite 只有 DFU 镜像，没有 UF2
cmake --preset hscant-lite-dfu
cmake --build --preset hscant-lite-dfu
```

预设默认是 Release；每种形态同时提供 Debug 变体：

```bash
cmake --preset hscant-uf2-debug
cmake --build --preset hscant-uf2-debug
```

或直接用 `ninja` 指定 jobs：

```bash
ninja -C build/hscant-uf2 -j8
```

如需强制一次全新配置（丢弃 CMake cache，保留源码）—— 切换工具链后很有用：

```bash
cmake --preset hscant-uf2 --fresh     # 需要 CMake >= 3.24
```

### 3.2 支持的 Board 与 Preset

| Board | DFU Release | DFU Debug | UF2 Release | UF2 Debug | `BOARD` |
| --- | --- | --- | --- | --- | --- |
| HSCanT | `hscant-dfu` | `hscant-dfu-debug` | `hscant-uf2` | `hscant-uf2-debug` | `hscant` |
| HSCanT-Lite | `hscant-lite-dfu` | `hscant-lite-dfu-debug` | — | — | `hscant-lite` |
| hpm_sdk EVK（MCAN + USB） | `<evk>-dfu` | `<evk>-dfu-debug` | — | — | `<evk>` |

`hscant-dfu` 预设继承隐藏的 `base`（`HPM_BUILD_TYPE=flash_dfu`）；`hscant-uf2` 预设继承 `base-uf2`（`HPM_BUILD_TYPE=flash_uf2`）；`-debug` 变体分别继承 `base-debug` / `base-uf2-debug`。查看全部 preset：

```bash
cmake --list-presets
```

#### hpm_sdk EVK 开发板

EVK 的板级包**不会复制进本仓库**：`BOARD` 直接选中 `$HPM_SDK_BASE/boards/<BOARD>`，应用统一以 `BOARD_hpmicro_evk` 编译。凡是 CAN 控制器使用 **MCAN** IP、并且带有片上 **USB device** 的 EVK，都可作为单通道 candle 设备使用：

| EVK | `BOARD` | MCAN 实例 |
| --- | --- | --- |
| HPM5100EVK | `hpm5100evk` | MCAN0 |
| HPM5300EVK | `hpm5300evk` | MCAN3 |
| HPM5E00EVK | `hpm5e00evk` | MCAN1 |
| HPM6200EVK | `hpm6200evk` | MCAN0 |
| HPM6800EVK | `hpm6800evk` | MCAN3 |
| HPM6E00EVK | `hpm6e00evk` | MCAN4 |
| HPM6P00EVK | `hpm6p00evk` | MCAN1 |

每块板都有 `<board>-dfu` / `<board>-dfu-debug` 预设，它们之间唯一不同的就是 `BOARD`：

```bash
cmake --preset hpm6800evk-dfu && cmake --build --preset hpm6800evk-dfu
# 换一块 EVK 只是换一个 BOARD（以及对应 preset）
cmake -B build/myevk -G Ninja -DBOARD=hpm6e00evk \
  -DHPM_BUILD_TYPE=flash_dfu -DCMAKE_BUILD_TYPE=Release \
  -DCUSTOM_TARGET_TRIPLET=riscv-none-elf
```

EVK 在本工程中**只使用 DFU**（不生成 UF2）：本工程唯一兼容的 UF2 boot 是 HSCanT 的闭源 boot，其它板卡需要先自行实现配套的 UF2 boot 工程（见 [3.3](#33-两种镜像形态)）。HPM6300EVK、HPM6750EVK2、HPM6750EVKMINI **不支持**：它们的 CAN 控制器是老的 bxCAN IP，不是 MCAN，配置阶段会直接给出说明并终止。

> **HSCanT-Lite** 的 `BOARD_hscant_lite` 分支与板级包已就位，`hscant-lite-dfu` / `hscant-lite-dfu-debug` preset 可正常构建。该板同样只使用 **DFU**。4 路 CAN、8 颗 GPIO 指示灯（每通道状态灯 + 错误灯）和 PA03 BOOT 按键的实机功能仍建议上板验证。

### 3.3 两种镜像形态

两者都是**应用程序镜像**（不是独立固件），均链接在 `0x80020000`，前面有 4 字节签名，由位于 `0x80000000` 的引导程序启动；区别在于签名与「请求引导程序」的方式：

- **`flash_dfu`** —— 携带 `.dfu_signature`，由 [`hpm_dfu_boot`](https://github.com/zhangjiance/hpm_dfu_boot.git) 启动。APP 通过保留寄存器写入 magic 请求进入 DFU 模式（`src/dfu_hpm.c`）。
- **`flash_uf2`** —— 携带 `.uf2_signature`（`BOARD_UF2_SIGNATURE = 0x0A4D5048`），由 UF2/MSC 引导程序（设备原版 boot）启动。构建时还会用 `tools/uf2conv.py` 生成 `<APP_NAME>.uf2`（family `0x0A4D5048`、基址 `0x80020000`），可直接拖入 boot 暴露出的 MSC 盘符。APP 通过在共享的 `bl_setting` 块（`src/bl_setting.c`）中置 `keep_bootloader`，再经 ROM 引导流程（`ROM_API_TABLE_ROOT->run_bootloader`）重新进入来请求引导程序。

每个形态搭配不同的 bootloader：

| 预设形态 | 配套 boot |
| --- | --- |
| `hscant-dfu` | 由 [`zhangjiance/hpm_dfu_boot`](https://github.com/zhangjiance/hpm_dfu_boot.git) 编译 |
| `hscant-uf2` | HSCanT 设备**原版闭源** UF2/MSC boot —— 修改自 CherryDAP 的 [HSLink-Pro bootloader](https://github.com/cherry-embedded/CherryDAP/tree/master/projects/HSLink-Pro/bootloader)。没有它的源码也能保持兼容；恢复出的契约见 [主 README](README_zh.md#引导程序与镜像兼容性)。 |

> **UF2 仅限 HSCanT，这是刻意为之。** HSCanT 出厂自带的 UF2/MSC boot 是闭源的；
> 本工程只为这块已经摸清兼容契约的板子保留 `flash_uf2` 形态。HSCanT-Lite 与
> hpm_sdk EVK **只有 DFU 预设**。UF2 镜像格式本身并不限制板卡
> （可以传 `-DHPM_BUILD_TYPE=flash_uf2` 编译），但目前没有能消费它的 UF2 boot：
> 需要用户参考 hpm_sdk 的 USB/MSC/bootloader 示例自行编写 boot 工程，并与本 APP
> 约定好 APP 基址、`BOARD_UF2_SIGNATURE`、UF2 family 和 `bl_setting` 契约。
> 在没有这样的 boot 之前，请使用 DFU 预设。

`HPM_BUILD_TYPE=flash_xip`（独立运行于 `0x80000000`）在本工程**没有任何 preset 使用**。

---

## 4. 编译产物

```
build/<preset>/output/candle_usb2canfd.elf
build/<preset>/output/candle_usb2canfd.hex
build/<preset>/output/candle_usb2canfd.bin
build/<preset>/output/candle_usb2canfd.uf2   # 仅 flash_uf2 预设
```

- `.elf`：用于调试 / OpenOCD 烧录；
- `.hex`：Intel HEX 格式，便于烧录工具写入；
- `.bin`：裸镜像，**就是喂给 `dfu-util -D` 的文件**；
- `.uf2`：UF2 镜像，**拖入 boot 暴露的 MSC 盘符即可烧录**。

工程配置要点（见 `CMakeLists.txt`）：

- 架构 `rv32imac_zicsr_zifencei`，ABI `ilp32`；
- `_flash_size` / `_noncacheable_size` 由 hpm_sdk 的 board YAML 与 SoC CMakeLists 决定（本工程不再强制，因为 HPM5E00EVK 等板的 AXI SRAM 较小）；
- `-DBOARD_<name> -DSTM32G4` —— `STM32G4` 仅作为**选择开关**，用于启用 candleLight 的 FDCAN 代码路径（`can.h`、`device.h`），并不编译任何 STM32 HAL；对 EVK 而言，选择开关固定为 `BOARD_hpmicro_evk`，`<name>` 则是 SDK 板名；
- `sdk_ld_options("--specs=nano.specs")` 配合 `HPM_SDK_LD_NO_NANO_SPECS=1`，避免 SDK 强制 `-u _printf_float` / `-u _scanf_float`；
- 配置阶段生成 `version.h`（含 `GIT_HASH`），candleLight 的 `config.h` 需要它；
- Release 构建额外添加 `-DNDEBUG`。

### 4.1 关于 `board.h` 与自动生成的 include 目录

构建时会生成 `build/<preset>/candle_include/`，里面是 **`candleLight_fw/include/` 下除 `board.h` 之外所有头文件的符号链接**。这是刻意为之：`board.h` 必须解析到板级包（本仓库的 `boards/<BOARD>/board.h`，或 EVK 的 `$HPM_SDK_BASE/boards/<BOARD>/board.h`），而 candleLight 自己的板抽象则通过 `board_layout.h` 访问。**不要**试图通过改名来"修好"它。

对 EVK 而言，`boards/hpmicro_evk/hpmicro_evk.h` 会被强制包含（`-include`）到每个应用源文件中：它先包含 SDK 的 `board.h`，再在其上定义 candle 所需的契约宏（`BOARD_CAN_COUNT`、`BOARD_CAN_INSTANCES`、`BOARD_USB_BASE` 等）。SDK 本身既不打补丁，也不复制。

---

## 5. 不使用 Preset 的手动编译

```bash
cmake -B build/manual \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBOARD=hscant \
  -DHPM_BUILD_TYPE=flash_dfu \
  -DCUSTOM_TARGET_TRIPLET=riscv-none-elf

cmake --build build/manual -j8
```

`BOARD` 是必填项 —— 否则 `CMakeLists.txt` 会以 `BOARD is not set. Use: cmake --preset <preset>` 直接终止。手动编译同样需要第 2.2 节中的 `HPM_SDK_BASE` / `GNURISCV_TOOLCHAIN_PATH` 环境变量。

---

## 6. 新增一块板子

`port/` 与 `src/` 中的代码无需任何改动。

**对于 hpm_sdk 里的 EVK**，本仓库不需要新增任何文件：把 `BOARD` 设为 SDK 板名（如 `hpm6800evk`），再加一个同样设置的 preset 即可。`CMakeLists.txt` 会检查该 SDK 板级包是否带 MCAN 控制器（`BOARD_APP_CAN_BASE HPM_MCAN...`）以及 `board_init_usb()`，然后以 `BOARD_hpmicro_evk` 编译，并强制包含 `boards/hpmicro_evk/hpmicro_evk.h`。

**对于需要自带板级包的板子**（自定义板或非 EVK 设计）：

1. 新建 `boards/<name>/`，至少包含 `board.h`、`board.c`、`pinmux.c`。所需宏与函数在 **`port/board_contract.h`** 中列出并在编译期强制检查（`BOARD_CAN_COUNT`、`BOARD_CAN_INSTANCES`、`BOARD_USB_BASE`、`BOARD_USB_IRQn`、`BOARD_HAS_SYS_LED`，BCFG 类 SoC 还需要 `BOARD_BGPR`，以及 `board_init()` / `board_init_usb()` / `board_delay_ms()` / `init_can_pins()`）。`board_init_can_clock()` 由板级包提供。
2. 在 `candleLight_fw/include/config.h` 中增加 `BOARD_<name>` 块 —— USB 字符串、`NUM_CAN_CHANNEL`、`CAN_CLOCK_SPEED`、`CONFIG_CANFD`（若该板确实有可切换的终端电阻，再加 `TERM_Pin`）。
3. 在 `CMakePresets.json` 中增加一个带 `-DBOARD=<name>` 的预设。

`BOARD_CAN_COUNT` 与 `NUM_CAN_CHANNEL` 应保持一致。

---

## 7. 烧录与验证

1. 用调试器把 bootloader（[`hpm_dfu_boot`](https://github.com/zhangjiance/hpm_dfu_boot.git)）烧到 `0x80000000`。
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
