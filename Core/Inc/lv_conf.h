#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>
#define LV_COLOR_DEPTH 16
#define LV_USE_QRCODE 0
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "stm32h7xx_hal.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (HAL_GetTick())
#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_CHART 1
#define LV_USE_TABVIEW 1
#define LV_USE_SPINBOX 1
#define LV_USE_MSGBOX 1
#endif
