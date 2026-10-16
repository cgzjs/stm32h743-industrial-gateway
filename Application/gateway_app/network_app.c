#include "gateway_system.h"
#include "bsp_board.h"
#include "modbus_tcp.h"
#include "fw_protocol.h"
#include "ota_manager.h"
#include "igw_bytes.h"
#include "lwip/sockets.h"
#include "task.h"
#include <errno.h>
#include <string.h>
static bool journal_uncertain;
/* Called with the Flash mutex held; never release motor inhibition on an uncertain commit. */
static bool refresh_upgrade_state(image_store_t *s) {
    bool expected = s->has_record || journal_uncertain;
    bool valid = image_store_open(s, &bsp_nor_ops);
    journal_uncertain = (!valid || !s->read_ok) && expected;
    return journal_uncertain || !s->read_ok || !valid || s->record.pending != IMAGE_NONE;
}
static bool read_all(int socket, uint8_t *p, size_t n, unsigned heart, uint32_t deadline) {
    uint32_t start = HAL_GetTick();
    while (n) {
        gateway_heartbeat(heart);
        int got = lwip_recv(socket, p, n, 0);
        if (got == 0)
            return false;
        if (got < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                return false;
        } else {
            p += got;
            n -= (size_t)got;
        }
        if (HAL_GetTick() - start > deadline)
            return false;
    }
    return true;
}
static bool send_all(int socket, const uint8_t *p, size_t n, unsigned heart) {
    while (n) {
        gateway_heartbeat(heart);
        int sent = lwip_send(socket, p, n, 0);
        if (sent <= 0)
            return false;
        p += sent;
        n -= (size_t)sent;
    }
    return true;
}
static int listener(uint16_t port, unsigned heart) {
    while (!gateway_is_eth_ready()) {
        gateway_heartbeat(heart);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    int socket = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket < 0)
        return -1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    if (lwip_bind(socket, (struct sockaddr *)&address, sizeof(address)) < 0 || lwip_listen(socket, 2) < 0) {
        lwip_close(socket);
        return -1;
    }
    return socket;
}
static int accept_client(int socket, unsigned heart) {
    gateway_heartbeat(heart);
    fd_set read;
    FD_ZERO(&read);
    FD_SET(socket, &read);
    struct timeval time = {.tv_sec = 0, .tv_usec = 250000};
    if (lwip_select(socket + 1, &read, NULL, NULL, &time) <= 0)
        return -1;
    int client = lwip_accept(socket, NULL, NULL);
    if (client >= 0) {
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 250000};
        lwip_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        lwip_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    return client;
}
void gateway_network_task(void *arg) {
    int server = listener(IGW_MODBUS_PORT, HEART_MODBUS);
    while (1) {
        gateway_heartbeat(HEART_MODBUS);
        if (server < 0) {
            vTaskDelay(250);
            server = listener(IGW_MODBUS_PORT, HEART_MODBUS);
            continue;
        }
        int client = accept_client(server, HEART_MODBUS);
        if (client < 0)
            continue;
        uint8_t in[260], out[260];
        while (read_all(client, in, 7, HEART_MODBUS, 2000)) {
            uint16_t length = igw_be16(in + 4);
            if (igw_be16(in + 2) || length < 2 || length > 254)
                break;
            if (!read_all(client, in + 7, length - 1, HEART_MODBUS, 2000))
                break;
            gateway_data_t snapshot;
            gateway_snapshot(&snapshot);
            size_t n = modbus_tcp_process(in, length + 6, out, &snapshot, HAL_GetTick(),
                                          gateway_enqueue_motor, NULL);
            if (!n || !send_all(client, out, n, HEART_MODBUS))
                break;
        }
        lwip_close(client);
    }
}
void gateway_upgrade_task(void *arg) {
    int server = listener(IGW_OTA_PORT, HEART_OTA);
    ota_manager_t ota = {0};
    while (1) {
        gateway_heartbeat(HEART_OTA);
        if (server < 0) {
            vTaskDelay(250);
            server = listener(IGW_OTA_PORT, HEART_OTA);
            continue;
        }
        int client = accept_client(server, HEART_OTA);
        if (client < 0)
            continue;
        uint8_t wire[FW_HEADER_SIZE + IGW_OTA_CHUNK], payload[IGW_OTA_CHUNK];
        while (read_all(client, wire, 24, HEART_OTA, 2000)) {
            fw_header_t h;
            if (fw_decode_header(wire, &h) || h.status ||
                !read_all(client, payload, h.length, HEART_OTA, 2000) || fw_verify(wire, payload, &h))
                break;
            int result = OTA_ARGUMENT;
            bool reboot = false;
            uint32_t reply_length = 0;
            if (!gateway_flash_lock(HEART_OTA, 500)) {
                result = OTA_BUSY;
            } else {
                image_store_t *s = gateway_image_store();
                switch (h.operation) {
                case FW_BEGIN:
                    if (h.length == 16 && !ota.receiving) {
                        gateway_set_ota(true, 0);
                        motor_command_t stop = {.type = MOTOR_DISABLE, .issued = HAL_GetTick()};
                        gateway_enqueue_motor(&stop, NULL);
                        gateway_data_t d;
                        uint32_t stop_started = HAL_GetTick();
                        do {
                            gateway_heartbeat(HEART_OTA);
                            vTaskDelay(pdMS_TO_TICKS(20));
                            gateway_snapshot(&d);
                        } while (d.motor_enabled && HAL_GetTick() - stop_started < 1200);
                        result = d.motor_enabled
                                     ? OTA_BUSY
                                     : ota_begin(&ota, s, h.session, igw_be32(payload), igw_be32(payload + 4),
                                                 igw_be32(payload + 8), igw_be32(payload + 12));
                        if (result != OTA_OK)
                            gateway_set_ota(refresh_upgrade_state(s), 0);
                    }
                    break;
                case FW_DATA:
                    result = ota_chunk(&ota, h.session, h.offset, payload, h.length);
                    if (result == OTA_OK)
                        gateway_set_ota(true, ota.size ? (1000u * ota.offset / ota.size) : 0);
                    break;
                case FW_END:
                    if (!h.length) {
                        result = ota_finish(&ota, h.session);
                        if (result == OTA_OK)
                            gateway_set_ota(true, 1000);
                    }
                    break;
                case FW_STATUS:
                    if (!h.length) {
                        result = OTA_OK;
                        reply_length = 24;
                        igw_put32(payload, IGW_BOARD_ID);
                        igw_put32(payload + 4, IGW_FW_VERSION);
                        igw_put32(payload + 8, s->has_record ? s->record.active : IMAGE_NONE);
                        igw_put32(payload + 12, s->has_record ? s->record.pending : IMAGE_NONE);
                        igw_put32(payload + 16, ota.offset);
                        igw_put32(payload + 20, ota.size);
                    }
                    break;
                case FW_ABORT:
                    if (!h.length) {
                        ota_abort(&ota);
                        gateway_set_ota(refresh_upgrade_state(s), 0);
                        result = OTA_OK;
                    }
                    break;
                case FW_REBOOT:
                    if (!h.length && !ota.receiving) {
                        gateway_data_t d;
                        gateway_snapshot(&d);
                        result = d.motor_enabled ? OTA_BUSY : OTA_OK;
                        reboot = result == OTA_OK;
                    }
                    break;
                case 7: /* configuration GET */
                    if (!h.length) {
                        gateway_config_t c;
                        gateway_get_config(&c);
                        config_encode(payload, &c);
                        reply_length = CONFIG_WIRE_SIZE;
                        result = OTA_OK;
                    }
                    break;
                case 8: /* configuration SET, atomic Flash persistence before ACK */
                    if (!ota.receiving && s->has_record && s->record.pending == IMAGE_NONE) {
                        gateway_config_t c;
                        if (config_decode(&c, payload, h.length)) {
                            gateway_data_t d;
                            gateway_snapshot(&d);
                            if (!d.motor_enabled && config_save(&bsp_nor_ops, &c)) {
                                extern void gateway_config_applied(const gateway_config_t *);
                                gateway_config_applied(&c);
                                result = OTA_OK;
                            } else
                                result = OTA_BUSY;
                        }
                    }
                    break;
                }
                gateway_flash_unlock();
            }
            h.status = (uint16_t)result;
            h.offset = ota.offset;
            h.length = reply_length;
            size_t count = fw_encode(wire, &h, payload);
            if (!send_all(client, wire, count, HEART_OTA))
                break;
            if (reboot) {
                vTaskDelay(pdMS_TO_TICKS(100));
                bsp_reset();
            }
        }
        lwip_close(client);
        if (ota.receiving) {
            ota_abort(&ota);
            bool busy = true;
            if (gateway_flash_lock(HEART_OTA, 500)) {
                busy = refresh_upgrade_state(gateway_image_store());
                gateway_flash_unlock();
            }
            gateway_set_ota(busy, 0);
        }
    }
}
