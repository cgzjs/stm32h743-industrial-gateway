#include "pd42.h"
#include "igw_bytes.h"
#include <string.h>
#include <math.h>
static uint8_t checksum(const uint8_t *p, size_t n) {
    uint8_t c = 0;
    while (n--)
        c = (uint8_t)(c + *p++);
    return c;
}
size_t pd42_request(uint8_t *p, uint8_t node, uint8_t code, const uint8_t *data, size_t n) {
    if (n > 59 || !node)
        return 0;
    p[0] = 0xC5;
    p[1] = node;
    p[2] = code;
    if (n)
        memcpy(p + 3, data, n);
    p[n + 3] = checksum(p, n + 3);
    p[n + 4] = 0x5C;
    return n + 5;
}
size_t pd42_velocity(uint8_t *p, uint8_t node, uint8_t dir, uint8_t acc, float rpm) {
    if (dir > 1 || acc > 200 || !isfinite(rpm) || rpm < 0 || rpm > 6000)
        return 0;
    uint8_t d[6] = {dir, acc};
    uint32_t bits;
    memcpy(&bits, &rpm, 4);
    igw_put32(d + 2, bits);
    return pd42_request(p, node, PD42_VELOCITY, d, 6);
}
size_t pd42_position(uint8_t *p, uint8_t node, uint8_t dir, uint8_t acc, uint16_t rpm, uint32_t pulses) {
    if (dir > 1 || acc > 200 || rpm > 6000)
        return 0;
    uint8_t d[8] = {dir, acc};
    igw_put16(d + 2, rpm);
    igw_put32(d + 4, pulses);
    return pd42_request(p, node, PD42_ABSOLUTE, d, 8);
}
bool pd42_validate(const uint8_t *p, size_t n, uint8_t node, uint8_t code) {
    if (n < 6 || n > 64 || p[0] != 0xC5 || p[1] != node || p[2] != code || p[n - 1] != 0x5C ||
        checksum(p, n - 2) != p[n - 2])
        return false;
    size_t expected = code == PD42_SPEED || code == PD42_CURRENT  ? 8
                      : code == PD42_STATE || code == PD42_ENABLE ? 7
                      : code == PD42_POSITION                     ? 10
                                                                  : 6;
    return p[3] == 1 ? n == expected : n == 6;
}
size_t pd42_stream_feed(pd42_stream_t *s, const uint8_t *p, size_t n, uint32_t now, uint8_t node,
                        uint8_t code) {
    if (!n || n > 8)
        return 0;
    if ((uint32_t)(now - s->last_tick) > 5)
        s->used = 0;
    s->last_tick = now;
    if (!s->used && p[0] != 0xC5)
        return 0;
    if (n > sizeof(s->data) - s->used) {
        s->used = 0;
        return 0;
    }
    memcpy(s->data + s->used, p, n);
    s->used += n;
    if (pd42_validate(s->data, s->used, node, code)) {
        size_t result = s->used;
        s->used = 0;
        return result;
    }
    return 0;
}
