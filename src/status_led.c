/*
 * status_led.c - see status_led.h.
 *
 * LED chain order (electrical: chain LED 0 is the first one driven by PA29),
 * measured on hardware and confirmed by the closed firmware's own LED indices
 * (FUN_ram_8002e2d0 hands channel c the LED pair c / c + 4, and FUN_80023908
 * paints index <= 3 green and index > 3 blue):
 *
 *     index  0      1      2      3      4      5      6      7
 *            CAN0   CAN1   CAN2   CAN3   CAN0   CAN1   CAN2   CAN3
 *            TX     TX     TX     TX     RX     RX     RX     RX
 *
 * so for channel n the transmit LED is n and the receive LED is
 * n + CHAN_LED_COUNT.  Transmitted frames light green, received ones blue.
 *
 * Per-channel indication, taken from the closed firmware's own LED task
 * (FUN_ram_8002e2d0 / FUN_80023aea / FUN_8002e18c):
 *
 *   - while the host keeps a channel started, both of its LEDs are lit in the
 *     channel colour - transmit green, receive blue - at the closed firmware's
 *     brightness 0x06 (FUN_ram_80023908), and
 *   - every frame on that channel forces the matching LED dark for 30 ms and it
 *     comes back on afterwards.  The dark window can only be re-armed once
 *     every 45 ms, so sustained traffic flickers (30 ms dark / 15 ms lit)
 *     instead of staying dark.  In the closed firmware's state 3 the two LEDs
 *     run on slightly shifted time bases (transmit -3 ms, receive -15 ms), so
 *     their dark windows are 27 ms / 15 ms.
 *
 * So the indication is a dark pulse on a lit LED, not a brighter flash: with
 * the LEDs already on, a purely brighter overprint is barely noticeable.
 *
 * Boards without a strip (BOARD_HAS_WS2812 == 0) get LED_COUNT == 0 and all the
 * code below turns into no-ops, so the module can stay in the common source
 * list.
 *
 * Besides the strip, status_led_tick() also owns the plain-GPIO "running" LED
 * that some boards carry (BOARD_HAS_SYS_LED, board_led_toggle()): it is toggled
 * every SYS_LED_TOGGLE_MS, so a live main loop can be told from a dead one
 * without a debugger.  Boards without such an LED skip it, and the toggling
 * does not depend on the strip (LED_COUNT == 0 is fine).
 *
 * The third thing here is the boot animation (status_led_boot_anim), the
 * single-click comet of feat/hscant-mode-ws2812: a rainbow head sweeps the
 * strip twice and the LEDs behind it fade out.  It is started by main.c right
 * after status_led_init() and paced by status_led_tick(), so the strip is only
 * occupied for the first ~1 s instead of blocking the boot.
 */

#include "status_led.h"

#include "board.h"
#include "hal_include.h" /* HAL_GetTick */
#include "ws2812.h"

/* A board that claims to have a strip must also say how many LEDs it has;
 * otherwise LED_COUNT would silently fall back to 0 (all LEDs dead). */
#if defined(BOARD_HAS_WS2812) && BOARD_HAS_WS2812 && !defined(BOARD_WS2812_LED_COUNT)
#error "BOARD_HAS_WS2812 is set but BOARD_WS2812_LED_COUNT is missing"
#endif

#ifndef BOARD_WS2812_LED_COUNT
#define BOARD_WS2812_LED_COUNT 0
#endif

#define LED_COUNT              BOARD_WS2812_LED_COUNT
#define CHAN_LED_COUNT         (LED_COUNT / 2U)

#define CHAN_STEADY_V          (0x06U) /* closed firmware's channel brightness */
#define CHAN_TRAFFIC_OFF_MS    (30U)   /* fun_8002e18c: field8 = t + 30        */
#define CHAN_TRAFFIC_REARM_MS  (45U)   /* fun_8002e18c: field4 = t + 45        */
#define CHAN_TX_SKEW_MS        (3U)    /* state 3: fun_80023aea(ledA, ts - 3)  */
#define CHAN_RX_SKEW_MS        (15U)   /* state 3: fun_80023aea(ledB, ts - 15) */

#define RENDER_MIN_INTERVAL_MS (10U)

/* Plain-GPIO "running" LED: half period, i.e. 1 Hz / 50 % duty. */
#define SYS_LED_TOGGLE_MS      (500U)

/* Boot animation: the comet from feat/hscant-mode-ws2812's single-click show
 * (status_led_click_show()).  The head sweeps LED 0 -> 7 -> 0 ANIM_SWEEPS
 * times, one step every ANIM_FRAME_MS; the tail halves per LED behind the head
 * (ANIM_COMET_V >> behind), so it fades out after 5 LEDs. */
