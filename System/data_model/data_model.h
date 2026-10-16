#ifndef DATA_MODEL_H
#define DATA_MODEL_H
#include <stdbool.h>
#include <stdint.h>
enum {
    ALARM_SENSOR = 1,
    ALARM_CAN = 2,
    ALARM_NETWORK = 4,
    ALARM_SD = 8,
    ALARM_MOTOR = 16,
    ALARM_LEASE = 32,
    ALARM_FLASH = 64
};
typedef struct {
    int16_t temperature, speed, current;
    uint16_t humidity, motor_state, motor_fault, target_rpm, direction;
    uint32_t sensor_tick, can_tick, alarms, uptime, version, ota_progress;
    bool sensor_valid, can_valid, network_link, motor_enabled, sd_ready, ota_busy;
    uint8_t control_source;
    int32_t position;
} gateway_data_t;
typedef enum { MOTOR_VELOCITY, MOTOR_DISABLE, MOTOR_POSITION, MOTOR_HEARTBEAT } motor_command_type_t;
typedef struct {
    motor_command_type_t type;
    uint16_t rpm, direction, enabled;
    int32_t position;
    uint32_t issued;
    uint8_t source; /* 0: remote, 1: HMI */
} motor_command_t;
bool data_read_register(const gateway_data_t *d, uint16_t address, uint16_t *value);
bool motor_command_allowed(const gateway_data_t *d, const motor_command_t *c);
#endif
