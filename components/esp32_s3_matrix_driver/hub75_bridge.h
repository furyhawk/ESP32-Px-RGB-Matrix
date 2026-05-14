#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool hub75_bridge_init(void);
void hub75_bridge_draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *buffer, bool big_endian);
void hub75_bridge_flip(void);
void hub75_bridge_set_brightness(uint8_t brightness);
void hub75_bridge_deinit(void);

#ifdef __cplusplus
}
#endif
