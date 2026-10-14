/*
 * status_led.h - owner of the board's WS2812 chain.
 *
 * Recovered from the closed-source image, the strip shows two things at once:
 *
 *  - per channel state: while the host keeps a CAN channel started, both of
 *    that channel's LEDs stay lit (transmit green, receive blue), and
 *  - per channel traffic: every frame forces the matching LED dark for 30 ms
 *    before it comes back on, so sustained traffic flickers.
 *
 * src/can_hpm.c reports channel start/stop through status_led_channel() and the
 * frames through status_led_activity(); status_led_tick() is called once per
 * main loop iteration and owns the render throttle.
 */
#ifndef CANDLE_STATUS_LED_H
#define CANDLE_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

/* Bring the strip up (SPI1 + DMA) and blank it.  Needs the DMA manager up. */
void status_led_init(void);

/* Report that the host started (or stopped) a CAN channel.  While a channel is
 * started its two LEDs stay lit.  Safe to call before status_led_init(). */
void status_led_channel(uint8_t channel, bool enabled);

/* Report one frame on a channel: rx == true for a received frame, false for a
 * transmitted one.  Safe to call before status_led_init(). */
void status_led_activity(uint8_t channel, bool rx);

/* Non-blocking: ages the traffic dark windows and pushes the frame to the strip
 * (throttled to RENDER_MIN_INTERVAL_MS). */
void status_led_tick(void);

#endif /* CANDLE_STATUS_LED_H */
