#ifndef CANDLE_WS2812_H
#define CANDLE_WS2812_H

#include <stdbool.h>
#include <stdint.h>

/* The chain length comes from BOARD_WS2812_LED_COUNT (board package). */

/* Bring up SPI1 + DMA and blank the strip.  Needs the DMA manager to be up. */
void ws2812_init(void);

void ws2812_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
void ws2812_fill(uint8_t r, uint8_t g, uint8_t b);

/* Push the frame buffer to SPI (waits for the previous frame); blocking = wait too. */
void ws2812_update(bool blocking);

/* True while the last frame's SPI DMA is in flight. */
bool ws2812_is_busy(void);

#endif /* CANDLE_WS2812_H */
