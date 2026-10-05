# candleLight_fw_hpm_port

基于 **HPMicro HPM5321/HPM5361 RISC-V MCU** 的 **candleLight / gs_usb 兼容 USB ⇄ CAN-FD 固件**，构建在 HPM SDK 与 CherryUSB 之上。

gs_usb 协议、USB 描述符，以及**最关键的 candleLight 帧链表引擎**，全部使用 candleLight 的**原始源码**；只有平台层是新写的。因此主机端用 Linux 自带的 `gs_usb` 驱动（或 `candle_usb` / `candump` / `can-utils`）看到的就是一个普通的 candleLight / CANable 设备。

> English overview: [README.md](./README.md). 编译指导： [README_build.md](./README_build.md)（英文）/ [README_build_zh.md](./README_build_zh.md)（中文）。

---

## 设备标识

| 项目 | 值 |
| --- | --- |
| USB VID:PID | `0x1d50:0x606f`（OpenMoko / candleLight） |
| USB 速度 | **高速，480 Mbit/s（512 B 批量端点）**；桥接层按主机协商结果提供 HS 或 FS 描述符。`main.c` 里的 `DEVICE_FS` 参数是无效的 —— `USBD_Init()` 会忽略它 —— 实际由 `src/usb_config.h` 默认选择 `CONFIG_USB_HS` |
| 接口 0 | `gs_usb`，厂商自定义类 `0xFF/0xFF/0xFF`，2 个批量端点 |
| 接口 1 | DFU runtime `0xFE/0x01/0x01`（仅 detach，无端点） |
| Windows 驱动 | **无需驱动** —— MS OS 1.0 / WCID 使 Windows 自动绑定 `winusb.sys` |
| CAN | 4 路 MCAN（兼容 Bosch M_CAN），支持 CAN-FD，CAN 时钟 80 MHz |
| Flash 布局 | `hpm_dfu_boot` 位于 `0x80000000`，本 APP 位于 `0x80020000` |

## 实现状态

"已测试"指在真实硬件上跑过。提 issue 前请先看这张表。

| 功能 | 状态 |
| --- | --- |
| 与 `hpm_dfu_boot` 配合的 DFU 烧录 | **已在硬件上验证** |
| USB 枚举（高速，Windows 下 WCID 免驱） | **已在硬件上验证** |
| CAN / CAN-FD 帧转发 | **已在硬件上验证** —— 经典帧与 CAN-FD BRS、双向，既对过 PEAK PCAN，也做过通道互连 |
| 多通道（4 路 MCAN） | **已在硬件上验证** —— 四路全测，含三节点共总线 |
| 位定时 / 采样点 | **已验证** —— 主机协商的采样点确实落进 `NBTP`/`DBTP`（500k/2M 与 1M/4M @80% 均核对过） |
| 四通道终端电阻开关 | **已在硬件上验证** —— `GS_CAN_FEATURE_TERMINATION`，用 `ip link set canX type can termination 120` 控制 |
| 总线错误上报（`CAN_ERR_ACK` / `PROT` / `CRTL`，TEC/REC） | 已实现；各类错误都在故意做坏的总线上实际触发过，但系统化回归还没做 |
| WS2812 收发指示灯驱动 | **未实现** |
| 吞吐 / 压力测试 | 未做 |

命令面（`BITTIMING`、`DATA_BITTIMING`、`MODE`、`BT_CONST(_EXT)`、`DEVICE_CONFIG`、`TIMESTAMP`、`IDENTIFY`、`HOST_FORMAT`、TX 回声、时间戳，以及 `LOOP_BACK` / `LISTEN_ONLY` / `ONE_SHOT` / `HW_TIMESTAMP` / `PAD_PKTS`）是完整的，且枚举、DFU、帧转发与终端电阻控制都已端到端跑过。

### 已知缺口 / 待办

