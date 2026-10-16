#include "bsp_board.h"
#include "gateway_system.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <string.h>
#include <stddef.h>
static ETH_DMADescTypeDef rx_desc[4] __attribute__((section(".dma"), aligned(32)));
static ETH_DMADescTypeDef tx_desc[4] __attribute__((section(".dma"), aligned(32)));
typedef struct rx_slot {
    struct rx_slot *next;
    uint16_t length;
    bool used;
    uint8_t buffer[1536] __attribute__((aligned(32)));
} rx_slot_t;
static rx_slot_t slots[8] __attribute__((section(".dma"), aligned(32)));
static uint8_t tx_buffer[1536] __attribute__((section(".dma"), aligned(32)));
static struct netif netif;
static SemaphoreHandle_t eth_mutex;
static uint32_t phy_address = UINT32_MAX;
static bool started;
static void tcpip_alive(void *unused) {
    gateway_heartbeat(HEART_TCPIP);
}
void gateway_tcpip_probe(void) {
    (void)tcpip_try_callback(tcpip_alive, NULL);
}
void HAL_ETH_RxAllocateCallback(uint8_t **out) {
    *out = NULL;
    for (unsigned i = 0; i < 8; i++)
        if (!slots[i].used) {
            slots[i].used = true;
            slots[i].next = NULL;
            slots[i].length = 0;
            *out = slots[i].buffer;
            break;
        }
}
void HAL_ETH_RxLinkCallback(void **start, void **end, uint8_t *buffer, uint16_t n) {
    rx_slot_t *s = (rx_slot_t *)(buffer - offsetof(rx_slot_t, buffer));
    s->length = n;
    s->next = NULL;
    if (*end)
        ((rx_slot_t *)*end)->next = s;
    else
        *start = s;
    *end = s;
}
static err_t output(struct netif *iface, struct pbuf *p) {
    if (p->tot_len > sizeof(tx_buffer))
        return ERR_BUF;
    if (xSemaphoreTake(eth_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return ERR_TIMEOUT;
    err_t result = ERR_IF;
    if (started) {
        pbuf_copy_partial(p, tx_buffer, p->tot_len, 0);
        ETH_BufferTypeDef b = {.buffer = tx_buffer, .len = p->tot_len, .next = NULL};
        ETH_TxPacketConfigTypeDef t = {0};
        t.Length = p->tot_len;
        t.TxBuffer = &b;
        t.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
        t.CRCPadCtrl = ETH_CRC_PAD_INSERT;
        result = HAL_ETH_Transmit(&heth, &t, 50) == HAL_OK ? ERR_OK : ERR_IF;
    }
    xSemaphoreGive(eth_mutex);
    return result;
}
static err_t gateway_netif_init(struct netif *n) {
    n->name[0] = 'i';
    n->name[1] = 'g';
    n->mtu = 1500;
    n->hwaddr_len = 6;
    memcpy(n->hwaddr, heth.Init.MACAddr, 6);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    n->output = etharp_output;
    n->linkoutput = output;
    return ERR_OK;
}
bool bsp_eth_init(void) {
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF11_ETH;
    g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &g);
    if (!bsp_pcf_write_bit(7, false))
        return false;
    HAL_Delay(10);
    if (!bsp_pcf_write_bit(7, true))
        return false;
    HAL_Delay(50);
    memset(slots, 0, sizeof(slots));
    memset(rx_desc, 0, sizeof(rx_desc));
    memset(tx_desc, 0, sizeof(tx_desc));
    static uint8_t mac[6] = {0x02, 0x74, 0x3A, 0, 0, 1};
    uint32_t uid = HAL_GetUIDw0();
    mac[3] = (uint8_t)(uid >> 16);
    mac[4] = (uint8_t)(uid >> 8);
    mac[5] = (uint8_t)uid;
    heth.Instance = ETH;
    heth.Init.MACAddr = mac;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc = tx_desc;
    heth.Init.RxDesc = rx_desc;
    heth.Init.RxBuffLen = 1536;
    if (HAL_ETH_Init(&heth) != HAL_OK)
        return false;
    HAL_ETH_SetMDIOClockRange(&heth);
    for (unsigned a = 0; a < 32; a++) {
        uint32_t id;
        if (HAL_ETH_ReadPHYRegister(&heth, a, 2, &id) == HAL_OK && id != 0 && id != 0xFFFF) {
            phy_address = a;
            break;
        }
    }
    if (phy_address == UINT32_MAX)
        return false;
    HAL_ETH_WritePHYRegister(&heth, phy_address, 0, 0x1200); /* autonegotiation enable/restart */
    eth_mutex = xSemaphoreCreateMutex();
    if (!eth_mutex)
        return false;
    tcpip_init(NULL, NULL);
    ip4_addr_t ip, mask, gw;
    gateway_config_t config;
    gateway_get_config(&config);
    IP4_ADDR(&ip, config.ip[0], config.ip[1], config.ip[2], config.ip[3]);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, config.ip[0], config.ip[1], config.ip[2], 1);
    LOCK_TCPIP_CORE();
    bool ok = netif_add(&netif, &ip, &mask, &gw, NULL, gateway_netif_init, tcpip_input) != NULL;
    if (ok) {
        netif_set_default(&netif);
        netif_set_up(&netif);
    }
    UNLOCK_TCPIP_CORE();
    return ok;
}
void gateway_ethernet_task(void *arg) {
    bool ok = bsp_eth_init();
    gateway_eth_ready(ok);
    uint32_t check = 0;
    while (1) {
        gateway_heartbeat(HEART_ETH);
        if (ok) {
            if (HAL_GetTick() - check >= 500) {
                check = HAL_GetTick();
                uint32_t status = 0;
                xSemaphoreTake(eth_mutex, portMAX_DELAY);
                HAL_ETH_ReadPHYRegister(&heth, phy_address, 1, &status);
                HAL_ETH_ReadPHYRegister(&heth, phy_address, 1, &status);
                bool link = (status & 4) != 0;
                if (link && !started && (status & 0x20)) {
                    uint32_t special = 0;
                    HAL_ETH_ReadPHYRegister(&heth, phy_address, 31, &special);
                    ETH_MACConfigTypeDef m;
                    HAL_ETH_GetMACConfig(&heth, &m);
                    m.Speed = (special & 4) ? ETH_SPEED_10M : ETH_SPEED_100M;
                    m.DuplexMode = (special & 16) ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
                    HAL_ETH_SetMACConfig(&heth, &m);
                    started = HAL_ETH_Start(&heth) == HAL_OK;
                } else if (!link && started) {
                    HAL_ETH_Stop(&heth);
                    started = false;
                }
                bool visible = started;
                xSemaphoreGive(eth_mutex);
                LOCK_TCPIP_CORE();
                if (visible)
                    netif_set_link_up(&netif);
                else
                    netif_set_link_down(&netif);
                UNLOCK_TCPIP_CORE();
                gateway_link_update(visible);
            }
            xSemaphoreTake(eth_mutex, portMAX_DELAY);
            rx_slot_t *frame = NULL;
            if (started && HAL_ETH_ReadData(&heth, (void **)&frame) == HAL_OK && frame) {
                uint16_t total = 0;
                for (rx_slot_t *s = frame; s; s = s->next)
                    total = (uint16_t)(total + s->length);
                struct pbuf *p = pbuf_alloc(PBUF_RAW, total, PBUF_POOL);
                uint16_t off = 0;
                for (rx_slot_t *s = frame; s;) {
                    rx_slot_t *next = s->next;
                    if (p)
                        pbuf_take_at(p, s->buffer, s->length, off);
                    off = (uint16_t)(off + s->length);
                    s->used = false;
                    s = next;
                }
                xSemaphoreGive(eth_mutex);
                if (p && netif.input(p, &netif) != ERR_OK)
                    pbuf_free(p);
            } else
                xSemaphoreGive(eth_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
