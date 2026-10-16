#include "igw_config.h"
typedef struct {
    char magic[8];
    uint32_t board, version, base;
} identity_t;
_Static_assert(sizeof(identity_t) == 20, "binary identity layout");
__attribute__((used, section(".image_identity")))
const identity_t gateway_identity = {"IGWID3", IGW_BOARD_ID, IGW_FW_VERSION, IGW_APP_BASE};
