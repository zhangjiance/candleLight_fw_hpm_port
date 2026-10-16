
#include "status_led.h"

#include "board.h"
#include "hal_include.h" /* HAL_GetTick */
#include "ws2812.h"

/* A board with a strip must also give its LED count, or LED_COUNT falls back to 0. */
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

/* Boot comet: ANIM_SWEEPS round trips, tail = ANIM_COMET_V >> distance. */
#define ANIM_FRAME_MS          (30U)
#define ANIM_SWEEPS            (1U)
#define ANIM_COMET_V           (0x20U)

#define CHAN_TX_LED(c)         ((uint32_t)(c))
#define CHAN_RX_LED(c)         ((uint32_t)(c) + CHAN_LED_COUNT)

/* Traffic dark window per LED (FUN_8002e18c's field8 / field4). */
static uint32_t s_off_until[LED_COUNT > 0U ? LED_COUNT : 1U];
static uint32_t s_rearm_at[LED_COUNT > 0U ? LED_COUNT : 1U];
static bool s_off_active[LED_COUNT > 0U ? LED_COUNT : 1U];

/* Channels the host has started (GS_CAN_MODE_START). */
static bool s_chan_on[CHAN_LED_COUNT > 0U ? CHAN_LED_COUNT : 1U];

static uint32_t s_last_render_ms;
static volatile bool s_dirty;
static bool s_ready;

/* Boot animation state: frames left, frame index (colour + head) and next due time. */
static uint32_t s_comet_left;
static uint32_t s_comet_frame;
static uint32_t s_comet_next_ms;

/* Only boards with a plain-GPIO "running" LED keep the toggle timestamp. */
#if defined(BOARD_HAS_SYS_LED) && BOARD_HAS_SYS_LED
static uint32_t s_last_sys_led_ms;
#endif

/* 8 bit HSV -> RGB (h: 0..255 = 0..360 deg), verbatim from feat/hscant-mode-ws2812. */
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

/* One comet frame: head bounces, tail halves, colour follows frame + index. */
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
        /* Drop leftover dark windows so both LEDs go dark at once. */
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
    /* Closed firmware state 3 shifts the time base (tx -3 ms, rx -15 ms). */
    const uint32_t t = now - (rx ? CHAN_RX_SKEW_MS : CHAN_TX_SKEW_MS);

    if ((!s_ready) || (index >= LED_COUNT)) {
        return;
    }

    /* FUN_8002e18c: re-arm once the previous arming is CHAN_TRAFFIC_REARM_MS old. */
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

    /* "Running" LED heartbeat: strip-independent, keeps blinking during the boot comet. */
#if defined(BOARD_HAS_SYS_LED) && BOARD_HAS_SYS_LED
    if ((uint32_t)(now - s_last_sys_led_ms) >= SYS_LED_TOGGLE_MS) {
        s_last_sys_led_ms = now;
        board_led_toggle();
    }
#endif

    /* The comet owns the strip: skip the repaint below and re-base the deadline on now. */
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
            s_off_active[i] = false;
            s_dirty = true;
        }
    }

    /* s_dirty is cleared only after the paint, so the throttle cannot swallow a state change. */
    if (s_dirty && ((uint32_t)(now - s_last_render_ms) >= RENDER_MIN_INTERVAL_MS)) {
        s_dirty = false;
        compose();
        ws2812_update(true);
        s_last_render_ms = now;
    }
}