| 项目 | 还差什么 |
| --- | --- |
| 错误上报回归 | 逐类系统性验证：`CAN_ERR_ACK`、`PROT`（stuff / form / bit / CRC）、`CRTL`（warning、passive）、`BUSOFF`、RX overrun，以及 payload 里的 TEC/REC 和"仅在 LEC 变化时上报"的行为 |
| 性能测试 | USB 高速下的帧速率、多通道同时打流、`RX dropped/missed`、TX 延迟。USB 已经是高速，所以瓶颈在 CAN 波特率与每帧的主机侧开销，而不是链路 |
| WS2812 收发指示灯 | 尚未实现；`boards/hscant/` 目前只驱动 GPIOB[10] 上的 `SYS_LED` |

### 向主机上报的特性位

```37:46:src/can_hpm.c
        GS_CAN_FEATURE_LISTEN_ONLY |
        GS_CAN_FEATURE_LOOP_BACK |
        GS_CAN_FEATURE_HW_TIMESTAMP |
        GS_CAN_FEATURE_IDENTIFY |
        GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE |
        (IS_ENABLED(CONFIG_CANFD) ?
         GS_CAN_FEATURE_FD | GS_CAN_FEATURE_BT_CONST_EXT : 0)
#ifdef TERM_Pin
        | GS_CAN_FEATURE_TERMINATION
#endif
```

`GS_CAN_FEATURE_TERMINATION` 对 `BOARD_hscant` **是会上报的**：那里定义了 `TERM_Pin`，从而选用 `set_term()` / `get_term()`（本移植没有编译上游的 `src/gpio.c`，所以这两个钩子实现在 `src/port_board.c`），并经 `config.termination_set` 走到板级的 `board_can_set_termination()`（每通道一个 `CANx_RES`）。主机用法：`ip link set canX type can termination 120`（或 `termination 0`），且只能在接口 down 时修改。复位后终端电阻是断开的，与 `pinmux.c` 给 `CANx_RES` 的电平一致。

---

## 架构

设计目标很简单：**保持 candleLight 的应用逻辑逐字节不变，把所有平台依赖收敛到一条窄接缝上**，这样上游对帧引擎或协议的修改可以直接靠拷贝文件吸收。

```
                       ┌──────────────────────────────────────────┐
  candleLight_fw       │  src/candle/main.c        （副本，仅接缝）│
  （git 子模块，        │  src/candle/usbd_gs_can.c （副本，仅接缝）│
    参考实现）          │  src/can_common.c   （原样编译）         │
                       │  src/led.c, util.c, usbd_desc.c（原样）  │
                       └──────────────────┬───────────────────────┘
                                          │  STM32 HAL + STM32 USBD API
                       ┌──────────────────▼───────────────────────┐
  port/（接缝层）       │  hal_include.h    HAL/CMSIS/GPIO 垫片    │
  不含硬件代码          │  cmsis_device.h   RISC-V CMSIS 垫片      │
                       │  usbd_compat.h    STM32 USBD 类型/原型   │
                       │  board_layout.h   candleLight 板抽象     │
                       │  board_contract.h 板包契约               │
                       └──────────────────┬───────────────────────┘
                                          │  实现
                       ┌──────────────────▼───────────────────────┐
  src/（HPMicro 实现）  │  usbd_bridge.c  CherryUSB ⇄ STM32 USBD   │
                       │  can_hpm.c      m_can.c → hpm_mcan_drv   │
                       │  timer.c  dfu_hpm.c  port_device.c       │
                       │  port_board.c                            │
                       └──────────────────┬───────────────────────┘
                                          │
                       ┌──────────────────▼───────────────────────┐
  boards/<BOARD>/      │  时钟树、引脚复用、CAN 时钟、LED、        │
                       │  DFU 签名、BGPR 选择                     │
                       └──────────────────────────────────────────┘
```

### 第 1 层 —— `candleLight_fw/`（原始代码）

| 文件 | 使用方式 |
| --- | --- |
| `src/can_common.c` | **原样编译** —— 帧链表池（`CAN_SendFrame` / `CAN_ReceiveFrame` / `CAN_HandleError`）、TX 回声、RX 队列 |
| `src/led.c`、`src/util.c`、`src/usbd_desc.c` | **原样编译** |
| `src/usbd_gs_can.c` | 复制到 `src/candle/`，**仅改动 `#include` 块** |
| `src/main.c` | 复制到 `src/candle/`，**仅改动 `#include` 块** |

