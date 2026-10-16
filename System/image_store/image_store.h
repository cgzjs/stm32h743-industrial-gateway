#ifndef IMAGE_STORE_H
#define IMAGE_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "igw_config.h"
typedef struct {
    bool (*read)(uint32_t address, void *out, size_t n);
    bool (*erase)(uint32_t address, size_t n);
    bool (*program)(uint32_t address, const void *data, size_t n);
    void (*watchdog)(void);
} flash_ops_t;
typedef struct {
    uint32_t magic, board, version, size, crc, base, reserved[8], header_crc, commit;
} image_header_t;
typedef struct {
    uint32_t magic, sequence, active, pending, trial, reserved[9], crc, commit;
} boot_record_t;
typedef struct {
    const flash_ops_t *nor;
    boot_record_t record;
    int record_sector;
    bool has_record;
    bool read_ok;
} image_store_t;
enum {
    IMAGE_NONE = 0xFFFFFFFFu,
    IMAGE_MAGIC = 0x494D4733u,
    BOOT_MAGIC = 0x424F5433u,
    IMAGE_COMMIT = 0xC03F17EDu
};
_Static_assert(sizeof(image_header_t) == 64, "on-flash image header layout");
_Static_assert(sizeof(boot_record_t) == 64, "on-flash boot journal layout");
bool image_store_open(image_store_t *s, const flash_ops_t *nor);
uint32_t image_slot_address(unsigned slot);
bool image_header_read(image_store_t *s, unsigned slot, image_header_t *h);
bool image_validate(image_store_t *s, unsigned slot, image_header_t *h);
bool image_write_header(image_store_t *s, unsigned slot, uint32_t version, uint32_t size, uint32_t crc);
bool image_record_save(image_store_t *s, const boot_record_t *record);
bool image_crc_region(const flash_ops_t *ops, uint32_t address, uint32_t size, uint32_t expected);
bool image_identity_check(const flash_ops_t *ops, uint32_t address, uint32_t size, uint32_t version);
#endif
