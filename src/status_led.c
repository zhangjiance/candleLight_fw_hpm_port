
#include "status_led.h"

#include "board.h"
#include "hal_include.h" /* HAL_GetTick */

#if defined(BOARD_HAS_WS2812) && BOARD_HAS_WS2812

#include "ws2812.h"

#if !defined(BOARD_WS2812_LED_COUNT)
#error "BOARD_HAS_WS2812 is set but BOARD_WS2812_LED_COUNT is missing"
#endif

#define LED_COUNT          BOARD_WS2812_LED_COUNT

/* Sys LED heartbeat. */
#define SYS_LED_TOGGLE_MS  (500U)

/* Boot comet. */
#define ANIM_FRAME_MS      (30U)
#define ANIM_SWEEPS        (1U)
#define ANIM_COMET_V       (0x20U)

static uint32_t s_last_sys_led_ms;
static uint32_t s_comet_left;
static uint32_t s_comet_frame;
static uint32_t s_comet_next_ms;
static uint32_t s_clear_at;
static uint8_t s_clear_retries;
static bool s_ready;

/* 8 bit HSV -> RGB. */
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

static void strip_clear(void)
{
    ws2812_fill(0U, 0U, 0U);
    ws2812_update(true);
}

void status_led_init(void)
{
    ws2812_init();
    strip_clear();
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

void status_led_tick(void)
{
    const uint32_t now = HAL_GetTick();

    if (!s_ready) {
        return;
    }

#if defined(BOARD_HAS_SYS_LED) && BOARD_HAS_SYS_LED
    if ((uint32_t)(now - s_last_sys_led_ms) >= SYS_LED_TOGGLE_MS) {
        s_last_sys_led_ms = now;
        board_led_toggle();
    }
#endif

    if (s_comet_left > 0U) {
        if ((int32_t)(now - s_comet_next_ms) >= 0) {
            s_comet_next_ms = now + ANIM_FRAME_MS;
            comet_render(s_comet_frame);
            s_comet_frame++;
            s_comet_left--;
            if (s_comet_left == 0U) {
                s_clear_at = now + ANIM_FRAME_MS;
                s_clear_retries = 0U;
            }
        }
    }

    if (s_clear_at != 0U && (int32_t)(now - s_clear_at) >= 0) {
        strip_clear();
        if (++s_clear_retries < 3U) {
            s_clear_at = now + 10U;
        } else {
            s_clear_at = 0U;
        }
    }
}

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
 * Backend 3: one plain GPIO "running" LED, no channel LEDs
 *
 * This is what the hpm_sdk EVKs use: the board package owns the pad and its
 * polarity, the application just toggles it as a 1 Hz heartbeat.  Channel
 * state and error events have no LED to go to.
 * ------------------------------------------------------------------------- */
#elif defined(BOARD_HAS_SYS_LED) && BOARD_HAS_SYS_LED

#define SYS_LED_TOGGLE_MS  (500U) /* half period: 1 Hz, 50 % duty */
#define SYS_LED_BOOT_STEPS (4U)

static uint32_t s_last_sys_led_ms;
static bool s_ready;

void status_led_init(void)
{
    board_init_led_pins();
    board_led_write(BOARD_LED_OFF_LEVEL);
    s_last_sys_led_ms = HAL_GetTick();
    s_ready = true;
}

void status_led_boot_anim(void)
{
    /* Short blink at power-on so the LED is known to work. */
    for (uint8_t step = 0U; step < SYS_LED_BOOT_STEPS; step++) {
        board_led_toggle();
        board_delay_ms(30U);
    }
    board_led_write(BOARD_LED_OFF_LEVEL);
}

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

void status_led_tick(void)
{
    const uint32_t now = HAL_GetTick();

    if (!s_ready) {
        return;
    }

    if ((uint32_t)(now - s_last_sys_led_ms) >= SYS_LED_TOGGLE_MS) {
        s_last_sys_led_ms = now;
        board_led_toggle();
    }
}

/* ---------------------------------------------------------------------------
 * Backend 4: no LEDs at all
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
