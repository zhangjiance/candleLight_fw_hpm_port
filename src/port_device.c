
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

#if defined(FLASH_UF2) && FLASH_UF2
#include "bl_setting.h"
#endif
#include "wdt_hpm.h"

/* USB iSerialNumber from the chip UUID (OTP shadow, 12 bytes); must be unique per board. */
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

#if defined(FLASH_UF2) && FLASH_UF2
    /* Clear the UF2 bootloader's bounced-boot counter and publish the version. */
    bl_setting_app_init();
#endif

    /* The bootloader may leave EWDG0 armed: clock it on and feed it before anything blocks. */
    wdt_hpm_init();

    /* Chip UUID -> USB iSerialNumber (must be unique per board). */
    hpm_uid_load();

    /* USB device controller + on-chip PHY (pins/clock/vbus from the board package). */
    board_init_usb(BOARD_USB_BASE);
    intc_set_irq_priority(BOARD_USB_IRQn, 2U);

    /* USB moves payloads by DMA and the frame pool is handed over: keep the D-cache off. */
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
