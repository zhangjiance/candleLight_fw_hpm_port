/*
 * ws2812.h - WS2812 (NeoPixel) status LEDs on the HSCanT.
 *
 * The closed-source HSCanT application drives eight WS2812 LEDs over SPI1
 * (0xF0074000) with the data pin on PA29 (SPI1_MOSI), 8 Mbit/s, MSB first.
 * The bit encoding recovered from the image is
 *
 *     0 bit -> 0xE0 (1110 0000, ~40 % high)
 *     1 bit -> 0xF8 (1111 1000, ~60 % high)
 *
 * which is exactly what a WS2812B expects at 1.25 us per bit; the byte order on
 * the wire is G, R, B.  SPI is used instead of a bit-banged/GPTMR+DMA scheme,
 * so the only per-frame cost is one DMA-driven 192 byte transfer, plus a low
 * ("reset") trailer that lets the strip latch the frame (ws2812.c).
 */
#ifndef CANDLE_WS2812_H
#define CANDLE_WS2812_H

#include <stdbool.h>
#include <stdint.h>

/* Number of LEDs in the chain is owned by the board package
 * (BOARD_WS2812_LED_COUNT, only required when BOARD_HAS_WS2812 is set). */

/* Bring up SPI1 + DMA and blank the strip.  Needs the DMA manager to be up. */
void ws2812_init(void);

void ws2812_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
void ws2812_fill(uint8_t r, uint8_t g, uint8_t b);

/* Re-encode the frame buffer and hand it to SPI.  With blocking == true the
 * call returns once the transfer has completed.  Either way the call first waits
 * for the previous frame to leave the SPI, so two consecutive calls are safe
 * and each frame is latched by the strip. */
void ws2812_update(bool blocking);

/* True while the SPI DMA of the last frame is still in flight (the frame may
 * still be shifting out of the SPI FIFO after that - see ws2812_update). */
bool ws2812_is_busy(void);

#endif /* CANDLE_WS2812_H */
