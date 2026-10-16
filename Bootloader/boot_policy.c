#include "boot_policy.h"
#include <string.h>
static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
bool boot_vectors_valid(const uint8_t v[8], uint32_t size) {
    uint32_t sp = le32(v), pc = le32(v + 4);
    /* Stack may point just beyond the last RAM word; reset must be Thumb. */
    bool ram = (sp > 0x20000000u && sp <= 0x20020000u) || (sp > 0x24000000u && sp <= 0x24080000u) ||
               (sp > 0x30000000u && sp <= 0x30048000u) || (sp > 0x38000000u && sp <= 0x38010000u);
    return ram && (sp & 7u) == 0 && (pc & 1u) && (pc & ~1u) >= IGW_APP_BASE &&
           (pc & ~1u) < IGW_APP_BASE + size;
}
static bool install(image_store_t *s, const flash_ops_t *internal, unsigned slot, image_header_t *h) {
    uint8_t buf[1024], v[8];
    if (!image_validate(s, slot, h) ||
        !s->nor->read(image_slot_address(slot) + IGW_IMAGE_DATA_OFFSET, v, 8) ||
        !boot_vectors_valid(v, h->size))
        return false;
    if (image_crc_region(internal, IGW_APP_BASE, h->size, h->crc))
        return true;
    /* Whole install is restartable. Metadata does not advance until read-back passes. */
    if (!internal->erase(IGW_APP_BASE, IGW_APP_CAPACITY))
        return false;
    for (uint32_t off = 0; off < h->size;) {
        size_t n = h->size - off > sizeof(buf) ? sizeof(buf) : h->size - off;
        if (!s->nor->read(image_slot_address(slot) + IGW_IMAGE_DATA_OFFSET + off, buf, n) ||
            !internal->program(IGW_APP_BASE + off, buf, n))
            return false;
        off += (uint32_t)n;
        if (internal->watchdog)
            internal->watchdog();
    }
    return image_crc_region(internal, IGW_APP_BASE, h->size, h->crc);
}
boot_result_t boot_select_install(image_store_t *s, const flash_ops_t *internal) {
    if (!s->read_ok)
        return BOOT_RECOVERY;
    uint8_t v[8];
    image_header_t h;
    boot_record_t r = s->record;
    if (!s->has_record) {
        uint32_t version = 0;
        return internal->read(IGW_APP_BASE, v, 8) && boot_vectors_valid(v, IGW_APP_CAPACITY) &&
                       internal->read(IGW_APP_BASE + IGW_IDENTITY_OFFSET + 12, &version, 4) &&
                       image_identity_check(internal, IGW_APP_BASE, IGW_APP_CAPACITY, version)
                   ? BOOT_FACTORY
                   : BOOT_RECOVERY;
    }
    if (r.pending != IMAGE_NONE) {
        if (!r.trial && install(s, internal, r.pending, &h)) {
            r.trial = 1;
            if (!image_record_save(s, &r))
                return BOOT_RECOVERY;
            return BOOT_TRIAL;
        }
        /* Invalid candidate or a reset before confirmation: restore confirmed slot. */
        if (!install(s, internal, r.active, &h))
            return BOOT_RECOVERY;
        r.pending = IMAGE_NONE;
        r.trial = 0;
        return image_record_save(s, &r) ? BOOT_ROLLBACK : BOOT_RECOVERY;
    }
    return install(s, internal, r.active, &h) ? BOOT_CONFIRMED : BOOT_RECOVERY;
}
bool boot_confirm(image_store_t *s) {
    if (!s->has_record || !s->read_ok)
        return false;
    if (s->record.pending == IMAGE_NONE)
        return true;
    if (!s->record.trial)
        return false;
    boot_record_t r = s->record;
    r.active = r.pending;
    r.pending = IMAGE_NONE;
    r.trial = 0;
    if (image_record_save(s, &r))
        return true;
    return image_store_open(s, s->nor) && s->read_ok && s->record.active == r.active &&
           s->record.pending == IMAGE_NONE;
}
