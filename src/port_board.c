/*
 * port_board.c - glue between candleLight_fw's board abstraction
 * (port/board_layout.h) and the selected board package (boards/<BOARD>/).
 *
 * This file contains no board specific constants.  Everything comes from the
 * board package through the contract documented in port/board_contract.h:
 *
 *   BOARD_CAN_INSTANCES / BOARD_CAN_COUNT / BOARD_CAN_CLOCKS
 *       the MCAN instances wired on the board and their clock gates
 *   BOARD_HAS_SYS_LED
 *       whether board_led_toggle() drives a real "running" LED
 *   board_init(), board_init_usb(), board_delay_ms(), init_can_pins()
 *       clock tree, pad muxing, USB PHY
 *   board_init_can_clock()
 *       optional: a generic default is provided below as a weak symbol
 */

#include "board_layout.h" /* candleLight's board abstraction, to be filled in */
#include "can.h"
#include "config.h"
#include "hal_include.h"

#include "hpm_soc.h"
#include "board.h"          /* the board package */
#include "board_contract.h" /* required macros + compile time checks */
#include "hpm_clock_drv.h"

static void board_setup(USBD_GS_CAN_HandleTypeDef *hcan);

/* MCAN instances wired on this board, from the board package. */
static MCAN_Type *const s_can_instances[BOARD_CAN_COUNT] = { BOARD_CAN_INSTANCES };

struct BoardConfig config = {
    .setup = board_setup,
    /* The CAN transceivers are left in the state the board package put them in
     * at boot.  Wire these up (and the termination GPIO) if a board provides
     * switchable power / termination. */
    .phy_power_set = NULL,
    .termination_set = NULL,
    .mainloop_callback = NULL,
    /* channels[] is deliberately left zero initialized and filled in by
     * board_setup(), so this file stays correct for any NUM_CAN_CHANNEL.
     * (leds[].port == NULL means "no RX/TX LED wired yet".) */
};

/*
 * Generic CAN clock default.
 *
 * Most HPM boards let clock.c configure clk_canN (source + divider) and only
 * need the gate enabled, which is exactly what this does.  A board whose CAN
 * clock differs overrides it with a strong definition in its board.c
 * (boards/hpm5321_usb2can does, because it also programs the divider).
 */
__attribute__((weak)) uint32_t board_init_can_clock(MCAN_Type *ptr)
{
    static const clock_name_t clocks[BOARD_CAN_COUNT] = { BOARD_CAN_CLOCKS };

    for (uint32_t i = 0U; i < BOARD_CAN_COUNT; i++) {
        if (ptr == s_can_instances[i]) {
            clock_add_to_group(clocks[i], 0U);
            return clock_get_frequency(clocks[i]);
        }
    }

    return 0U;
}

static void board_mainloop(void)
{
#if BOARD_HAS_SYS_LED
    static uint32_t next_toggle = 0U;
    uint32_t now = HAL_GetTick();

    if ((int32_t)(now - next_toggle) >= 0) {
        next_toggle = now + 500U;
        board_led_toggle();
    }
#endif
}

static void board_setup(USBD_GS_CAN_HandleTypeDef *hcan)
{
    unsigned int count;

    (void)hcan;

    count = (NUM_CAN_CHANNEL < BOARD_CAN_COUNT) ? NUM_CAN_CHANNEL : BOARD_CAN_COUNT;

    for (unsigned int i = 0U; i < count; i++) {
        MCAN_Type *instance = s_can_instances[i];

        /* Publish the instance so main.c's can_init() sees it, then make sure
         * the pads are muxed and the CAN clock is running. */
        config.channels[i].interface = instance;
        init_can_pins(instance);
        (void)board_init_can_clock(instance);
    }

    config.mainloop_callback = board_mainloop;
}
