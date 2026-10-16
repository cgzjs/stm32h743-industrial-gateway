#include "bsp_board.h"
#include "gateway_system.h"
int main(void) {
    SCB->VTOR = IGW_APP_BASE;
    __DSB();
    __ISB();
    bsp_early_init();
    HAL_Init();
    bool flash_ok = bsp_board_init(false);
#ifdef IGW_TEST_BOOT_FAIL
    /* Bootloader IWDG remains active; this test image cannot confirm startup. */
    while (1) {
        __NOP();
    }
#endif
    gateway_start(flash_ok);
    while (1) {
    }
}
