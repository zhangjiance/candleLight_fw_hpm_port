# candleLight_fw_hpm_port

**candleLight / gs_usb compatible USB ⇄ CAN-FD firmware for HPMicro HPM5321/HPM5361 RISC-V MCUs**, built on the HPM SDK and CherryUSB.

The gs_usb protocol, the USB descriptors and — the important part — candleLight's frame linked-list engine are the *original* candleLight sources. Only the platform layer is new. A host running the stock Linux `gs_usb` driver (or `candle_usb` / `candump` / `can-utils`) therefore sees an ordinary candleLight / CANable device.

> 中文说明见 [README_zh.md](./README_zh.md)。Build instructions: [README_build.md](./README_build.md) (English) / [README_build_zh.md](./README_build_zh.md) (中文).

---

## Device Identity

| Item | Value |
| --- | --- |
| USB VID:PID | `0x1d50:0x606f` (OpenMoko / candleLight) |
| USB speed | **High-Speed, 480 Mbit/s (512 B bulk)**; the bridge serves HS or FS descriptors depending on what the host negotiates. The `DEVICE_FS` argument in `main.c` is inert — `USBD_Init()` ignores it — and `src/usb_config.h` selects `CONFIG_USB_HS` by default |
| Interface 0 | `gs_usb`, vendor specific `0xFF/0xFF/0xFF`, 2 bulk endpoints |
| Interface 1 | DFU runtime `0xFE/0x01/0x01` (detach only, no endpoints) |
| Windows driver | **none** — MS OS 1.0 / WCID makes Windows bind `winusb.sys` |
| CAN | 4 × MCAN (Bosch M_CAN compatible), CAN-FD, 80 MHz CAN clock |
| Flash layout | `hpm_dfu_boot` at `0x80000000`, this application at `0x80020000` |

## Implementation Status

Tested means "exercised on real hardware". Please read this table before filing bugs.

| Area | State |
| --- | --- |
| DFU flashing together with `hpm_dfu_boot` | **Verified on hardware** |
| USB enumeration (High-Speed, driver-less on Windows via WCID) | **Verified on hardware** |
| CAN / CAN-FD frame forwarding | **Verified on hardware** — classic and CAN-FD BRS, both directions, against a PEAK PCAN and channel-to-channel |
| Multi-channel (4 × MCAN) operation | **Verified on hardware** — all four channels, including a three node bus |
| Bit timing / sample point | **Verified** — host-negotiated sample points land in `NBTP`/`DBTP` (checked 500k/2M and 1M/4M at 80 %) |
| Per-channel terminal resistor switch | **Verified on hardware** — `GS_CAN_FEATURE_TERMINATION`, driven by `ip link set canX type can termination 120` |
| Bus error reporting (`CAN_ERR_ACK` / `PROT` / `CRTL`, TEC/REC) | Implemented; all classes have been observed on a deliberately broken bus, a systematic regression pass is still missing |
| WS2812 TX/RX activity LEDs | **Not implemented** |
| Throughput / stress testing | Not done |

The command surface (`BITTIMING`, `DATA_BITTIMING`, `MODE`, `BT_CONST(_EXT)`, `DEVICE_CONFIG`, `TIMESTAMP`, `IDENTIFY`, `HOST_FORMAT`, TX echo, timestamps, `LOOP_BACK` / `LISTEN_ONLY` / `ONE_SHOT` / `HW_TIMESTAMP` / `PAD_PKTS`) is complete, and enumeration, DFU, frame forwarding and the terminator control have all been exercised end to end.

### Known Gaps / Next Steps

| Item | What is left |
| --- | --- |
| Error reporting regression | Exercise every class systematically: `CAN_ERR_ACK`, `PROT` (stuff / form / bit / CRC), `CRTL` (warning, passive), `BUSOFF`, RX overrun, plus the TEC/REC fields in the payload and the LEC-transition de-duplication |
| Performance testing | Frames/s over USB High-Speed, multi-channel stress (`cangen` on several channels at once), `RX dropped/missed`, TX latency. USB is already HS, so the ceiling is the CAN bit rate and the per-frame host overhead, not the link |
| WS2812 TX/RX LEDs | Still missing; `boards/hscant/` currently drives only the single `SYS_LED` on GPIOB[10] |

### Feature Flags Reported to the Host

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

