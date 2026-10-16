#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H
#include <stddef.h>
#include <stdint.h>
size_t rtu_read_request(uint8_t *out, uint8_t slave, uint8_t fn, uint16_t start, uint16_t count);
int rtu_read_response(const uint8_t *frame, size_t n, uint8_t slave, uint8_t fn, uint16_t *registers,
                      size_t count);
#endif
