/*
 * board_layout.h - candleLight_fw's board abstraction.
 *
 * A verbatim port of the structs in candleLight_fw/include/board.h, but living
 * in the port layer under a different name.
 *
 * Why: the board package (boards/<BOARD>/board.h) must keep its natural name,
 * so "board.h" has to mean "the board package".  Therefore the original
 * candleLight_fw/include/board.h is excluded from the include path
 * (see the filtered include directory in CMakeLists.txt) and its content is
 * provided here instead.  candleLight_fw/src/main.c is the only consumer and
 * pulls this header in through its documented platform include seam.
 */

#pragma once

#include "config.h"
#include "can.h"
#include "gs_usb.h"
#include "led.h"
#include "usbd_gs_can.h"

struct LEDConfig {
    GPIO_TypeDef *port;
    uint16_t pin;
    bool active_high;
    bool invert;
};

struct BoardChannelConfig {
    FDCAN_GlobalTypeDef *interface;
    struct LEDConfig leds[LED_MAX];
};

struct BoardConfig {
    void (*setup)(USBD_GS_CAN_HandleTypeDef *hcan);
    void (*phy_power_set)(can_data_t *channel, bool enable);
    void (*termination_set)(can_data_t *channel, enum gs_can_termination_state state);
    void (*mainloop_callback)(void);

    struct BoardChannelConfig channels[NUM_CAN_CHANNEL];
};

/* Not const: src/port_board.c fills channels[].interface from the board
 * package (board_can_instances[]) at runtime, which keeps the MCAN list board
 * specific instead of hardcoded in the port layer. */
extern struct BoardConfig config;
