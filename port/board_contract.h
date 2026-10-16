
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
