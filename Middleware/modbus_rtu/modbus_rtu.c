#include "modbus_rtu.h"
#include "igw_crc.h"
#include "igw_bytes.h"
size_t rtu_read_request(uint8_t *p, uint8_t slave, uint8_t fn, uint16_t start, uint16_t count) {
    if (!slave || slave > 247 || (fn != 3 && fn != 4) || !count || count > 125 ||
        (uint32_t)start + count > 65536u)
        return 0;
    p[0] = slave;
    p[1] = fn;
    igw_put16(p + 2, start);
    igw_put16(p + 4, count);
    uint16_t c = igw_crc16(p, 6);
    p[6] = (uint8_t)c;
    p[7] = (uint8_t)(c >> 8);
    return 8;
}
int rtu_read_response(const uint8_t *p, size_t n, uint8_t slave, uint8_t fn, uint16_t *r, size_t count) {
    if (n < 5 || p[0] != slave || igw_crc16(p, n - 2) != (uint16_t)(p[n - 2] | (uint16_t)p[n - 1] << 8))
        return -1;
    if (p[1] == (fn | 0x80u))
        return n == 5 ? -(int)p[2] - 10 : -1;
    if (!count || count > 125 || p[1] != fn || n != 5 + 2 * count || p[2] != 2 * count)
        return -1;
    for (size_t i = 0; i < count; i++)
        r[i] = igw_be16(p + 3 + 2 * i);
    return 0;
}
