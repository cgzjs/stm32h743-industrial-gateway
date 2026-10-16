#ifndef IGW_CRC_H
#define IGW_CRC_H
#include <stddef.h>
#include <stdint.h>
uint32_t igw_crc32_update(uint32_t running, const void *data, size_t length);
uint32_t igw_crc32(const void *data, size_t length);
uint16_t igw_crc16(const void *data, size_t length);
#endif
