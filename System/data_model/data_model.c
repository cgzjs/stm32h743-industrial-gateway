#include "data_model.h"
#include "igw_config.h"
bool data_read_register(const gateway_data_t *d, uint16_t a, uint16_t *v) {
    switch (a) {
    case 0:
        *v = (uint16_t)d->temperature;
        break;
    case 1:
        *v = d->humidity;
        break;
    case 2:
        *v = (d->sensor_valid ? 1u : 0u) | (d->can_valid ? 2u : 0u) | (d->network_link ? 4u : 0u) |
             (d->sd_ready ? 8u : 0u);
        break;
    case 3:
        *v = (uint16_t)d->alarms;
        break;
    case 0x10:
        *v = (uint16_t)d->speed;
        break;
    case 0x11:
        *v = (uint16_t)d->current;
        break;
    case 0x12:
        *v = d->motor_state;
        break;
    case 0x13:
        *v = d->motor_fault;
        break;
    case 0x14:
        *v = (uint16_t)((uint32_t)d->position >> 16);
        break;
    case 0x15:
        *v = (uint16_t)d->position;
        break;
    case 0x27:
        *v = d->control_source;
        break;
    case 0x20:
        *v = d->target_rpm;
        break;
    case 0x21:
        *v = d->motor_enabled;
        break;
    case 0x22:
        *v = d->direction;
        break;
    case 0x30:
        *v = (uint16_t)(d->version >> 16);
        break;
    case 0x31:
        *v = (uint16_t)d->version;
        break;
    case 0x32:
        *v = (uint16_t)(d->uptime >> 16);
        break;
    case 0x33:
        *v = (uint16_t)d->uptime;
        break;
    case 0x34:
        *v = d->ota_busy;
        break;
    case 0x35:
        *v = (uint16_t)d->ota_progress;
        break;
    default:
        return false;
    }
    return true;
}
bool motor_command_allowed(const gateway_data_t *d, const motor_command_t *c) {
    if (c->type == MOTOR_DISABLE)
        return true;
    if (c->type == MOTOR_HEARTBEAT)
        return !d->ota_busy && d->motor_enabled && c->source == d->control_source;
    return (c->type == MOTOR_VELOCITY || c->type == MOTOR_POSITION) && !d->ota_busy && d->can_valid &&
           !d->motor_fault && c->rpm <= IGW_MAX_RPM && c->direction <= 1 && c->enabled <= 1 && c->source <= 1;
}
