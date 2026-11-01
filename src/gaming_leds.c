/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * While the gaming layer is active, light only the configured LEDs of the
 * underglow strip. The built-in underglow must be off (the to_gaming macro
 * turns it off) so it does not repaint the whole strip.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/ext_power.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_LEN DT_PROP(STRIP_NODE, chain_length)
#define POWER_UP_MS 100

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#endif
static struct led_rgb pixels[STRIP_LEN];

static const uint8_t lit[] = {CONFIG_GAMING_LEDS_KEY_1, CONFIG_GAMING_LEDS_KEY_2,
                              CONFIG_GAMING_LEDS_KEY_3};

static void gaming_leds_update(struct k_work *work) {
    bool on = zmk_keymap_layer_active(CONFIG_GAMING_LEDS_LAYER);
    LOG_INF("gaming_leds: update, gaming layer active=%d", on);

    memset(pixels, 0, sizeof(pixels));

    if (on) {
        for (size_t i = 0; i < ARRAY_SIZE(lit); i++) {
            if (lit[i] < STRIP_LEN) {
                pixels[lit[i]] = (struct led_rgb){.r = CONFIG_GAMING_LEDS_RED,
                                                  .g = CONFIG_GAMING_LEDS_GREEN,
                                                  .b = CONFIG_GAMING_LEDS_BLUE};
            }
        }
    }

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    bool underglow_on = false;
    zmk_rgb_underglow_get_state(&underglow_on);

    LOG_INF("gaming_leds: underglow_on=%d", underglow_on);

    if (on) {
        int rc = ext_power_enable(ext_power);
        LOG_INF("gaming_leds: ext_power_enable rc=%d", rc);
        /* The strip needs time to power up; the first frame is lost otherwise. */
        k_msleep(POWER_UP_MS);
    } else if (!underglow_on) {
        ext_power_disable(ext_power);
    }
#endif

#if IS_ENABLED(CONFIG_GAMING_LEDS_DEBUG_CHASE)
    if (on) {
        /* Candidate thumb LEDs: blue = {14,17,20}, red = {0,5,10}; green = {12,15,18}. */
        static const uint8_t blue[] = {14, 17, 20};
        static const uint8_t red[] = {0, 5, 10};
        static const uint8_t green[] = {12, 15, 18};
        for (size_t k = 0; k < 3; k++) {
            pixels[blue[k]] = (struct led_rgb){.r = 0, .g = 0, .b = 150};
            pixels[red[k]] = (struct led_rgb){.r = 150, .g = 0, .b = 0};
            pixels[green[k]] = (struct led_rgb){.r = 0, .g = 150, .b = 0};
        }
        LOG_INF("gaming_leds: debug candidates rc=%d", led_strip_update_rgb(strip, pixels, STRIP_LEN));
        k_msleep(POWER_UP_MS);
        led_strip_update_rgb(strip, pixels, STRIP_LEN);
        return;
    }
#endif

    int err = led_strip_update_rgb(strip, pixels, STRIP_LEN);
    LOG_INF("gaming_leds: led_strip_update_rgb rc=%d", err);

    if (on) {
        /* Send the frame again in case the strip dropped the first one. */
        k_msleep(POWER_UP_MS);
        led_strip_update_rgb(strip, pixels, STRIP_LEN);
    }
}

/*
 * Runs on the same queue as the underglow's own "all off" work, so a
 * RGB_OFF issued just before the layer change is always processed first.
 */
static K_WORK_DEFINE(gaming_leds_work, gaming_leds_update);

static int gaming_leds_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer_ev = as_zmk_layer_state_changed(eh);
    if (layer_ev != NULL) {
        LOG_INF("gaming_leds: layer %d state %d", layer_ev->layer, layer_ev->state);
        if (layer_ev->layer == CONFIG_GAMING_LEDS_LAYER) {
            k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &gaming_leds_work);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_activity_state_changed *activity_ev = as_zmk_activity_state_changed(eh);
    if (activity_ev != NULL && activity_ev->state == ZMK_ACTIVITY_ACTIVE &&
        zmk_keymap_layer_active(CONFIG_GAMING_LEDS_LAYER)) {
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &gaming_leds_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(gaming_leds, gaming_leds_listener);
ZMK_SUBSCRIPTION(gaming_leds, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(gaming_leds, zmk_activity_state_changed);
