#ifndef IGW_SYS_ARCH_H
#define IGW_SYS_ARCH_H
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t sys_mbox_t;
typedef TaskHandle_t sys_thread_t;
typedef UBaseType_t sys_prot_t;
#define SYS_SEM_NULL NULL
#define SYS_MBOX_NULL NULL
#define sys_sem_valid(x) (*(x) != NULL)
#define sys_sem_set_invalid(x) (*(x) = NULL)
#define sys_mbox_valid(x) (*(x) != NULL)
#define sys_mbox_set_invalid(x) (*(x) = NULL)
#define sys_mutex_valid(x) (*(x) != NULL)
#define sys_mutex_set_invalid(x) (*(x) = NULL)
#endif
