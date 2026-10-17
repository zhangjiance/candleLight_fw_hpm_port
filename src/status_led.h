#ifndef CANDLE_STATUS_LED_H
#define CANDLE_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

/* Bring up the board's indicator LEDs (WS2812 chain or discrete GPIO) and
 * blank them.  On the WS2812 backend the DMA manager must already be up. */
void status_led_init(void);

/* Report a channel start/stop (status LED on/off; error indication cleared on stop). */
void status_led_channel(uint8_t channel, bool enabled);

/* Report one frame on a channel (rx == true for a received frame). */
void status_led_activity(uint8_t channel, bool rx);

/* Report a channel error: the error LED blinks while active, then goes off. */
void status_led_error(uint8_t channel, bool active);

/* Non-blocking: ages the traffic/error windows and refreshes the board's LEDs. */
void status_led_tick(void);

/* Start the boot comet (non-blocking, paced by status_led_tick()); call after status_led_init(). */
void status_led_boot_anim(void);

#endif /* CANDLE_STATUS_LED_H */
