#include "bsp_board.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
static DMA_HandleTypeDef rx_dma, tx_dma;
static TaskHandle_t volatile waiting_task;
static volatile uint16_t received;
static uint8_t rx_buffer[256] __attribute__((section(".dma"), aligned(32)));
static uint8_t tx_buffer[256] __attribute__((section(".dma"), aligned(32)));
bool bsp_uart_init(void) {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &g);
    g.Alternate = GPIO_AF7_USART3;
    g.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOB, &g);
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
        return false;
    huart3.Init = huart1.Init;
    huart3.Instance = USART3;
    huart3.Init.BaudRate = IGW_SENSOR_BAUD;
    if (HAL_UART_Init(&huart3) != HAL_OK)
        return false;
    rx_dma.Instance = DMA1_Stream0;
    rx_dma.Init.Request = DMA_REQUEST_USART3_RX;
    rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    rx_dma.Init.Mode = DMA_NORMAL;
    rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    rx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    tx_dma.Init = rx_dma.Init;
    tx_dma.Instance = DMA1_Stream1;
    tx_dma.Init.Request = DMA_REQUEST_USART3_TX;
    tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    if (HAL_DMA_Init(&rx_dma) != HAL_OK || HAL_DMA_Init(&tx_dma) != HAL_OK)
        return false;
    __HAL_LINKDMA(&huart3, hdmarx, rx_dma);
    __HAL_LINKDMA(&huart3, hdmatx, tx_dma);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    return true;
}
void DMA1_Stream0_IRQHandler(void) {
    HAL_DMA_IRQHandler(&rx_dma);
}
void DMA1_Stream1_IRQHandler(void) {
    HAL_DMA_IRQHandler(&tx_dma);
}
void USART3_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart3);
}
static void notify(uint32_t bits) {
    if (waiting_task) {
        BaseType_t wake = pdFALSE;
        xTaskNotifyFromISR(waiting_task, bits, eSetBits, &wake);
        portYIELD_FROM_ISR(wake);
    }
}
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *h, uint16_t n) {
    if (h == &huart3) {
        received = n;
        notify(2);
    }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h) {
    if (h == &huart3)
        notify(1);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h) {
    if (h == &huart3)
        notify(4);
}
static bool wait_for(uint32_t wanted, uint32_t *seen, uint32_t start, uint32_t timeout) {
    while (!(*seen & wanted)) {
        uint32_t delta = HAL_GetTick() - start, bits;
        if (delta >= timeout)
            return false;
        if (xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(timeout - delta)) != pdTRUE)
            return false;
        *seen |= bits;
        if (*seen & 4)
            return false;
    }
    return true;
}
bool bsp_rs485_exchange(const uint8_t *p, size_t n, uint8_t *out, size_t *out_n, uint32_t timeout) {
    if (!n || n > sizeof(tx_buffer))
        return false;
    waiting_task = xTaskGetCurrentTaskHandle();
    uint32_t discard;
    xTaskNotifyWait(0, UINT32_MAX, &discard, 0);
    uint32_t start = HAL_GetTick(), seen = 0;
    received = 0;
    memcpy(tx_buffer, p, n);
    bool ok = bsp_pcf_write_bit(6, true);
    if (ok)
        ok = HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_buffer, sizeof(rx_buffer)) == HAL_OK;
    if (ok)
        __HAL_DMA_DISABLE_IT(&rx_dma, DMA_IT_HT);
    if (ok)
        ok = HAL_UART_Transmit_DMA(&huart3, tx_buffer, (uint16_t)n) == HAL_OK;
    if (ok)
        ok = wait_for(1, &seen, start, timeout); /* callback after UART TC, not DMA TC */
    bool direction_ok = bsp_pcf_write_bit(6, false);
    if (ok && direction_ok)
        ok = wait_for(2, &seen, start, timeout);
    else
        ok = false;
    if (ok && received <= *out_n) {
        memcpy(out, rx_buffer, received);
        *out_n = received;
    } else
        ok = false;
    HAL_UART_AbortReceive(&huart3);
    if (!ok)
        HAL_UART_AbortTransmit(&huart3);
    waiting_task = NULL;
    return ok;
}
