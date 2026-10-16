#ifndef CANDLE_STATUS_LED_H
#define CANDLE_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

/* Bring the strip up (SPI1 + DMA) and blank it.  Needs the DMA manager up. */
void status_led_init(void);

/* Report a channel start/stop (both LEDs stay lit while started). */
void status_led_channel(uint8_t channel, bool enabled);

/* Report one frame on a channel (rx == true for a received frame). */
void status_led_activity(uint8_t channel, bool rx);

/* Non-blocking: ages the dark windows, toggles the GPIO LED, pushes the frame. */
void status_led_tick(void);

/* Start the boot comet (non-blocking, paced by status_led_tick()); call after status_led_init(). */
void status_led_boot_anim(void);

#endif /* CANDLE_STATUS_LED_H */
