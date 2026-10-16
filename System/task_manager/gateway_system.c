#include "gateway_system.h"
#include "bsp_board.h"
#include "boot_policy.h"
#include "ota_manager.h"
#include "modbus_rtu.h"
#include "pd42.h"
#include "igw_bytes.h"
#include "task.h"
#include <string.h>
static gateway_data_t model;
static gateway_config_t configuration;
static SemaphoreHandle_t model_mutex, config_mutex, flash_mutex;
static QueueHandle_t events, motor_queue, config_queue;
static image_store_t image_store;
static volatile uint32_t heartbeat[HEART_COUNT];
static volatile bool eth_initialized, flash_ready;
static volatile bool watchdog_allowed = true;
static unsigned flash_owner;
void gateway_heartbeat(unsigned task) {
    if (task < HEART_COUNT)
        heartbeat[task] = HAL_GetTick();
}
void gateway_snapshot(gateway_data_t *out) {
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    *out = model;
    xSemaphoreGive(model_mutex);
}
void gateway_get_config(gateway_config_t *out) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    *out = configuration;
    xSemaphoreGive(config_mutex);
}
bool gateway_save_config(const gateway_config_t *c) {
    return config_valid(c) && xQueueSend(config_queue, c, 0) == pdTRUE;
}
bool gateway_enqueue_motor(const motor_command_t *c, void *context) {
    gateway_data_t d;
    gateway_snapshot(&d);
    return motor_command_allowed(&d, c) && xQueueSend(motor_queue, c, 0) == pdTRUE;
}
bool gateway_post_event(const gateway_event_t *event) {
    return xQueueSend(events, event, 0) == pdTRUE;
}
void gateway_link_update(bool link) {
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.network_link = link;
    xSemaphoreGive(model_mutex);
}
void gateway_sd_update(bool ready) {
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.sd_ready = ready;
    xSemaphoreGive(model_mutex);
}
void gateway_eth_ready(bool ready) {
    eth_initialized = ready;
}
bool gateway_is_eth_ready(void) {
    return eth_initialized;
}
void gateway_set_ota(bool busy, uint32_t progress) {
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.ota_busy = busy;
    model.ota_progress = progress;
    xSemaphoreGive(model_mutex);
}
bool gateway_flash_lock(unsigned owner, uint32_t timeout) {
    if (xSemaphoreTake(flash_mutex, pdMS_TO_TICKS(timeout)) != pdTRUE)
        return false;
    flash_owner = owner;
    return true;
}
void gateway_flash_unlock(void) {
    xSemaphoreGive(flash_mutex);
}
image_store_t *gateway_image_store(void) {
    return &image_store;
}
static bool tasks_healthy(void) {
    uint32_t now = HAL_GetTick();
    for (unsigned i = 0; i < HEART_COUNT; i++) {
        if (i == HEART_TCPIP && !eth_initialized)
            continue;
        if (now - heartbeat[i] > 3000)
            return false;
    }
    return true;
}
static void flash_yield(void) {
    gateway_heartbeat(flash_owner);
    if (watchdog_allowed && tasks_healthy())
        bsp_watchdog_feed();
    vTaskDelay(1);
}
void igw_assert_fail(const char *file, int line) {
    (void)file;
    (void)line;
    __disable_irq();
    while (1) {
    }
}
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    igw_assert_fail(name, 0);
}
void vApplicationMallocFailedHook(void) {
    igw_assert_fail("heap", 0);
}
static void sensor_task(void *arg) {
    while (1) {
        gateway_heartbeat(HEART_SENSOR);
        gateway_config_t c;
        gateway_get_config(&c);
        if (huart3.Init.BaudRate != c.sensor_baud) {
            HAL_UART_Abort(&huart3);
            huart3.Init.BaudRate = c.sensor_baud;
            HAL_UART_Init(&huart3);
        }
        uint8_t tx[8], rx[32];
        uint16_t values[2];
        gateway_event_t event = {.source = 0, .tick = HAL_GetTick()};
        rtu_read_request(tx, (uint8_t)c.sensor_address, (uint8_t)c.sensor_function, c.sensor_register, 2);
        for (unsigned retry = 0; retry < 3 && !event.valid; retry++) {
            size_t n = sizeof(rx);
            event.valid = bsp_rs485_exchange(tx, 8, rx, &n, 200) &&
                          rtu_read_response(rx, n, (uint8_t)c.sensor_address, (uint8_t)c.sensor_function,
                                            values, 2) == 0;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (event.valid) {
            int32_t t = config_scale(c.multiplier, (int16_t)values[c.temp_index]);
            uint32_t h = (uint32_t)config_scale(c.multiplier, values[c.hum_index]);
            event.valid = t >= -400 && t <= 1250 && h <= 1000;
            event.temperature = (int16_t)t;
            event.humidity = (uint16_t)h;
        }
        event.tick = HAL_GetTick();
        gateway_post_event(&event);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
static bool motor_exchange(uint8_t code, const uint8_t *data, size_t n, uint8_t *reply, size_t *reply_n) {
    uint8_t tx[64];
    size_t count = pd42_request(tx, IGW_MOTOR_NODE, code, data, n);
    return bsp_can_exchange(tx, count, code, reply, reply_n);
}
static bool motor_disable(void) {
    uint8_t reply[64], data = 1;
    size_t n = sizeof(reply);
    return motor_exchange(PD42_ENABLE, &data, 1, reply, &n);
}
static void can_task(void *arg) {
    uint32_t lease = 0, last_poll = 0;
    bool enabled = !motor_disable();
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.motor_enabled = enabled;
    xSemaphoreGive(model_mutex);
    while (1) {
        gateway_heartbeat(HEART_CAN);
        uint32_t now = HAL_GetTick();
        motor_command_t command;
        if (xQueueReceive(motor_queue, &command, pdMS_TO_TICKS(10)) == pdTRUE) {
            now = HAL_GetTick();
            gateway_data_t d;
            gateway_snapshot(&d);
            if (now - command.issued > 500 || !motor_command_allowed(&d, &command))
                continue;
            uint8_t reply[64], tx[64];
            size_t n = sizeof(reply);
            bool ok = false;
            if (command.type == MOTOR_DISABLE || !command.enabled) {
                ok = motor_disable();
                if (ok)
                    enabled = false;
            } else if (command.type == MOTOR_HEARTBEAT) {
                lease = now;
                continue;
            } else {
                uint8_t enable = 0;
                /* An enable may reach the device even if its acknowledgement is lost. */
                enabled = true;
                ok = motor_exchange(PD42_ENABLE, &enable, 1, reply, &n);
                n = sizeof(reply);
                size_t tx_n = command.type == MOTOR_POSITION
                                  ? pd42_position(tx, IGW_MOTOR_NODE, (uint8_t)command.direction, 100,
                                                  command.rpm, (uint32_t)command.position)
                                  : pd42_velocity(tx, IGW_MOTOR_NODE, (uint8_t)command.direction, 100,
                                                  (float)command.rpm);
                if (ok) {
                    enabled = true;
                    ok = bsp_can_exchange(
                        tx, tx_n, command.type == MOTOR_POSITION ? PD42_ABSOLUTE : PD42_VELOCITY, reply, &n);
                }
                if (ok) {
                    lease = HAL_GetTick();
                } else if (motor_disable())
                    enabled = false;
            }
            xSemaphoreTake(model_mutex, portMAX_DELAY);
            model.motor_enabled = enabled;
            if (ok) {
                model.target_rpm = command.rpm;
                model.direction = command.direction;
                model.control_source = command.source;
                model.alarms &= ~ALARM_LEASE;
            }
            xSemaphoreGive(model_mutex);
        }
        now = HAL_GetTick();
        gateway_data_t d;
        gateway_snapshot(&d);
        if (enabled && (now - lease > IGW_COMMAND_LEASE_MS || !d.can_valid || d.motor_fault ||
                        (!d.network_link && d.control_source == 0) || d.ota_busy)) {
            if (motor_disable())
                enabled = false;
            xSemaphoreTake(model_mutex, portMAX_DELAY);
            model.motor_enabled = enabled;
            model.alarms |= ALARM_LEASE;
            xSemaphoreGive(model_mutex);
        }
        if (now - last_poll >= 250) {
            last_poll = now;
            gateway_event_t event = {.source = 1};
            uint8_t reply[64];
            size_t n = sizeof(reply);
            event.valid = motor_exchange(PD42_SPEED, NULL, 0, reply, &n);
            if (event.valid)
                event.speed = (int16_t)igw_be16(reply + 4);
            n = sizeof(reply);
            bool current_ok = motor_exchange(PD42_CURRENT, NULL, 0, reply, &n);
            if (current_ok)
                event.current = (int16_t)igw_be16(reply + 4);
            n = sizeof(reply);
            bool state_ok = motor_exchange(PD42_STATE, NULL, 0, reply, &n);
            if (state_ok)
                event.motor_state = reply[4];
            n = sizeof(reply);
            bool position_ok = motor_exchange(PD42_POSITION, NULL, 0, reply, &n);
            if (position_ok)
                event.position = (int32_t)igw_be32(reply + 4);
            event.valid = event.valid && current_ok && state_ok && position_ok;
            event.tick = HAL_GetTick();
            gateway_post_event(&event);
        }
    }
}
static void data_task(void *arg) {
    gateway_event_t event;
    while (1) {
        gateway_heartbeat(HEART_DATA);
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            xSemaphoreTake(model_mutex, portMAX_DELAY);
            if (event.source == 0) {
                model.sensor_valid = event.valid;
                if (event.valid) {
                    model.temperature = event.temperature;
                    model.humidity = event.humidity;
                    model.sensor_tick = event.tick;
                }
            } else {
                model.can_valid = event.valid;
                if (event.valid) {
                    model.speed = event.speed;
                    model.current = event.current;
                    model.position = event.position;
                    model.motor_state = event.motor_state;
                    model.motor_fault = event.motor_state >= 3 ? event.motor_state : 0;
                    model.can_tick = event.tick;
                }
            }
            xSemaphoreGive(model_mutex);
        }
        xSemaphoreTake(model_mutex, portMAX_DELAY);
        uint32_t now = HAL_GetTick();
        model.uptime = now / 1000;
        if (now - model.sensor_tick > IGW_DEVICE_TIMEOUT_MS)
            model.sensor_valid = false;
        if (now - model.can_tick > IGW_DEVICE_TIMEOUT_MS)
            model.can_valid = false;
        uint32_t alarms = model.alarms & ALARM_LEASE;
        if (!model.sensor_valid)
            alarms |= ALARM_SENSOR;
        if (!model.can_valid)
            alarms |= ALARM_CAN;
        if (!model.network_link)
            alarms |= ALARM_NETWORK;
        if (!model.sd_ready)
            alarms |= ALARM_SD;
        if (model.motor_fault)
            alarms |= ALARM_MOTOR;
        if (!flash_ready)
            alarms |= ALARM_FLASH;
        model.alarms = alarms;
        xSemaphoreGive(model_mutex);
    }
}
static void monitor_task(void *arg) {
    bsp_watchdog_start();
    uint32_t healthy_since = 0, started = HAL_GetTick();
    bool confirmed = false;
    uint32_t reason = bsp_boot_reason();
    bool trial = reason == BOOT_TRIAL || (image_store.has_record && image_store.record.trial);
    bool factory = reason == BOOT_FACTORY || (reason == 0 && image_store.read_ok && !image_store.has_record);
    while (1) {
        gateway_heartbeat(HEART_MONITOR);
        if (eth_initialized)
            gateway_tcpip_probe();
        watchdog_allowed = !trial || confirmed || HAL_GetTick() - started < 45000;
        bool healthy = tasks_healthy();
        if (healthy && watchdog_allowed)
            bsp_watchdog_feed();
        else
            healthy_since = 0;
        if (healthy && !healthy_since)
            healthy_since = HAL_GetTick();
        if (watchdog_allowed && !confirmed && flash_ready && healthy_since &&
            HAL_GetTick() - healthy_since >= 10000 && eth_initialized &&
            gateway_flash_lock(HEART_MONITOR, 100)) {
            extern uint8_t _flash_image_end;
            uint32_t size = (uint32_t)&_flash_image_end - IGW_APP_BASE;
            if (!image_store.read_ok || !image_store.has_record)
                image_store_open(&image_store, &bsp_nor_ops);
            bool ok = image_store.read_ok;
            if (ok && !image_store.has_record)
                ok = factory && ota_seed_factory(&image_store, &bsp_internal_ops, size, IGW_FW_VERSION);
            if (ok)
                ok = boot_confirm(&image_store);
            gateway_flash_unlock();
            confirmed = ok;
            if (ok)
                gateway_set_ota(false, 0);
        }
        bsp_led(healthy);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
bool gateway_take_config(gateway_config_t *c) {
    return xQueueReceive(config_queue, c, 0) == pdTRUE;
}
void gateway_config_applied(const gateway_config_t *c) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    configuration = *c;
    xSemaphoreGive(config_mutex);
}
void gateway_start(bool ready) {
    flash_ready = ready;
    model.version = IGW_FW_VERSION;
    model_mutex = xSemaphoreCreateMutex();
    config_mutex = xSemaphoreCreateMutex();
    flash_mutex = xSemaphoreCreateMutex();
    events = xQueueCreate(16, sizeof(gateway_event_t));
    motor_queue = xQueueCreate(8, sizeof(motor_command_t));
    config_queue = xQueueCreate(2, sizeof(gateway_config_t));
    configASSERT(model_mutex && config_mutex && flash_mutex && events && motor_queue && config_queue);
    if (ready) {
        config_load(&bsp_nor_ops, &configuration);
        image_store_open(&image_store, &bsp_nor_ops);
    } else
        config_defaults(&configuration);
    /* No motion before the baseline exists, or during a trial/uncertain journal read. */
    model.ota_busy = !ready || !image_store.read_ok || !image_store.has_record ||
                     image_store.record.pending != IMAGE_NONE || bsp_boot_reason() == BOOT_TRIAL;
    bsp_nor_set_yield(flash_yield);
    for (unsigned i = 0; i < HEART_COUNT; i++)
        heartbeat[i] = HAL_GetTick();
    configASSERT(xTaskCreate(sensor_task, "ModbusRTU", 768, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(can_task, "MotorCAN", 768, NULL, 4, NULL) == pdPASS);
    configASSERT(xTaskCreate(data_task, "Data", 768, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(gateway_storage_task, "Storage", 1536, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(gateway_ui_task, "HMI", 2048, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(gateway_network_task, "ModbusTCP", 1024, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(gateway_upgrade_task, "OTA", 1536, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(gateway_ethernet_task, "Ethernet", 1536, NULL, 4, NULL) == pdPASS);
    configASSERT(xTaskCreate(monitor_task, "Monitor", 768, NULL, 5, NULL) == pdPASS);
    vTaskStartScheduler();
    igw_assert_fail("scheduler", 0);
}
