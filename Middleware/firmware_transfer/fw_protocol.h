#ifndef FW_PROTOCOL_H
#define FW_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define FW_HEADER_SIZE 24u
#define FW_MAGIC 0x49475733u
enum { FW_BEGIN = 1, FW_DATA = 2, FW_END = 3, FW_STATUS = 4, FW_ABORT = 5, FW_REBOOT = 6 };
typedef struct {
    uint8_t operation;
    uint16_t status;
    uint32_t session, offset, length;
} fw_header_t;
int fw_decode_header(const uint8_t *wire, fw_header_t *h);
int fw_verify(const uint8_t *wire, const uint8_t *payload, const fw_header_t *h);
size_t fw_encode(uint8_t *wire, const fw_header_t *h, const uint8_t *payload);
#endif
