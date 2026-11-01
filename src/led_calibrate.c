/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Debug aid: lights the strip's LEDs one by one and logs which key position
 * is pressed for each, so the LED chain can be mapped to key positions.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/ext_power.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_LEN DT_PROP(STRIP_NODE, chain_length)

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#endif
static struct led_rgb pixels[STRIP_LEN];

static K_SEM_DEFINE(pressed_sem, 0, 1);
static volatile int32_t pressed_position = -1;
static volatile bool listening;

static int calibrate_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev != NULL && ev->state && listening) {
        pressed_position = ev->position;
        listening = false;
        k_sem_give(&pressed_sem);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(led_calibrate, calibrate_listener);
ZMK_SUBSCRIPTION(led_calibrate, zmk_position_state_changed);

static void calibrate_thread(void *a, void *b, void *c) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    ext_power_enable(ext_power);
    k_msleep(200);
#endif
    LOG_INF("calibrate: start, %d LEDs", STRIP_LEN);

    for (int idx = 0; idx < STRIP_LEN; idx++) {
        memset(pixels, 0, sizeof(pixels));
        pixels[idx] = (struct led_rgb){.r = 120, .g = 0, .b = 0};
        led_strip_update_rgb(strip, pixels, STRIP_LEN);
        k_msleep(100);
        led_strip_update_rgb(strip, pixels, STRIP_LEN);

        k_sem_reset(&pressed_sem);
        pressed_position = -1;
        listening = true;
        LOG_INF("calibrate: led %d lit, press its key", idx);
        k_sem_take(&pressed_sem, K_FOREVER);
        LOG_INF("calibrate: RESULT led %d -> position %d", idx, (int)pressed_position);
        k_msleep(400);
    }

    memset(pixels, 0, sizeof(pixels));
    led_strip_update_rgb(strip, pixels, STRIP_LEN);
    LOG_INF("calibrate: done");
}

K_THREAD_DEFINE(led_calibrate_tid, 1536, calibrate_thread, NULL, NULL, NULL, 10, 0, 10000);