之所以要拷贝这两个文件，原因只有一个：它们会 include 本平台不存在的 STM32 USB Device Library 头文件。include 块以下的部分一字未改，因此与子模块 `diff` 永远只有五行。

```42:45:src/candle/usbd_gs_can.c
 * are replaced by our CherryUSB-backed compatibility layer.  Everything else
 * in this file is an unmodified copy of candleLight_fw/src/usbd_gs_can.c.
 */
#include "usbd_compat.h"
```

### 第 2 层 —— `port/`（平台接缝）

**不含任何硬件代码**，只有契约与垫片：

- **`hal_include.h`** —— candleLight 头文件所期望的 STM32 风格 HAL：`GPIO_PIN_SET/RESET`、`HAL_GPIO_WritePin/TogglePin`、`HAL_GetTick`/`HAL_Delay`、`HAL_Init`、`UNUSED`，并把 `FDCAN_GlobalTypeDef`/`FDCAN_HandleTypeDef` 映射到 HPM 的 `MCAN_Type`。同时声明用于 USB 序列号的 `hpm_uid[12]` / `UID_BASE`。
- **`cmsis_device.h`** —— 原代码用到的少数 CMSIS 内建函数在 RISC-V 上的等价物：`__disable_irq`、`__enable_irq`、`__DMB`、`__BKPT`、`NVIC_SystemReset`。
- **`usbd_compat.h`** —— 取自 `usbd_def.h` 的 STM32 USB Device Library **类型、常量与原型**（`USBD_HandleTypeDef`、`USBD_ClassTypeDef`、整套 `USBD_LL_*` / `USBD_Ctl*`、描述符索引宏）。
- **`usbd_def.h`、`usbd_ctlreq.h`** —— 极薄的替身，让原代码的 `#include` 能解析。
- **`board_layout.h`** —— candleLight 的板抽象（`struct BoardConfig`、`config.channels[]`），改名以腾出 `board.h` 这个名字。
- **`board_contract.h`** —— 每个板包必须提供什么，用 `#error` 在编译期强制检查。
- **`usbd_bridge.h`** —— 固件其余部分唯一需要从桥接层获取的符号。

### 第 3 层 —— `src/`（HPMicro 实现）

| 文件 | 作用 |
| --- | --- |
| `usbd_bridge.c` | **兼容层** —— 详见下文 |
| `can_hpm.c` | `candleLight_fw/src/can/m_can.c` 到 `hpm_mcan_drv` 的移植 |
| `timer.c` | `timer_get()` = 基于 `mcycle` 的自由运行微秒计数器（不占用外设定时器，可在中断中安全读取） |
| `dfu_hpm.c` | `dfu_run_bootloader()` —— 写入 BGPR/PDGO magic 后重新进入 ROM 引导流程 |
| `port_device.c` | `HAL_Init()` 底层初始化（时钟树、USB PHY、D-Cache 策略）、GPIO 垫片、芯片 UUID 装载 |
| `port_board.c` | 弱符号默认 `board_init_can_clock()`，填充 `config.channels[].interface` |
| `usb_config.h` | CherryUSB 配置 |
| `candle/` | 上面那两个改造过的原始文件 |

### 第 4 层 —— `boards/<BOARD>/`

板级包：时钟树、引脚复用、CAN 时钟选择、LED、DFU 签名，以及 DFU 复位请求所用的保留寄存器。目前有两个包：`hscant` 与 `hpm5321_usb2can`。

---

## 兼容层详解

`src/usbd_bridge.c` 是"沿用原始源码"这一目标得以成立的关键，它是唯一同时了解两个世界的文件。

### 1. 在 CherryUSB 之上实现 STM32 USBD API

- **入口函数**（由原版 `main.c` 调用）：`USBD_Init()`、`USBD_RegisterClass()`、`USBD_Start()`。
- **底层接口**：`USBD_LL_OpenEP` / `CloseEP` / `Transmit` / `PrepareReceive` / `GetRxDataSize` / `SetUSBAddress` / `Delay`。
- **控制传输辅助**：`USBD_CtlSendData` / `USBD_CtlPrepareRx` / `USBD_CtlError` / `USBD_GetString`。
- **描述符**：设备、配置（FS **与** HS）、other-speed、device qualifier 与字符串回调，全部通过 CherryUSB 的高级描述符接口注册。
- **事件转换**：CherryUSB 的 `USBD_EVENT_*` 事件与按接口分发的请求处理器，被派发到 STM32 `USBD_ClassTypeDef` 的回调（`Init`、`DeInit`、`Setup`、`DataIn`、`DataOut`、`EP0_RxReady`、`SOF` 等）。

