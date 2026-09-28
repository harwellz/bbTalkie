#pragma once

#include "esp32-spi-ssd1327.h"

void amoled_co5300_init(void);
void amoled_co5300_deinit(void);
void amoled_co5300_flush(struct spi_ssd1327 *spi_ssd1327, uint8_t x, uint8_t y, uint8_t width, uint8_t height);
