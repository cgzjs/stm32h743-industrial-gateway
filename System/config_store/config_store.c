#include "config_store.h"
#include "igw_crc.h"
#include "igw_bytes.h"
#include <string.h>
#include <stddef.h>
typedef struct {
    uint32_t magic, sequence;
    gateway_config_t config;
    uint32_t crc, commit;
} config_record_t;
void config_defaults(gateway_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->ip[0] = 192;
    c->ip[1] = 168;
    c->ip[2] = 10;
    c->ip[3] = 50;
    c->sensor_address = IGW_SENSOR_ADDRESS;
    c->sensor_function = IGW_SENSOR_FUNCTION;
    c->sensor_register = IGW_SENSOR_FIRST_REGISTER;
    c->temp_index = IGW_SENSOR_TEMP_INDEX;
    c->hum_index = IGW_SENSOR_HUM_INDEX;
    c->multiplier = IGW_SENSOR_MULTIPLIER;
    c->sensor_baud = IGW_SENSOR_BAUD;
}
bool config_valid(const gateway_config_t *c) {
    bool scale =
        (c->multiplier >= 1 && c->multiplier <= 10) ||
        ((c->multiplier & 0x8000u) && (c->multiplier & 0x7FFFu) >= 1 && (c->multiplier & 0x7FFFu) <= 1000);
    bool baud = c->sensor_baud == 4800 || c->sensor_baud == 9600 || c->sensor_baud == 19200 ||
                c->sensor_baud == 38400 || c->sensor_baud == 57600 || c->sensor_baud == 115200;
    return c->ip[0] > 0 && c->ip[0] < 224 && c->ip[3] > 0 && c->ip[3] < 255 && c->sensor_address > 0 &&
           c->sensor_address < 248 && (c->sensor_function == 3 || c->sensor_function == 4) &&
           c->sensor_register <= 65534 && c->temp_index < 2 && c->hum_index < 2 &&
           c->temp_index != c->hum_index && scale && c->touch_calibrated <= 1 && baud;
}
int32_t config_scale(uint16_t encoding, int32_t raw) {
    if (encoding & 0x8000u) {
        int32_t divisor = encoding & 0x7FFFu;
        if (!divisor)
            return 0;
        return raw < 0 ? -((-raw + divisor / 2) / divisor) : (raw + divisor / 2) / divisor;
    }
    return raw * (int32_t)encoding;
}
static bool record_read(const flash_ops_t *ops, uint32_t a, config_record_t *r) {
    return ops->read(a, r, sizeof(*r)) && r->magic == 0x43464733 && r->commit == IMAGE_COMMIT &&
           config_valid(&r->config) && igw_crc32(r, offsetof(config_record_t, crc)) == r->crc;
}
static int newest(const flash_ops_t *ops, config_record_t *r) {
    config_record_t a, b;
    bool va = record_read(ops, IGW_CONFIG0, &a), vb = record_read(ops, IGW_CONFIG1, &b);
    if (va && (!vb || (int32_t)(a.sequence - b.sequence) > 0)) {
        *r = a;
        return 0;
    }
    if (vb) {
        *r = b;
        return 1;
    }
    return -1;
}
bool config_load(const flash_ops_t *ops, gateway_config_t *c) {
    config_record_t r;
    if (newest(ops, &r) < 0) {
        config_defaults(c);
        return false;
    }
    *c = r.config;
    return true;
}
bool config_save(const flash_ops_t *ops, const gateway_config_t *c) {
    if (!config_valid(c))
        return false;
    config_record_t old, next = {0};
    int current = newest(ops, &old);
    next.magic = 0x43464733;
    next.sequence = current < 0 ? 1 : old.sequence + 1;
    next.config = *c;
    next.commit = IMAGE_COMMIT;
    next.crc = igw_crc32(&next, offsetof(config_record_t, crc));
    uint32_t address = current == 0 ? IGW_CONFIG1 : IGW_CONFIG0;
    if (!ops->erase(address, 4096) || !ops->program(address, &next, offsetof(config_record_t, commit)) ||
        !ops->program(address + offsetof(config_record_t, commit), &next.commit, 4))
        return false;
    return record_read(ops, address, &next);
}
void config_encode(uint8_t *w, const gateway_config_t *c) {
    memcpy(w, c->ip, 4);
    igw_put16(w + 4, c->sensor_address);
    igw_put16(w + 6, c->sensor_function);
    igw_put16(w + 8, c->sensor_register);
    igw_put16(w + 10, c->temp_index);
    igw_put16(w + 12, c->hum_index);
    igw_put16(w + 14, c->multiplier);
    igw_put32(w + 16, c->sensor_baud);
    for (unsigned i = 0; i < 6; i++)
        igw_put32(w + 20 + 4 * i, (uint32_t)c->touch[i]);
    igw_put32(w + 44, c->touch_calibrated);
}
bool config_decode(gateway_config_t *c, const uint8_t *w, size_t n) {
    if (n != CONFIG_WIRE_SIZE)
        return false;
    memcpy(c->ip, w, 4);
    c->sensor_address = igw_be16(w + 4);
    c->sensor_function = igw_be16(w + 6);
    c->sensor_register = igw_be16(w + 8);
    c->temp_index = igw_be16(w + 10);
    c->hum_index = igw_be16(w + 12);
    c->multiplier = igw_be16(w + 14);
    c->sensor_baud = igw_be32(w + 16);
    for (unsigned i = 0; i < 6; i++)
        c->touch[i] = (int32_t)igw_be32(w + 20 + 4 * i);
    c->touch_calibrated = igw_be32(w + 44);
    return config_valid(c);
}
