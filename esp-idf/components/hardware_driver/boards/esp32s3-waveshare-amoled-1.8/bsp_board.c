/**
 * @brief Waveshare ESP32-S3-Touch-AMOLED-1.8 audio board support (ES8311 codec)
 *
 * References:
 * - Waveshare BSP waveshare/esp32_s3_touch_amoled_1_8 v2.0.3 (bsp_audio_init, bsp_audio_codec_*_init, bsp_sdcard_mount)
 *   https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/bsp/esp32_s3_touch_amoled_1_8
 * - esp_codec_dev ES8311 driver: with no_dac_ref = false and 2-channel recording, the left channel is the
 *   ADC (mic) and the right channel is the DAC output (reg 0x44 = 0x58)
 *   https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev
 * - xiaozhi-esp32 Es8311AudioCodec (duplex I2S master, MCLK = 256 * fs, one IN_OUT codec device)
 *   https://github.com/78/xiaozhi-esp32/blob/main/main/audio/codecs/es8311_audio_codec.cc
 */

#include <stdlib.h>
#include <string.h>
#include "bsp_board.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define BOARD_I2C_PORT          (I2C_NUM_0)
#define BOARD_I2S_PORT          (I2S_NUM_0)
#define BOARD_SAMPLE_RATE       (16000)
#define BOARD_I2S_CHANNELS      (2)     /* left: mic, right: ES8311 DAC reference */
#define BOARD_MIC_GAIN_DB       (30.0)
#define BOARD_DEFAULT_VOLUME    (90)

static const char *TAG = "board";

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static esp_codec_dev_handle_t codec_dev = NULL;
static sdmmc_card_t *card = NULL;
static int play_volume = BOARD_DEFAULT_VOLUME;

i2c_master_bus_handle_t bsp_board_get_i2c_bus(void)
{
    return i2c_bus;
}

static esp_err_t bsp_i2c_init(void)
{
    if (i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = GPIO_I2C_SDA,
        .scl_io_num = GPIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    return i2c_new_master_bus(&bus_cfg, &i2c_bus);
}

static esp_err_t bsp_i2s_init(uint32_t sample_rate)
{
    esp_err_t ret_val = ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ret_val |= i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_I2S_MCLK,
            .bclk = GPIO_I2S_SCLK,
            .ws   = GPIO_I2S_LRCK,
            .dout = GPIO_I2S_DOUT,
            .din  = GPIO_I2S_SDIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ret_val |= i2s_channel_init_std_mode(tx_handle, &std_cfg);
    ret_val |= i2s_channel_init_std_mode(rx_handle, &std_cfg);
    ret_val |= i2s_channel_enable(tx_handle);
    ret_val |= i2s_channel_enable(rx_handle);

    return ret_val;
}

static esp_err_t bsp_codec_init(uint32_t sample_rate)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BOARD_I2S_PORT,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if == NULL) {
        return ESP_FAIL;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == NULL) {
        return ESP_FAIL;
    }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL) {
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = GPIO_POWER_AMP,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .no_dac_ref = false, /* right channel carries the DAC output as AEC reference */
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    if (codec_if == NULL) {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    codec_dev = esp_codec_dev_new(&dev_cfg);
    if (codec_dev == NULL) {
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = BOARD_I2S_CHANNELS,
        .channel_mask = 0,
        .sample_rate = sample_rate,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(codec_dev, &fs) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    esp_codec_dev_set_in_gain(codec_dev, BOARD_MIC_GAIN_DB);
    esp_codec_dev_set_out_vol(codec_dev, play_volume);

    return ESP_OK;
}

esp_err_t bsp_board_init(uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    ESP_LOGI(TAG, "Waveshare ESP32-S3-Touch-AMOLED-1.8 audio init");

    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_i2s_init(BOARD_SAMPLE_RATE));
    ESP_ERROR_CHECK(bsp_codec_init(BOARD_SAMPLE_RATE));

    return ESP_OK;
}

esp_err_t bsp_get_feed_data(bool is_get_raw_channel, int16_t *buffer, int buffer_len)
{
    /* Interleaved 16-bit stereo: [mic, ref, mic, ref, ...], matches input format "MR" */
    int ret = esp_codec_dev_read(codec_dev, buffer, buffer_len);
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_play(const int16_t *data, int length, TickType_t ticks_to_wait)
{
    /* ES8311 has a mono DAC; send the same sample on both slots of the stereo I2S frame */
    int16_t *stereo_buffer = malloc(length * BOARD_I2S_CHANNELS * sizeof(int16_t));
    if (stereo_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < length; i++) {
        stereo_buffer[i * 2] = data[i];
        stereo_buffer[i * 2 + 1] = data[i];
    }

    int ret = esp_codec_dev_write(codec_dev, stereo_buffer, length * BOARD_I2S_CHANNELS * sizeof(int16_t));
    free(stereo_buffer);

    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

int bsp_get_feed_channel(void)
{
    return BOARD_I2S_CHANNELS;
}

char *bsp_get_input_format(void)
{
    return "MR";
}

esp_err_t bsp_audio_set_play_vol(int volume)
{
    play_volume = volume;
    if (codec_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_vol(codec_dev, volume) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_get_play_vol(int *volume)
{
    if (volume == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *volume = play_volume;
    return ESP_OK;
}

esp_err_t bsp_sdcard_init(char *mount_point, size_t max_files)
{
    if (NULL != card) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = max_files,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = GPIO_SDMMC_CLK;
    slot_config.cmd = GPIO_SDMMC_CMD;
    slot_config.d0 = GPIO_SDMMC_D0;
    slot_config.cd = SDMMC_SLOT_NO_CD;
    slot_config.wp = SDMMC_SLOT_NO_WP;

    esp_err_t ret_val = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret_val));
        return ret_val;
    }

    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

esp_err_t bsp_sdcard_deinit(char *mount_point)
{
    if (NULL == mount_point) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret_val = esp_vfs_fat_sdcard_unmount(mount_point, card);
    card = NULL;

    return ret_val;
}
