#include "gateway_system.h"
#include "bsp_board.h"
#include "ff.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
extern bool gateway_take_config(gateway_config_t *c);
extern void gateway_config_applied(const gateway_config_t *c);
static FATFS filesystem;
static FIL log_file, alarm_file;
static bool write_line(FIL *f, const char *s) {
    UINT written;
    UINT n = (UINT)strlen(s);
    return f_write(f, s, n, &written) == FR_OK && written == n;
}
void gateway_storage_task(void *arg) {
    bool mounted = false, config_pending = false;
    gateway_config_t config;
    uint32_t last_mount = 0, last_log = 0, last_alarm = UINT32_MAX, file_index = 0;
    while (1) {
        gateway_heartbeat(HEART_STORAGE);
        if (!config_pending)
            config_pending = gateway_take_config(&config);
        if (config_pending && gateway_flash_lock(HEART_STORAGE, 100)) {
            if (config_save(&bsp_nor_ops, &config)) {
                gateway_config_applied(&config);
                config_pending = false;
            }
            gateway_flash_unlock();
        }
        uint32_t now = HAL_GetTick();
        if (!mounted && now - last_mount >= 3000) {
            last_mount = now;
            if (bsp_sd_init() && f_mount(&filesystem, "0:", 1) == FR_OK) {
                /* Unique file for every boot; never truncate a prior session. */
                char name[32];
                FRESULT result = FR_DENIED;
                while (file_index < 10000) {
                    gateway_heartbeat(HEART_STORAGE);
                    snprintf(name, sizeof(name), "0:/DATA%04lu.CSV", (unsigned long)file_index++);
                    result = f_open(&log_file, name, FA_WRITE | FA_CREATE_NEW);
                    if (result != FR_EXIST)
                        break;
                    vTaskDelay(1);
                }
                mounted = result == FR_OK;
                if (mounted)
                    mounted =
                        write_line(&log_file,
                                   "uptime_s,temp_x10,humidity_x10,rpm,current_ma,state,alarms,valid\r\n") &&
                        f_sync(&log_file) == FR_OK &&
                        f_open(&alarm_file, "0:/ALARMS.CSV", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK;
                if (mounted) {
                    mounted = f_lseek(&alarm_file, f_size(&alarm_file)) == FR_OK;
                    if (mounted && !f_size(&alarm_file))
                        mounted = write_line(&alarm_file, "uptime_s,alarm_mask\r\n");
                }
                if (!mounted) {
                    f_close(&log_file);
                    f_close(&alarm_file);
                    f_mount(NULL, "0:", 0);
                }
            }
            gateway_sd_update(mounted);
        }
        if (mounted && now - last_log >= 1000) {
            last_log = now;
            gateway_data_t d;
            gateway_snapshot(&d);
            char line[160];
            snprintf(line, sizeof(line), "%lu,%d,%u,%d,%d,%u,%lu,%u\r\n", (unsigned long)d.uptime,
                     d.temperature, d.humidity, d.speed, d.current, d.motor_state, (unsigned long)d.alarms,
                     (d.sensor_valid ? 1 : 0) | (d.can_valid ? 2 : 0));
            bool ok = write_line(&log_file, line) && f_sync(&log_file) == FR_OK;
            if (d.alarms != last_alarm) {
                last_alarm = d.alarms;
                snprintf(line, sizeof(line), "%lu,%lu\r\n", (unsigned long)d.uptime, (unsigned long)d.alarms);
                ok = ok && write_line(&alarm_file, line) && f_sync(&alarm_file) == FR_OK;
            }
            if (!ok) {
                f_close(&log_file);
                f_close(&alarm_file);
                f_mount(NULL, "0:", 0);
                mounted = false;
                gateway_sd_update(false);
            }
            if (mounted && f_size(&log_file) > 8u * 1024u * 1024u) {
                f_close(&log_file);
                f_close(&alarm_file);
                f_mount(NULL, "0:", 0);
                mounted = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
