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
#include "hpm_romapi.h"

#include "device.h"
#include "hal_include.h"
#include "timer.h"

/*
 * Device unique ID used for the USB iSerialNumber.
 *
 * This has to be unique per board.  Windows builds the device instance id as
 * USB\VID_xxxx&PID_xxxx\<serial> and will not enumerate a second device whose
 * serial collides, so a constant value breaks as soon as two boards are
 * plugged in at once (and makes `dfu-util -S <serial>` useless).
 *
 * Loaded from the top 96 bits of the chip UUID in the OTP shadow
 * (OTP_SOC_UUID_IDX = 88, 4 words) through the boot ROM API -- the same way the
 * SDK's TinyUSB BSP does it in board_get_unique_id().  The bootloader reads
 * words 88..90 too, so one board reports the same serial in both modes.
 */
uint8_t hpm_uid[12];

#define OTP_UUID_WORD_IDX (88U) /* hpm_soc_feature.h: OTP_SOC_UUID_IDX */

static void hpm_uid_load(void)
{
    uint32_t i;

    for (i = 0U; i < 3U; i++) {
        const uint32_t word =
            ROM_API_TABLE_ROOT->otp_driver_if->read_from_shadow(OTP_UUID_WORD_IDX + i);

        hpm_uid[4U * i + 0U] = (uint8_t)(word);
        hpm_uid[4U * i + 1U] = (uint8_t)(word >> 8);
        hpm_uid[4U * i + 2U] = (uint8_t)(word >> 16);
        hpm_uid[4U * i + 3U] = (uint8_t)(word >> 24);
    }
}

void HAL_Init(void)
{
    board_init();

    /* Chip UUID -> USB iSerialNumber (must be unique per board). */
    hpm_uid_load();

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
