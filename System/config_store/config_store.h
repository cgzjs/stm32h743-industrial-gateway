#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H
#include "image_store.h"
typedef struct {
    uint8_t ip[4];
    uint16_t sensor_address, sensor_function, sensor_register, temp_index, hum_index, multiplier;
    uint32_t sensor_baud;
    int32_t touch[6]; /* affine transform Q16.16 */
    uint32_t touch_calibrated;
} gateway_config_t;
#define CONFIG_WIRE_SIZE 48u
_Static_assert(sizeof(gateway_config_t) == 48, "persistent configuration layout");
void config_defaults(gateway_config_t *c);
bool config_load(const flash_ops_t *ops, gateway_config_t *c);
bool config_save(const flash_ops_t *ops, const gateway_config_t *c);
bool config_valid(const gateway_config_t *c);
/* bit 15 selects division; low 15 bits hold divisor. Otherwise multiply. */
int32_t config_scale(uint16_t encoding, int32_t raw);
void config_encode(uint8_t *wire, const gateway_config_t *c);
bool config_decode(gateway_config_t *c, const uint8_t *wire, size_t n);
#endif
