#ifndef PD42_H
#define PD42_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum {
    PD42_CURRENT = 0x23,
    PD42_SPEED = 0x29,
    PD42_POSITION = 0x2A,
    PD42_STATE = 0x2C,
    PD42_VELOCITY = 0xF1,
    PD42_ABSOLUTE = 0xF2,
    PD42_ENABLE = 0xFA,
    PD42_CLEAR = 0xFB,
    PD42_STOP = 0xFC
};
size_t pd42_request(uint8_t *out, uint8_t node, uint8_t code, const uint8_t *payload, size_t n);
size_t pd42_velocity(uint8_t *out, uint8_t node, uint8_t direction, uint8_t acceleration, float rpm);
size_t pd42_position(uint8_t *out, uint8_t node, uint8_t direction, uint8_t acceleration, uint16_t rpm,
                     uint32_t pulses);
bool pd42_validate(const uint8_t *frame, size_t n, uint8_t node, uint8_t code);
typedef struct {
    uint8_t data[64];
    size_t used;
    uint32_t last_tick;
} pd42_stream_t;
/* Concatenation of <=8 byte Classic CAN extended frames, same ID, vendor rule. */
size_t pd42_stream_feed(pd42_stream_t *s, const uint8_t *p, size_t n, uint32_t now, uint8_t node,
                        uint8_t expected);
#endif
