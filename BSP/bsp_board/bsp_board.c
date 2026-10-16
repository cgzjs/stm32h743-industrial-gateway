#include "bsp_board.h"
#include <string.h>
UART_HandleTypeDef huart1, huart3;
FDCAN_HandleTypeDef hfdcan1;
QSPI_HandleTypeDef hqspi;
SD_HandleTypeDef hsd1;
ETH_HandleTypeDef heth;
static IWDG_HandleTypeDef hiwdg;
static uint8_t pcf_shadow = 0xBF; /* Receiver on; other quasi-bidirectional inputs high. */
static void pin(GPIO_TypeDef *port, uint16_t mask, uint32_t mode, uint32_t alternate) {
    GPIO_InitTypeDef g = {0};
    g.Pin = mask;
    g.Mode = mode;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = alternate;
    HAL_GPIO_Init(port, &g);
}
void bsp_delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000u);
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}
/* Board PCF8574 is wired to PH4/PH5, not an available hardware I2C pair. */
static void scl(bool value) {
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
    bsp_delay_us(5);
}
static void sda(bool value) {
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_5, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
    bsp_delay_us(5);
}
static bool i2c_byte(uint8_t b) {
    for (unsigned i = 0; i < 8; i++) {
        sda((b & 0x80) != 0);
        scl(true);
        scl(false);
        b <<= 1;
    }
    sda(true);
    scl(true);
    bool ack = HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_5) == GPIO_PIN_RESET;
    scl(false);
    return ack;
}
bool bsp_pcf_write_bit(unsigned bit, bool value) {
    if (bit > 7)
        return false;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint8_t next = value ? (uint8_t)(pcf_shadow | (1u << bit)) : (uint8_t)(pcf_shadow & ~(1u << bit));
    sda(true);
    scl(true);
    sda(false);
    scl(false);
    bool ok = i2c_byte(0x40) && i2c_byte(next);
    sda(false);
    scl(true);
    sda(true);
    if (ok)
        pcf_shadow = next;
    __set_PRIMASK(primask);
    return ok;
}
void bsp_early_init(void) {
    /* D2 SRAM for all DMA buffers: normal noncacheable memory, not DTCM. */
    HAL_MPU_Disable();
    MPU_Region_InitTypeDef r = {0};
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER0;
    r.BaseAddress = 0x30000000;
    r.Size = MPU_REGION_SIZE_256KB;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    r.TypeExtField = MPU_TEX_LEVEL1;
    HAL_MPU_ConfigRegion(&r);
    /* FMC LCD command/data accesses must never be cached or speculated. */
    r.Number = MPU_REGION_NUMBER1;
    r.BaseAddress = 0x60000000;
    r.Size = MPU_REGION_SIZE_256MB;
    r.TypeExtField = MPU_TEX_LEVEL0;
    r.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache();
    SCB_EnableDCache();
}
static bool clock_init(void) {
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    uint32_t start = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
        if (HAL_GetTick() - start > 100)
            return false;
    }
    RCC_OscInitTypeDef o = {0};
    o.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    o.HSEState = RCC_HSE_ON;
    o.LSIState = RCC_LSI_ON;
    o.PLL.PLLState = RCC_PLL_ON;
    o.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    o.PLL.PLLM = 5;
    o.PLL.PLLN = 160;
    o.PLL.PLLP = 2;
    o.PLL.PLLQ = 8;
    o.PLL.PLLR = 2;
    o.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    o.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    if (HAL_RCC_OscConfig(&o) != HAL_OK)
        return false;
    RCC_ClkInitTypeDef c = {0};
    c.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    c.SYSCLKDivider = RCC_SYSCLK_DIV1;
    c.AHBCLKDivider = RCC_HCLK_DIV2;
    c.APB3CLKDivider = RCC_APB3_DIV2;
    c.APB1CLKDivider = RCC_APB1_DIV2;
    c.APB2CLKDivider = RCC_APB2_DIV2;
    c.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&c, FLASH_LATENCY_4) != HAL_OK)
        return false;
    RCC_PeriphCLKInitTypeDef p = {0};
    p.PeriphClockSelection = RCC_PERIPHCLK_FDCAN | RCC_PERIPHCLK_SDMMC | RCC_PERIPHCLK_QSPI;
    p.FdcanClockSelection = RCC_FDCANCLKSOURCE_PLL;
    p.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
    p.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    return HAL_RCCEx_PeriphCLKConfig(&p) == HAL_OK;
}
bool bsp_board_init(bool bootloader) {
    if (!clock_init())
        return false;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    pin(GPIOB, GPIO_PIN_1, GPIO_MODE_OUTPUT_PP, 0);
    pin(GPIOH, GPIO_PIN_4 | GPIO_PIN_5, GPIO_MODE_OUTPUT_OD, 0);
    sda(true);
    scl(true);
    bsp_pcf_write_bit(6, false);
    bool flash = (*(volatile uint16_t *)FLASHSIZE_BASE == 2048u) && bsp_nor_init();
#ifndef IGW_BOOTLOADER
    if (!bootloader) {
        bsp_uart_init();
        bsp_can_init();
    }
#else
    (void)bootloader;
#endif
    return flash;
}
void bsp_watchdog_start(void) {
    hiwdg.Instance = IWDG1;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload = 2047;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&hiwdg);
}
void bsp_watchdog_feed(void) {
    if (hiwdg.Instance)
        HAL_IWDG_Refresh(&hiwdg);
}
void bsp_led(bool on) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
void bsp_reset(void) {
    __DSB();
    NVIC_SystemReset();
}
uint32_t bsp_boot_reason(void) {
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    volatile uint32_t *p = (volatile uint32_t *)0x30040000u;
    return p[0] == 0x48373433u && p[2] == ~p[1] && p[3] == IGW_BOARD_ID && p[1] >= 1 && p[1] <= 4 ? p[1] : 0;
}
