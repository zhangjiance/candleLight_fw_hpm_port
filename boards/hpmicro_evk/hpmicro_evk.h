/*
 * Copyright (c) 2026 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Candle bindings for the HPMicro EVK boards shipped in hpm_sdk.
 *
 * This header is force-included (-include) into the application when BOARD
 * names an EVK found in ${HPM_SDK_BASE}/boards.  It pulls in that board's own
 * board.h first, and then maps the candleLight port contract onto what the
 * SDK board package already provides:
 *
 *   - the board's single BOARD_APP_CAN_BASE (an MCAN instance, pins and clock
 *     are handled by the SDK board package's init_can_pins() /
 *     board_init_can_clock()),
 *   - the on-chip USB0 device controller (board_init_usb()),
 *   - the user LED (board_led_toggle()) for the "running" heartbeat.
 *
 * Nothing from the EVK is copied into this repository: BOARD=hpm5300evk,
 * BOARD=hpm6800evk, ... all build the same candle application against the
 * board package that ships with the SDK.
 */

#ifndef HPMICRO_EVK_H
#define HPMICRO_EVK_H

/* The SDK board package selected by -DBOARD=<evk>. */
#include "board.h"

/* The SDK board package must be an MCAN + USB EVK; CMakeLists.txt checks
 * this before enabling this header, so a missing macro is a real error. */
#ifndef BOARD_APP_CAN_BASE
#error "hpmicro_evk: the SDK board package has no BOARD_APP_CAN_BASE (MCAN)"
#endif

/* ------------------------------------------------------------------ */
/* candleLight port contract (port/board_contract.h)                   */
/* ------------------------------------------------------------------ */

/* One CAN channel per EVK, on the instance the SDK board package wires up. */
#define BOARD_CAN_COUNT     1
#define BOARD_CAN_INSTANCES BOARD_APP_CAN_BASE

/* All supported EVKs expose the on-chip USB0 device controller.  Its pins,
 * PHY and clock are brought up by the SDK board package's board_init_usb(). */
#ifndef BOARD_USB_BASE
#define BOARD_USB_BASE HPM_USB0
#endif
#ifndef BOARD_USB_IRQn
#define BOARD_USB_IRQn IRQn_USB0
#endif

/* DFU trigger register.  Some SoCs (HPM6200/6300/6700/6800) use BCFG/BGPR,
 * others (HPM5100/5300/5E00/6P00) use PDGO only; src/dfu_hpm.c selects the
 * backend from HPM_BCFG_BASE / HPM_PDGO_BASE.  Only the BCFG boards need
 * BOARD_BGPR. */
#if defined(HPM_BCFG_BASE) && !defined(BOARD_BGPR)
#define BOARD_BGPR HPM_BGPR
#endif

/* Single user LED on every EVK: status_led.c toggles it as a heartbeat.
 * No addressable or per-channel LEDs on the EVKs. */
#ifndef BOARD_HAS_SYS_LED
#define BOARD_HAS_SYS_LED 1
#endif
#define BOARD_HAS_WS2812   0
#define BOARD_HAS_CHAN_LEDS 0

#endif /* HPMICRO_EVK_H */
