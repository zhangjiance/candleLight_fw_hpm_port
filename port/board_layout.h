
#pragma once

#include <stdbool.h>

#include "config.h"
#include "can.h"
#include "gs_usb.h"
#include "led.h"
#include "usbd_gs_can.h"

/* CandleLight board abstraction mapped onto the HPMicro board package. */

struct led_config {
    GPIO_TypeDef *port;
    uint16_t pin;
    bool active_high;
};

struct board_channel_config {
    FDCAN_GlobalTypeDef *interface;
    struct led_config leds[LED_MAX];
};

struct board_config {
    void (*phy_power_set)(can_data_t *channel, bool enable);
    void (*termination_set)(can_data_t *channel, enum gs_can_termination_state state);
    /* Extra: DFU button callback. */
    void (*mainloop_callback)(void);

    struct board_channel_config channel[NUM_CAN_CHANNEL];
};

extern struct board_config config;

void board_setup(USBD_GS_CAN_HandleTypeDef *hcan);

#define SET_PHY_POWER_FN(set_fn) \
        .phy_power_set = (set_fn),

#define SET_TERMINATION_FN(set_fn) \
        .termination_set = (set_fn),

static inline void board_phy_power_set(can_data_t *channel, bool enable)
{
    if (config.phy_power_set) {
        config.phy_power_set(channel, enable);
    }
}

static inline void board_termination_set(can_data_t *channel,
                                         enum gs_can_termination_state state)
{
    if (config.termination_set) {
        config.termination_set(channel, state);
    }
}
