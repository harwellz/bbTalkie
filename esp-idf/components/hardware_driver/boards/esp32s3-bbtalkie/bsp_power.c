/**
 * @brief bbTalkie power / battery support (see PCB/SCH_bbTalkie.pdf)
 *
 * Moved from main.c without behaviour changes.
 */

#include "bsp_board.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

#define WAKEUP_PIN_MASK ((1ULL << BOARD_CHARGER_CHRG_GPIO) | (1ULL << BOARD_BUTTON_GPIO) | (1ULL << BOARD_CHARGER_STDBY_GPIO))

esp_err_t bsp_power_init(void)
{
    // Configure output GPIOs first
    gpio_config_t io_conf_out = {
        .pin_bit_mask = (1ULL << BOARD_AMP_EN_GPIO) | (1ULL << BOARD_PERIPH_PWR_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_out);

    gpio_set_level(BOARD_AMP_EN_GPIO, 1);
    gpio_set_level(BOARD_PERIPH_PWR_EN_GPIO, 1);

    // Configure wake up GPIOs (charger CHRG / STDBY and button)
    gpio_config_t io_conf = {
        .pin_bit_mask = WAKEUP_PIN_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    rtc_gpio_pullup_en(BOARD_CHARGER_CHRG_GPIO);
    rtc_gpio_pullup_en(BOARD_BUTTON_GPIO);
    rtc_gpio_pullup_en(BOARD_CHARGER_STDBY_GPIO);

    rtc_gpio_pulldown_dis(BOARD_CHARGER_CHRG_GPIO);
    rtc_gpio_pulldown_dis(BOARD_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BOARD_CHARGER_STDBY_GPIO);

    esp_sleep_enable_ext1_wakeup(WAKEUP_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW);

    // Battery monitoring: 0-2.2V range (4.2V max / 2 by resistors)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BOARD_BATTERY_ADC1_CHANNEL, ADC_ATTEN_DB_6);

    return ESP_OK;
}

bool bsp_power_woke_by_charger(void)
{
    uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
    return (wakeup_pin_mask & (1ULL << BOARD_CHARGER_CHRG_GPIO)) || (wakeup_pin_mask & (1ULL << BOARD_CHARGER_STDBY_GPIO));
}

bsp_charge_state_t bsp_power_get_charge_state(void)
{
    if (gpio_get_level(BOARD_CHARGER_STDBY_GPIO) == 0) {
        return BSP_CHARGE_STATE_FULL;
    }
    if (gpio_get_level(BOARD_CHARGER_CHRG_GPIO) == 0) {
        return BSP_CHARGE_STATE_CHARGING;
    }
    return BSP_CHARGE_STATE_NONE;
}

int bsp_power_get_battery_mv(void)
{
    int adc_raw = adc1_get_raw(BOARD_BATTERY_ADC1_CHANNEL);
    float voltage = (adc_raw * 2.2f / 4095.0f) * 2.0f; // Convert to actual battery voltage
    return (int)(voltage * 1000.0f);
}

void bsp_power_off(void)
{
    gpio_set_level(BOARD_AMP_EN_GPIO, 0);
    gpio_set_level(BOARD_PERIPH_PWR_EN_GPIO, 0);
    esp_deep_sleep_start();
}
