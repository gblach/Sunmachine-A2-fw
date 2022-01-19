#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gap.h>
#include <bluetooth/gatt.h>
#include <settings/settings.h>

#include "types.h"
#include "storage.h"
#include "cron.h"

#define SRVC_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x00, 0x34, 0x16, 0x20)

#define IDV_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x01, 0x34, 0x16, 0x20)

#define CTRL_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x02, 0x34, 0x16, 0x20)

#define STRIP_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x03, 0x34, 0x16, 0x20)

#define LUX_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x04, 0x34, 0x16, 0x20)

#define CRON_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x05, 0x34, 0x16, 0x20)

#define RTC_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x06, 0x34, 0x16, 0x20)

#define TZ_UUID BT_UUID_DECLARE_128( \
    0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
    0x77, 0x4e, 0x04, 0xf7, 0x07, 0x34, 0x16, 0x20)

const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, \
        0xfe, 0xd0, 0xe2, 0xad, 0xb7, 0x07, 0xcc, 0x9a, \
        0x77, 0x4e, 0x04, 0xf7, 0x00, 0x34, 0x16, 0x20),
};

char idv[8] = { 'S', 'M', 'A', '2', 0, 0, 0, 0 };
bool notify_enable = false;

extern uint8_t control[4];
extern uint8_t strip[6];
extern uint16_t lux;
extern uint8_t cronbuf[40];
extern char timezone[50];
extern sun_ws2812_t ws2812;
extern sun_auto_t auto_state;
extern struct k_timer auto_timer;
extern struct k_timer luxoff_timer;

ssize_t idv_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(idv));
}

ssize_t control_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(control));
}

ssize_t control_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if(offset != 0 || len != sizeof(control)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    uint8_t *value = attr->user_data;
    uint8_t *update = (uint8_t*)buf;
    bool trigger[3] = {
        (value[0] & 0x0f) != (update[0] & 0x0f),
        value[1] != update[1] && auto_state == auto_on,
        value[2] != update[2] && auto_state == auto_on,
    };
    memcpy(value + offset, buf, len);
    if(MODE != mode_auto) {
        auto_state = auto_off;
        k_timer_stop(&auto_timer);
    }
    if(trigger[0]) ws2812.update = true;
    if(trigger[1]) k_timer_start(&auto_timer, K_SECONDS(control[1] * 5), K_NO_WAIT);
    if(trigger[2]) k_timer_start(&luxoff_timer, K_SECONDS(1), K_NO_WAIT);
    storage_keep_control();
    return len;
}

ssize_t strip_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(strip));
}

ssize_t strip_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if(offset != 0 || len != sizeof(strip)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    uint8_t *value = attr->user_data;
    uint8_t *update = (uint8_t*)buf;
    bool trigger = false;
    for(int i=0; i<len; i++) {
        if(value[i] != update[i]) trigger = true;
    }
    memcpy(value, buf, len);
    ws2812.update = trigger;
    storage_keep_strip();
    return len;
}

ssize_t lux_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(lux));
}

void lux_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    notify_enable = (value == BT_GATT_CCC_NOTIFY);
}

ssize_t crontab_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(cronbuf));
}

ssize_t crontab_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if(offset != 0 || len != sizeof(cronbuf)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    uint8_t *value = attr->user_data;
    memcpy(value, buf, len);
    crontab_load();
    storage_keep_cronbuf();
    return len;
}

ssize_t rtc_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    time_t rtc_value = rtc_get();
    const uint8_t *value = (uint8_t*)&rtc_value;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(rtc_value));
}

ssize_t rtc_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if(offset != 0 || len != sizeof(time_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    uint8_t *value = (uint8_t*)malloc(len);
    memcpy(value, buf, len);
    rtc_set(*(time_t*)value);
    free(value);
    return len;
}

ssize_t timezone_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(timezone));
}

ssize_t timezone_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if(offset != 0 || len >= sizeof(timezone)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    uint8_t *value = attr->user_data;
    memcpy(value, buf, len);
    _setenv_r(_REENT, "TZ", value, 1);
    return len;
}

BT_GATT_SERVICE_DEFINE(vnd_svc,
    BT_GATT_PRIMARY_SERVICE(SRVC_UUID),
    BT_GATT_CHARACTERISTIC(IDV_UUID,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        idv_read, NULL, &idv),
    BT_GATT_CHARACTERISTIC(CTRL_UUID,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        control_read, control_write, &control),
    BT_GATT_CHARACTERISTIC(STRIP_UUID,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        strip_read, strip_write, &strip),
    BT_GATT_CHARACTERISTIC(LUX_UUID,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        lux_read, NULL, &lux),
    BT_GATT_CCC(lux_ccc_cfg_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(CRON_UUID,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        crontab_read, crontab_write, &cronbuf),
    BT_GATT_CHARACTERISTIC(RTC_UUID,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        rtc_read, rtc_write, NULL),
    BT_GATT_CHARACTERISTIC(TZ_UUID,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        timezone_read, timezone_write, &timezone),
);

void bluetooth_lux_notify() {
    if(notify_enable) bt_gatt_notify(NULL, &vnd_svc.attrs[7], &lux, sizeof(lux));
}

void bt_ready(int err) {
    settings_load();

    if(!strcmp(CONFIG_BT_DEVICE_NAME, bt_get_name())) {
        bt_addr_le_t addr[CONFIG_BT_ID_MAX];
        int addrs_count = 1;
        bt_id_get(addr, &addrs_count);
        if(addrs_count) {
            char btname[16];
            sprintf(btname, "%s %02X%02X",
                CONFIG_BT_DEVICE_NAME,
                addr[0].a.val[1], addr[0].a.val[0]);
            bt_set_name(btname);
        }
    }

    bt_le_adv_start(
        BT_LE_ADV_PARAM(
            BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
            BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL
        ),
        ad, ARRAY_SIZE(ad), NULL, 0
    );
}

void bluetooth_init() {
    bt_enable(bt_ready);
}
