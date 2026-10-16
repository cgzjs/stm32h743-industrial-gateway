#include "ota_manager.h"
#include "boot_policy.h"
#include "igw_crc.h"
#include <string.h>
int ota_begin(ota_manager_t *o, image_store_t *s, uint32_t session, uint32_t board, uint32_t version,
              uint32_t size, uint32_t crc) {
    if (!s->read_ok)
        return OTA_IO;
    if (!session || board != IGW_BOARD_ID || size < 8 || size > IGW_APP_CAPACITY)
        return OTA_ARGUMENT;
    if (o->receiving || !s->has_record || s->record.pending != IMAGE_NONE)
        return OTA_BUSY;
    image_header_t active;
    if (!image_validate(s, s->record.active, &active))
        return OTA_IMAGE;
    if (version <= active.version)
        return OTA_ARGUMENT;
    memset(o, 0, sizeof(*o));
    o->store = s;
    o->session = session;
    o->slot = 1u - s->record.active;
    o->size = size;
    o->version = version;
    o->crc = crc;
    o->running_crc = 0xFFFFFFFFu;
    uint32_t erase_size = (size + IGW_IMAGE_DATA_OFFSET + IGW_NOR_SECTOR - 1u) & ~(IGW_NOR_SECTOR - 1u);
    if (!s->nor->erase(image_slot_address(o->slot), erase_size))
        return OTA_IO;
    o->receiving = true;
    return OTA_OK;
}
int ota_chunk(ota_manager_t *o, uint32_t session, uint32_t offset, const uint8_t *data, size_t n) {
    if (!o->receiving || session != o->session)
        return OTA_SEQUENCE;
    if (!n || n > IGW_OTA_CHUNK || offset != o->offset || n > o->size - o->offset)
        return OTA_ARGUMENT;
    uint8_t verify[IGW_OTA_CHUNK];
    uint32_t a = image_slot_address(o->slot) + IGW_IMAGE_DATA_OFFSET + offset;
    if (!o->store->nor->program(a, data, n) || !o->store->nor->read(a, verify, n) || memcmp(data, verify, n))
        return OTA_IO;
    o->running_crc = igw_crc32_update(o->running_crc, data, n);
    o->offset += (uint32_t)n;
    return OTA_OK;
}
int ota_finish(ota_manager_t *o, uint32_t session) {
    if (!o->receiving || session != o->session || o->offset != o->size)
        return OTA_SEQUENCE;
    if ((o->running_crc ^ 0xFFFFFFFFu) != o->crc)
        return OTA_CRC;
    uint8_t v[8];
    uint32_t a = image_slot_address(o->slot) + IGW_IMAGE_DATA_OFFSET;
    if (!o->store->nor->read(a, v, 8) || !boot_vectors_valid(v, o->size))
        return OTA_IMAGE;
    if (!image_identity_check(o->store->nor, a, o->size, o->version))
        return OTA_IMAGE;
    if (!image_crc_region(o->store->nor, a, o->size, o->crc) ||
        !image_write_header(o->store, o->slot, o->version, o->size, o->crc))
        return OTA_IO;
    boot_record_t r = o->store->record;
    r.pending = o->slot;
    r.trial = 0;
    if (!image_record_save(o->store, &r)) {
        /* Commit may have reached NOR even when its first read-back failed. */
        if (!image_store_open(o->store, o->store->nor) || !o->store->read_ok ||
            o->store->record.pending != o->slot || o->store->record.trial)
            return OTA_IO;
    }
    o->receiving = false;
    return OTA_OK;
}
void ota_abort(ota_manager_t *o) {
    o->receiving = false;
}
bool ota_seed_factory(image_store_t *s, const flash_ops_t *internal, uint32_t size, uint32_t version) {
    if (!s->read_ok) {
        image_store_open(s, s->nor);
        if (!s->read_ok)
            return false;
    }
    if (s->has_record)
        return true;
    uint8_t b[1024], v[8];
    uint32_t crc = 0xFFFFFFFFu;
    if (size < 8 || size > IGW_APP_CAPACITY || !internal->read(IGW_APP_BASE, v, 8) ||
        !boot_vectors_valid(v, size))
        return false;
    if (!image_identity_check(internal, IGW_APP_BASE, size, version))
        return false;
    uint32_t n_erase = (size + IGW_IMAGE_DATA_OFFSET + IGW_NOR_SECTOR - 1u) & ~(IGW_NOR_SECTOR - 1u);
    if (!s->nor->erase(IGW_SLOT_A, n_erase))
        return false;
    for (uint32_t off = 0; off < size;) {
        size_t n = size - off > sizeof(b) ? sizeof(b) : size - off;
        if (!internal->read(IGW_APP_BASE + off, b, n) ||
            !s->nor->program(IGW_SLOT_A + IGW_IMAGE_DATA_OFFSET + off, b, n))
            return false;
        crc = igw_crc32_update(crc, b, n);
        off += (uint32_t)n;
        if (s->nor->watchdog)
            s->nor->watchdog();
    }
    crc ^= 0xFFFFFFFFu;
    if (!image_crc_region(s->nor, IGW_SLOT_A + IGW_IMAGE_DATA_OFFSET, size, crc) ||
        !image_write_header(s, 0, version, size, crc))
        return false;
    boot_record_t r = {0};
    r.active = 0;
    r.pending = IMAGE_NONE;
    if (image_record_save(s, &r))
        return true;
    return image_store_open(s, s->nor) && s->read_ok && s->record.active == 0 &&
           s->record.pending == IMAGE_NONE;
}
