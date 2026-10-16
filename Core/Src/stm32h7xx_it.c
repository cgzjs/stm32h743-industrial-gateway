#include "stm32h7xx_hal.h"
#ifndef IGW_BOOTLOADER
#include "FreeRTOS.h"
#include "task.h"
extern void xPortSysTickHandler(void);
#endif
void SysTick_Handler(void) {
    HAL_IncTick();
#ifndef IGW_BOOTLOADER
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
#endif
}
void NMI_Handler(void) {
    while (1) {
    }
}
void HardFault_Handler(void) {
    while (1) {
    }
}
void MemManage_Handler(void) {
    while (1) {
    }
}
void BusFault_Handler(void) {
    while (1) {
    }
}
void UsageFault_Handler(void) {
    while (1) {
    }
}
#ifdef IGW_BOOTLOADER
void SVC_Handler(void) {}
void PendSV_Handler(void) {}
#endif
void DebugMon_Handler(void) {}
