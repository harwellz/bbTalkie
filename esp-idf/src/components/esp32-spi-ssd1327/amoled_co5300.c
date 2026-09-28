/**
 * @brief CO5300 AMOLED backend for the SSD1327 framebuffer API (Waveshare ESP32-S3-Touch-AMOLED-1.8)
 *
 * The 128x128 4bpp grayscale framebuffer is kept as is. Refreshing a region converts it to RGB565,
 * scales it 2x and draws it centered on the 368x448 panel.
 *
 * Panel bring-up follows the Waveshare BSP waveshare/esp32_s3_touch_amoled_1_8 v2.0.3 (bsp_display_new,
 * lcd_init_cmds, BSP_LCD_CST816S_X_GAP) and the espressif/esp_lcd_co5300 driver:
 *   https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/bsp/esp32_s3_touch_amoled_1_8
 *   https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_co5300
 * - QSPI window coordinates must start on an even pixel and end on an odd one (BSP LVGL rounder);
 *   2x scaling with even offsets always satisfies this.
 * - RGB565 is sent high byte first (BSP sets swap_bytes for LVGL).
 * - The CO5300 + CST816-compatible touch board revision needs an X gap of 16 (also DISPLAY_OFFSET_X 16
 *   in xiaozhi-esp32 boards/waveshare/esp32-s3-touch-amoled-1.8-v2).
 * - esp_lcd_panel_io_tx_color() is queued; the color buffer is reused only after on_color_trans_done.
 */

#include "sdkconfig.h"

#if CONFIG_ESP32_S3_WAVESHARE_AMOLED_1_8

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "bsp_board.h"
#include "amoled_co5300.h"

#define AMOLED_SCALE            2
#define AMOLED_X_OFFSET         ((BOARD_LCD_H_RES - SSD1327_WIDTH * AMOLED_SCALE) / 2)
#define AMOLED_Y_OFFSET         ((BOARD_LCD_V_RES - SSD1327_HEIGHT * AMOLED_SCALE) / 2)
#define AMOLED_X_GAP            16
#define AMOLED_STRIP_LINES      16  /* panel lines per transfer */
#define AMOLED_STRIP_PIXELS     (BOARD_LCD_H_RES * AMOLED_STRIP_LINES)

static const char *TAG = "amoled";

static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t trans_done = NULL;
static uint16_t *strip_buf = NULL;
static uint16_t gray_lut[16];

static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    xSemaphoreGiveFromISR(trans_done, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void draw_and_wait(int x_start, int y_start, int x_end, int y_end)
{
    if (esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, strip_buf) == ESP_OK) {
        xSemaphoreTake(trans_done, portMAX_DELAY);
    }
}

static void build_gray_lut(void)
{
    for (int gs = 0; gs < 16; gs++) {
        uint16_t r = gs * 31 / 15;
        uint16_t g = gs * 63 / 15;
        uint16_t b = gs * 31 / 15;
        uint16_t rgb565 = (r << 11) | (g << 5) | b;
        gray_lut[gs] = (rgb565 >> 8) | (rgb565 << 8); /* high byte first on the wire */
    }
}

static void amoled_fill_black(void)
{
    memset(strip_buf, 0, AMOLED_STRIP_PIXELS * sizeof(uint16_t));
    for (int y = 0; y < BOARD_LCD_V_RES; y += AMOLED_STRIP_LINES) {
        int lines = (BOARD_LCD_V_RES - y < AMOLED_STRIP_LINES) ? BOARD_LCD_V_RES - y : AMOLED_STRIP_LINES;
        draw_and_wait(0, y, BOARD_LCD_H_RES, y + lines);
    }
}

void amoled_co5300_init(void)
{
    build_gray_lut();

    trans_done = xSemaphoreCreateBinary();
    strip_buf = heap_caps_malloc(AMOLED_STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(trans_done && strip_buf);

    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(BOARD_LCD_PCLK,
                                                                 BOARD_LCD_DATA0,
                                                                 BOARD_LCD_DATA1,
                                                                 BOARD_LCD_DATA2,
                                                                 BOARD_LCD_DATA3,
                                                                 AMOLED_STRIP_PIXELS * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(BOARD_LCD_CS, on_color_trans_done, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_config, &io_handle));

    co5300_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io_handle, &panel_config, &panel_handle));
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, AMOLED_X_GAP, 0));

    amoled_fill_black();
    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_LOGI(TAG, "CO5300 ready, %dx%d framebuffer at (%d,%d) x%d",
             SSD1327_WIDTH, SSD1327_HEIGHT, AMOLED_X_OFFSET, AMOLED_Y_OFFSET, AMOLED_SCALE);
}

void amoled_co5300_deinit(void)
{
    if (panel_handle) {
        esp_lcd_panel_disp_on_off(panel_handle, false);
    }
}

void amoled_co5300_flush(struct spi_ssd1327 *spi_ssd1327, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    if (!panel_handle || !strip_buf) {
        return;
    }

    const int src_lines_per_strip = AMOLED_STRIP_LINES / AMOLED_SCALE;
    const int dst_width = width * AMOLED_SCALE;

    for (int row = y; row < y + height; row += src_lines_per_strip) {
        int src_lines = (y + height - row < src_lines_per_strip) ? y + height - row : src_lines_per_strip;
        uint16_t *p = strip_buf;

        for (int r = row; r < row + src_lines; r++) {
            uint16_t *line = p;
            for (int c = x; c < x + width; c++) {
                uint16_t px = gray_lut[spi_oled_get_pixel(spi_ssd1327, c, r)];
                *p++ = px;
                *p++ = px;
            }
            /* duplicate the line for vertical scaling */
            memcpy(p, line, dst_width * sizeof(uint16_t));
            p += dst_width;
        }

        draw_and_wait(AMOLED_X_OFFSET + x * AMOLED_SCALE,
                      AMOLED_Y_OFFSET + row * AMOLED_SCALE,
                      AMOLED_X_OFFSET + (x + width) * AMOLED_SCALE,
                      AMOLED_Y_OFFSET + (row + src_lines) * AMOLED_SCALE);
    }
}

#endif /* CONFIG_ESP32_S3_WAVESHARE_AMOLED_1_8 */
