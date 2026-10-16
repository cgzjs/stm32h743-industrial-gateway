#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "data_model.h"
typedef bool (*motor_enqueue_fn)(const motor_command_t *c, void *context);
/* Input is exactly one MBAP ADU, no socket assumptions. Output capacity >=260. */
size_t modbus_tcp_process(const uint8_t *in, size_t n, uint8_t *out, const gateway_data_t *data, uint32_t now,
                          motor_enqueue_fn enqueue, void *context);
#endif
