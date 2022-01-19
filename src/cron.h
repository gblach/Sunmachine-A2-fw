#pragma once
#include <time.h>

void rtc_set(time_t value);
time_t rtc_get();
void crontab_load();
void cron_init();
