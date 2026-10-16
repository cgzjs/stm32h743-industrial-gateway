#include "bsp_board.h"
static SRAM_HandleTypeDef lcd_sram;
static uint16_t detected_id;
uint16_t bsp_lcd_id(void) {
    return detected_id;
}
static volatile uint16_t *const lcd_cmd = (volatile uint16_t *)0x6007FFFEu;
static volatile uint16_t *const lcd_data = (volatile uint16_t *)0x60080000u;
static void reg(uint16_t v) {
    *lcd_cmd = v;
    __DSB();
}
static void dat(uint16_t v) {
    *lcd_data = v;
    __DSB();
}
static void init_command(uint8_t c, const uint8_t *data, unsigned n) {
    reg(c);
    while (n--)
        dat(*data++);
}
bool bsp_lcd_init(void) {
    __HAL_RCC_FMC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF12_FMC;
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 |
            GPIO_PIN_10 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
            GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &g);
    g.Pin = GPIO_PIN_5;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Alternate = 0;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    lcd_sram.Instance = FMC_NORSRAM_DEVICE;
    lcd_sram.Extended = FMC_NORSRAM_EXTENDED_DEVICE;
    lcd_sram.Init.NSBank = FMC_NORSRAM_BANK1;
    lcd_sram.Init.DataAddressMux = FMC_DATA_ADDRESS_MUX_DISABLE;
    lcd_sram.Init.MemoryType = FMC_MEMORY_TYPE_SRAM;
    lcd_sram.Init.MemoryDataWidth = FMC_NORSRAM_MEM_BUS_WIDTH_16;
    lcd_sram.Init.BurstAccessMode = FMC_BURST_ACCESS_MODE_DISABLE;
    lcd_sram.Init.WaitSignalPolarity = FMC_WAIT_SIGNAL_POLARITY_LOW;
    lcd_sram.Init.WaitSignalActive = FMC_WAIT_TIMING_BEFORE_WS;
    lcd_sram.Init.WriteOperation = FMC_WRITE_OPERATION_ENABLE;
    lcd_sram.Init.WaitSignal = FMC_WAIT_SIGNAL_DISABLE;
    lcd_sram.Init.ExtendedMode = FMC_EXTENDED_MODE_ENABLE;
    lcd_sram.Init.AsynchronousWait = FMC_ASYNCHRONOUS_WAIT_DISABLE;
    lcd_sram.Init.WriteBurst = FMC_WRITE_BURST_DISABLE;
    lcd_sram.Init.ContinuousClock = FMC_CONTINUOUS_CLOCK_SYNC_ASYNC;
    lcd_sram.Init.WriteFifo = FMC_WRITE_FIFO_DISABLE;
    FMC_NORSRAM_TimingTypeDef read = {.AddressSetupTime = 15,
                                      .DataSetupTime = 85,
                                      .BusTurnAroundDuration = 2,
                                      .AccessMode = FMC_ACCESS_MODE_A};
    FMC_NORSRAM_TimingTypeDef write = {.AddressSetupTime = 8,
                                       .DataSetupTime = 12,
                                       .BusTurnAroundDuration = 2,
                                       .AccessMode = FMC_ACCESS_MODE_A};
    if (HAL_SRAM_Init(&lcd_sram, &read, &write) != HAL_OK)
        return false;
    HAL_Delay(50);
    reg(0xD3);
    (void)*lcd_data;
    (void)*lcd_data;
    uint16_t id = (uint16_t)(*lcd_data << 8);
    id |= *lcd_data;
    detected_id = id;
    if (id != 0x9341)
        return false; /* Do not send an unknown panel a guessed init sequence. */
    reg(0x01);
    HAL_Delay(10);
    const uint8_t power1[] = {0x23}, power2[] = {0x10}, vcom[] = {0x3E, 0x28}, pix[] = {0x55},
                  orient[] = {0x28}, frame[] = {0, 0x18};
    init_command(0xC0, power1, 1);
    init_command(0xC1, power2, 1);
    init_command(0xC5, vcom, 2);
    init_command(0x3A, pix, 1);
    init_command(0x36, orient, 1);
    init_command(0xB1, frame, 2);
    reg(0x11);
    HAL_Delay(120);
    reg(0x29);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    return true;
}
void bsp_lcd_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *p) {
    reg(0x2A);
    dat(x1 >> 8);
    dat(x1 & 255);
    dat(x2 >> 8);
    dat(x2 & 255);
    reg(0x2B);
    dat(y1 >> 8);
    dat(y1 & 255);
    dat(y2 >> 8);
    dat(y2 & 255);
    reg(0x2C);
    uint32_t n = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);
    while (n--)
        *lcd_data = *p++;
    __DSB();
}
static void touch_pin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value) {
    HAL_GPIO_WritePin(port, pin, value);
    bsp_delay_us(2);
}
static uint16_t touch_adc(uint8_t command) {
    touch_pin(GPIOH, GPIO_PIN_6, GPIO_PIN_RESET);
    for (unsigned i = 0; i < 8; i++) {
        touch_pin(GPIOI, GPIO_PIN_3, (command & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        command <<= 1;
        touch_pin(GPIOH, GPIO_PIN_6, GPIO_PIN_SET);
        touch_pin(GPIOH, GPIO_PIN_6, GPIO_PIN_RESET);
    }
    bsp_delay_us(6);
    uint16_t v = 0;
    for (unsigned i = 0; i < 16; i++) {
        touch_pin(GPIOH, GPIO_PIN_6, GPIO_PIN_SET);
        v = (uint16_t)((v << 1) | (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_3) == GPIO_PIN_SET));
        touch_pin(GPIOH, GPIO_PIN_6, GPIO_PIN_RESET);
    }
    return (v >> 3) & 4095u;
}
bool bsp_touch_read(uint16_t *x, uint16_t *y) {
    static bool initialized;
    if (!initialized) {
        GPIO_InitTypeDef g = {0};
        g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Pin = GPIO_PIN_6;
        HAL_GPIO_Init(GPIOH, &g);
        g.Pin = GPIO_PIN_3 | GPIO_PIN_8;
        HAL_GPIO_Init(GPIOI, &g);
        g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLUP;
        g.Pin = GPIO_PIN_7;
        HAL_GPIO_Init(GPIOH, &g);
        g.Pin = GPIO_PIN_3;
        HAL_GPIO_Init(GPIOG, &g);
        HAL_GPIO_WritePin(GPIOI, GPIO_PIN_8, GPIO_PIN_SET);
        initialized = true;
    }
    if (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_7) != GPIO_PIN_RESET)
        return false;
    touch_pin(GPIOI, GPIO_PIN_8, GPIO_PIN_RESET);
    uint32_t sx = 0, sy = 0;
    for (unsigned i = 0; i < 5; i++) {
        sx += touch_adc(0xD0);
        sy += touch_adc(0x90);
    }
    touch_pin(GPIOI, GPIO_PIN_8, GPIO_PIN_SET);
    *x = (uint16_t)(sx / 5);
    *y = (uint16_t)(sy / 5);
    return HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_7) == GPIO_PIN_RESET;
}