gs_usb 的命令请求（`bRequest` 0…14，厂商类型，接口接收者）与 WCID 的厂商码 `0x20` 共用同一条链路，因此"CherryUSB 先尝试 MS OS、失败后才落到接口的 vendor handler"这一顺序很关键 —— 两者永不重叠。

### 2. 控制 IN 的数据必须拷贝一次

candleLight 的 STM32 风格是"把缓冲区指针交给 `USBD_CtlSendData()`，由协议栈负责发送"。而 CherryUSB 是在请求处理函数**返回之后**才去取 EP0 载荷的。因此桥接层必须先把控制 IN 数据搬进一个私有影子缓冲区再返回 —— 这个接缝在原始源码里完全看不见，但在这里必不可少。

### 3. WCID 住在桥接层，而不是类文件里

原版 `usbd_gs_can.c` 里带着 candleLight 私有的 WCID（MS OS 1.0）数据块以及它自己的厂商请求分支。在 CherryUSB 下这些是**失效的**：CherryUSB 内核会从 `msosv1_descriptor` 提供 OS 字符串描述符（`0xEE`），并且在类 vendor handler 之前自行处理 MS OS 厂商请求。因此桥接层注册了自己的等价 WCID 描述符，保证只有**一处**真源：

| 描述符 | 作用 |
| --- | --- |
| MS OS 字符串（`0xEE`） | `"MSFT100"` + 厂商码 `0x20` |
| Compatible ID（`wIndex 0x0004`） | `bCount = 2`：接口 0 与接口 1 → `"WINUSB"` |
| Extended properties（`wIndex 0x0005`） | **每个 function** 一组 `DeviceInterfaceGUIDs` |

这里刻意使用了三个不同的 GUID：gs_usb 接口沿用 candleLight 的 `{c15b4308-…}`，以保证既有上位机工具继续可用；DFU runtime 接口与 bootloader 则各自拥有独立的 GUID。

> Windows 下"每 function 一份 `DeviceInterfaceGUIDs`"并非可有可无：`winusb.sys` 只为其中列出的 GUID 注册设备接口，而 libusb（以及 `dfu-util`）正是通过设备接口枚举设备的。属性集为空的 function 在设备管理器里依然可见，但不暴露任何设备接口，claim 时会失败并报 `Cannot claim interface 1: LIBUSB_ERROR_NOT_SUPPORTED`。

### 4. `board.h` 重名问题

candleLight 的 `include/board.h`（抽象板接口）与 `boards/<BOARD>/board.h`（具体硬件）都想叫 `board.h`。与其给板包改名，构建时生成了一个私有 include 目录，把 candleLight 的每个头文件（**除 `board.h` 外**）做成符号链接，于是：

- `#include "board.h"` → `boards/<BOARD>/board.h`
- `#include "board_layout.h"` → `port/board_layout.h`（存放 candleLight 的原始内容）

`port/` 与 `src/` 中的代码无需关心这件事。

### 5. CAN：`m_can.c` → `can_hpm.c`

HPM 的 MCAN 就是 Bosch M_CAN，因此 `candleLight_fw/src/can/m_can.c` 几乎可以 1:1 映射 —— 相同的位定时约束、相同的 `gs_host_frame` 映射、相同的 PSR/ECR/CCCR 错误状态处理。只有 STM32 HAL 调用被替换：

| STM32 FDCAN | HPM MCAN |
| --- | --- |
| `HAL_FDCAN_Init` | `mcan_init()`（消息 RAM + CCCR） |
| `HAL_FDCAN_Start` / `_Stop` | `CCCR.INIT` 位 |
| `HAL_FDCAN_AddMessageToTxFifoQ` | `mcan_transmit_via_txfifo_nonblocking()` |
| `HAL_FDCAN_GetRxMessage` | `mcan_read_rxfifo()` |
| `HAL_FDCAN_GetRxFifoFillLevel` | `RXF0S.F0FL` |

