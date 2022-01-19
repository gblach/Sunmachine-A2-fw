#include <zephyr.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <device.h>
#include <drivers/gpio.h>
#include <drivers/led_strip.h>
#include <drivers/adc.h>
#include <hal/nrf_saadc.h>

#include "types.h"
#include "storage.h"
#include "cron.h"
#include "bluetooth.h"
#include "colors.h"

#define STRIP_NODE DT_NODELABEL(led_strip)
#define MOTION_NODE DT_ALIAS(pir)
#define MOTION_PIN DT_GPIO_PIN(MOTION_NODE, gpios)
#define MOTION_FLAGS GPIO_INPUT | DT_GPIO_FLAGS(MOTION_NODE, gpios)

struct k_thread thread_led;
K_THREAD_STACK_DEFINE(thr_stack_led, 1024);

uint8_t control[4] = { 0, 12, 10, 10 };
uint8_t strip[6] = { 0, 0, 30, 0, 100, 20 };
sun_auto_t auto_state = auto_off;
sun_ws2812_t ws2812;
uint16_t lux = 0, luxoff = 0xffff;

const struct device *gpio;
struct gpio_callback motion_cb;

const struct device *adc;
const struct adc_channel_cfg adc_channel = {
    .gain = ADC_GAIN_1_6,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0,
    .input_positive = NRF_SAADC_INPUT_AIN4,
};
int32_t adc_voltage[1];
const struct adc_sequence adc_sequence = {
    .channels = BIT(adc_channel.channel_id),
    .buffer = adc_voltage,
    .buffer_size = sizeof(adc_voltage),
    .resolution = 12,
};

const uint16_t delay_table[] = {
    210, 200, 190, 180, 170,
    160, 150, 140, 130, 120,
    110, 100, 90, 80, 70,
    60, 50, 40, 30, 20,
};

void luxoff_timeout(struct k_timer *timer);
K_TIMER_DEFINE(luxoff_timer, luxoff_timeout, NULL);

void auto_timeout(struct k_timer *timer);
K_TIMER_DEFINE(auto_timer, auto_timeout, NULL);

void ws2812_loop(void *a, void *b, void *c) {
    for(;;) {
        if(ws2812.update) {
            ws2812.update = false;

            if(MODE == mode_on || auto_state) {
                uint16_t hue = strip[2] | strip[3] << 8;
                uint8_t saturation = strip[4] <= 100 ? strip[4] : 100;
                uint8_t brightness = strip[5];
                if(auto_state == auto_semi) brightness /= 4;
                ws2812.pixbuf[0] = HSV(hue, saturation, brightness);
            } else {
                memset(&ws2812.pixbuf[0], 0, sizeof(struct led_rgb));
            }

            uint16_t pixlen = strip[0] | strip[1] << 8;
            uint32_t pixel_first = led_rgb_to_int(ws2812.pixbuf[0]);
            uint32_t pixel_last = led_rgb_to_int(ws2812.pixbuf[pixlen-1]);

            if(pixel_first != pixel_last) {
                for(uint16_t i=0; i<pixlen; i++) {
                    memcpy(&ws2812.pixbuf[i], &ws2812.pixbuf[0], sizeof(struct led_rgb));
                    led_strip_update_rgb(ws2812.dev, ws2812.pixbuf, pixlen);
                    k_msleep(delay_table[control[3]-1]);
                }
            }
        }

        k_msleep(10);
    }
}

void luxoff_timeout(struct k_timer *timer) {
    uint16_t a = control[2] * 7;
    uint16_t b = lux * 1.4;
    luxoff = a > b ? a : b;
}

void auto_timeout(struct k_timer *timer) {
    if(auto_state) auto_state--;
    if(auto_state == auto_semi) {
        double time_factor = control[1] * 7.5;
        double sqrt = time_factor / 3.0;
        for(uint8_t i=0; i<6; i++) {
            sqrt = (sqrt + time_factor / sqrt) / 2;
        }
        k_timer_start(&auto_timer, K_SECONDS(sqrt + 0.5), K_NO_WAIT);
    }
    ws2812.update = true;
}

void on_motion(const struct device *dev, struct gpio_callback *cb, gpio_port_pins_t pins) {
    if(MODE == mode_auto) {
        if(gpio_pin_get(gpio, MOTION_PIN)) {
            if(lux <= control[2] * 5) {
                k_timer_stop(&auto_timer);
                luxoff = 0xffff;
                auto_state = auto_on;
                ws2812.update = true;
                k_timer_start(&luxoff_timer, K_SECONDS(5), K_NO_WAIT);
            }
        } else {
            k_timer_start(&auto_timer, K_SECONDS(control[1] * 5), K_NO_WAIT);
        }
    }
}

void board_init() {
    ws2812.dev = DEVICE_DT_GET(STRIP_NODE);
    memset(&ws2812.pixbuf, 0, sizeof(ws2812.pixbuf));
    led_strip_update_rgb(ws2812.dev, ws2812.pixbuf, strip[0] | strip[1] << 8);
    ws2812.update = true;

    gpio = device_get_binding("GPIO_0");
    gpio_pin_configure(gpio, MOTION_PIN, MOTION_FLAGS);
    gpio_pin_interrupt_configure(gpio, MOTION_PIN, GPIO_INT_EDGE_BOTH);
    gpio_init_callback(&motion_cb, on_motion, BIT(MOTION_PIN));
    gpio_add_callback(gpio, &motion_cb);

    adc = device_get_binding("ADC_0");
    adc_channel_setup(adc, &adc_channel);

    k_thread_create(&thread_led,
        thr_stack_led, K_THREAD_STACK_SIZEOF(thr_stack_led),
        ws2812_loop, NULL, NULL, NULL,
        5, 0, K_NO_WAIT);
}

void main() {
    storage_init();
    cron_init();
    board_init();
    bluetooth_init();

    for(;;) {
        adc_read(adc, &adc_sequence);
        adc_raw_to_millivolts(adc_ref_internal(adc),
            adc_channel.gain, adc_sequence.resolution, &adc_voltage[0]);
        double resistance = (3300.0 - adc_voltage[0]) / adc_voltage[0] * 10000;
        lux = round(37500000 * pow(resistance, -1.41));
        bluetooth_lux_notify();

        if(auto_state && lux > luxoff) {
            auto_state = auto_off;
            ws2812.update = true;
        }

        if(MODE == mode_sleep && lux > control[2] * 7) {
            MODE_SET(mode_auto);
        }

        k_msleep(1000);
    }
}
