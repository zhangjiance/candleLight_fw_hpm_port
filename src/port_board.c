/*
 * port_board.c - glue between candleLight_fw's board abstraction
 * (port/board_layout.h) and the selected board package (boards/<BOARD>/).
 *
 * This file contains no board specific constants.  Everything comes from the
 * board package through the contract documented in port/board_contract.h:
 *
 *   BOARD_CAN_INSTANCES / BOARD_CAN_COUNT
 *       the MCAN instances wired on the board
 *   BOARD_HAS_SYS_LED
 *       whether board_led_toggle() drives a real "running" LED
 *   board_init(), board_init_usb(), board_delay_ms(), init_can_pins()
 *       clock tree, pad muxing, USB PHY
 *   board_init_can_clock()
 *       board owned CAN clock source/divider
 */

#include "board_layout.h" /* candleLight's board abstraction, to be filled in */
#include "can.h"
#include "config.h"
#include "gpio.h"
#include "hal_include.h"

#include "hpm_soc.h"
#include "board.h"          /* the board package */
#include "board_contract.h" /* required macros + compile time checks */
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_gpio_drv.h"
#include "hpm_mcan_drv.h"

#include "dfu.h"
#include "timer.h"

static void board_setup(USBD_GS_CAN_HandleTypeDef *hcan);

/* MCAN instances wired on this board, from the board package. */
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

#ifdef TERM_Pin
static uint32_t s_termination;

static void board_termination_set(can_data_t *channel, enum gs_can_termination_state state)
{
    board_can_set_termination((uint8_t)channel->nr,
                              (state == GS_CAN_TERMINATION_STATE_ON) ? 1U : 0U);
}

enum gs_can_termination_state set_term(can_data_t *channel, enum gs_can_termination_state state)
{
    uint8_t nr = (uint8_t)channel->nr;

    if (config.termination_set == NULL) {
        return GS_CAN_TERMINATION_UNSUPPORTED;
    }

    if (state == GS_CAN_TERMINATION_STATE_ON) {
        s_termination |= (1UL << (nr & 31U));
    } else if (state == GS_CAN_TERMINATION_STATE_OFF) {
        s_termination &= ~(1UL << (nr & 31U));
    } else {
        return GS_CAN_TERMINATION_UNSUPPORTED;
    }

    config.termination_set(channel, state);

    return state;
}

enum gs_can_termination_state get_term(can_data_t *channel)
{
    if (config.termination_set == NULL) {
        return GS_CAN_TERMINATION_UNSUPPORTED;
    }

    return (((s_termination >> ((uint8_t)channel->nr & 31U)) & 1UL) != 0UL) ?
           GS_CAN_TERMINATION_STATE_ON : GS_CAN_TERMINATION_STATE_OFF;
}
#endif

/*
 * Long-press the user button (~500 ms) to request the DFU bootloader.  Called
 * from the candleLight main loop through config.mainloop_callback; the hold
 * time is measured with timer_get() so the CAN/USB loop is never blocked.
 */
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

struct BoardConfig config = {
    .setup = board_setup,
    /* The CAN transceivers are left in the state the board package put them in
     * at boot.  Wire these up (and the termination GPIO) if a board provides
     * switchable power / termination. */
    .phy_power_set = NULL,
#ifdef TERM_Pin
    .termination_set = board_termination_set,
#else
    .termination_set = NULL,
#endif
#ifdef BOARD_APP_GPIO_CTRL
    .mainloop_callback = board_mainloop_callback,
#else
    .mainloop_callback = NULL,
#endif
    /* channels[] is deliberately left zero initialized and filled in by
     * board_setup(), so this file stays correct for any NUM_CAN_CHANNEL.
     * (leds[].port == NULL means "no RX/TX LED wired yet".) */
};

static void board_setup(USBD_GS_CAN_HandleTypeDef *hcan)
{
    unsigned int count;

    (void)hcan;

    board_mcan_msg_buf_init();

    count = (NUM_CAN_CHANNEL < BOARD_CAN_COUNT) ? NUM_CAN_CHANNEL : BOARD_CAN_COUNT;

    for (unsigned int i = 0U; i < count; i++) {
        MCAN_Type *instance = s_can_instances[i];

        /* Publish the instance so main.c's can_init() sees it, then make sure
         * the pads are muxed and the CAN clock is running. */
        config.channels[i].interface = instance;
        init_can_pins(instance);
        (void)board_init_can_clock(instance);
    }

}
