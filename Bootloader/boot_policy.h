#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H
#include "image_store.h"
typedef enum {
    BOOT_RECOVERY = 0,
    BOOT_FACTORY = 1,
    BOOT_CONFIRMED = 2,
    BOOT_TRIAL = 3,
    BOOT_ROLLBACK = 4
} boot_result_t;
bool boot_vectors_valid(const uint8_t vectors[8], uint32_t size);
boot_result_t boot_select_install(image_store_t *store, const flash_ops_t *internal);
bool boot_confirm(image_store_t *store);
#endif
