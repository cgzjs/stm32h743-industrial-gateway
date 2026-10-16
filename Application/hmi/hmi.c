#include "gateway_system.h"
#include "bsp_board.h"
#include "lvgl.h"
#include "task.h"
#include <stdio.h>
static lv_color_t draw_buffer[IGW_LCD_WIDTH * 20];
static lv_disp_draw_buf_t draw;
static lv_obj_t *status, *sensor, *motor, *chart, *speed, *direction, *calibration, *cross, *position;
static lv_chart_series_t *temperature_series, *humidity_series;
static gateway_config_t touch_config;
static uint16_t raw_points[3][2];
static unsigned cal_point;
static bool cal_down, cal_active;
static lv_obj_t *alarm_label;
static struct {
    uint32_t uptime, mask;
} alarm_history[32];
static unsigned alarm_count, alarm_next;
static uint32_t previous_alarm = UINT32_MAX;
static void update_alarms(const gateway_data_t *d) {
    if (d->alarms == previous_alarm)
        return;
    previous_alarm = d->alarms;
    alarm_history[alarm_next].uptime = d->uptime;
    alarm_history[alarm_next].mask = d->alarms;
    alarm_next = (alarm_next + 1) % 32;
    if (alarm_count < 32)
        alarm_count++;
    char text[1100];
    size_t used =
        (size_t)snprintf(text, sizeof(text), "Bits: sensor=1 CAN=2 NET=4\nSD=8 motor=16 lease=32 Flash=64\n");
    for (unsigned i = 0; i < alarm_count && used < sizeof(text); i++) {
        unsigned p = (alarm_next + 31 - i) % 32;
        int n = snprintf(text + used, sizeof(text) - used, "%lus  mask 0x%02lX\n",
                         (unsigned long)alarm_history[p].uptime, (unsigned long)alarm_history[p].mask);
        if (n < 0 || (size_t)n >= sizeof(text) - used)
            break;
        used += (size_t)n;
    }
    lv_label_set_text(alarm_label, text);
}
static const int16_t targets[3][2] = {{30, 30}, {290, 40}, {50, 210}};
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *pixels) {
    bsp_lcd_window((uint16_t)a->x1, (uint16_t)a->y1, (uint16_t)a->x2, (uint16_t)a->y2, (uint16_t *)pixels);
    lv_disp_flush_ready(d);
}
static void input(lv_indev_drv_t *driver, lv_indev_data_t *d) {
    uint16_t x, y;
    bool pressed = bsp_touch_read(&x, &y);
    d->state = LV_INDEV_STATE_RELEASED;
    if (pressed && !cal_active && touch_config.touch_calibrated) {
        int32_t sx = (int32_t)(((int64_t)touch_config.touch[0] * x + (int64_t)touch_config.touch[1] * y +
                                touch_config.touch[2]) >>
                               16);
        int32_t sy = (int32_t)(((int64_t)touch_config.touch[3] * x + (int64_t)touch_config.touch[4] * y +
                                touch_config.touch[5]) >>
                               16);
        if (sx >= 0 && sx < (int32_t)IGW_LCD_WIDTH && sy >= 0 && sy < (int32_t)IGW_LCD_HEIGHT) {
            d->point.x = (lv_coord_t)sx;
            d->point.y = (lv_coord_t)sy;
            d->state = LV_INDEV_STATE_PRESSED;
        }
    }
}
static bool calibrate(void) {
    double x1 = raw_points[0][0], y1 = raw_points[0][1], x2 = raw_points[1][0], y2 = raw_points[1][1],
           x3 = raw_points[2][0], y3 = raw_points[2][1];
    double det = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2);
    if (det > -1000 && det < 1000)
        return false;
    for (unsigned axis = 0; axis < 2; axis++) {
        double t1 = targets[0][axis], t2 = targets[1][axis], t3 = targets[2][axis];
        double a = (t1 * (y2 - y3) + t2 * (y3 - y1) + t3 * (y1 - y2)) / det;
        double b = (t1 * (x3 - x2) + t2 * (x1 - x3) + t3 * (x2 - x1)) / det;
        double c = (t1 * (x2 * y3 - x3 * y2) + t2 * (x3 * y1 - x1 * y3) + t3 * (x1 * y2 - x2 * y1)) / det;
        if (a < -4 || a > 4 || b < -4 || b > 4 || c < -8192 || c > 8192)
            return false;
        touch_config.touch[axis * 3] = (int32_t)(a * 65536);
        touch_config.touch[axis * 3 + 1] = (int32_t)(b * 65536);
        touch_config.touch[axis * 3 + 2] = (int32_t)(c * 65536);
    }
    gateway_config_t current;
    gateway_get_config(&current);
    for (unsigned i = 0; i < 6; i++)
        current.touch[i] = touch_config.touch[i];
    current.touch_calibrated = 1;
    touch_config = current;
    return gateway_save_config(&touch_config);
}
static void show_calibration(lv_event_t *event) {
    cal_point = 0;
    cal_down = false;
    cal_active = true;
    calibration = lv_obj_create(lv_scr_act());
    lv_obj_set_size(calibration, 320, 240);
    lv_obj_set_pos(calibration, 0, 0);
    lv_obj_t *label = lv_label_create(calibration);
    lv_label_set_text(label, "Touch each cross, then release");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    cross = lv_label_create(calibration);
    lv_label_set_text(cross, "+");
    lv_obj_set_pos(cross, targets[0][0] - 8, targets[0][1] - 10);
}
static void command(lv_event_t *event) {
    bool enable = (bool)(uintptr_t)lv_event_get_user_data(event);
    motor_command_t c = {.type = enable ? MOTOR_VELOCITY : MOTOR_DISABLE,
                         .enabled = enable,
                         .source = 1,
                         .issued = HAL_GetTick()};
    c.rpm = (uint16_t)lv_spinbox_get_value(speed);
    c.direction = lv_obj_has_state(direction, LV_STATE_CHECKED);
    gateway_enqueue_motor(&c, NULL);
}
static void adjust(lv_event_t *e) {
    if ((intptr_t)lv_event_get_user_data(e) > 0)
        lv_spinbox_increment(speed);
    else
        lv_spinbox_decrement(speed);
}
static void adjust_position(lv_event_t *e) {
    int32_t v = lv_spinbox_get_value(position);
    v += (intptr_t)lv_event_get_user_data(e) > 0 ? 51200 : -51200;
    if (v < 0)
        v = 0;
    if (v > 99999999)
        v = 99999999;
    lv_spinbox_set_value(position, v);
}
static void move_position(lv_event_t *e) {
    motor_command_t c = {.type = MOTOR_POSITION,
                         .enabled = 1,
                         .source = 1,
                         .issued = HAL_GetTick(),
                         .rpm = (uint16_t)lv_spinbox_get_value(speed),
                         .direction = lv_obj_has_state(direction, LV_STATE_CHECKED),
                         .position = lv_spinbox_get_value(position)};
    gateway_enqueue_motor(&c, NULL);
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *data) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 70, 32);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}
void gateway_ui_task(void *arg) {
    gateway_heartbeat(HEART_UI);
    bool lcd = bsp_lcd_init();
    if (!lcd)
        printf("LCD ID=0x%04X unsupported; expected ILI9341/0x9341\r\n", (unsigned)bsp_lcd_id());
    if (lcd) {
        gateway_get_config(&touch_config);
        lv_init();
        lv_disp_draw_buf_init(&draw, draw_buffer, NULL, IGW_LCD_WIDTH * 20);
        static lv_disp_drv_t disp;
        lv_disp_drv_init(&disp);
        disp.hor_res = 320;
        disp.ver_res = 240;
        disp.flush_cb = flush;
        disp.draw_buf = &draw;
        lv_disp_drv_register(&disp);
        static lv_indev_drv_t indev;
        lv_indev_drv_init(&indev);
        indev.type = LV_INDEV_TYPE_POINTER;
        indev.read_cb = input;
        lv_indev_drv_register(&indev);
        lv_obj_t *tabs = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 28);
        lv_obj_t *dashboard = lv_tabview_add_tab(tabs, "Monitor");
        lv_obj_t *control = lv_tabview_add_tab(tabs, "Motor");
        lv_obj_t *settings = lv_tabview_add_tab(tabs, "System");
        lv_obj_t *alarms = lv_tabview_add_tab(tabs, "Alarms");
        alarm_label = lv_label_create(alarms);
        lv_obj_set_width(alarm_label, 280);
        lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_12, 0);
        lv_label_set_text(alarm_label, "No transitions yet");
        sensor = lv_label_create(dashboard);
        lv_obj_set_pos(sensor, 0, 0);
        motor = lv_label_create(dashboard);
        lv_obj_set_pos(motor, 0, 25);
        chart = lv_chart_create(dashboard);
        lv_obj_set_size(chart, 280, 105);
        lv_obj_set_pos(chart, 0, 55);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -40, 100);
        lv_chart_set_point_count(chart, 60);
        temperature_series =
            lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        humidity_series =
            lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
        speed = lv_spinbox_create(control);
        lv_spinbox_set_range(speed, 0, IGW_MAX_RPM);
        lv_spinbox_set_digit_format(speed, 3, 0);
        lv_spinbox_set_value(speed, 100);
        lv_obj_set_pos(speed, 70, 0);
        lv_obj_set_width(speed, 100);
        lv_obj_set_pos(button(control, "-", adjust, (void *)(intptr_t)-1), 0, 0);
        lv_obj_set_pos(button(control, "+", adjust, (void *)(intptr_t)1), 180, 0);
        direction = lv_switch_create(control);
        lv_obj_set_pos(direction, 180, 50);
        lv_obj_t *label = lv_label_create(control);
        lv_label_set_text(label, "Reverse");
        lv_obj_set_pos(label, 80, 55);
        lv_obj_set_pos(button(control, "Run", command, (void *)1), 25, 95);
        lv_obj_set_pos(button(control, "Stop", command, NULL), 170, 95);
        position = lv_spinbox_create(control);
        lv_spinbox_set_range(position, 0, 99999999);
        lv_spinbox_set_digit_format(position, 8, 0);
        lv_spinbox_set_value(position, 51200);
        lv_obj_set_pos(position, 0, 137);
        lv_obj_set_width(position, 120);
        lv_obj_set_style_text_font(position, &lv_font_montserrat_12, 0);
        lv_obj_t *minus = button(control, "-", adjust_position, (void *)(intptr_t)-1),
                 *plus = button(control, "+", adjust_position, (void *)1);
        lv_obj_set_size(minus, 36, 28);
        lv_obj_set_size(plus, 36, 28);
        lv_obj_set_pos(minus, 123, 137);
        lv_obj_set_pos(plus, 161, 137);
        lv_obj_set_pos(button(control, "Move", move_position, NULL), 205, 137);
        status = lv_label_create(settings);
        lv_obj_set_pos(status, 0, 0);
        lv_obj_set_pos(button(settings, "Touch", show_calibration, NULL), 0, 145);
        if (!touch_config.touch_calibrated)
            show_calibration(NULL);
    }
    uint32_t refresh = 0;
    while (1) {
        gateway_heartbeat(HEART_UI);
        if (lcd) {
            if (cal_active) {
                uint16_t x, y;
                bool down = bsp_touch_read(&x, &y);
                if (down && !cal_down) {
                    raw_points[cal_point][0] = x;
                    raw_points[cal_point][1] = y;
                    cal_down = true;
                }
                if (!down && cal_down) {
                    cal_down = false;
                    if (++cal_point == 3) {
                        if (calibrate()) {
                            cal_active = false;
                            lv_obj_del(calibration);
                        } else
                            cal_point = 0;
                    }
                    if (cal_active)
                        lv_obj_set_pos(cross, targets[cal_point][0] - 8, targets[cal_point][1] - 10);
                }
            }
            if (HAL_GetTick() - refresh >= 500) {
                refresh = HAL_GetTick();
                gateway_data_t d;
                gateway_snapshot(&d);
                update_alarms(&d);
                char text[180];
                int t = d.temperature < 0 ? -d.temperature : d.temperature;
                snprintf(text, sizeof(text), "T: %s%d.%d C   RH: %u.%u %% %s", d.temperature < 0 ? "-" : "",
                         t / 10, t % 10, d.humidity / 10, d.humidity % 10, d.sensor_valid ? "" : "OFFLINE");
                lv_label_set_text(sensor, text);
                snprintf(text, sizeof(text), "Motor: %d rpm / %d mA  %s", d.speed, d.current,
                         d.can_valid ? "OK" : "OFFLINE");
                lv_label_set_text(motor, text);
                lv_chart_set_next_value(chart, temperature_series,
                                        d.sensor_valid ? d.temperature / 10 : LV_CHART_POINT_NONE);
                lv_chart_set_next_value(chart, humidity_series,
                                        d.sensor_valid ? d.humidity / 10 : LV_CHART_POINT_NONE);
                snprintf(text, sizeof(text),
                         "FW: %lu.%lu.%lu\nEthernet: %s\nSD: %s\nAlarms: 0x%02lX\nOTA: %lu.%lu %%\nMotor "
                         "lease: 2 seconds",
                         (unsigned long)(d.version >> 16), (unsigned long)((d.version >> 8) & 255),
                         (unsigned long)(d.version & 255), d.network_link ? "UP" : "DOWN",
                         d.sd_ready ? "READY" : "MISSING", (unsigned long)d.alarms,
                         (unsigned long)(d.ota_progress / 10), (unsigned long)(d.ota_progress % 10));
                lv_label_set_text(status, text);
                if (d.motor_enabled && d.control_source == 1) {
                    motor_command_t hb = {
                        .type = MOTOR_HEARTBEAT, .enabled = 1, .source = 1, .issued = HAL_GetTick()};
                    gateway_enqueue_motor(&hb, NULL);
                }
            }
            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
