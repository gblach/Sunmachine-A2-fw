#pragma once
#include <drivers/led_strip.h>

struct led_rgb HSV(uint16_t h1, uint8_t s1, uint8_t v1);
uint32_t led_rgb_to_int(struct led_rgb color);
