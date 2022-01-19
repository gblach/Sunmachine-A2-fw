#include <zephyr.h>
#include <stdlib.h>
#include <time.h>
#include <device.h>
#include <drivers/counter.h>

#include "types.h"
#include "storage.h"

#define RTC_TOP_TICKS 500

const struct device *rtc;
time_t base = -1000000000;
char timezone[50] = "";
uint8_t cronbuf[40] = {};
sun_cron_t crontab[10] = {};
struct counter_alarm_cfg alarm_cfg;

extern uint8_t control[4];
extern uint8_t strip[6];
extern sun_ws2812_t ws2812;

K_SEM_DEFINE(callback_sem, 1, 1);

void rtc_set(time_t value) {
    uint32_t ticks;
    counter_get_value(rtc, &ticks);
    base = value - counter_ticks_to_us(rtc, ticks) / USEC_PER_SEC;
}

time_t rtc_get() {
    uint32_t ticks;
    counter_get_value(rtc, &ticks);
    return base + counter_ticks_to_us(rtc, ticks) / USEC_PER_SEC;
}

void crontab_load() {
    uint8_t len = sizeof(crontab) / sizeof(crontab[0]);
    for(int i=0; i<len; i++) {
        int offset = i * 4;
        sun_cron_t *job = &crontab[i];
        job->enabled = cronbuf[0 + offset] >> 7;
        job->dow = cronbuf[0 + offset] & 0x7f;
        job->hh = cronbuf[1 + offset] >> 3;
        job->mm = (cronbuf[1 + offset] & 0x07) << 3 | cronbuf[2 + offset] >> 5;
        job->routine = (cronbuf[2 + offset] & 0x18) >> 3;
        job->value = (cronbuf[2 + offset] & 0x01) << 8 | cronbuf[3 + offset];
    }
}

void cron_dispatch(sun_cron_t *job) {
    switch(crontab->routine) {
        case routine_mode:
            MODE_SET(job->value);
            ws2812.update = true;
            storage_keep_control();
            break;

        case routine_hue:
            strip[2] = job->value & 0xff;
            strip[3] = job->value >> 8;
            ws2812.update = true;
            storage_keep_strip();
            break;

        case routine_saturation:
            strip[4] = job->value;
            ws2812.update = true;
            storage_keep_strip();
            break;

        case routine_brightness:
            strip[5] = job->value;
            ws2812.update = true;
            storage_keep_strip();
            break;
    }
}

void alarm_cb(const struct device *counter_dev, uint8_t chan_id, uint32_t ticks, void *user_data) {
    k_sem_take(&callback_sem, K_FOREVER);
    time_t t0 = base + counter_ticks_to_us(rtc, ticks) / USEC_PER_SEC;
    k_sem_give(&callback_sem);

    struct tm *t1 = localtime(&t0);
    alarm_cfg.ticks = counter_us_to_ticks(rtc, (60 - t1->tm_sec) * USEC_PER_SEC);
    counter_set_channel_alarm(rtc, 0, &alarm_cfg);

    if(base > 0) {
        uint8_t len = sizeof(crontab) / sizeof(crontab[0]);
        for(int i=0; i<len; i++) {
            sun_cron_t *job = &crontab[i];
            bool match = job->enabled && (job->dow & 1 << t1->tm_wday) \
                && job->hh == t1->tm_hour && job->mm == t1->tm_min;
            if(match) cron_dispatch(job);
        }
    }
}

void top_value_cb(const struct device *counter_dev, void *user_data) {
    k_sem_take(&callback_sem, K_FOREVER);
    base += RTC_TOP_TICKS;
    k_sem_give(&callback_sem);
}

void cron_init() {
    crontab_load();
    rtc = device_get_binding("RTC_2");
    counter_start(rtc);

    struct counter_top_cfg top_value_cfg = {
        .ticks = counter_us_to_ticks(rtc, RTC_TOP_TICKS * USEC_PER_SEC),
        .callback = top_value_cb,
        .user_data = NULL,
        .flags = 0,
    };
    counter_set_top_value(rtc, &top_value_cfg);

    struct tm *t1 = localtime(&base);
    alarm_cfg.ticks = counter_us_to_ticks(rtc, (60 - t1->tm_sec) * USEC_PER_SEC);
    alarm_cfg.callback = alarm_cb;
    alarm_cfg.user_data = NULL;
    alarm_cfg.flags = 0;
    counter_set_channel_alarm(rtc, 0, &alarm_cfg);
}
