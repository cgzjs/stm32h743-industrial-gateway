#include "bsp_board.h"
#include "pd42.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>
typedef struct {
    uint32_t id, tick;
    uint8_t n, data[8];
} can_rx_t;
static QueueHandle_t rx_queue;
bool bsp_can_init(void) {
    __HAL_RCC_FDCAN_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOA, &g);
    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = DISABLE;
    hfdcan1.Init.TransmitPause = DISABLE;
    hfdcan1.Init.ProtocolException = DISABLE;
    /* PLL1Q=100 MHz / 10 / (1+15+4) = 500 kbit/s. */
    hfdcan1.Init.NominalPrescaler = 10;
    hfdcan1.Init.NominalSyncJumpWidth = 4;
    hfdcan1.Init.NominalTimeSeg1 = 15;
    hfdcan1.Init.NominalTimeSeg2 = 4;
    hfdcan1.Init.DataPrescaler = 10;
    hfdcan1.Init.DataSyncJumpWidth = 4;
    hfdcan1.Init.DataTimeSeg1 = 15;
    hfdcan1.Init.DataTimeSeg2 = 4;
    hfdcan1.Init.MessageRAMOffset = 0;
    hfdcan1.Init.StdFiltersNbr = 0;
    hfdcan1.Init.ExtFiltersNbr = 1;
    hfdcan1.Init.RxFifo0ElmtsNbr = 16;
    hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 4;
    hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
        return false;
    FDCAN_FilterTypeDef f = {0};
    f.IdType = FDCAN_EXTENDED_ID;
    f.FilterType = FDCAN_FILTER_MASK;
    f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    f.FilterID1 = 0x1000;
    f.FilterID2 = 0x1FFFFFF0;
    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &f) != HAL_OK ||
        HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
        return false;
    rx_queue = xQueueCreate(24, sizeof(can_rx_t));
    if (!rx_queue)
        return false;
    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
    return HAL_FDCAN_Start(&hfdcan1) == HAL_OK &&
           HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) == HAL_OK;
}
void FDCAN1_IT0_IRQHandler(void) {
    HAL_FDCAN_IRQHandler(&hfdcan1);
}
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *h, uint32_t flags) {
    if (!(flags & FDCAN_IT_RX_FIFO0_NEW_MESSAGE))
        return;
    BaseType_t wake = pdFALSE;
    while (HAL_FDCAN_GetRxFifoFillLevel(h, FDCAN_RX_FIFO0)) {
        FDCAN_RxHeaderTypeDef hdr;
        can_rx_t rx = {0};
        if (HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO0, &hdr, rx.data) != HAL_OK)
            break;
        rx.id = hdr.Identifier;
        rx.n = (uint8_t)(hdr.DataLength >> 16);
        rx.tick = HAL_GetTick();
        if (hdr.IdType == FDCAN_EXTENDED_ID && rx.n <= 8 && rx_queue)
            xQueueSendFromISR(rx_queue, &rx, &wake);
    }
    portYIELD_FROM_ISR(wake);
}
bool bsp_can_exchange(const uint8_t *p, size_t n, uint8_t expected, uint8_t *out, size_t *out_n) {
    if (!rx_queue || !n || n > 64)
        return false;
    can_rx_t discard;
    while (xQueueReceive(rx_queue, &discard, 0) == pdTRUE) {
    }
    FDCAN_TxHeaderTypeDef t = {0};
    t.Identifier = IGW_CAN_ID;
    t.IdType = FDCAN_EXTENDED_ID;
    t.TxFrameType = FDCAN_DATA_FRAME;
    t.FDFormat = FDCAN_CLASSIC_CAN;
    t.BitRateSwitch = FDCAN_BRS_OFF;
    t.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    t.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    for (size_t offset = 0; offset < n;) {
        uint8_t k = (uint8_t)(n - offset > 8 ? 8 : n - offset);
        t.DataLength = (uint32_t)k << 16;
        if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &t, (uint8_t *)p + offset) != HAL_OK)
            return false;
        uint32_t start = HAL_GetTick();
        while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) != 4) {
            if (HAL_GetTick() - start > 5)
                return false;
        }
        offset += k;
    }
    pd42_stream_t s = {0};
    uint32_t start = HAL_GetTick();
    can_rx_t rx;
    while (HAL_GetTick() - start < 100) {
        if (xQueueReceive(rx_queue, &rx, pdMS_TO_TICKS(5)) != pdTRUE)
            continue;
        size_t count = pd42_stream_feed(&s, rx.data, rx.n, rx.tick, IGW_MOTOR_NODE, expected);
        if (count) {
            if (count > *out_n)
                return false;
            memcpy(out, s.data, count);
            *out_n = count;
            vTaskDelay(pdMS_TO_TICKS(2));
            return out[3] == 1;
        }
    }
    /* Bus-off recovery; physical disconnection remains visible in the data model. */
    FDCAN_ProtocolStatusTypeDef status;
    if (HAL_FDCAN_GetProtocolStatus(&hfdcan1, &status) == HAL_OK && status.BusOff) {
        HAL_FDCAN_Stop(&hfdcan1);
        HAL_FDCAN_Start(&hfdcan1);
    }
    return false;
}