所有排队与链表逻辑仍然发生在未修改的 `can_common.c` + `usbd_gs_can.c` 中。

有四处不能 1:1 对应，也最耗调试时间：

- **消息 RAM** —— 当 `MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1`（HPM5321/HPM5361）时它只是 AHB SRAM 的一块，应用必须在第一次 `mcan_init()` 之前用 `mcan_set_msg_buf_attr()` 登记；否则 `mcan_get_ram_size()` 返回 0，`mcan_init()` 在清 `CCCR.INIT` **之前**就失败，四路在主机侧都能起来，却既不发、不收，也不报错。
- **发送** —— `TXFQS.TFQPI` 是整个 TX 缓冲数组的下标（先是专用缓冲，再是 FIFO 槽），所以必须走 TX FIFO 接口；传给只接受 `TXBC.NDTB` 以内下标的 `mcan_write_txbuf()` 会让每一帧都被丢弃。八个缓冲在这里全部配成同一个队列。
- **CAN 时钟** —— 必须等于上报给主机的 `CAN_CLOCK_SPEED`（本板 80 MHz），因为主机据此推算预分频；否则两路之间仍然彼此一致（内部 loopback 也能通过），但谁都不符合主机认定的波特率。
- **位定时兜底值** —— `can_init()` 里那组硬编码只在主机从未下发 `GS_USB_BREQ_BITTIMING` 时使用，已按本板 80 MHz 写成 500k / 2M @80%。

---

## 目录结构

```
candleLight_fw_hpm_port/
├── CMakeLists.txt              # 工程构建脚本（HPM SDK + CherryUSB）
├── CMakePresets.json           # 各板预设（全部为 flash_dfu）
├── boards/
│   ├── hscant/                 # 板级包（时钟、引脚复用、CAN 时钟、LED）
│   └── hpm5321_usb2can/        # 板级包
├── port/                       # 平台接缝：不含硬件代码
│   ├── hal_include.h           # STM32 风格 HAL 垫片
│   ├── cmsis_device.h          # RISC-V CMSIS 垫片
│   ├── usbd_compat.h           # STM32 USBD 类型/原型
│   ├── usbd_def.h              # 兼容替身
│   ├── usbd_ctlreq.h           # 兼容替身
│   ├── board_layout.h          # candleLight 的板抽象
│   ├── board_contract.h        # 板包契约 + 编译期检查
│   └── usbd_bridge.h           # 面向桥接层的 API
├── src/
│   ├── candle/                 # candleLight 原始文件（仅 include 接缝）
│   │   ├── main.c
│   │   └── usbd_gs_can.c
│   ├── usbd_bridge.c           # 兼容层核心（CherryUSB ⇄ STM32 USBD）
│   ├── can_hpm.c               # m_can.c 到 hpm_mcan_drv 的移植
│   ├── timer.c                 # 基于 mcycle 的 timer_get()
│   ├── dfu_hpm.c               # dfu_run_bootloader()
│   ├── port_device.c           # HAL_Init()、GPIO 垫片、UUID 装载
│   ├── port_board.c            # config.channels[] 装配、弱 CAN 时钟
│   └── usb_config.h            # CherryUSB 配置
├── candleLight_fw/             # git 子模块（参考源码）
├── README.md                   # 工程说明（英文）
├── README_zh.md                # 工程说明（中文，本文件）
├── README_build.md             # 编译指导（英文）
└── README_build_zh.md          # 编译指导（中文）
```

---

## 烧录与 DFU

本 APP 链接在 `0x80020000`，其前放置 `.dfu_signature`；引导程序 [`hpm_dfu_boot`](../hpm_dfu_boot) 位于 `0x80000000`。

