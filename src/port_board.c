
#include "board_layout.h" /* CandleLight board API */
#include "can.h"
#include "can_common.h"
#include "config.h"
#include "gpio.h"
#include "hal_include.h"

#include "hpm_soc.h"
#include "board.h"          /* hardware board */
#include "board_contract.h" /* board contract */
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_gpio_drv.h"
#include "hpm_mcan_drv.h"

#include "dfu.h"
#include "timer.h"

/* Board MCAN instances. */
static MCAN_Type *const s_can_instances[BOARD_CAN_COUNT] = { BOARD_CAN_INSTANCES };

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_mcan_msg_buf[BOARD_CAN_COUNT][MCAN_MSG_BUF_SIZE_IN_WORDS];

static void board_mcan_msg_buf_init(void)
{
    for (uint32_t i = 0U; i < BOARD_CAN_COUNT; i++) {
        mcan_msg_buf_attr_t attr;

        attr.ram_base = (uint32_t)&s_mcan_msg_buf[i][0];
        attr.ram_size = sizeof(s_mcan_msg_buf[i]);
        (void)mcan_set_msg_buf_attr(s_can_instances[i], &attr);
    }
}
#else
static void board_mcan_msg_buf_init(void)
{
}
#endif

#ifdef CONFIG_TERMINATION
static uint32_t s_termination;

static void board_termination_set_cb(can_data_t *channel, enum gs_can_termination_state state)
{
    board_can_set_termination((uint8_t)can_channel_get_nr(channel),
                              (state == GS_CAN_TERMINATION_STATE_ON) ? 1U : 0U);
}

void set_term(can_data_t *channel, enum gs_can_termination_state state)
{
    uint8_t nr = can_channel_get_nr(channel);

    if (config.termination_set == NULL) {
        return;
    }

    if (state == GS_CAN_TERMINATION_STATE_ON) {
        s_termination |= (1UL << (nr & 31U));
    } else if (state == GS_CAN_TERMINATION_STATE_OFF) {
        s_termination &= ~(1UL << (nr & 31U));
    } else {
        return;
    }

    config.termination_set(channel, state);
}

enum gs_can_termination_state get_term(can_data_t *channel)
{
    if (config.termination_set == NULL) {
        return GS_CAN_TERMINATION_UNSUPPORTED;
    }

    return (((s_termination >> (can_channel_get_nr(channel) & 31U)) & 1UL) != 0UL) ?
           GS_CAN_TERMINATION_STATE_ON : GS_CAN_TERMINATION_STATE_OFF;
}
#endif

/* Long-press to enter DFU. */
#ifdef BOARD_APP_GPIO_CTRL
static void board_mainloop_callback(void)
{
    static bool btn_held = false;
    static uint32_t btn_start_us = 0U;

    if (gpio_read_pin(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX,
                      BOARD_APP_GPIO_PIN) == BOARD_BUTTON_PRESSED_VALUE) {
        if (!btn_held) {
            btn_held = true;
            btn_start_us = timer_get();
        } else if ((uint32_t)(timer_get() - btn_start_us) >= 500000U) {
            dfu_run_bootloader();
        }
    } else {
        btn_held = false;
    }
}
#endif

struct board_config config = {
    .phy_power_set = NULL,
#ifdef CONFIG_TERMINATION
    .termination_set = board_termination_set_cb,
#else
    .termination_set = NULL,
#endif
#ifdef BOARD_APP_GPIO_CTRL
    .mainloop_callback = board_mainloop_callback,
#else
    .mainloop_callback = NULL,
#endif
};

void board_setup(USBD_GS_CAN_HandleTypeDef *hcan)
{
    unsigned int count;

    (void)hcan;

    board_mcan_msg_buf_init();

    count = (NUM_CAN_CHANNEL < BOARD_CAN_COUNT) ? NUM_CAN_CHANNEL : BOARD_CAN_COUNT;

    for (unsigned int i = 0U; i < count; i++) {
        MCAN_Type *instance = s_can_instances[i];

        /* Publish instance, mux pins, start clock. */
        config.channel[i].interface = instance;
        init_can_pins(instance);
        (void)board_init_can_clock(instance);
    }
}
