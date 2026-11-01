/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Light only some key columns of the underglow strip while the gaming layer is
 * active. The behavior is global, so it runs on both halves; the built-in
 * underglow must be off (the to_gaming macro turns it off) so it does not
 * repaint the whole strip.
 */

#define DT_DRV_COMPAT zmk_behavior_gaming_leds

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <drivers/ext_power.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_LEN DT_PROP(STRIP_NODE, chain_length)
#define POWER_UP_MS 100

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define LIT_MASK CONFIG_GAMING_LEDS_LEFT_MASK
#else
#define LIT_MASK CONFIG_GAMING_LEDS_RIGHT_MASK
#endif

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#endif
static struct led_rgb pixels[STRIP_LEN];
static volatile bool gaming_on;

static void gaming_leds_update(struct k_work *work) {
    bool on = gaming_on;

    memset(pixels, 0, sizeof(pixels));

    if (on) {
        for (size_t i = 0; i < STRIP_LEN; i++) {
            if (LIT_MASK & BIT(i)) {
                pixels[i] = (struct led_rgb){.r = CONFIG_GAMING_LEDS_RED,
                                             .g = CONFIG_GAMING_LEDS_GREEN,
                                             .b = CONFIG_GAMING_LEDS_BLUE};
            }
        }
    }

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    bool underglow_on = false;
    zmk_rgb_underglow_get_state(&underglow_on);

    if (on) {
        ext_power_enable(ext_power);
        /* The strip needs time to power up; the first frame is lost otherwise. */
        k_msleep(POWER_UP_MS);
    } else if (!underglow_on) {
        ext_power_disable(ext_power);
    }
#endif

    int err = led_strip_update_rgb(strip, pixels, STRIP_LEN);
    if (err < 0) {
        LOG_ERR("Failed to update gaming LEDs: %d", err);
    }

    if (on) {
        /* Send the frame again in case the strip dropped the first one. */
        k_msleep(POWER_UP_MS);
        led_strip_update_rgb(strip, pixels, STRIP_LEN);
    }
}

/*
 * Runs on the same queue as the underglow's own "all off" work, so a RGB_OFF
 * issued just before this behavior is always processed first.
 */
static K_WORK_DELAYABLE_DEFINE(gaming_leds_work, gaming_leds_update);

static void gaming_leds_schedule(k_timeout_t delay) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &gaming_leds_work, delay);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    gaming_on = binding->param1 != 0;
    gaming_leds_schedule(K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_gaming_leds_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_gaming_leds_driver_api);

/* The underglow's idle handling blanks the strip; draw again after waking up. */
static int gaming_leds_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev != NULL && ev->state == ZMK_ACTIVITY_ACTIVE && gaming_on) {
        gaming_leds_schedule(K_MSEC(300));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(gaming_leds, gaming_leds_listener);
ZMK_SUBSCRIPTION(gaming_leds, zmk_activity_state_changed);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
