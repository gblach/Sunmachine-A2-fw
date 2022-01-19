#pragma once
#include <device.h>
#include <drivers/led_strip.h>

#define MODE (sun_mode_t)(control[0] & 0x0f)
#define MODE_SET(mode) (control[0] = (control[0] & 0xf0) | mode)

typedef enum {
    mode_off,
    mode_on,
    mode_sleep,
    mode_auto,
} sun_mode_t;

typedef enum {
    auto_off,
    auto_semi,
    auto_on,
} sun_auto_t;

typedef enum {
    routine_mode,
    routine_brightness,
    routine_hue,
    routine_saturation,
} sun_routine_t;

typedef struct {
    const struct device *dev;
    struct led_rgb pixbuf[600];
    bool update:1;
} sun_ws2812_t;

typedef struct {
    unsigned int enabled:1;
    unsigned int dow:7;
    unsigned int hh:5;
    unsigned int mm:6;
    unsigned int routine:2;
    unsigned int value:9;
} sun_cron_t;
