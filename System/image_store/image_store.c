#include "image_store.h"
#include "igw_crc.h"
#include <string.h>
#include <stddef.h>
uint32_t image_slot_address(unsigned slot) {
    return slot == 0 ? IGW_SLOT_A : IGW_SLOT_B;
}
static bool valid_record(const boot_record_t *r) {
    return r->magic == BOOT_MAGIC && r->commit == IMAGE_COMMIT && r->active < 2 &&
           (r->pending < 2 || r->pending == IMAGE_NONE) && r->pending != r->active && r->trial <= 1 &&
           (r->pending != IMAGE_NONE || r->trial == 0) &&
           igw_crc32(r, offsetof(boot_record_t, crc)) == r->crc;
}
bool image_store_open(image_store_t *s, const flash_ops_t *nor) {
    memset(s, 0, sizeof(*s));
    s->nor = nor;
    s->record_sector = -1;
    boot_record_t a, b;
    bool ra = nor->read(IGW_META0, &a, sizeof(a)), rb = nor->read(IGW_META1, &b, sizeof(b));
    s->read_ok = ra && rb;
    bool va = ra && valid_record(&a);
    bool vb = rb && valid_record(&b);
    if (va && (!vb || (int32_t)(a.sequence - b.sequence) > 0)) {
        s->record = a;
        s->record_sector = 0;
    } else if (vb) {
        s->record = b;
        s->record_sector = 1;
    }
    s->has_record = va || vb;
    return s->has_record;
}
bool image_record_save(image_store_t *s, const boot_record_t *record) {
    if (!s->read_ok)
        return false;
    boot_record_t next = *record;
    next.magic = BOOT_MAGIC;
    next.sequence = s->has_record ? s->record.sequence + 1u : 1u;
    next.commit = IMAGE_COMMIT;
    next.crc = igw_crc32(&next, offsetof(boot_record_t, crc));
    if (!valid_record(&next))
        return false;
    int sector = s->record_sector == 0 ? 1 : 0;
    uint32_t address = sector ? IGW_META1 : IGW_META0;
    /* Never erase the newest valid journal. Commit is written separately last. */
    if (!s->nor->erase(address, IGW_NOR_SECTOR) ||
        !s->nor->program(address, &next, offsetof(boot_record_t, commit)) ||
        !s->nor->program(address + offsetof(boot_record_t, commit), &next.commit, 4))
        return false;
    boot_record_t verify;
    if (!s->nor->read(address, &verify, sizeof(verify)) || !valid_record(&verify)) {
        s->read_ok = false;
        return false;
    }
    s->record = verify;
    s->record_sector = sector;
    s->has_record = true;
    return true;
}
bool image_header_read(image_store_t *s, unsigned slot, image_header_t *h) {
    if (slot > 1 || !s->nor->read(image_slot_address(slot), h, sizeof(*h)))
        return false;
    return h->magic == IMAGE_MAGIC && h->board == IGW_BOARD_ID && h->base == IGW_APP_BASE && h->size >= 8 &&
           h->size <= IGW_APP_CAPACITY && h->commit == IMAGE_COMMIT &&
           igw_crc32(h, offsetof(image_header_t, header_crc)) == h->header_crc;
}
bool image_crc_region(const flash_ops_t *ops, uint32_t address, uint32_t size, uint32_t expected) {
    uint8_t buf[1024];
    uint32_t c = 0xFFFFFFFFu;
    while (size) {
        size_t n = size > sizeof(buf) ? sizeof(buf) : size;
        if (!ops->read(address, buf, n))
            return false;
        c = igw_crc32_update(c, buf, n);
        address += (uint32_t)n;
        size -= (uint32_t)n;
        if (ops->watchdog)
            ops->watchdog();
    }
    return (c ^ 0xFFFFFFFFu) == expected;
}
bool image_validate(image_store_t *s, unsigned slot, image_header_t *h) {
    return image_header_read(s, slot, h) &&
           image_identity_check(s->nor, image_slot_address(slot) + IGW_IMAGE_DATA_OFFSET, h->size,
                                h->version) &&
           image_crc_region(s->nor, image_slot_address(slot) + IGW_IMAGE_DATA_OFFSET, h->size, h->crc);
}
bool image_identity_check(const flash_ops_t *ops, uint32_t address, uint32_t size, uint32_t version) {
    uint8_t bytes[20];
    uint32_t words[3];
    if (size < IGW_IDENTITY_OFFSET + sizeof(bytes) ||
        !ops->read(address + IGW_IDENTITY_OFFSET, bytes, sizeof(bytes)))
        return false;
    memcpy(words, bytes + 8, sizeof(words));
    return !memcmp(bytes, "IGWID3\0", 8) && words[0] == IGW_BOARD_ID && words[1] == version &&
           words[2] == IGW_APP_BASE;
}
bool image_write_header(image_store_t *s, unsigned slot, uint32_t version, uint32_t size, uint32_t crc) {
    if (slot > 1 || size < 8 || size > IGW_APP_CAPACITY)
        return false;
    image_header_t h = {0};
    h.magic = IMAGE_MAGIC;
    h.board = IGW_BOARD_ID;
    h.version = version;
    h.size = size;
    h.crc = crc;
    h.base = IGW_APP_BASE;
    h.commit = IMAGE_COMMIT;
    h.header_crc = igw_crc32(&h, offsetof(image_header_t, header_crc));
    uint32_t address = image_slot_address(slot);
    if (!s->nor->program(address, &h, offsetof(image_header_t, commit)) ||
        !s->nor->program(address + offsetof(image_header_t, commit), &h.commit, 4))
        return false;
    image_header_t verify;
    return image_header_read(s, slot, &verify);
}
