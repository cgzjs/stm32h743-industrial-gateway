#ifndef GATEWAY_SYSTEM_H
#define GATEWAY_SYSTEM_H
#include "data_model.h"
#include "config_store.h"
#include "image_store.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
enum {
    HEART_SENSOR,
    HEART_CAN,
    HEART_DATA,
    HEART_STORAGE,
    HEART_UI,
    HEART_MODBUS,
    HEART_OTA,
    HEART_ETH,
    HEART_MONITOR,
    HEART_TCPIP,
    HEART_COUNT
};
typedef struct {
    uint8_t source;
    int16_t temperature, current, speed;
    uint16_t humidity, motor_state;
    uint32_t tick;
    int32_t position;
    bool valid;
} gateway_event_t;
void gateway_start(bool flash_ready);
void gateway_heartbeat(unsigned task);
void gateway_snapshot(gateway_data_t *out);
void gateway_get_config(gateway_config_t *out);
bool gateway_save_config(const gateway_config_t *c);
bool gateway_enqueue_motor(const motor_command_t *c, void *context);
bool gateway_post_event(const gateway_event_t *event);
void gateway_link_update(bool link);
void gateway_sd_update(bool ready);
void gateway_eth_ready(bool ready);
bool gateway_is_eth_ready(void);
void gateway_set_ota(bool busy, uint32_t progress);
bool gateway_flash_lock(unsigned owner, uint32_t timeout_ms);
void gateway_flash_unlock(void);
image_store_t *gateway_image_store(void);
void gateway_ethernet_task(void *arg);
void gateway_tcpip_probe(void);
void gateway_network_task(void *arg);
void gateway_upgrade_task(void *arg);
void gateway_storage_task(void *arg);
void gateway_ui_task(void *arg);
void igw_assert_fail(const char *file, int line);
#endif