`GS_CAN_FEATURE_TERMINATION` **is** advertised for `BOARD_hscant`: `TERM_Pin` is defined there, which selects `set_term()` / `get_term()` (implemented in `src/port_board.c`, since this port does not compile upstream's `src/gpio.c`) and reaches the board through `config.termination_set` → `board_can_set_termination()` (`CANx_RES`, one per channel). The host uses it with `ip link set canX type can termination 120` (or `termination 0`), which only works while the interface is down. After reset the terminators are off, matching the level `pinmux.c` leaves `CANx_RES` in.

---

## Architecture

The design goal is simple: **keep candleLight's application logic byte-for-byte intact and put every platform dependency behind one narrow seam**, so that upstream fixes to the frame engine or the protocol can be picked up by copying files.

```
                       ┌──────────────────────────────────────────┐
  candleLight_fw       │  src/candle/main.c        (copy, seam)   │
  (git submodule,      │  src/candle/usbd_gs_can.c (copy, seam)   │
   the reference)      │  src/can_common.c   (compiled verbatim)  │
                       │  src/led.c, util.c, usbd_desc.c (verbatim)│
                       └──────────────────┬───────────────────────┘
                                          │  STM32 HAL + STM32 USBD API
                       ┌──────────────────▼───────────────────────┐
  port/ (the seam)     │  hal_include.h    HAL/CMSIS/GPIO shim    │
  no hardware code     │  cmsis_device.h   RISC-V CMSIS shim      │
                       │  usbd_compat.h    STM32 USBD types/proto │
                       │  board_layout.h   candleLight board API  │
                       │  board_contract.h board package contract │
                       └──────────────────┬───────────────────────┘
                                          │  implementation
                       ┌──────────────────▼───────────────────────┐
  src/ (HPMicro)       │  usbd_bridge.c  CherryUSB ⇄ STM32 USBD   │
                       │  can_hpm.c      m_can.c → hpm_mcan_drv   │
                       │  timer.c  dfu_hpm.c  port_device.c       │
                       │  port_board.c                            │
                       └──────────────────┬───────────────────────┘
                                          │
                       ┌──────────────────▼───────────────────────┐
  boards/<BOARD>/      │  clock tree, pad muxing, CAN clock, LED,  │
                       │  DFU signature, BGPR selection            │
                       └──────────────────────────────────────────┘
```

### Layer 1 — `candleLight_fw/` (original code)

| File | How it is used |
| --- | --- |
| `src/can_common.c` | **compiled verbatim** — the frame linked-list pool (`CAN_SendFrame` / `CAN_ReceiveFrame` / `CAN_HandleError`), TX echo, RX queue |
| `src/led.c`, `src/util.c`, `src/usbd_desc.c` | **compiled verbatim** |
| `src/usbd_gs_can.c` | copied to `src/candle/`, **only the `#include` block changed** |
| `src/main.c` | copied to `src/candle/`, **only the `#include` block changed** |

The copies exist for exactly one reason: those two files pull in STM32 USB Device Library headers that do not exist here. Everything below the include block is untouched, so a `diff` against the submodule stays a five-line hunk.

```42:45:src/candle/usbd_gs_can.c
 * are replaced by our CherryUSB-backed compatibility layer.  Everything else
 * in this file is an unmodified copy of candleLight_fw/src/usbd_gs_can.c.
 */
#include "usbd_compat.h"
```

### Layer 2 — `port/` (the platform seam)

Contains **no hardware code**, only contracts and shims:

- **`hal_include.h`** — the STM32-style HAL that candleLight's headers expect: `GPIO_PIN_SET/RESET`, `HAL_GPIO_WritePin/TogglePin`, `HAL_GetTick`/`HAL_Delay`, `HAL_Init`, `UNUSED`, plus `FDCAN_GlobalTypeDef`/`FDCAN_HandleTypeDef` mapped onto HPM `MCAN_Type`. Also declares `hpm_uid[12]` / `UID_BASE` used for the USB serial number.
- **`cmsis_device.h`** — the handful of CMSIS intrinsics the original code touches, on RISC-V: `__disable_irq`, `__enable_irq`, `__DMB`, `__BKPT`, `NVIC_SystemReset`.
- **`usbd_compat.h`** — STM32 USB Device Library **types, constants and prototypes** taken from `usbd_def.h` (`USBD_HandleTypeDef`, `USBD_ClassTypeDef`, the `USBD_LL_*` / `USBD_Ctl*` set, descriptor index macros).
- **`usbd_def.h`, `usbd_ctlreq.h`** — thin stand-ins so the original `#include`s resolve.
- **`board_layout.h`** — candleLight's board abstraction (`struct BoardConfig`, `config.channels[]`), renamed to free the name `board.h`.
- **`board_contract.h`** — what every board package must provide, enforced with `#error` at compile time.
- **`usbd_bridge.h`** — the one symbol the rest of the firmware needs from the bridge.

### Layer 3 — `src/` (HPMicro implementation)

| File | Role |
| --- | --- |
| `usbd_bridge.c` | **the compatibility layer** — see below |
| `can_hpm.c` | port of `candleLight_fw/src/can/m_can.c` onto `hpm_mcan_drv` |
| `timer.c` | `timer_get()` = free-running µs counter on `mcycle` (no peripheral timer consumed, safe from interrupts) |
| `dfu_hpm.c` | `dfu_run_bootloader()` — writes the BGPR/PDGO magic, then a software reset |
| `port_device.c` | `HAL_Init()` bring-up (clock tree, USB PHY, D-cache policy), GPIO shim, chip UUID loader |
| `port_board.c` | weak default `board_init_can_clock()`, fills `config.channels[].interface` |
| `usb_config.h` | CherryUSB configuration |
| `candle/` | the two adapted original files |

### Layer 4 — `boards/<BOARD>/`

Per-board package: clock tree, pad muxing, CAN clock selection, LED, DFU signature and the retention register used for the DFU reboot request. Two packages exist today: `hscant` and `hpm5321_usb2can`.

---

## The Compatibility Layer in Detail

`src/usbd_bridge.c` is what makes "use the original sources" possible. It is the only file that knows both worlds.

### 1. It implements the STM32 USBD API on top of CherryUSB

- **Entry points** called by the original `main.c`: `USBD_Init()`, `USBD_RegisterClass()`, `USBD_Start()`.
- **Low level**: `USBD_LL_OpenEP` / `CloseEP` / `Transmit` / `PrepareReceive` / `GetRxDataSize` / `SetUSBAddress` / `Delay`.
- **Control helpers**: `USBD_CtlSendData` / `USBD_CtlPrepareRx` / `USBD_CtlError` / `USBD_GetString`.
- **Descriptors**: device, configuration FS **and** HS, other-speed, device qualifier and string callbacks, all registered through CherryUSB's advanced-descriptor API.
- **Event translation**: CherryUSB's `USBD_EVENT_*` events and per-interface request handlers are dispatched into the STM32 `USBD_ClassTypeDef` callbacks (`Init`, `DeInit`, `Setup`, `DataIn`, `DataOut`, `EP0_RxReady`, `SOF`, …).

The gs_usb command requests (`bRequest` 0…14, vendor type, interface recipient) share the link with the WCID vendor code `0x20`, so it matters that CherryUSB tries MS OS first and only then falls through to the interface's vendor handler. The two never overlap.

### 2. Control-IN payloads have to be copied

candleLight's STM32 style is "hand a buffer pointer to `USBD_CtlSendData()` and let the stack transmit it". CherryUSB fetches the EP0 payload *after* the request handler returns. The bridge therefore stages control-IN data through a private shadow buffer before returning — a seam that is invisible in the original sources but essential here.

### 3. WCID lives in the bridge, not in the class file

The original `usbd_gs_can.c` carries candleLight's private WCID (MS OS 1.0) blobs and its own vendor-request branch. Under CherryUSB those are **inert**: CherryUSB's core serves the OS string descriptor (`0xEE`) from `msosv1_descriptor`, and handles the MS OS vendor requests itself before the class vendor handler runs. The bridge therefore registers its own, equivalent WCID descriptors so that there is exactly **one** source of truth:

| Descriptor | Purpose |
| --- | --- |
| MS OS string (`0xEE`) | `"MSFT100"` + vendor code `0x20` |
| Compatible ID (`wIndex 0x0004`) | `bCount = 2`: interface 0 and interface 1 → `"WINUSB"` |
| Extended properties (`wIndex 0x0005`) | a `DeviceInterfaceGUIDs` **per function** |

Three different GUIDs are in use, deliberately: the gs_usb interface keeps candleLight's `{c15b4308-…}` so existing host tooling keeps working, while the DFU runtime interface and the bootloader each get their own.

> The per-function `DeviceInterfaceGUIDs` is not cosmetic on Windows: `winusb.sys` only registers a device interface for the GUIDs listed there, and libusb (hence `dfu-util`) enumerates through those interfaces. A function with an empty property set still appears in Device Manager but exposes no device interface, and claiming it fails with `Cannot claim interface 1: LIBUSB_ERROR_NOT_SUPPORTED`.

### 4. The `board.h` name collision

candleLight's `include/board.h` (the abstract board interface) and `boards/<BOARD>/board.h` (the concrete hardware) both want the name `board.h`. Rather than renaming the board package, the build generates a private include directory of symlinks to every candleLight header **except** `board.h`, so:

- `#include "board.h"` → `boards/<BOARD>/board.h`
- `#include "board_layout.h"` → `port/board_layout.h` (holds candleLight's original content)

Nothing in `port/` or `src/` has to care.

### 5. CAN: `m_can.c` → `can_hpm.c`

The HPM MCAN is a Bosch M_CAN, so `candleLight_fw/src/can/m_can.c` maps over almost 1:1 — same bit-timing constraints, same `gs_host_frame` mapping, same PSR/ECR/CCCR error-status handling. Only the STM32 HAL calls are replaced:

| STM32 FDCAN | HPM MCAN |
| --- | --- |
| `HAL_FDCAN_Init` | `mcan_init()` (message RAM + CCCR) |
| `HAL_FDCAN_Start` / `_Stop` | `CCCR.INIT` bit |
| `HAL_FDCAN_AddMessageToTxFifoQ` | `mcan_transmit_via_txfifo_nonblocking()` |
| `HAL_FDCAN_GetRxMessage` | `mcan_read_rxfifo()` |
| `HAL_FDCAN_GetRxFifoFillLevel` | `RXF0S.F0FL` |

All queueing and linked-list work still happens in the unmodified `can_common.c` + `usbd_gs_can.c`.

Four things do not map over 1:1 and cost most of the debugging time:

- **Message RAM** — with `MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1` (HPM5321/HPM5361) it is a slice of AHB SRAM that the
  application must register through `mcan_set_msg_buf_attr()` before the first `mcan_init()`. Otherwise
  `mcan_get_ram_size()` returns 0, `mcan_init()` fails *before* clearing `CCCR.INIT`, and every channel comes
  up on the host side but never transmits, receives or reports an error.
- **TX** — `TXFQS.TFQPI` indexes the whole TX buffer array (dedicated buffers first, then the FIFO slots), so it
  must go to the TX FIFO API. Passing it to `mcan_write_txbuf()`, which only accepts indexes below `TXBC.NDTB`,
  fails for every frame. All eight buffers are configured as a single queue.
- **CAN clock** — must equal the `CAN_CLOCK_SPEED` reported to the host, because the host derives the prescaler
  from it. If it does not, both channels still agree with each other (and internal loopback passes) but nothing
  matches the host's idea of the bit rate.
- **Bit timing fallback** — the hardcoded values in `can_init()` are only used if the host never sends
  `GS_USB_BREQ_BITTIMING`; they are set for this board's 80 MHz clock (500k and 2M at 80 %).

---

## Directory Structure

```
candleLight_fw_hpm_port/
├── CMakeLists.txt              # Build script (HPM SDK + CherryUSB)
├── CMakePresets.json           # Per-board presets (all flash_dfu)
├── boards/
│   ├── hscant/                 # Board package (clock, pinmux, CAN clock, LED)
│   └── hpm5321_usb2can/        # Board package
├── port/                       # Platform seam: no hardware code
│   ├── hal_include.h           # STM32-style HAL shim
│   ├── cmsis_device.h          # RISC-V CMSIS shim
│   ├── usbd_compat.h           # STM32 USBD types / prototypes
│   ├── usbd_def.h              # Compatibility stand-in
│   ├── usbd_ctlreq.h           # Compatibility stand-in
│   ├── board_layout.h          # candleLight's board abstraction
│   ├── board_contract.h        # Board package requirements + static checks
│   └── usbd_bridge.h           # Bridge-facing API
├── src/
│   ├── candle/                 # Original candleLight files (include seam only)
│   │   ├── main.c
│   │   └── usbd_gs_can.c
│   ├── usbd_bridge.c           # THE COMPATIBILITY LAYER (CherryUSB ⇄ STM32 USBD)
│   ├── can_hpm.c               # m_can.c port onto hpm_mcan_drv
│   ├── timer.c                 # timer_get() on mcycle
│   ├── dfu_hpm.c               # dfu_run_bootloader()
│   ├── port_device.c           # HAL_Init(), GPIO shim, UUID loader
│   ├── port_board.c            # config.channels[] wiring, weak CAN clock
│   └── usb_config.h            # CherryUSB configuration
├── candleLight_fw/             # git submodule (reference sources)
├── README.md                   # Project overview (English, this file)
├── README_zh.md                # Project overview (中文)
├── README_build.md             # Build guide (English)
└── README_build_zh.md          # Build guide (中文)
```

---

## Flashing and DFU

The application is linked at `0x80020000` behind a `.dfu_signature`; the bootloader [`hpm_dfu_boot`](../hpm_dfu_boot) lives at `0x80000000`.

1. **First time** — write the bootloader with a debugger / flasher, then flash this application through DFU.
2. **Application → bootloader** — either hold the boot pin at power-on, or have the host issue a DFU `DETACH` request on the runtime interface. The application sets `dfu_detach_requested`, `main.c` calls `dfu_run_bootloader()`, which writes a magic value to a retention register and resets the SoC; the bootloader sees the magic and stays in DFU mode.
3. **Download** — the bootloader enumerates under the HPMicro VID (`0x34b7`).

Both the bootloader and the application are driver-less on Windows via WCID, so no Zadig and no vendor INF are needed:

```bash
# enter DFU from the running application, then write the image
#   dfu-util's -d takes vendor[:product], so a VID alone is enough -- and it keeps
#   working when a PID changes: 0x1d50 = this application, 0x34b7 = the bootloader
dfu-util.exe -d 1d50,34b7 -R -E 1 -s 0x80020000:leave -D candle_usb2canfd.bin
# or target the bootloader directly
dfu-util.exe -d 34b7 -a 0 -s 0x80020000:leave -D candle_usb2canfd.bin
```

Every board reports a **unique** `iSerialNumber` derived from the chip UUID, so multiple devices can be attached at once; use `dfu-util -l` and `-S <serial>` to select one.

> Host-side gotcha: Windows caches the "does this device speak MS OS descriptors?" answer per `VID:PID:REV` in
> `HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<VID><PID><REV>\osvc`. If a device revision was ever enumerated
> **without** WCID, `osvc` is stuck at `00 00` and Windows will not re-query. Delete that key (or bump `bcdDevice`).

---

## How to Build

See **[README_build.md](./README_build.md)** (English) / **[README_build_zh.md](./README_build_zh.md)** (中文).

In short: prepare the HPM SDK and the RISC-V toolchain, then use a CMake preset:

```bash
cmake --preset hscant-dfu
cmake --build --preset hscant-dfu
```

Artifacts land in `build/<preset>/output/candle_usb2canfd.elf` / `.hex` / `.bin`.

---

## Testing the Board

Everything below is stock `can-utils` / `iproute2` — no helper script is involved.

```bash
# bring one channel up: 500 kbit/s, 80 % sample point
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 sample-point 0.8
sudo ip link set can0 up
ip -details -details link show can0        # the timing the kernel negotiated

# classic frames, both directions
candump can1 &                             # listen on the peer
cansend can0 100#1122334455667788          # transmit from the device

# CAN-FD with BRS: 500 kbit/s arbitration, 2 Mbit/s data, 80 % in both
sudo ip link set can0 type can bitrate 500000 sample-point 0.8 \
                             dbitrate 2000000 dsample-point 0.8 fd on
cansend can0 300##1AABBCCDDEEFF00112233445566778899AABBCCDD

# controller internal loopback: no bus, no second node - frame forwarding alone
sudo ip link set can0 type can loopback on
candump can0 & cansend can0 100#1122334455667788

# terminator, as the host sees it (the interface must be down; reset leaves them off)
sudo ip link set can0 down
sudo ip link set can0 type can termination 120     # or 0

# what the hardware actually holds, and what went wrong
ip -details -details link show can0        # NBTP/DBTP: prop-seg, phase-seg, sjw, sample point
ip -details -statistics link show can0     # re-started / error-warn / error-pass / bus-off, TEC/REC
candump -e "can0,#FFFFFFFF"                # error frames, while transmitting
```

Never send and listen within the same step: SocketCAN's local TX loopback never touches the wire, so a
`candump` that runs while `cansend` runs on the *same* interface would "receive" frames that no other node
ever saw. A clean bus shows `error-warn` / `error-pass` / `bus-off` / `re-started` at zero and no
`ERRORFRAME` lines at all.

> Host gotcha worth remembering: `candump -e canX` does **not** arm the kernel's `CAN_RAW_ERR_FILTER` — `-e`
> only selects the output format, so error frames never reach the socket. Use `candump "canX,#FFFFFFFF"` to
> actually see them.
