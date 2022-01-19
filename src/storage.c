#include <zephyr.h>
#include <drivers/flash.h>
#include <storage/flash_map.h>
#include <fs/nvs.h>

struct nvs_fs fs;

extern uint8_t control[4];
extern uint8_t strip[6];
extern uint8_t cronbuf[40];

void keep_control_cb(struct k_work *work) {
    nvs_write(&fs, 1, &control, sizeof(control));
}
K_WORK_DEFINE(keep_control_work, keep_control_cb);

void storage_keep_control() {
    k_work_submit(&keep_control_work);
}

void keep_strip_cb(struct k_work *work) {
    nvs_write(&fs, 2, &strip, sizeof(strip));
}
K_WORK_DEFINE(keep_strip_work, keep_strip_cb);

void storage_keep_strip() {
    k_work_submit(&keep_strip_work);
}

void keep_cronbuf_cb(struct k_work *work) {
    nvs_write(&fs, 3, &cronbuf, sizeof(cronbuf));
}
K_WORK_DEFINE(keep_cronbuf_work, keep_cronbuf_cb);

void storage_keep_cronbuf() {
    k_work_submit(&keep_cronbuf_work);
}

void storage_init() {
    struct flash_pages_info info;

    fs.offset = FLASH_AREA_OFFSET(storage);
    flash_get_page_info_by_offs(
        device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL),
        fs.offset, &info);
    fs.sector_size = info.size;
    fs.sector_count = 6;

    nvs_init(&fs, DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);
    nvs_read(&fs, 1, &control, sizeof(control));
    nvs_read(&fs, 2, &strip, sizeof(strip));
    nvs_read(&fs, 3, &cronbuf, sizeof(cronbuf));
}