#define ANIM_FRAME_MS          (30U)
#define ANIM_SWEEPS            (1U)
#define ANIM_COMET_V           (0x20U)

/* Closed firmware index maths (FUN_ram_8002e2d0): channel c owns the chain LED
 * c for transmit and c + CHAN_LED_COUNT for receive. */
#define CHAN_TX_LED(c)         ((uint32_t)(c))
#define CHAN_RX_LED(c)         ((uint32_t)(c) + CHAN_LED_COUNT)

/* Closed firmware's traffic dark window, per LED: s_off_until == field8 and
 * s_rearm_at == field4 of FUN_8002e18c. */
static uint32_t s_off_until[LED_COUNT > 0U ? LED_COUNT : 1U];
static uint32_t s_rearm_at[LED_COUNT > 0U ? LED_COUNT : 1U];
static bool s_off_active[LED_COUNT > 0U ? LED_COUNT : 1U];

/* Channels the host has started (GS_CAN_MODE_START). */
static bool s_chan_on[CHAN_LED_COUNT > 0U ? CHAN_LED_COUNT : 1U];

static uint32_t s_last_render_ms;
static volatile bool s_dirty;
static bool s_ready;

/* Boot animation (status_led_boot_anim): frames left to paint, the frame index
 * (which sets the colour and the head position) and when the next one is due. */
static uint32_t s_comet_left;
static uint32_t s_comet_frame;
static uint32_t s_comet_next_ms;

/* Only boards with a plain-GPIO "running" LED keep the toggle timestamp. */
#if defined(BOARD_HAS_SYS_LED) && BOARD_HAS_SYS_LED
static uint32_t s_last_sys_led_ms;
#endif

/* 8 bit HSV -> RGB (h: 0..255 = 0..360 deg).  Verbatim from
 * feat/hscant-mode-ws2812, where it colourises the comet. */
