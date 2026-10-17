
#include "status_led.h"

#include "board.h"
#include "hal_include.h" /* HAL_GetTick */

#if defined(BOARD_HAS_WS2812) && BOARD_HAS_WS2812

#include "ws2812.h"

/* A board with a strip must also give its LED count, or LED_COUNT falls back to 0. */
#if !defined(BOARD_WS2812_LED_COUNT)
#error "BOARD_HAS_WS2812 is set but BOARD_WS2812_LED_COUNT is missing"
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

void status_led_error(uint8_t channel, bool active)
{
    /* The WS2812 chain has no dedicated error LED. */
    (void)channel;
    (void)active;
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

/* ---------------------------------------------------------------------------
 * Backend 2: discrete GPIO LEDs (hscant-lite)
 *
 * White status LED: steady on while the channel is started, with a short dark
 * window on traffic so TX/RX reads as a flash.
 * Red error LED: blinks while an error is latched, off otherwise.
 * The board package owns the pads and the per-LED polarity.
 * ------------------------------------------------------------------------- */
#elif defined(BOARD_HAS_CHAN_LEDS) && BOARD_HAS_CHAN_LEDS

#include "board_contract.h"

/* Traffic shows as a short dark window on the otherwise steady status LED. */
#define STATUS_OFF_MS      (30U)
#define STATUS_REARM_MS    (60U)

/* One error keeps the red LED blinking this long, so even a single error is visible. */
#define ERROR_HOLD_MS      (500U)
#define ERROR_BLINK_MS     (100U)

static bool s_chan_on[BOARD_CHAN_LED_COUNT];
static bool s_status_painted[BOARD_CHAN_LED_COUNT];
static bool s_error_painted[BOARD_CHAN_LED_COUNT];
static uint32_t s_status_off_until[BOARD_CHAN_LED_COUNT];
static uint32_t s_status_rearm_at[BOARD_CHAN_LED_COUNT];
static uint32_t s_error_until[BOARD_CHAN_LED_COUNT];
static bool s_ready;

static void paint(uint8_t channel, bool error, bool on)
{
    bool *painted = error ? &s_error_painted[channel] : &s_status_painted[channel];

    if (*painted == on) {
        return;
    }

    *painted = on;
    board_chan_led_set(channel, error, on);
}

void status_led_init(void)
{
    /* board_init() already did this; repeat it so the LED layer is self-contained. */
    board_init_led_pins();

    for (uint8_t channel = 0U; channel < BOARD_CHAN_LED_COUNT; channel++) {
        s_chan_on[channel] = false;
        s_status_painted[channel] = false;
        s_error_painted[channel] = false;
        s_status_off_until[channel] = 0U;
        s_status_rearm_at[channel] = 0U;
        s_error_until[channel] = 0U;
        board_chan_led_set(channel, false, false);
        board_chan_led_set(channel, true, false);
    }

    s_ready = true;
}

void status_led_boot_anim(void)
{
    /* Application-level power-on chase: eight steps, 30 ms apart, then blank. */
    for (uint8_t step = 0U; step < BOARD_ACTIVITY_LED_COUNT; step++) {
        board_led_toggle();
        board_delay_ms(30U);
    }
    board_led_write(BOARD_LED_OFF_LEVEL);
}

void status_led_channel(uint8_t channel, bool enabled)
{
    if (channel >= BOARD_CHAN_LED_COUNT) {
        return;
    }

    s_chan_on[channel] = enabled;

    if (!enabled) {
        s_status_off_until[channel] = 0U;
        s_status_rearm_at[channel] = 0U;
        s_error_until[channel] = 0U;
        paint(channel, false, false);
        paint(channel, true, false);
    } else {
        /* A fresh start: white status on, red error off until a fault appears. */
        s_error_until[channel] = 0U;
        paint(channel, true, false);
        paint(channel, false, true);
    }
}

void status_led_activity(uint8_t channel, bool rx)
{
    const uint32_t now = HAL_GetTick();
    (void)rx; /* one shared status LED per channel */

    if ((!s_ready) || (channel >= BOARD_CHAN_LED_COUNT) || (!s_chan_on[channel])) {
        return;
    }

    if ((int32_t)(now - s_status_rearm_at[channel]) >= 0) {
        s_status_off_until[channel] = now + STATUS_OFF_MS;
        s_status_rearm_at[channel] = now + STATUS_REARM_MS;
    }
}

void status_led_error(uint8_t channel, bool active)
{
    const uint32_t now = HAL_GetTick();

    if ((!s_ready) || (channel >= BOARD_CHAN_LED_COUNT)) {
        return;
    }

    if (!s_chan_on[channel]) {
        s_error_until[channel] = 0U;
        paint(channel, true, false);
        return;
    }

    if (active) {
        /* Latch so even a single transient error is visible. */
        s_error_until[channel] = now + ERROR_HOLD_MS;
    }
}

void status_led_tick(void)
{
    const uint32_t now = HAL_GetTick();

    if (!s_ready) {
        return;
    }

    for (uint8_t channel = 0U; channel < BOARD_CHAN_LED_COUNT; channel++) {
        const bool status_on = s_chan_on[channel] &&
                               ((int32_t)(now - s_status_off_until[channel]) >= 0);
        const bool error_active = (int32_t)(now - s_error_until[channel]) < 0;
        const bool error_on = error_active && (((now / ERROR_BLINK_MS) & 1U) != 0U);

        paint(channel, false, status_on);
        paint(channel, true, error_on);
    }
}

/* ---------------------------------------------------------------------------
 * Backend 3: no LEDs at all
 * ------------------------------------------------------------------------- */
#else

void status_led_init(void) {}
void status_led_boot_anim(void) {}
void status_led_channel(uint8_t channel, bool enabled)
{
    (void)channel;
    (void)enabled;
}
void status_led_activity(uint8_t channel, bool rx)
{
    (void)channel;
    (void)rx;
}
void status_led_error(uint8_t channel, bool active)
{
    (void)channel;
    (void)active;
}
void status_led_tick(void) {}

#endif
