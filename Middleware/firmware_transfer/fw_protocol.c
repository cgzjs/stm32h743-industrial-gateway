#include "fw_protocol.h"
#include "igw_bytes.h"
#include "igw_crc.h"
#include "igw_config.h"
#include <string.h>
int fw_decode_header(const uint8_t *w, fw_header_t *h) {
    if (igw_be32(w) != FW_MAGIC || w[4] != 1)
        return -1;
    h->operation = w[5];
    h->status = igw_be16(w + 6);
    h->session = igw_be32(w + 8);
    h->offset = igw_be32(w + 12);
    h->length = igw_be32(w + 16);
    return h->length <= IGW_OTA_CHUNK ? 0 : -1;
}
int fw_verify(const uint8_t *w, const uint8_t *p, const fw_header_t *h) {
    uint32_t c = igw_crc32_update(0xFFFFFFFFu, w, 20);
    c = igw_crc32_update(c, p, h->length) ^ 0xFFFFFFFFu;
    return c == igw_be32(w + 20) ? 0 : -1;
}
size_t fw_encode(uint8_t *w, const fw_header_t *h, const uint8_t *p) {
    if (h->length > IGW_OTA_CHUNK)
        return 0;
    igw_put32(w, FW_MAGIC);
    w[4] = 1;
    w[5] = h->operation;
    igw_put16(w + 6, h->status);
    igw_put32(w + 8, h->session);
    igw_put32(w + 12, h->offset);
    igw_put32(w + 16, h->length);
    if (h->length)
        memcpy(w + 24, p, h->length);
    uint32_t c = igw_crc32_update(0xFFFFFFFFu, w, 20);
    c = igw_crc32_update(c, p, h->length) ^ 0xFFFFFFFFu;
    igw_put32(w + 20, c);
    return 24u + h->length;
}
