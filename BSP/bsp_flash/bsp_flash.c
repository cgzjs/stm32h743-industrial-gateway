#include "bsp_board.h"
#include <string.h>
static void (*yield_fn)(void);
void bsp_nor_set_yield(void (*fn)(void)) {
    yield_fn = fn;
}
static void flash_yield(void) {
    if (yield_fn)
        yield_fn();
    else
        bsp_watchdog_feed();
}
static bool command(uint8_t opcode, uint32_t address, uint32_t address_mode, uint32_t n) {
    QSPI_CommandTypeDef c = {0};
    c.Instruction = opcode;
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Address = address;
    c.AddressMode = address_mode;
    c.AddressSize = QSPI_ADDRESS_32_BITS;
    c.DataMode = n ? QSPI_DATA_1_LINE : QSPI_DATA_NONE;
    c.NbData = n;
    c.DdrMode = QSPI_DDR_MODE_DISABLE;
    c.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    c.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return HAL_QSPI_Command(&hqspi, &c, 100) == HAL_OK;
}
static bool ready(uint32_t timeout) {
    uint32_t start = HAL_GetTick();
    uint8_t status;
    do {
        if (!command(0x05, 0, QSPI_ADDRESS_NONE, 1) || HAL_QSPI_Receive(&hqspi, &status, 100) != HAL_OK)
            return false;
        if (!(status & 1))
            return true;
        flash_yield();
    } while ((uint32_t)(HAL_GetTick() - start) < timeout);
    return false;
}
static bool write_enable(void) {
    return ready(1000) && command(0x06, 0, QSPI_ADDRESS_NONE, 0);
}
static bool nor_read(uint32_t a, void *p, size_t n) {
    if (a > IGW_NOR_CAPACITY || n > IGW_NOR_CAPACITY - a)
        return false;
    return !n || (ready(1000) && command(0x13, a, QSPI_ADDRESS_1_LINE, (uint32_t)n) &&
                  HAL_QSPI_Receive(&hqspi, p, 5000) == HAL_OK);
}
static bool nor_erase(uint32_t a, size_t n) {
    if (a % 4096 || n % 4096 || a > IGW_NOR_CAPACITY || n > IGW_NOR_CAPACITY - a)
        return false;
    while (n) {
        if (!write_enable() || !command(0x21, a, QSPI_ADDRESS_1_LINE, 0) || !ready(3000))
            return false;
        a += 4096;
        n -= 4096;
        flash_yield();
    }
    return true;
}
static bool nor_program(uint32_t a, const void *src, size_t n) {
    if (a > IGW_NOR_CAPACITY || n > IGW_NOR_CAPACITY - a)
        return false;
    const uint8_t *p = src;
    while (n) {
        size_t k = 256 - (a & 255u);
        if (k > n)
            k = n;
        if (!write_enable() || !command(0x12, a, QSPI_ADDRESS_1_LINE, (uint32_t)k) ||
            HAL_QSPI_Transmit(&hqspi, (uint8_t *)p, 1000) != HAL_OK || !ready(1000))
            return false;
        a += (uint32_t)k;
        p += k;
        n -= k;
        flash_yield();
    }
    return true;
}
bool bsp_nor_init(void) {
    __HAL_RCC_QSPI_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    g.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &g);
    g.Pin = GPIO_PIN_2;
    g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_6;
    g.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOB, &g);
    hqspi.Instance = QUADSPI;
    hqspi.Init.ClockPrescaler = 7;
    hqspi.Init.FifoThreshold = 4;
    hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    hqspi.Init.FlashSize = 24;
    hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_4_CYCLE;
    hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
    hqspi.Init.FlashID = QSPI_FLASH_ID_1;
    hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    if (HAL_QSPI_Init(&hqspi) != HAL_OK)
        return false;
    QSPI_CommandTypeDef exit_qpi = {0};
    exit_qpi.Instruction = 0xFF;
    exit_qpi.InstructionMode = QSPI_INSTRUCTION_4_LINES;
    exit_qpi.AddressMode = QSPI_ADDRESS_NONE;
    exit_qpi.DataMode = QSPI_DATA_NONE;
    exit_qpi.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    HAL_QSPI_Command(&hqspi, &exit_qpi, 100);
    command(0xAB, 0, QSPI_ADDRESS_NONE, 0);
    bsp_delay_us(10);
    /* W25Q256 dedicated 4-byte opcodes avoid persistent address-mode state. */
    command(0x66, 0, QSPI_ADDRESS_NONE, 0);
    command(0x99, 0, QSPI_ADDRESS_NONE, 0);
    HAL_Delay(1);
    uint8_t id[3];
    return command(0x9F, 0, QSPI_ADDRESS_NONE, 3) && HAL_QSPI_Receive(&hqspi, id, 100) == HAL_OK &&
           id[0] == 0xEF && id[2] == 0x19;
}
static bool internal_read(uint32_t a, void *p, size_t n) {
    if (a < IGW_APP_BASE || a > IGW_APP_BASE + IGW_APP_CAPACITY || n > IGW_APP_BASE + IGW_APP_CAPACITY - a)
        return false;
    memcpy(p, (const void *)a, n);
    return true;
}
static bool internal_erase(uint32_t a, size_t n) {
    if (a != IGW_APP_BASE || n != IGW_APP_CAPACITY)
        return false;
    HAL_FLASH_Unlock();
    bool ok = true;
    for (unsigned bank = 0; bank < 2 && ok; bank++) {
        for (unsigned sector = bank ? 0 : 1; sector < 8 && ok; sector++) {
            FLASH_EraseInitTypeDef e = {0};
            e.TypeErase = FLASH_TYPEERASE_SECTORS;
            e.Banks = bank ? FLASH_BANK_2 : FLASH_BANK_1;
            e.Sector = sector;
            e.NbSectors = 1;
            e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
            uint32_t error = 0;
            ok = HAL_FLASHEx_Erase(&e, &error) == HAL_OK;
            bsp_watchdog_feed();
        }
    }
    HAL_FLASH_Lock();
    SCB_InvalidateICache();
    return ok;
}
static bool internal_program(uint32_t a, const void *src, size_t n) {
    if (a < IGW_APP_BASE || a % 32 || a > IGW_APP_BASE + IGW_APP_CAPACITY ||
        n > IGW_APP_BASE + IGW_APP_CAPACITY - a)
        return false;
    const uint8_t *p = src;
    bool ok = true;
    HAL_FLASH_Unlock();
    while (n && ok) {
        uint8_t aligned[32] __attribute__((aligned(32)));
        memset(aligned, 0xFF, 32);
        size_t k = n > 32 ? 32 : n;
        memcpy(aligned, p, k);
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, a, (uint32_t)aligned) == HAL_OK;
        /* Invalidate potentially cached erased Flash before read-back. */
        SCB_InvalidateDCache_by_Addr((uint32_t *)a, 32);
        a += 32;
        p += k;
        n -= k;
    }
    HAL_FLASH_Lock();
    return ok;
}
const flash_ops_t bsp_nor_ops = {nor_read, nor_erase, nor_program, flash_yield};
const flash_ops_t bsp_internal_ops = {internal_read, internal_erase, internal_program, flash_yield};
