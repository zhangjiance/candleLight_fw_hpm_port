
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

extern struct BoardConfig config;
