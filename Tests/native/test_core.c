/* Runs the production C modules. A cut interrupts a physical mutation via longjmp. */
#include "ota_manager.h"
#include "boot_policy.h"
#include "config_store.h"
#include "modbus_tcp.h"
#include "modbus_rtu.h"
#include "pd42.h"
#include "fw_protocol.h"
#include "igw_crc.h"
#include "igw_bytes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define NOR_SIZE (IGW_SLOT_B + IGW_SLOT_SPAN)
#define IMAGE_SIZE 4096u
static unsigned checks, cuts, events, cut_at;
static bool fail_metadata, fail_commit_read, read_fail_once;
static jmp_buf power_cut;
static uint8_t nor[NOR_SIZE], flash[IGW_APP_CAPACITY], snapshot[NOR_SIZE], flash_snapshot[IGW_APP_CAPACITY];
static uint8_t old_image[IMAGE_SIZE], new_image[IMAGE_SIZE];
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        checks++;                                                                                            \
        if (!(c)) {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s (cut %u)\n", __LINE__, #c, cut_at);                            \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
static void event(void) {
    events++;
    if (cut_at && events == cut_at)
        longjmp(power_cut, 1);
}
static bool read_nor(uint32_t a, void *p, size_t n) {
    if ((a == IGW_META0 || a == IGW_META1) && n == sizeof(boot_record_t)) {
        if (fail_metadata)
            return false;
        if (read_fail_once) {
            read_fail_once = false;
            return false;
        }
    }
    if (a > NOR_SIZE || n > NOR_SIZE - a)
        return false;
    memcpy(p, nor + a, n);
    return true;
}
static bool erase_region(uint8_t *base, uint32_t a, size_t n, size_t sector) {
    while (n) {
        size_t k = n < sector ? n : sector;
        memset(base + a, 0xFF, k / 2);
        event();
        memset(base + a + k / 2, 0xFF, k - k / 2);
        event();
        a += (uint32_t)k;
        n -= k;
    }
    return true;
}
static bool program_region(uint8_t *base, uint32_t a, const void *p, size_t n) {
    const uint8_t *data = p;
    while (n) {
        size_t k = n < 32 ? n : 32;
        for (size_t i = 0; i < k; i++)
            if ((base[a + i] & data[i]) != data[i])
                return false;
        for (size_t i = 0; i < k / 2; i++)
            base[a + i] &= data[i];
        event();
        for (size_t i = k / 2; i < k; i++)
            base[a + i] &= data[i];
        event();
        a += (uint32_t)k;
        data += k;
        n -= k;
    }
    return true;
}
static bool erase_nor(uint32_t a, size_t n) {
    return a <= NOR_SIZE && n <= NOR_SIZE - a && a % 4096 == 0 && n % 4096 == 0 &&
           erase_region(nor, a, n, 4096);
}
static bool program_nor(uint32_t a, const void *p, size_t n) {
    bool ok = a <= NOR_SIZE && n <= NOR_SIZE - a && program_region(nor, a, p, n);
    if (ok && fail_commit_read && (a == IGW_META0 + 60 || a == IGW_META1 + 60) && n == 4) {
        fail_commit_read = false;
        read_fail_once = true;
    }
    return ok;
}
static bool read_flash(uint32_t a, void *p, size_t n) {
    if (a < IGW_APP_BASE || a > IGW_APP_BASE + IGW_APP_CAPACITY || n > IGW_APP_BASE + IGW_APP_CAPACITY - a)
        return false;
    memcpy(p, flash + a - IGW_APP_BASE, n);
    return true;
}
static bool erase_flash(uint32_t a, size_t n) {
    return a == IGW_APP_BASE && n == IGW_APP_CAPACITY && erase_region(flash, 0, n, 128 * 1024);
}
static bool program_flash(uint32_t a, const void *p, size_t n) {
    return a >= IGW_APP_BASE && a <= IGW_APP_BASE + IGW_APP_CAPACITY &&
           n <= IGW_APP_BASE + IGW_APP_CAPACITY - a && program_region(flash, a - IGW_APP_BASE, p, n);
}
static const flash_ops_t nor_ops = {read_nor, erase_nor, program_nor, NULL},
                         flash_ops = {read_flash, erase_flash, program_flash, NULL};
static void image_make(uint8_t *p, unsigned salt) {
    for (unsigned i = 0; i < IMAGE_SIZE; i++)
        p[i] = (uint8_t)(i * 37 + salt);
    uint32_t sp = 0x24080000, pc = IGW_APP_BASE + 0x101;
    memcpy(p, &sp, 4);
    memcpy(p + 4, &pc, 4);
    uint32_t identity[3] = {IGW_BOARD_ID, IGW_FW_VERSION + salt - 1, IGW_APP_BASE};
    memcpy(p + IGW_IDENTITY_OFFSET, "IGWID3\0", 8);
    memcpy(p + IGW_IDENTITY_OFFSET + 8, identity, sizeof(identity));
}
static void reset_factory(void) {
    fail_metadata = fail_commit_read = read_fail_once = false;
    cut_at = 0;
    events = 0;
    memset(nor, 0xFF, sizeof(nor));
    memset(flash, 0xFF, sizeof(flash));
    memcpy(flash, old_image, IMAGE_SIZE);
}
static void seed(void) {
    image_store_t s;
    image_store_open(&s, &nor_ops);
    CHECK(ota_seed_factory(&s, &flash_ops, IMAGE_SIZE, IGW_FW_VERSION));
}
static void download(void) {
    image_store_t s;
    image_store_open(&s, &nor_ops);
    ota_manager_t o = {0};
    CHECK(ota_begin(&o, &s, 123, IGW_BOARD_ID, IGW_FW_VERSION + 1, IMAGE_SIZE,
                    igw_crc32(new_image, IMAGE_SIZE)) == OTA_OK);
    for (unsigned a = 0; a < IMAGE_SIZE; a += 1024)
        CHECK(ota_chunk(&o, 123, a, new_image + a, 1024) == OTA_OK);
    CHECK(ota_finish(&o, 123) == OTA_OK);
}
static void save_snapshot(void) {
    memcpy(snapshot, nor, sizeof(nor));
    memcpy(flash_snapshot, flash, sizeof(flash));
}
static void restore_snapshot(void) {
    memcpy(nor, snapshot, sizeof(nor));
    memcpy(flash, flash_snapshot, sizeof(flash));
    events = 0;
}
static void boot_once(void) {
    image_store_t s;
    image_store_open(&s, &nor_ops);
    CHECK(boot_select_install(&s, &flash_ops) != BOOT_RECOVERY);
}
static void confirm(void) {
    image_store_t s;
    CHECK(image_store_open(&s, &nor_ops));
    CHECK(boot_confirm(&s));
}
static gateway_config_t config_new;
static void save_config(void) {
    CHECK(config_save(&nor_ops, &config_new));
}
enum scenario { SEED, DOWNLOAD, INSTALL, ROLLBACK, CONFIRM, CONFIG };
static void operation(enum scenario scenario) {
    switch (scenario) {
    case SEED:
        seed();
        break;
    case DOWNLOAD:
        download();
        break;
    case INSTALL:
    case ROLLBACK:
        boot_once();
        break;
    case CONFIRM:
        confirm();
        break;
    case CONFIG:
        save_config();
        break;
    }
}
static void cut_scenario(enum scenario scenario, const char *label) {
    reset_factory();
    gateway_config_t config_old;
    config_defaults(&config_old);
    config_new = config_old;
    config_new.ip[3] = 51;
    if (scenario != SEED)
        seed();
    if (scenario == INSTALL || scenario == ROLLBACK || scenario == CONFIRM) {
        download();
        if (scenario != INSTALL)
            boot_once();
    }
    if (scenario == CONFIG)
        CHECK(config_save(&nor_ops, &config_old));
    save_snapshot();
    events = 0;
    operation(scenario);
    unsigned total = events;
    for (unsigned cut = 1; cut <= total + 1; cut++) {
        restore_snapshot();
        cut_at = cut;
        if (setjmp(power_cut) == 0)
            operation(scenario);
        cut_at = 0;
        cuts++;
        if (scenario == CONFIG) {
            gateway_config_t c;
            CHECK(config_load(&nor_ops, &c));
            CHECK(!memcmp(&c, &config_old, sizeof(c)) || !memcmp(&c, &config_new, sizeof(c)));
            continue;
        }
        image_store_t s;
        image_store_open(&s, &nor_ops);
        boot_result_t result = boot_select_install(&s, &flash_ops);
        CHECK(result != BOOT_RECOVERY);
        if (scenario == CONFIRM) {
            CHECK(!memcmp(flash, s.record.active == 1 ? new_image : old_image, IMAGE_SIZE));
        } else if (result == BOOT_TRIAL) {
            CHECK(!memcmp(flash, new_image, IMAGE_SIZE));
            image_store_open(&s, &nor_ops);
            CHECK(boot_select_install(&s, &flash_ops) == BOOT_ROLLBACK);
            CHECK(!memcmp(flash, old_image, IMAGE_SIZE));
        } else
            CHECK(!memcmp(flash, old_image, IMAGE_SIZE));
        if (scenario == SEED) {
            seed();
            image_store_open(&s, &nor_ops);
            CHECK(s.has_record);
        }
    }
    printf("PASS %-20s %u interrupted mutations\n", label, total + 1);
}
static motor_command_t command;
static bool queue(const motor_command_t *c, void *ctx) {
    command = *c;
    return true;
}
static void protocols(void) {
    CHECK(igw_crc32("123456789", 9) == 0xCBF43926);
    CHECK(igw_crc16("123456789", 9) == 0x4B37);
    uint8_t buf[1100], out[260];
    fw_header_t h = {.operation = FW_DATA, .session = 123, .offset = 10, .length = 3}, decoded;
    CHECK(fw_encode(buf, &h, (uint8_t *)"abc") == 27);
    CHECK(!fw_decode_header(buf, &decoded));
    CHECK(!fw_verify(buf, buf + 24, &decoded));
    buf[25] ^= 1;
    CHECK(fw_verify(buf, buf + 24, &decoded));
    igw_put32(buf + 16, 1025);
    CHECK(fw_decode_header(buf, &decoded));
    static const uint8_t expected[] = {0xC5, 1, 0xF1, 0, 0x64, 0x44, 0x16, 0, 0, 0x75, 0x5C};
    CHECK(pd42_velocity(buf, 1, 0, 100, 600) == sizeof(expected));
    CHECK(!memcmp(buf, expected, sizeof(expected)));
    CHECK(pd42_velocity(buf, 1, 2, 100, 100) == 0);
    uint8_t feedback[] = {0xC5, 1, 0x29, 1, 2, 0x58, 0x4A, 0x5C}; /* 600 rpm */
    CHECK(pd42_validate(feedback, 8, 1, 0x29));
    pd42_stream_t stream = {0};
    CHECK(!pd42_stream_feed(&stream, feedback, 4, 1, 1, 0x29));
    CHECK(pd42_stream_feed(&stream, feedback + 4, 4, 2, 1, 0x29) == 8);
    CHECK(!pd42_stream_feed(&stream, feedback, 4, 3, 1, 0x29));
    CHECK(!pd42_stream_feed(&stream, feedback + 4, 4, 10, 1, 0x29));
    feedback[6] ^= 1;
    CHECK(!pd42_validate(feedback, 8, 1, 0x29));
    uint8_t rtu[] = {1, 3, 4, 0xFF, 0xCE, 2, 0x58, 0, 0};
    uint16_t crc = igw_crc16(rtu, 7);
    rtu[7] = (uint8_t)crc;
    rtu[8] = (uint8_t)(crc >> 8);
    uint16_t v[2];
    CHECK(!rtu_read_response(rtu, 9, 1, 3, v, 2));
    CHECK((int16_t)v[0] == -50 && v[1] == 600);
    rtu[8] ^= 1;
    CHECK(rtu_read_response(rtu, 9, 1, 3, v, 2) != 0);
    gateway_data_t d = {.temperature = -50, .humidity = 600, .can_valid = true, .network_link = true};
    uint8_t read[] = {0, 7, 0, 0, 0, 6, 1, 3, 0, 0, 0, 2};
    CHECK(modbus_tcp_process(read, 12, out, &d, 100, queue, NULL) == 13);
    CHECK(igw_be16(out + 9) == (uint16_t)-50);
    read[9] = 4;
    CHECK(modbus_tcp_process(read, 12, out, &d, 100, queue, NULL) == 9 && out[8] == 2);
    uint8_t write[] = {0, 8, 0, 0, 0, 13, 1, 16, 0, 0x20, 0, 3, 6, 0, 100, 0, 1, 0, 0};
    CHECK(modbus_tcp_process(write, sizeof(write), out, &d, 100, queue, NULL) == 12);
    CHECK(command.rpm == 100 && command.enabled == 1 && command.type == MOTOR_VELOCITY);
    write[13] = 3;
    CHECK(modbus_tcp_process(write, sizeof(write), out, &d, 100, queue, NULL) == 9 && out[8] == 3);
    write[13] = 0;
    d.ota_busy = true;
    CHECK(modbus_tcp_process(write, sizeof(write), out, &d, 100, queue, NULL) == 9);
    write[16] = 0;
    CHECK(modbus_tcp_process(write, sizeof(write), out, &d, 100, queue, NULL) == 12 &&
          command.type == MOTOR_DISABLE);
    d.motor_enabled = true;
    d.ota_busy = false;
    d.control_source = 0;
    motor_command_t hb = {.type = MOTOR_HEARTBEAT, .source = 1};
    CHECK(!motor_command_allowed(&d, &hb));
    hb.source = 0;
    CHECK(motor_command_allowed(&d, &hb));
    gateway_config_t c, copy;
    config_defaults(&c);
    config_encode(buf, &c);
    CHECK(config_decode(&copy, buf, 48));
    CHECK(!memcmp(&c, &copy, sizeof(c)));
    CHECK(config_scale(10, -25) == -250);
    CHECK(config_scale(0x800A, -255) == -26);
    CHECK(config_scale(0x800A, 255) == 26);
    CHECK(config_scale(0x800A, 254) == 25);
    c.multiplier = 0x800A;
    CHECK(config_valid(&c));
    c.multiplier = 0x8000;
    CHECK(!config_valid(&c));
    buf[4] = 0xFF;
    CHECK(!config_decode(&copy, buf, 48));
    reset_factory();
    seed();
    image_store_t s;
    image_store_open(&s, &nor_ops);
    ota_manager_t o = {0};
    CHECK(ota_begin(&o, &s, 1, 0, 4, IMAGE_SIZE, 0) == OTA_ARGUMENT);
    CHECK(ota_begin(&o, &s, 1, IGW_BOARD_ID, IGW_FW_VERSION, IMAGE_SIZE, 0) == OTA_ARGUMENT);
    CHECK(ota_begin(&o, &s, 1, IGW_BOARD_ID, IGW_FW_VERSION + 1, IMAGE_SIZE, 0) == OTA_OK);
    CHECK(ota_chunk(&o, 2, 0, new_image, 1024) == OTA_SEQUENCE);
    CHECK(ota_chunk(&o, 1, 1, new_image, 1024) == OTA_ARGUMENT);
    for (unsigned i = 0; i < IMAGE_SIZE; i += 1024)
        CHECK(!ota_chunk(&o, 1, i, new_image + i, 1024));
    CHECK(ota_finish(&o, 1) == OTA_CRC);
    CHECK(s.record.pending == IMAGE_NONE);
    ota_abort(&o);
    download();
    CHECK(image_store_open(&s, &nor_ops));
    CHECK(boot_select_install(&s, &flash_ops) == BOOT_TRIAL);
    CHECK(boot_confirm(&s));
    CHECK(boot_select_install(&s, &flash_ops) == BOOT_CONFIRMED);
    CHECK(!memcmp(flash, new_image, IMAGE_SIZE));
    nor[IGW_SLOT_B + IGW_IMAGE_DATA_OFFSET + 100] ^= 1;
    image_header_t image;
    CHECK(!image_validate(&s, 1, &image));
    CHECK(boot_select_install(&s, &flash_ops) == BOOT_RECOVERY);
    printf("PASS protocol bounds, CRC, interlocks and successful confirmation\n");
}
static void metadata_io(void) {
    reset_factory();
    seed();
    image_store_t s;
    CHECK(image_store_open(&s, &nor_ops));
    ota_manager_t o = {0};
    CHECK(ota_begin(&o, &s, 321, IGW_BOARD_ID, IGW_FW_VERSION + 1, IMAGE_SIZE,
                    igw_crc32(new_image, IMAGE_SIZE)) == OTA_OK);
    for (unsigned off = 0; off < IMAGE_SIZE; off += 1024)
        CHECK(ota_chunk(&o, 321, off, new_image + off, 1024) == OTA_OK);
    fail_commit_read = true;
    CHECK(ota_finish(&o, 321) == OTA_OK);
    CHECK(s.read_ok && s.record.pending == 1);
    fail_metadata = true;
    CHECK(!image_store_open(&s, &nor_ops) && !s.read_ok);
    CHECK(boot_select_install(&s, &flash_ops) == BOOT_RECOVERY);
    CHECK(!ota_seed_factory(&s, &flash_ops, IMAGE_SIZE, IGW_FW_VERSION));
    CHECK(ota_begin(&o, &s, 322, IGW_BOARD_ID, IGW_FW_VERSION + 2, IMAGE_SIZE, 0) == OTA_IO);
    CHECK(!memcmp(nor + IGW_SLOT_A + IGW_IMAGE_DATA_OFFSET, old_image, IMAGE_SIZE));
    fail_metadata = false;
    CHECK(image_store_open(&s, &nor_ops) && s.record.pending == 1);
    CHECK(boot_select_install(&s, &flash_ops) == BOOT_TRIAL);
    fail_commit_read = true;
    CHECK(boot_confirm(&s));
    CHECK(s.read_ok && s.record.active == 1 && s.record.pending == IMAGE_NONE);
    printf("PASS metadata I/O failure, ambiguous commit reconciliation and scale conversion\n");
}
int main(void) {
    image_make(old_image, 1);
    image_make(new_image, 2);
    protocols();
    metadata_io();
    cut_scenario(SEED, "factory seed");
    cut_scenario(DOWNLOAD, "download/commit");
    cut_scenario(INSTALL, "install/trial");
    cut_scenario(ROLLBACK, "failed boot rollback");
    cut_scenario(CONFIRM, "boot confirmation");
    cut_scenario(CONFIG, "parameter journal");
    printf("RESULT checks=%u power_cuts=%u\n", checks, cuts);
    return 0;
}
