/*
 * board_contract.h - the interface every board package (boards/<BOARD>/) must
 * provide to the candleLight port layer, plus compile time checks.
 *
 * Supported boards
 * ----------------
 *   hscant            HPMicro HSCanT (HPM5321/HPM5361)
 *   hpm5321_usb2can   HPMicro HPM5321 USB2CAN dongle (QFN48)
 *
 * Adding a board
 * --------------
 *   1. put a package in boards/<name>/ containing at least
 *          board.h    - the macros below, plus the usual HPM SDK board defines
 *          board.c    - board_init(), board_init_usb(), board_delay_ms(), ...
 *          pinmux.c   - init_can_pins() and friends
 *      (a board may reuse the "clock.c" style of the HPM pinmux tool)
 *   2. add a "BOARD_<name>" block to candleLight_fw/include/config.h
 *      (USB strings, NUM_CAN_CHANNEL, CAN_CLOCK_SPEED, CONFIG_CANFD)
 *   3. add a preset in CMakePresets.json with -DBOARD=<name>
 *   Nothing in port/ or src/ has to change.
 *
 * Required macros (in boards/<name>/board.h)
 * ------------------------------------------
 *   BOARD_CAN_COUNT      number of MCAN instances populated on the board
 *   BOARD_CAN_INSTANCES  comma separated MCAN instance list, e.g.
 *                        HPM_MCAN0, HPM_MCAN1, HPM_MCAN2, HPM_MCAN3
 *   BOARD_USB_BASE       USB device controller instance (e.g. HPM_USB0)
 *   BOARD_USB_IRQn       matching IRQn (e.g. IRQn_USB0)
 *   BOARD_BGPR           retention register used for the DFU reboot request
 *   BOARD_HAS_SYS_LED    1 if board_led_toggle() drives a real "running" LED,
 *                        otherwise 0
 *
 * Required functions
 * ------------------
 *   void     board_init(void);                  SoC clock tree + pin muxing
 *   void     board_init_usb(USB_Type *ptr);     USB pins, clock, vbus source
 *   void     board_delay_ms(uint32_t ms);
 *   void     init_can_pins(MCAN_Type *ptr);     MCAN pad muxing
 *   uint32_t board_init_can_clock(MCAN_Type *ptr);
 *       Selects the CAN clock source/divider (e.g. clk_canN sourced from
 *       clk_src_pll1_clk0 / 10) and returns its frequency.  Called by the
 *       port layer for every MCAN instance before can_init().
 *   void     board_led_toggle(void);            only if BOARD_HAS_SYS_LED == 1
 */

#pragma once

#include <stdint.h>

#include "hpm_common.h"
#include "hpm_soc.h"

/* --- required macros ------------------------------------------------------ */
#ifndef BOARD_CAN_COUNT
#error "board package: BOARD_CAN_COUNT is missing (see port/board_contract.h)"
#endif
#ifndef BOARD_CAN_INSTANCES
#error "board package: BOARD_CAN_INSTANCES is missing (see port/board_contract.h)"
#endif
#ifndef BOARD_USB_BASE
#error "board package: BOARD_USB_BASE is missing (see port/board_contract.h)"
#endif
#ifndef BOARD_USB_IRQn
#error "board package: BOARD_USB_IRQn is missing (see port/board_contract.h)"
#endif
#ifndef BOARD_BGPR
#error "board package: BOARD_BGPR is missing (see port/board_contract.h)"
#endif
#ifndef BOARD_HAS_SYS_LED
#error "board package: BOARD_HAS_SYS_LED is missing (see port/board_contract.h)"
#endif

/* --- required functions --------------------------------------------------- */
void board_init(void);
void board_init_usb(USB_Type *ptr);
void board_delay_ms(uint32_t ms);
void init_can_pins(MCAN_Type *ptr);
uint32_t board_init_can_clock(MCAN_Type *ptr);
