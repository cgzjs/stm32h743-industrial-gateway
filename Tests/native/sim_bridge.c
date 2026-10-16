/* Native transport adapter: production protocol/OTA code, synthetic peripherals. */
#include "ota_manager.h"
#include "boot_policy.h"
#include "config_store.h"
#include "modbus_tcp.h"
#include "fw_protocol.h"
#include "igw_crc.h"
#include "igw_bytes.h"
#include <string.h>
#define EXPORT __declspec(dllexport)
#define NOR_SIZE (IGW_SLOT_B + IGW_SLOT_SPAN)
static uint8_t nor[NOR_SIZE], internal[IGW_APP_CAPACITY];
static image_store_t store;
static ota_manager_t ota;
static gateway_data_t data;
static gateway_config_t config;
static uint32_t clock_ms, lease;
static bool fail_trial;
static bool nr(uint32_t a, void *p, size_t n) {
    if (a > NOR_SIZE || n > NOR_SIZE - a)
        return false;
    memcpy(p, nor + a, n);
    return true;
}
static bool ne(uint32_t a, size_t n) {
    if (a > NOR_SIZE || n > NOR_SIZE - a || a % 4096 || n % 4096)
        return false;
    memset(nor + a, 255, n);
    return true;
}
static bool np(uint32_t a, const void *p, size_t n) {
    if (a > NOR_SIZE || n > NOR_SIZE - a)
        return false;
    const uint8_t *src = p;
    for (size_t i = 0; i < n; i++) {
        if ((nor[a + i] & src[i]) != src[i])
            return false;
        nor[a + i] &= src[i];
    }
    return true;
}
static bool ir(uint32_t a, void *p, size_t n) {
    if (a < IGW_APP_BASE || a > IGW_APP_BASE + IGW_APP_CAPACITY || n > IGW_APP_BASE + IGW_APP_CAPACITY - a)
        return false;
    memcpy(p, internal + a - IGW_APP_BASE, n);
    return true;
}
static bool ie(uint32_t a, size_t n) {
    if (a != IGW_APP_BASE || n != IGW_APP_CAPACITY)
        return false;
    memset(internal, 255, sizeof(internal));
    return true;
}
static bool ip(uint32_t a, const void *p, size_t n) {
    if (a < IGW_APP_BASE || a > IGW_APP_BASE + IGW_APP_CAPACITY || n > IGW_APP_BASE + IGW_APP_CAPACITY - a)
        return false;
    memcpy(internal + a - IGW_APP_BASE, p, n);
    return true;
}
static const flash_ops_t nops = {nr, ne, np, NULL}, iops = {ir, ie, ip, NULL};
static bool enqueue(const motor_command_t *c, void *context) {
    if (!motor_command_allowed(&data, c))
        return false;
    if (c->type == MOTOR_HEARTBEAT) {
        lease = clock_ms;
        return true;
    }
    data.target_rpm = c->rpm;
    data.direction = c->direction;
    data.motor_enabled = c->enabled;
    data.control_source = c->source;
    lease = clock_ms;
    data.speed = c->enabled ? (c->direction ? -(int16_t)c->rpm : (int16_t)c->rpm) : 0;
    data.current = c->enabled ? 320 : 0;
    data.motor_state = c->enabled ? 2 : 0;
    if (c->type == MOTOR_POSITION) {
        data.position = c->direction ? -(int32_t)c->position : c->position;
        data.motor_state = 1;
    }
    return true;
}
EXPORT void sim_init(void) {
    memset(nor, 255, sizeof(nor));
    memset(internal, 255, sizeof(internal));
    memset(&data, 0, sizeof(data));
    memset(&ota, 0, sizeof(ota));
    clock_ms = lease = 0;
    fail_trial = false;
    uint32_t sp = 0x24080000, pc = IGW_APP_BASE + 0x101;
    memcpy(internal, &sp, 4);
    memcpy(internal + 4, &pc, 4);
    uint32_t identity[3]={IGW_BOARD_ID,IGW_FW_VERSION,IGW_APP_BASE};memcpy(internal+IGW_IDENTITY_OFFSET,"IGWID3\0",8);memcpy(internal+IGW_IDENTITY_OFFSET+8,identity,sizeof(identity));
    image_store_open(&store, &nops);
    ota_seed_factory(&store, &iops, 4096, IGW_FW_VERSION);
    config_defaults(&config);
    data.version = IGW_FW_VERSION;
    data.sensor_valid = data.can_valid = data.network_link = data.sd_ready = true;
    data.temperature = 253;
    data.humidity = 615;
}
EXPORT void sim_tick(uint32_t elapsed) {
    clock_ms += elapsed;
    data.uptime = clock_ms / 1000;
    data.temperature = (int16_t)(250 + (clock_ms / 1000) % 20);
    if (data.motor_enabled && clock_ms - lease > 2000) {
        data.motor_enabled = false;
        data.speed = 0;
        data.alarms |= ALARM_LEASE;
    }
}
EXPORT void sim_fail_trial(int fail) {
    fail_trial = fail != 0;
}
EXPORT size_t sim_modbus(const uint8_t *p, size_t n, uint8_t *out) {
    return modbus_tcp_process(p, n, out, &data, clock_ms, enqueue, NULL);
}
EXPORT size_t sim_frame(const uint8_t *p, size_t n, uint8_t *out) {
    fw_header_t h;
    if (n < 24 || fw_decode_header(p, &h) || h.status || n != 24 + h.length || fw_verify(p, p + 24, &h))
        return 0;
    int result = OTA_ARGUMENT;
    uint8_t response[48];
    uint32_t length = 0;
    switch (h.operation) {
    case FW_BEGIN:
        if (h.length == 16) {
            data.motor_enabled = false;
            data.speed = 0;
            result = ota_begin(&ota, &store, h.session, igw_be32(p + 24), igw_be32(p + 28), igw_be32(p + 32),
                               igw_be32(p + 36));
            data.ota_busy = result == 0;
        }
        break;
    case FW_DATA:
        result = ota_chunk(&ota, h.session, h.offset, p + 24, h.length);
        data.ota_progress = ota.size ? 1000 * ota.offset / ota.size : 0;
        break;
    case FW_END:
        if (!h.length)
            result = ota_finish(&ota, h.session);
        break;
    case FW_STATUS:
        if (!h.length) {
            result = 0;
            length = 24;
            igw_put32(response, IGW_BOARD_ID);
            igw_put32(response + 4, data.version);
            igw_put32(response + 8, store.record.active);
            igw_put32(response + 12, store.record.pending);
            igw_put32(response + 16, ota.offset);
            igw_put32(response + 20, ota.size);
        }
        break;
    case FW_ABORT:
        if (!h.length) {
            ota_abort(&ota);
            data.ota_busy = store.record.pending != IMAGE_NONE;
            result = 0;
        }
        break;
    case FW_REBOOT:
        if (!h.length && !ota.receiving && !data.motor_enabled) {
            image_store_open(&store, &nops);
            boot_result_t boot = boot_select_install(&store, &iops);
            if (boot == BOOT_TRIAL) {
                if (fail_trial) {
                    image_store_open(&store, &nops);
                    boot = boot_select_install(&store, &iops);
                } else
                    boot_confirm(&store);
            }
            if (boot != BOOT_RECOVERY) {
                image_header_t active;
                image_header_read(&store, store.record.active, &active);
                data.version = active.version;
                result = 0;
                data.ota_busy = false;
                data.ota_progress = 0;
                ota = (ota_manager_t){0};
            }
        }
        break;
    case 7:
        if (!h.length) {
            config_encode(response, &config);
            length = 48;
            result = 0;
        }
        break;
    case 8:
        if (!ota.receiving && store.record.pending == IMAGE_NONE && !data.motor_enabled) {
            gateway_config_t c;
            if (config_decode(&c, p + 24, h.length) && config_save(&nops, &c)) {
                config = c;
                result = 0;
            }
        }
        break;
    }
    h.status = (uint16_t)result;
    h.offset = ota.offset;
    h.length = length;
    return fw_encode(out, &h, response);
}
EXPORT void sim_drop_download(void) {
    if (ota.receiving) {
        ota_abort(&ota);
        data.ota_busy = false;
    }
}
