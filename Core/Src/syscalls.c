#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>
#include "stm32h7xx_hal.h"
#ifndef IGW_BOOTLOADER
#include "bsp_board.h"
#include "FreeRTOS.h"
#include "task.h"
#endif
extern char _end, _estack;
static uintptr_t heap;
void *_sbrk(ptrdiff_t n) {
    if (!heap)
        heap = (uintptr_t)&_end;
    uintptr_t old = heap, limit = (uintptr_t)&_estack - 16384u;
    if (n < 0 || heap > limit || (uintptr_t)n > limit - heap) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap += (uintptr_t)n;
    return (void *)old;
}
#ifndef IGW_BOOTLOADER
/* Newlib reentrancy does not serialize its shared allocator by itself. */
struct _reent;
void __malloc_lock(struct _reent *r) {
    (void)r;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        vTaskSuspendAll();
}
void __malloc_unlock(struct _reent *r) {
    (void)r;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xTaskResumeAll();
}
#endif
int _write(int fd, char *p, int n) {
#ifndef IGW_BOOTLOADER
    if (huart1.Instance)
        HAL_UART_Transmit(&huart1, (uint8_t *)p, (uint16_t)n, 100);
#endif
    return n;
}
int _read(int fd, char *p, int n) {
    return 0;
}
int _close(int fd) {
    return -1;
}
int _fstat(int fd, struct stat *s) {
    s->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int fd) {
    return 1;
}
int _lseek(int fd, int offset, int whence) {
    return 0;
}
int _getpid(void) {
    return 1;
}
int _kill(int pid, int sig) {
    errno = EINVAL;
    return -1;
}
void _exit(int status) {
    while (1) {
    }
}
