#ifndef IGW_LWIP_CC_H
#define IGW_LWIP_CC_H
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x)                                                                                \
    do {                                                                                                     \
        printf x;                                                                                            \
    } while (0)
#define LWIP_PLATFORM_ASSERT(x)                                                                              \
    do {                                                                                                     \
        igw_assert_fail((x), 0);                                                                             \
    } while (0)
void igw_assert_fail(const char *, int);
#define LWIP_RAND() ((uint32_t)rand())
#define LWIP_TIMEVAL_PRIVATE 0
#include <sys/time.h>
#endif