1. **首次** —— 用调试器 / 烧录器写入 bootloader，之后通过 DFU 烧录本 APP。
2. **APP → bootloader** —— 上电时按住 Boot 引脚、长按用户按键约 500 ms，或由主机在 runtime 接口上发出 DFU `DETACH` 请求。APP 调用 `dfu_run_bootloader()`：向保留寄存器写入 magic 后重新进入 ROM 引导流程，bootloader 读到该 magic 便停留在 DFU 模式。
3. **下载** —— bootloader 以 HPMicro 的 VID（`0x34b7`）枚举。

bootloader 与 APP 在 Windows 下都通过 WCID 免驱，无需 Zadig、无需厂商 INF：

```bash
# 从运行中的 APP 进入 DFU 并烧录
#   dfu-util 的 -d 形式为 vendor[:product]，只给 VID 就够 —— 且 PID 变更时无需改命令：
#   0x1d50 = 本 APP，0x34b7 = bootloader
dfu-util.exe -d 1d50,34b7 -R -E 1 -s 0x80020000:leave -D candle_usb2canfd.bin
# 或直接指定 bootloader
dfu-util.exe -d 34b7 -a 0 -s 0x80020000:leave -D candle_usb2canfd.bin
```

每块板子都上报由芯片 UUID 推导出的**唯一** `iSerialNumber`，因此可以同时接入多台设备；用 `dfu-util -l` 列出序列号，再用 `-S <serial>` 指定目标。

> 主机侧坑点：Windows 会按 `VID:PID:REV` 缓存"该设备是否支持 MS OS 描述符"的结论，位置在
> `HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<VID><PID><REV>\osvc`。若某个固件版本曾在**没有** WCID 的情况下枚举过，
> `osvc` 会一直是 `00 00`，Windows 之后不再重新查询。删除该键（或提升 `bcdDevice`）即可。

---

## 如何编译

详见 **[README_build.md](./README_build.md)**（英文）/ **[README_build_zh.md](./README_build_zh.md)**（中文）。

简而言之，先准备 HPM SDK 与 RISC-V 工具链，然后使用 CMake preset：

```bash
cmake --preset hscant-dfu
cmake --build --preset hscant-dfu
```

产物位于 `build/<preset>/output/candle_usb2canfd.elf` / `.hex` / `.bin`。

---

## 上机测试

下面全部是 `can-utils` / `iproute2` 原生命令，不依赖任何辅助脚本。

```bash
# 起一路接口：500 kbit/s、80% 采样点
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 sample-point 0.8
sudo ip link set can0 up
ip -details -details link show can0        # 内核最终协商出的时序

# 经典帧，双向
candump can1 &                             # 在对端监听
cansend can0 100#1122334455667788          # 从设备发出

# CAN-FD（带 BRS）：仲裁 500 kbit/s、数据 2 Mbit/s，两段都是 80%
sudo ip link set can0 type can bitrate 500000 sample-point 0.8 \
                             dbitrate 2000000 dsample-point 0.8 fd on
cansend can0 300##1AABBCCDDEEFF00112233445566778899AABBCCDD

# 控制器内部回环：不需要总线、不需要第二个节点，单独证明帧转发通路
sudo ip link set can0 type can loopback on
candump can0 & cansend can0 100#1122334455667788

# 终端电阻（接口必须处于 down；复位后两路都是断开）
sudo ip link set can0 down
sudo ip link set can0 type can termination 120     # 或 0

# 硬件里到底存了什么，以及哪里出了问题
ip -details -details link show can0        # NBTP/DBTP：prop-seg、phase-seg、sjw、采样点
ip -details -statistics link show can0     # re-started / error-warn / error-pass / bus-off，TEC/REC
candump -e "can0,#FFFFFFFF"                # 错误帧（发送过程中）
```

绝不要在同一步里既发又收：SocketCAN 的本地 TX 回环根本不经过总线，在**同一个**接口上一边
`cansend` 一边 `candump`，会"收到"别的节点从未见过的帧。干净总线应表现为 `error-warn` /
`error-pass` / `bus-off` / `re-started` 全为 0，且日志里没有任何 `ERRORFRAME`。

> 值得记住的主机侧坑：`candump -e canX` **不会**打开内核的 `CAN_RAW_ERR_FILTER` —— `-e` 只决定输出格式，
> 错误帧根本到不了 socket。要看到它们得用 `candump "canX,#FFFFFFFF"`。
