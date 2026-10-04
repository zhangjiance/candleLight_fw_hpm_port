/*
 * port_device.c - platform bring-up for candleLight_fw on HPMicro HPM5321/HPM5361.
 *
 * candleLight's main.c calls HAL_Init() and device_sysclock_config(); here
 * HAL_Init() performs the SoC/board bring-up (clock tree, pin mux, USB PHY)
 * and device_sysclock_config() is a no-op because board_init() already applies
 * the SoC clock tree.
 */

#include <stdint.h>

#include "hpm_soc.h"
#include "board.h"
#include "hpm_clock_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_interrupt.h"

#include "device.h"
#include "hal_include.h"
#include "timer.h"

/* Device unique ID used for the USB iSerialNumber.
 * Weak so that a board can override it with the real OTP/UUID. */
__attribute__((weak)) const uint8_t hpm_uid[12] = {
    0x48U, 0x50U, 0x4DU, 0x35U, 0x33U, 0x36U, 0x31U, 0x43U, 0x41U, 0x4EU, 0x46U, 0x44U
};

void HAL_Init(void)
{
    board_init();

    /* USB device controller + on-chip PHY (board package selects the pins,
     * the clock and the vbus source) */
    board_init_usb(BOARD_USB_BASE);
    intc_set_irq_priority(BOARD_USB_IRQn, 2U);

    /*
     * The HPM USB controller moves payloads with descriptor DMA (qHD/qTD), so
     * every buffer handed to it has to stay coherent.  candleLight_fw hands its
     * frame pool over directly, therefore keep the D-cache off.  The I-cache is
     * DMA-irrelevant and stays enabled.
     */
    l1c_dc_disable();
    l1c_ic_enable();
}

void device_sysclock_config(void)
{
    /* board_init() / board_init_clock() already configured the clock tree. */
}

/* ------------------------------------------------------------------ */
/* GPIO                                                               */
/* ------------------------------------------------------------------ */
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    (void)pin;

    if (port == NULL) {
        return;
    }

    gpio_write_pin(port->ctrl, port->group, (uint8_t)port->index,
                   (state == GPIO_PIN_SET) ? 1U : 0U);
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)pin;

    if (port == NULL) {
        return GPIO_PIN_RESET;
    }

    return gpio_read_pin(port->ctrl, port->group, (uint8_t)port->index) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)pin;

    if (port == NULL) {
        return;
    }

    gpio_toggle_pin(port->ctrl, port->group, (uint8_t)port->index);
}
