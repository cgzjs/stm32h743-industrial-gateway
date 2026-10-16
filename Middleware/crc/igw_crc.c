#include "igw_crc.h"
uint32_t igw_crc32_update(uint32_t c, const void *data, size_t n) {
    const uint8_t *p = data;
    while (n--) {
        c ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xEDB88320u);
    }
    return c;
}
uint32_t igw_crc32(const void *p, size_t n) {
    return igw_crc32_update(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
}
uint16_t igw_crc16(const void *data, size_t n) {
    uint16_t c = 0xFFFFu;
    const uint8_t *p = data;
    while (n--) {
        c ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            c = (uint16_t)((c >> 1) ^ ((0u - (c & 1u)) & 0xA001u));
    }
    return c;
}
