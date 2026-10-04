/*
 * hal_include.h - platform seam for candleLight_fw on HPMicro HPM5361.
 *
 * candleLight_fw's headers (can.h, led.h, gpio.h) expect an STM32 style HAL:
 *   - FDCAN_GlobalTypeDef / FDCAN_HandleTypeDef   (used by can.h)
 *   - GPIO_PIN_SET / GPIO_PIN_RESET, HAL_GPIO_WritePin / TogglePin (led.c, main.c)
 *   - HAL_GetTick() / HAL_Delay()                 (led.c, main.c)
 *   - UNUSED()                                    (usbd_gs_can.c)
 *
 * Everything the CAN data path needs is implemented in src/can_hpm.c on top of
 * the hpm_sdk MCAN driver / MCAN registers.  FDCAN_HandleTypeDef below is only
 * the per-channel handle embedded in can.h's can_data_t.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "hpm_common.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

/* ------------------------------------------------------------------ */
/* GPIO                                                                */
/* ------------------------------------------------------------------ */
typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET,
} GPIO_PinState;

/* HPM has one GPIO controller (HPM_GPIO0) with several ports ("groups").
 * A GPIO_TypeDef describes a single pad. */
typedef struct {
    GPIO_Type *ctrl;  /* HPM_GPIO0 / HPM_FGPIO */
    uint32_t group;   /* port index, e.g. GPIO_DO_GPIOB */
    uint32_t index;   /* pin index inside the port */
} GPIO_TypeDef;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);

/* ------------------------------------------------------------------ */
/* Tick (1 ms) / busy wait                                             */
/* ------------------------------------------------------------------ */
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);

/* SoC / board bring-up, called by candleLight_fw/src/main.c */
void HAL_Init(void);

/* ------------------------------------------------------------------ */
/* CAN: STM32 FDCAN naming mapped onto HPM MCAN                        */
/* ------------------------------------------------------------------ */
typedef MCAN_Type FDCAN_GlobalTypeDef;

/* Operation modes / frame formats - same meaning as the STM32 names. */
#define FDCAN_MODE_NORMAL            0U
#define FDCAN_MODE_INTERNAL_LOOPBACK 1U
#define FDCAN_MODE_EXTERNAL_LOOPBACK 2U
#define FDCAN_MODE_BUS_MONITORING    3U

#define FDCAN_FRAME_CLASSIC 0U
#define FDCAN_FRAME_FD_BRS  1U

#define HAL_FDCAN_STATE_RESET 0U
#define HAL_FDCAN_STATE_READY 1U
#define HAL_FDCAN_STATE_BUSY  2U

typedef struct {
    FDCAN_GlobalTypeDef *Instance; /* HPM_MCAN0 .. HPM_MCAN3 */

    struct {
        /* Nominal (arbitration phase) bit timing */
        uint16_t NominalPrescaler;
        uint8_t NominalSyncJumpWidth;
        uint8_t NominalTimeSeg1; /* prop_seg + phase_seg1 */
        uint8_t NominalTimeSeg2;

        /* Data phase bit timing (CAN-FD) */
        uint16_t DataPrescaler;
        uint8_t DataSyncJumpWidth;
        uint8_t DataTimeSeg1;
        uint8_t DataTimeSeg2;

        uint8_t FrameFormat;         /* FDCAN_FRAME_* */
        uint8_t Mode;                /* FDCAN_MODE_* */
        uint8_t AutoRetransmission;  /* ENABLE / DISABLE */
    } Init;

    uint32_t State;        /* HAL_FDCAN_STATE_* */
    bool     configured;   /* set once can_enable() succeeded */
} FDCAN_HandleTypeDef;

/* ------------------------------------------------------------------ */
/* Unique ID, used by candleLight_fw/src/usbd_desc.c for the USB iSerial */
/* Filled from the chip UUID by HAL_Init(); not a compile-time constant  */
/* because two boards must not report the same serial number.            */
/* ------------------------------------------------------------------ */
extern uint8_t hpm_uid[12];
#define UID_BASE ((uint32_t)(uintptr_t)hpm_uid)
