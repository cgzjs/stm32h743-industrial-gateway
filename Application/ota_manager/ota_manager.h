#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H
#include "image_store.h"
typedef struct {
    image_store_t *store;
    uint32_t session, offset, size, crc, version, running_crc;
    unsigned slot;
    bool receiving;
} ota_manager_t;
enum { OTA_OK = 0, OTA_ARGUMENT = 1, OTA_BUSY = 2, OTA_IO = 3, OTA_SEQUENCE = 4, OTA_CRC = 5, OTA_IMAGE = 6 };
int ota_begin(ota_manager_t *o, image_store_t *s, uint32_t session, uint32_t board, uint32_t version,
              uint32_t size, uint32_t crc);
int ota_chunk(ota_manager_t *o, uint32_t session, uint32_t offset, const uint8_t *data, size_t n);
int ota_finish(ota_manager_t *o, uint32_t session);
void ota_abort(ota_manager_t *o);
bool ota_seed_factory(image_store_t *s, const flash_ops_t *internal, uint32_t size, uint32_t version);
#endif
