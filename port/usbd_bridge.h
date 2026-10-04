/*
 * usbd_bridge.h - bridge between candleLight_fw's STM32 USBD usage and
 * CherryUSB on HPMicro.
 */

#pragma once

#include <stdint.h>

#include "usbd_core.h"
#include "usbd_compat.h"

/* Filled in by USBD_Init(); used by can_common.c / usbd_gs_can.c callers. */
USBD_HandleTypeDef *usbd_bridge_get_handle(void);
