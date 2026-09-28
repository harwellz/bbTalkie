/**
 * @brief Waveshare ESP32-S3-Touch-AMOLED-1.8 power / battery support (AXP2101 PMU)
 *
 * Register usage follows:
 * - XPowersLib AXP2101 (used by Waveshare example 90_axp2101_pmu)
 *   https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8/tree/main/examples/esp-idf/90_axp2101_pmu
 *     IC_TYPE 0x03 = 0x4A, STATUS1 0x00 bit3 battery present, STATUS2 0x01 bits[2:0] charger state
 *     (4 = charge done), bits[7:5] current direction (1 = charging), COMMON_CONFIG 0x10 bit0 shutdown,
 *     PWRON_STATUS 0x20 bit2 VBUS insert as power-on source, ADC_CHANNEL_CTRL 0x30 bit0 battery voltage
 *     measure / bit1 TS pin measure (disabled on boards without NTC), ADC_DATA 0x34[4:0]/0x35 battery mV,
 *     LDO_ONOFF_CTRL0 0x90 bit0 ALDO1
 * - Waveshare official firmware system_status.cpp (0x00/0x01 decode)
 * - xiaozhi-esp32 boards/waveshare/esp32-s3-touch-amoled-1.8-v2 (ALDO1 powers the mic, 0x10 bit0 power off)
 *
 * The AXP2101 IRQ line is not connected to the ESP32-S3 (CONFIG_PMU_INTERRUPT_PIN=-1 in the Waveshare example),
 * so the device is turned off through the PMU and turned back on with the PWR key.
 */

#include "bsp_board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2101_ADDR                0x34
#define AXP2101_CHIP_ID             0x4A

#define AXP2101_REG_STATUS1         0x00
#define AXP2101_REG_STATUS2         0x01
#define AXP2101_REG_IC_TYPE         0x03
#define AXP2101_REG_COMMON_CONFIG   0x10
#define AXP2101_REG_PWRON_STATUS    0x20
#define AXP2101_REG_ADC_CHANNEL     0x30
#define AXP2101_REG_ADC_VBAT_H      0x34
#define AXP2101_REG_ADC_VBAT_L      0x35
#define AXP2101_REG_LDO_ONOFF0      0x90

#define AXP2101_CHG_DONE_STATE      4
#define AXP2101_DIR_CHARGING        1

#define AXP2101_TIMEOUT_MS          100

static const char *TAG = "power";
static i2c_master_dev_handle_t pmu_dev = NULL;

static esp_err_t pmu_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(pmu_dev, &reg, 1, val, 1, AXP2101_TIMEOUT_MS);
}

static esp_err_t pmu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(pmu_dev, buf, sizeof(buf), AXP2101_TIMEOUT_MS);
}

static esp_err_t pmu_update_bits(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t cur = 0;
    esp_err_t ret = pmu_read(reg, &cur);
    if (ret != ESP_OK) {
        return ret;
    }
    return pmu_write(reg, (cur & ~mask) | (val & mask));
}

esp_err_t bsp_power_init(void)
{
    i2c_master_bus_handle_t bus = bsp_board_get_i2c_bus();
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized, call bsp_board_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &pmu_dev));

    uint8_t chip_id = 0;
    ESP_ERROR_CHECK(pmu_read(AXP2101_REG_IC_TYPE, &chip_id));
    if (chip_id != AXP2101_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected PMU chip id 0x%02X", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    // Enable battery voltage measurement, disable TS pin measurement (no NTC on this board)
    ESP_ERROR_CHECK(pmu_update_bits(AXP2101_REG_ADC_CHANNEL, 0x03, 0x01));
    // ALDO1 powers the microphone
    ESP_ERROR_CHECK(pmu_update_bits(AXP2101_REG_LDO_ONOFF0, 0x01, 0x01));

    // BOOT button, active low
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    return ESP_OK;
}

bool bsp_power_woke_by_charger(void)
{
    // PWRON_STATUS keeps the last PMU power-on source, so only trust it on a real power-on reset
    if (esp_reset_reason() != ESP_RST_POWERON) {
        return false;
    }
    uint8_t pwron = 0;
    if (pmu_read(AXP2101_REG_PWRON_STATUS, &pwron) != ESP_OK) {
        return false;
    }
    return (pwron & (1 << 2)) != 0;
}

bsp_charge_state_t bsp_power_get_charge_state(void)
{
    uint8_t status2 = 0;
    if (pmu_read(AXP2101_REG_STATUS2, &status2) != ESP_OK) {
        return BSP_CHARGE_STATE_NONE;
    }
    if ((status2 & 0x07) == AXP2101_CHG_DONE_STATE) {
        return BSP_CHARGE_STATE_FULL;
    }
    if (((status2 >> 5) & 0x07) == AXP2101_DIR_CHARGING) {
        return BSP_CHARGE_STATE_CHARGING;
    }
    return BSP_CHARGE_STATE_NONE;
}

int bsp_power_get_battery_mv(void)
{
    uint8_t status1 = 0, h = 0, l = 0;
    if (pmu_read(AXP2101_REG_STATUS1, &status1) != ESP_OK || !(status1 & (1 << 3))) {
        return -1;
    }
    if (pmu_read(AXP2101_REG_ADC_VBAT_H, &h) != ESP_OK || pmu_read(AXP2101_REG_ADC_VBAT_L, &l) != ESP_OK) {
        return -1;
    }
    return ((h & 0x1F) << 8) | l;
}

void bsp_power_off(void)
{
    ESP_LOGI(TAG, "PMU power off");
    pmu_update_bits(AXP2101_REG_COMMON_CONFIG, 0x01, 0x01);
    // Should not get here; keep the CPU idle if the PMU did not cut power
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
