/*
 * ws2812.c - see ws2812.h.
 *
 * Implementation notes
 * --------------------
 * The strip is driven over SPI1 (PA29 = SPI1_MOSI) at 8 Mbit/s.  At that rate
 * one SPI bit is 125 ns, so a WS2812 bit is encoded as a byte:
 *
 *     bit '0' -> 0xE0   111 00000   ~375 ns high  (spec: 0.4 us +-0.15)
 *     bit '1' -> 0xF8   11111 000   ~625 ns high  (spec: 0.8 us +-0.15)
 *
 * These are the exact constants the closed-source image uses (they live in its
 * .data at gp+0x474 / gp+0x407).  The frame is 8 LEDs x 24 bits = 192 bytes,
 * sent with MSB first and in the canonical WS2812 G,R,B byte order, followed by
 * a low ("reset") trailer so the strip latches it - see WS2812_RESET_BYTES.
 *
 * The transfer is DMA backed through the SDK SPI component; a frame therefore
 * costs the main loop almost nothing.
 *
 * Boards without a strip (BOARD_HAS_WS2812 == 0) get no-op stubs so the module
 * can stay in the common source list.
 */

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

/*
 * Frame layout in the DMA buffer: 8 LEDs x 24 encoded bits, then a reset
 * trailer of zero bytes.  A zero byte holds the data line low for one byte time
 * (1 us at 8 Mbit/s), so the trailer keeps it low for well over the strip's
 * latch time - a WS2812/WS2812B latches a frame only after the line has been
 * low for >= 50 us (an SK6812 wants 80 us).
 *
 * The trailer is what makes two frames issued back to back safe: the boot
 * animation's last frame and the repaint of the channel state happen in
 * consecutive main loop iterations, microseconds apart.  Without a latch gap in
 * between, the strip sees ONE long bit stream, keeps the first 8 x 24 bits (the
 * animation's frame) and shifts the second frame out through the end of the
 * chain - it is lost, and the animation's last frame stays visible (its head LED
 * lit) until something else repaints the strip.
 */
#define WS2812_RESET_BYTES (96U)

#define WS2812_RGB_BYTES   ((uint32_t)BOARD_WS2812_LED_COUNT * WS2812_BITS_PER_LED)

ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4)
static uint8_t s_bits[WS2812_RGB_BYTES + WS2812_RESET_BYTES];

/* Give up on the line going idle after this many microseconds of polling: a
 * stuck SPIACTIVE must not hang the main loop, which also carries USB and CAN. */
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

    /*
     * The strip is fed by SPI + DMA, so the DMA manager and its clock have to
     * be up first.  Both are brought up here rather than by the caller: this is
     * the one place where BOARD_HAS_WS2812 is known to be visible, so a caller
     * (src/candle/main.c does not include the board package header) can never
     * compile the whole init away by accident.
     */
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

/*
 * Wait for the SPI to have shifted the whole previous frame out.  The DMA is not
 * a usable "frame is over" signal on its own: it completes as soon as its last
 * byte has been written into the SPI TX FIFO (SPI_SOC_FIFO_DEPTH = 8 bytes =
 * 8 us at 8 Mbit/s), so the tail of the frame - with the reset trailer - can
 * still be on its way to the strip.
 *
 * Returns false if the controller never reports idle.
 */
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

void ws2812_update(bool blocking)
{
    if (!s_ready) {
        return;
    }

    /*
     * Never rewrite the DMA buffer or restart the transfer while the previous
     * frame is still going out:
     *
     *  - rewriting s_bits under a running DMA would garble that frame, and
     *  - hpm_spi_transmit_nonblocking() fails with status_spi_master_busy while
     *    the SPI is active (hpm_spi_transfer_init() checks exactly that), and
     *    starting one also resets the TX FIFO, which drops the queued bytes.
     *
     * The previous version took that failure as "frame dropped" and returned.
     * That is what left the boot animation's last frame on the strip: the very
     * next frame - the repaint that was meant to clear it - was swallowed
     * because it was issued while the last animation frame was still shifting.
     * Waiting for the line to go idle makes the start succeed, and it also
     * guarantees the reset trailer has been sent, i.e. that the strip has
     * latched the frame.
     */
    if (!ws2812_wait_line_idle()) {
        /* Stuck controller: keep the frame the strip already has rather than
         * hanging the main loop. */
        return;
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

    /* The trailer (s_bits[WS2812_RGB_BYTES ..]) stays zero: it is only written
     * once, by ws2812_init()'s memset. */

    s_busy = true;
    if (hpm_spi_transmit_nonblocking(WS2812_SPI, s_bits, sizeof(s_bits)) != status_success) {
        s_busy = false;
        return;
    }

    if (blocking) {
        while (s_busy) {
            ;
        }
    }
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

void ws2812_update(bool blocking)
{
    (void)blocking;
}

bool ws2812_is_busy(void)
{
    return false;
}

#endif /* BOARD_HAS_WS2812 */