static void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint8_t region = (uint8_t)(h / 43U);
    const uint8_t rem = (uint8_t)((h - (region * 43U)) * 6U);
    const uint8_t p = (uint8_t)(((uint32_t)v * (255U - s)) >> 8);
    const uint8_t q = (uint8_t)(((uint32_t)v * (255U - (((uint32_t)s * rem) >> 8))) >> 8);
    const uint8_t t = (uint8_t)(((uint32_t)v * (255U - (((uint32_t)s * (255U - rem)) >> 8))) >> 8);

    switch (region) {
    case 0U: *r = v; *g = t; *b = p; break;
    case 1U: *r = q; *g = v; *b = p; break;
    case 2U: *r = p; *g = v; *b = t; break;
    case 3U: *r = p; *g = q; *b = v; break;
    case 4U: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

static void compose(void)
{
    for (uint32_t i = 0U; i < LED_COUNT; i++) {
        /* Chain LEDs 0..3 are the transmit LEDs of CAN0..CAN3, 4..7 the receive
         * LEDs. */
        const bool tx = (i < CHAN_LED_COUNT);
        const uint32_t channel = tx ? i : (i - CHAN_LED_COUNT);
        uint8_t r = 0U;
        uint8_t g = 0U;
        uint8_t b = 0U;

        if ((channel < CHAN_LED_COUNT) && s_chan_on[channel] && !s_off_active[i]) {
            if (tx) {
                g = CHAN_STEADY_V;
            } else {
                b = CHAN_STEADY_V;
            }
        }

        ws2812_set_pixel(i, r, g, b);
    }
}

void status_led_init(void)
{
    ws2812_init();
    ws2812_fill(0U, 0U, 0U);
    ws2812_update(true);
    s_ready = true;
}

/*
 * One frame of the boot comet.  The head position bounces between the two ends
 * of the strip (frame % 2 * LED_COUNT is a full round trip) and the LEDs behind
 * it are the tail, each one dimmer than the one in front; the colour is taken
 * from the frame counter and the LED index, which is what makes the rainbow
 * flow while the head moves.
 */
static void comet_render(uint32_t frame)
{
    const uint32_t phase = frame % (2U * LED_COUNT);
    const uint32_t head = (phase < LED_COUNT) ? phase : ((2U * LED_COUNT) - 1U - phase);

    for (uint32_t led = 0U; led < LED_COUNT; led++) {
        uint8_t r = 0U;
        uint8_t g = 0U;
        uint8_t b = 0U;

        if (led <= head) {
            const uint32_t behind = head - led;

            if (behind <= 4U) {
                hsv2rgb((uint8_t)((frame * 20U) + (uint8_t)(led * 12U)), 255U,
                        (uint8_t)(ANIM_COMET_V >> behind), &r, &g, &b);
            }
        }
        ws2812_set_pixel(led, r, g, b);
    }

    ws2812_update(true);
}

void status_led_boot_anim(void)
{
    if ((!s_ready) || (LED_COUNT == 0U)) {
        return;
    }

    s_comet_left = (2U * LED_COUNT) * ANIM_SWEEPS;
    s_comet_frame = 0U;
    s_comet_next_ms = HAL_GetTick();
}

void status_led_channel(uint8_t channel, bool enabled)
{
    if (channel >= CHAN_LED_COUNT) {
        return;
    }

    s_chan_on[channel] = enabled;

    if (!enabled) {
        /* Drop any leftover dark window so both LEDs go dark at once and a
         * re-started channel begins lit. */
        const uint32_t tx = CHAN_TX_LED(channel);
        const uint32_t rx = CHAN_RX_LED(channel);

        s_off_active[tx] = false;
        s_off_active[rx] = false;
        s_rearm_at[tx] = 0U;
        s_rearm_at[rx] = 0U;
    }

    if (s_ready) {
        s_dirty = true;
    }
}

void status_led_activity(uint8_t channel, bool rx)
{
    /* Chain: CAN0..CAN3 TX, then CAN0..CAN3 RX. */
    const uint32_t index = rx ? CHAN_RX_LED(channel) : CHAN_TX_LED(channel);
    const uint32_t now = HAL_GetTick();
    /* Closed firmware state 3 evaluates the window on a shifted time base:
     * transmit -3 ms, receive -15 ms, which is what makes their dark windows
     * 27 ms and 15 ms instead of 30 ms. */
    const uint32_t t = now - (rx ? CHAN_RX_SKEW_MS : CHAN_TX_SKEW_MS);

    if ((!s_ready) || (index >= LED_COUNT)) {
        return;
    }

    /* FUN_8002e18c: only re-arm the dark window once the previous arming is
     * CHAN_TRAFFIC_REARM_MS old, then keep the LED dark for
     * CHAN_TRAFFIC_OFF_MS.  A frame inside the re-arm time does not extend it,
     * so sustained traffic flickers instead of staying dark. */
    if ((int32_t)(t - s_rearm_at[index]) >= 0) {
        s_off_until[index] = t + CHAN_TRAFFIC_OFF_MS;
        s_rearm_at[index] = t + CHAN_TRAFFIC_REARM_MS;
        s_off_active[index] = true;
        s_dirty = true;
    }
}

void status_led_tick(void)
{
    const uint32_t now = HAL_GetTick();

    if (!s_ready) {
        return;
    }

    /* "Running" LED heartbeat: independent of the strip, so a board with only
     * this LED still gets it (LED_COUNT == 0), and it keeps blinking while the
     * boot comet below owns the strip. */
#if defined(BOARD_HAS_SYS_LED) && BOARD_HAS_SYS_LED
    if ((uint32_t)(now - s_last_sys_led_ms) >= SYS_LED_TOGGLE_MS) {
        s_last_sys_led_ms = now;
        board_led_toggle();
    }
#endif

    /* Boot animation: while it runs it owns the strip, so the traffic
     * housekeeping and the repaint below are skipped - the composed frame (all
     * dark at boot) would overwrite the comet otherwise.
     *
     * The next deadline is re-based on the current time instead of accumulating
     * ANIM_FRAME_MS (as the original show does): the animation is armed before
     * the rest of the boot, so the first tick can be milliseconds later - an
     * accumulated deadline would consider all 32 frames overdue and flash them
     * in as many consecutive loop iterations, i.e. in microseconds. */
    if (s_comet_left > 0U) {
        if ((int32_t)(now - s_comet_next_ms) >= 0) {
            s_comet_next_ms = now + ANIM_FRAME_MS;
            comet_render(s_comet_frame);
            s_comet_frame++;
            s_comet_left--;
            if (s_comet_left == 0U) {
                s_dirty = true; /* animation over: back to the channel state */
            }
        }
        return;
    }

    for (uint32_t i = 0U; i < LED_COUNT; i++) {
        if (s_off_active[i] && ((int32_t)(now - s_off_until[i]) >= 0)) {
            s_off_active[i] = false; /* dark window over: LED back on */
            s_dirty = true;
        }
    }

    /*
     * s_dirty means "the strip is out of date"; it is cleared only once the
     * frame has actually been painted, so no state change can be swallowed by
     * the render throttle (a state change dropped there would never be painted
     * - e.g. the host starting a second channel a few ms after the first one,
     * or a traffic pulse inside the window).
     */
    if (s_dirty && ((uint32_t)(now - s_last_render_ms) >= RENDER_MIN_INTERVAL_MS)) {
        s_dirty = false; /* painted: only now is it safe to clear */
        compose();
        ws2812_update(true);
        s_last_render_ms = now;
    }
}
