# Copyright (c) 2025 The ZMK Contributors
# SPDX-License-Identifier: MIT

# Built into the app target so the ZMK include paths are available.
target_sources_ifdef(CONFIG_GAMING_LEDS app PRIVATE src/gaming_leds.c)
target_sources_ifdef(CONFIG_GAMING_LEDS_CALIBRATE app PRIVATE src/led_calibrate.c)
