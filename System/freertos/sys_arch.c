#include "lwip/sys.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
void sys_init(void) {}
u32_t sys_now(void) {
    return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
u32_t sys_jiffies(void) {
    return (u32_t)xTaskGetTickCount();
}
static TickType_t ticks(u32_t ms) {
    return ms ? pdMS_TO_TICKS(ms) + 1 : portMAX_DELAY;
}
err_t sys_sem_new(sys_sem_t *s, u8_t count) {
    *s = xSemaphoreCreateCounting(1, count ? 1 : 0);
    return *s ? ERR_OK : ERR_MEM;
}
void sys_sem_free(sys_sem_t *s) {
    vSemaphoreDelete(*s);
    *s = NULL;
}
void sys_sem_signal(sys_sem_t *s) {
    xSemaphoreGive(*s);
}
u32_t sys_arch_sem_wait(sys_sem_t *s, u32_t timeout) {
    u32_t t = sys_now();
    return xSemaphoreTake(*s, ticks(timeout)) == pdTRUE ? sys_now() - t : SYS_ARCH_TIMEOUT;
}
err_t sys_mutex_new(sys_mutex_t *m) {
    *m = xSemaphoreCreateMutex();
    return *m ? ERR_OK : ERR_MEM;
}
void sys_mutex_free(sys_mutex_t *m) {
    vSemaphoreDelete(*m);
    *m = NULL;
}
void sys_mutex_lock(sys_mutex_t *m) {
    xSemaphoreTake(*m, portMAX_DELAY);
}
void sys_mutex_unlock(sys_mutex_t *m) {
    xSemaphoreGive(*m);
}
err_t sys_mbox_new(sys_mbox_t *m, int n) {
    *m = xQueueCreate((UBaseType_t)n, sizeof(void *));
    return *m ? ERR_OK : ERR_MEM;
}
void sys_mbox_free(sys_mbox_t *m) {
    vQueueDelete(*m);
    *m = NULL;
}
void sys_mbox_post(sys_mbox_t *m, void *p) {
    xQueueSend(*m, &p, portMAX_DELAY);
}
err_t sys_mbox_trypost(sys_mbox_t *m, void *p) {
    return xQueueSend(*m, &p, 0) == pdTRUE ? ERR_OK : ERR_MEM;
}
err_t sys_mbox_trypost_fromisr(sys_mbox_t *m, void *p) {
    BaseType_t wake = pdFALSE;
    BaseType_t ok = xQueueSendFromISR(*m, &p, &wake);
    portYIELD_FROM_ISR(wake);
    return ok == pdTRUE ? ERR_OK : ERR_MEM;
}
u32_t sys_arch_mbox_fetch(sys_mbox_t *m, void **p, u32_t timeout) {
    void *dummy;
    u32_t t = sys_now();
    return xQueueReceive(*m, p ? p : &dummy, ticks(timeout)) == pdTRUE ? sys_now() - t : SYS_ARCH_TIMEOUT;
}
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *m, void **p) {
    void *dummy;
    return xQueueReceive(*m, p ? p : &dummy, 0) == pdTRUE ? 0 : SYS_MBOX_EMPTY;
}
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn fn, void *arg, int stack, int priority) {
    TaskHandle_t t = NULL;
    if (xTaskCreate(fn, name, (uint16_t)((unsigned)stack / sizeof(StackType_t)), arg, (UBaseType_t)priority,
                    &t) != pdPASS)
        return NULL;
    return t;
}
sys_prot_t sys_arch_protect(void) {
    taskENTER_CRITICAL();
    return 0;
}
void sys_arch_unprotect(sys_prot_t p) {
    (void)p;
    taskEXIT_CRITICAL();
}
