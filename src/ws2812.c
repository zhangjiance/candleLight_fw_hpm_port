
#include "ws2812.h"

#include "board.h"

#if defined(BOARD_HAS_WS2812) && BOARD_HAS_WS2812

#include <string.h>

#include "pinmux.h"
#include "hpm_common.h"
#include "hpm_clock_drv.h"
#include "hpm_csr_drv.h" /* hpm_csr_get_core_cycle, for the idle timeout */
#include "hpm_dma_mgr.h"
#include "hpm_spi.h"

#define WS2812_SPI_FREQ_HZ  (8000000UL)
#define WS2812_BITS_PER_LED (24U)
#define WS2812_BIT0         (0xE0U)
#define WS2812_BIT1         (0xF8U)

#ifndef BOARD_APP_SPI_BASE
#error "BOARD_HAS_WS2812 is set but the board package has no BOARD_APP_SPI_BASE"
#endif

#define WS2812_SPI BOARD_APP_SPI_BASE

/* Zero-byte trailer: the line must stay low longer than the latch time (50 us, 80 us SK6812). */
#define WS2812_RESET_BYTES (96U)

#define WS2812_RGB_BYTES   ((uint32_t)BOARD_WS2812_LED_COUNT * WS2812_BITS_PER_LED)

ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4)
static uint8_t s_bits[WS2812_RGB_BYTES + WS2812_RESET_BYTES];

/* Give up on the line going idle after this many us (a stuck SPIACTIVE must not hang the loop). */
#define WS2812_IDLE_TIMEOUT_US (2000U)

/* Per-LED R/G/B, only touched from the main loop. */
static uint8_t s_rgb[BOARD_WS2812_LED_COUNT][3];

static volatile bool s_busy;
static bool s_ready;

static void ws2812_tx_complete(uint32_t channel)
{
    (void)channel;
    s_busy = false;
}

void ws2812_init(void)
{
    spi_initialize_config_t init_config;

    if (s_ready) {
        return;
    }

    /* Bring up the DMA manager + clock here: this file is where BOARD_HAS_WS2812 is visible. */
    clock_add_to_group(clock_hdma, 0);
    dma_mgr_init();

    clock_add_to_group(BOARD_APP_SPI_CLK_NAME, 0);
    init_spi1_pins(); /* PA29 -> SPI1_MOSI */

    hpm_spi_get_default_init_config(&init_config);
    init_config.direction = spi_msb_first;
    init_config.mode = spi_master_mode;
    init_config.clk_phase = spi_sclk_sampling_odd_clk_edges;
    init_config.clk_polarity = spi_sclk_low_idle;
    init_config.data_len = 8U;

    if (hpm_spi_initialize(WS2812_SPI, &init_config) != status_success) {
        return;
    }
    if (hpm_spi_set_sclk_frequency(WS2812_SPI, WS2812_SPI_FREQ_HZ) != status_success) {
        return;
    }
    /* Allocates the DMA channels and enables their interrupts. */
    if (hpm_spi_dma_mgr_install_callback(WS2812_SPI, ws2812_tx_complete, NULL) != status_success) {
        return;
    }

    memset(s_rgb, 0, sizeof(s_rgb));
    memset(s_bits, 0, sizeof(s_bits));
    s_ready = true;
}

void ws2812_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= BOARD_WS2812_LED_COUNT) {
        return;
    }

    s_rgb[index][0] = r;
    s_rgb[index][1] = g;
    s_rgb[index][2] = b;
}

void ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
        ws2812_set_pixel(i, r, g, b);
    }
}

/* Wait for the line to go idle: DMA completion only means the last byte entered the FIFO. */
static bool ws2812_wait_line_idle(void)
{
    const uint32_t ticks_per_us = clock_get_core_clock_ticks_per_us();
    const uint64_t deadline =
        hpm_csr_get_core_cycle() + ((uint64_t)ticks_per_us * WS2812_IDLE_TIMEOUT_US);

    while (spi_is_active(WS2812_SPI)) {
        if (hpm_csr_get_core_cycle() > deadline) {
            return false;
        }
    }

    return true;
}

bool ws2812_update(bool blocking)
{
    if (!s_ready) {
        return true;
    }

    /* Never restart a running transfer (it would garble the frame; the SDK returns busy). */
    if (!ws2812_wait_line_idle()) {
        return false;
    }

    for (uint32_t led = 0; led < BOARD_WS2812_LED_COUNT; led++) {
        const uint8_t r = s_rgb[led][0];
        const uint8_t g = s_rgb[led][1];
        const uint8_t b = s_rgb[led][2];
        const uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
        const uint32_t base = led * WS2812_BITS_PER_LED;

        for (uint32_t bit = 0; bit < WS2812_BITS_PER_LED; bit++) {
            s_bits[base + bit] = ((grb >> (WS2812_BITS_PER_LED - 1U - bit)) & 1U) ? WS2812_BIT1 : WS2812_BIT0;
        }
    }

    /* The trailer stays zero (written once by ws2812_init()'s memset). */

    s_busy = true;
    if (hpm_spi_transmit_nonblocking(WS2812_SPI, s_bits, sizeof(s_bits)) != status_success) {
        s_busy = false;
        return false;
    }

    if (blocking) {
        while (s_busy) {
            ;
        }
    }

    return true;
}

bool ws2812_is_busy(void)
{
    return s_busy;
}

#else /* !BOARD_HAS_WS2812 */

void ws2812_init(void) {}

void ws2812_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    (void)index;
    (void)r;
    (void)g;
    (void)b;
}

void ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
{
    (void)r;
    (void)g;
    (void)b;
}

bool ws2812_update(bool blocking)
{
    (void)blocking;

    return true;
}

bool ws2812_is_busy(void)
{
    return false;
}

#endif /* BOARD_HAS_WS2812 */
