#include "bsp_board.h"
#include "diskio.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
static uint8_t sd_bounce[512] __attribute__((section(".dma"), aligned(32)));
static TaskHandle_t volatile sd_waiter;
void SDMMC1_IRQHandler(void) {
    HAL_SD_IRQHandler(&hsd1);
}
static void sd_notify(uint32_t event) {
    BaseType_t wake = pdFALSE;
    if (sd_waiter)
        xTaskNotifyFromISR(sd_waiter, event, eSetBits, &wake);
    portYIELD_FROM_ISR(wake);
}
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *sd) {
    sd_notify(1);
}
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *sd) {
    sd_notify(2);
}
void HAL_SD_ErrorCallback(SD_HandleTypeDef *sd) {
    sd_notify(4);
}
static bool transfer(bool write, uint32_t sector) {
    uint32_t ignored, result = 0;
    sd_waiter = xTaskGetCurrentTaskHandle();
    xTaskNotifyWait(0, UINT32_MAX, &ignored, 0);
    HAL_StatusTypeDef status = write ? HAL_SD_WriteBlocks_DMA(&hsd1, sd_bounce, sector, 1)
                                     : HAL_SD_ReadBlocks_DMA(&hsd1, sd_bounce, sector, 1);
    bool ok = status == HAL_OK && xTaskNotifyWait(0, UINT32_MAX, &result, pdMS_TO_TICKS(500)) == pdTRUE &&
              !(result & 4) && (result & (write ? 2u : 1u));
    if (!ok)
        HAL_SD_Abort(&hsd1);
    sd_waiter = NULL;
    return ok;
}
bool bsp_sd_init(void) {
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF12_SDIO1;
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &g);
    hsd1.Instance = SDMMC1;
    hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_1B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv = 4;
    HAL_NVIC_SetPriority(SDMMC1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
    return HAL_SD_Init(&hsd1) == HAL_OK && HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) == HAL_OK;
}
DSTATUS disk_initialize(BYTE pdrv) {
    return pdrv == 0 && bsp_sd_init() ? 0 : STA_NOINIT;
}
DSTATUS disk_status(BYTE pdrv) {
    return pdrv == 0 && HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER ? 0 : STA_NOINIT;
}
DRESULT disk_read(BYTE drive, BYTE *out, DWORD sector, UINT count) {
    if (drive || !count)
        return RES_PARERR;
    while (count--) {
        if (!transfer(false, sector++))
            return RES_ERROR;
        memcpy(out, sd_bounce, 512);
        out += 512;
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *in, DWORD sector, UINT count) {
    if (drive || !count)
        return RES_PARERR;
    while (count--) {
        memcpy(sd_bounce, in, 512);
        if (!transfer(true, sector++))
            return RES_ERROR;
        uint32_t start = HAL_GetTick();
        while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
            if (HAL_GetTick() - start > 500)
                return RES_ERROR;
            vTaskDelay(1);
        }
        in += 512;
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE cmd, void *out) {
    if (drive)
        return RES_PARERR;
    HAL_SD_CardInfoTypeDef info;
    if (cmd == CTRL_SYNC)
        return disk_status(0) == 0 ? RES_OK : RES_NOTRDY;
    if (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK)
        return RES_ERROR;
    if (cmd == GET_SECTOR_COUNT)
        *(DWORD *)out = info.LogBlockNbr;
    else if (cmd == GET_SECTOR_SIZE)
        *(WORD *)out = 512;
    else if (cmd == GET_BLOCK_SIZE)
        *(DWORD *)out = 8;
    else
        return RES_PARERR;
    return RES_OK;
}
