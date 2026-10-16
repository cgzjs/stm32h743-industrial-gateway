#include "modbus_tcp.h"
#include "igw_bytes.h"
#include "igw_config.h"
#include <string.h>
static size_t exception(uint8_t *o, uint8_t fn, uint8_t code) {
    o[7] = fn | 0x80u;
    o[8] = code;
    igw_put16(o + 4, 3);
    return 9;
}
size_t modbus_tcp_process(const uint8_t *p, size_t n, uint8_t *o, const gateway_data_t *d, uint32_t now,
                          motor_enqueue_fn enqueue, void *context) {
    if (n < 8 || n > 260 || igw_be16(p + 2) || igw_be16(p + 4) != (uint16_t)(n - 6) || p[6] != 1)
        return 0;
    memcpy(o, p, 7);
    uint8_t fn = p[7];
    o[7] = fn;
    if (fn == 3 || fn == 4) {
        if (n != 12)
            return exception(o, fn, 3);
        uint16_t a = igw_be16(p + 8), count = igw_be16(p + 10);
        if (!count || count > 125 || (uint32_t)a + count > 65536)
            return exception(o, fn, 3);
        for (unsigned i = 0; i < count; i++) {
            uint16_t v;
            if (!data_read_register(d, (uint16_t)(a + i), &v))
                return exception(o, fn, 2);
            igw_put16(o + 9 + 2 * i, v);
        }
        o[8] = (uint8_t)(2 * count);
        igw_put16(o + 4, (uint16_t)(3 + 2 * count));
        return 9 + 2 * count;
    }
    if (fn != 6 && fn != 16)
        return exception(o, fn, 1);
    if (n < 12)
        return exception(o, fn, 3);
    uint16_t a = igw_be16(p + 8), count = fn == 6 ? 1 : igw_be16(p + 10);
    if (fn == 6 && n != 12)
        return exception(o, fn, 3);
    if (fn == 16 && (!count || count > 6 || n != 13u + 2u * count || p[12] != 2u * count))
        return exception(o, fn, 3);
    if (a == 0x26 && count == 1) {
        if (igw_be16(p + (fn == 6 ? 10 : 13)) != 1)
            return exception(o, fn, 3);
        motor_command_t hb = {.type = MOTOR_HEARTBEAT, .enabled = 1, .issued = now, .source = 0};
        if (!motor_command_allowed(d, &hb))
            return exception(o, fn, 3);
        if (!enqueue || !enqueue(&hb, context))
            return exception(o, fn, 6);
        memcpy(o + 8, p + 8, 4);
        igw_put16(o + 4, 6);
        return 12;
    }
    bool position = fn == 16 && a == 0x20 && count == 6;
    if (a < 0x20 || (uint32_t)a + count > (position ? 0x26u : 0x23u))
        return exception(o, fn, 2);
    motor_command_t c = {.type = MOTOR_VELOCITY,
                         .rpm = d->target_rpm,
                         .direction = d->direction,
                         .enabled = d->motor_enabled,
                         .issued = now};
    for (unsigned i = 0; i < count; i++) {
        uint16_t v = igw_be16(p + (fn == 6 ? 10 : 13) + 2 * i);
        switch (a + i) {
        case 0x20:
            c.rpm = v;
            break;
        case 0x21:
            if (v > 1)
                return exception(o, fn, 3);
            c.enabled = v;
            break;
        case 0x22:
            c.direction = v;
            break;
        case 0x23:
            c.position = (int32_t)((uint32_t)v << 16);
            break;
        case 0x24:
            c.position = (int32_t)((uint32_t)c.position | v);
            break;
        case 0x25:
            if (v != 1)
                return exception(o, fn, 3);
            c.type = MOTOR_POSITION;
            break;
        default:
            return exception(o, fn, 2);
        }
    }
    if (c.rpm > IGW_MAX_RPM || c.direction > 1 || c.enabled > 1)
        return exception(o, fn, 3);
    if (!c.enabled)
        c.type = MOTOR_DISABLE;
    if (!motor_command_allowed(d, &c))
        return exception(o, fn, 3);
    if (!enqueue || !enqueue(&c, context))
        return exception(o, fn, 6);
    memcpy(o + 8, p + 8, 4);
    igw_put16(o + 4, 6);
    return 12;
}
