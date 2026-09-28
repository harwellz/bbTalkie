/**
 * @brief Waveshare ESP32-S3-Touch-AMOLED-1.8 (CO5300 display variant) pin definitions
 *
 * Source: Waveshare BSP `waveshare/esp32_s3_touch_amoled_1_8` v2.0.3,
 *         include/bsp/esp32_s3_touch_amoled_1_8.h and include/bsp/display.h
 *         https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/bsp/esp32_s3_touch_amoled_1_8
 */
#pragma once

#include "driver/gpio.h"
#include "esp_idf_version.h"
#include "driver/i2c_master.h"

/* Shared I2C bus: ES8311, AXP2101, TCA9554, touch, RTC, IMU */
#define GPIO_I2C_SCL            (GPIO_NUM_14)
#define GPIO_I2C_SDA            (GPIO_NUM_15)

/* I2S to ES8311 codec */
#define GPIO_I2S_MCLK           (GPIO_NUM_16)
#define GPIO_I2S_SCLK           (GPIO_NUM_9)
#define GPIO_I2S_LRCK           (GPIO_NUM_45)
#define GPIO_I2S_DOUT           (GPIO_NUM_8)
#define GPIO_I2S_SDIN           (GPIO_NUM_10)
#define GPIO_POWER_AMP          (GPIO_NUM_46)

/* CO5300 AMOLED on QSPI (no reset / backlight GPIO) */
#define BOARD_LCD_SPI_HOST      (SPI2_HOST)
#define BOARD_LCD_CS            (GPIO_NUM_12)
#define BOARD_LCD_PCLK          (GPIO_NUM_11)
#define BOARD_LCD_DATA0         (GPIO_NUM_4)
#define BOARD_LCD_DATA1         (GPIO_NUM_5)
#define BOARD_LCD_DATA2         (GPIO_NUM_6)
#define BOARD_LCD_DATA3         (GPIO_NUM_7)
#define BOARD_LCD_RST           (GPIO_NUM_NC)
#define BOARD_LCD_H_RES         (368)
#define BOARD_LCD_V_RES         (448)

/* Touch interrupt (reset is not a direct GPIO) */
#define BOARD_TOUCH_INT         (GPIO_NUM_21)

/* BOOT button, active low (xiaozhi-esp32 boards/waveshare/esp32-s3-touch-amoled-1.8-v2/config.h).
 * The PWR key is wired to the AXP2101, not to a GPIO. */
#define BOARD_BUTTON_GPIO       (GPIO_NUM_0)

/* No WS2812 on this board (GPIO15 is I2C SDA) */
#define BOARD_HAS_WS2812        (0)

/* microSD, SDMMC 1-bit */
#define GPIO_SDMMC_CLK          (GPIO_NUM_2)
#define GPIO_SDMMC_CMD          (GPIO_NUM_1)
#define GPIO_SDMMC_D0           (GPIO_NUM_3)

/**
 * @brief Shared I2C master bus, created by bsp_board_init()
 */
i2c_master_bus_handle_t bsp_board_get_i2c_bus(void);
