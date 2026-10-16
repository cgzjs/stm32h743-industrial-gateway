#ifndef BSP_BOARD_H
#define BSP_BOARD_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include "image_store.h"
extern UART_HandleTypeDef huart1, huart3;
extern FDCAN_HandleTypeDef hfdcan1;
extern QSPI_HandleTypeDef hqspi;
extern SD_HandleTypeDef hsd1;
extern ETH_HandleTypeDef heth;
void bsp_early_init(void);
bool bsp_board_init(bool bootloader);
void bsp_delay_us(uint32_t us);
void bsp_watchdog_start(void);
void bsp_watchdog_feed(void);
void bsp_led(bool on);
void bsp_reset(void);
uint32_t bsp_boot_reason(void);
bool bsp_pcf_write_bit(unsigned bit, bool value);
extern const flash_ops_t bsp_nor_ops, bsp_internal_ops;
void bsp_nor_set_yield(void (*fn)(void));
bool bsp_nor_init(void);
bool bsp_can_init(void);
bool bsp_uart_init(void);
bool bsp_rs485_exchange(const uint8_t *request, size_t n, uint8_t *response, size_t *response_n,
                        uint32_t timeout);
bool bsp_can_exchange(const uint8_t *request, size_t n, uint8_t expected, uint8_t *response,
                      size_t *response_n);
bool bsp_lcd_init(void);
uint16_t bsp_lcd_id(void);
void bsp_lcd_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *pixels);
bool bsp_touch_read(uint16_t *raw_x, uint16_t *raw_y);
bool bsp_sd_init(void);
bool bsp_eth_init(void);
#endif
