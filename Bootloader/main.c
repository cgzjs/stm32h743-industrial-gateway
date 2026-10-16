#include "bsp_board.h"
#include "boot_policy.h"
volatile uint32_t boot_recovery_request;
/* A debugger stops here before IWDG starts; normal reset leaves the request zero. */
__attribute__((noinline)) bool boot_factory_gate(void) {
    if (boot_recovery_request != 0x52454333u)
        return true;
    boot_recovery_request = 0;
    return bsp_nor_ops.erase(IGW_META0, IGW_NOR_SECTOR) && bsp_nor_ops.erase(IGW_META1, IGW_NOR_SECTOR);
}
static void jump_to_app(boot_result_t reason) __attribute__((noreturn));
__attribute__((naked, noreturn)) static void jump_vectors(uint32_t stack, uint32_t reset) {
    __asm volatile("msr msp, r0\n movs r2, #0\n msr control, r2\n isb\n cpsie i\n bx r1");
}
static void jump_to_app(boot_result_t reason) {
    uint32_t sp = *(uint32_t *)IGW_APP_BASE, pc = *(uint32_t *)(IGW_APP_BASE + 4);
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (unsigned i = 0; i < 8; i++) {
        NVIC->ICER[i] = UINT32_MAX;
        NVIC->ICPR[i] = UINT32_MAX;
    }
    HAL_QSPI_DeInit(&hqspi);
    HAL_RCC_DeInit();
    HAL_DeInit();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    volatile uint32_t *handoff = (volatile uint32_t *)0x30040000u;
    handoff[0] = 0x48373433u;
    handoff[1] = (uint32_t)reason;
    handoff[2] = ~(uint32_t)reason;
    handoff[3] = IGW_BOARD_ID;
    SCB_CleanDCache_by_Addr((uint32_t *)handoff, 32);
    SCB_DisableDCache();
    SCB_DisableICache();
    HAL_MPU_Disable();
    SCB->VTOR = IGW_APP_BASE;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    __DSB();
    __ISB();
    jump_vectors(sp, pc);
}
int main(void) {
    bsp_early_init();
    HAL_Init();
    bool flash_ok = bsp_board_init(true);
    if (flash_ok)
        flash_ok = boot_factory_gate();
    bsp_watchdog_start();
    image_store_t s;
    image_store_open(&s, &bsp_nor_ops);
    boot_result_t result = flash_ok ? boot_select_install(&s, &bsp_internal_ops) : BOOT_RECOVERY;
    if (result != BOOT_RECOVERY)
        jump_to_app(result);
    /* Keep SWD available if both images are invalid. No invalid vector is executed. */
    while (1) {
        bsp_led(true);
        HAL_Delay(150);
        bsp_led(false);
        HAL_Delay(150);
        bsp_watchdog_feed();
    }
}
