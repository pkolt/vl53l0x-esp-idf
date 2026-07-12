#include <stdbool.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"

#include "vl53l0x.h"

static const char *TAG = "VL53L0X_EXAMPLE";

#define I2C_PORT_NUM      I2C_NUM_0
#define I2C_SDA_PIN       GPIO_NUM_1
#define I2C_SCL_PIN       GPIO_NUM_0

#define VL53L0X_XSHUT_PIN GPIO_NUM_2

#define VL53L0X_CALIBRATION_OFFSET_UM 15000
#define VL53L0X_CALIBRATION_XTALK_MCPS 0.0
#define VL53L0X_CALIBRATION_REF_SPAD \
    &(vl53l0x_ref_spad_calibration_t){ \
        .count = 3, \
        .is_aperture = true \
    }


static void print_data(const vl53l0x_data_t *data) {
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

static void vl53l0x_hard_reset() {
    // On common VL53L0X breakout boards, XSHUT is usually pulled up to HIGH with a 10 kΩ resistor.
    // Drive LOW briefly to force a clean hardware reset before I2C initialization.
    gpio_set_direction(VL53L0X_XSHUT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(VL53L0X_XSHUT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(VL53L0X_XSHUT_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void app_main(void)
{
    vl53l0x_hard_reset();

    i2c_master_bus_handle_t bus = NULL;

    const i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    // Create and configure the I2C master bus used by the sensor.
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    vl53l0x_handle_t sensor = NULL;
    ESP_ERROR_CHECK(vl53l0x_create(&sensor, bus));
    ESP_ERROR_CHECK(vl53l0x_init(sensor));
    // Apply calibration values tuned for this setup (replace with your own measured values for best accuracy).
    ESP_ERROR_CHECK(vl53l0x_set_reference_spads(sensor, VL53L0X_CALIBRATION_REF_SPAD));
    ESP_ERROR_CHECK(vl53l0x_perform_ref_calibration(sensor, &(vl53l0x_ref_calibration_t){}));
    ESP_ERROR_CHECK(vl53l0x_set_offset_calibration(sensor, VL53L0X_CALIBRATION_OFFSET_UM));
    ESP_ERROR_CHECK(vl53l0x_set_xtalk_calibration(sensor, VL53L0X_CALIBRATION_XTALK_MCPS));
    ESP_ERROR_CHECK(vl53l0x_set_xtalk_compensation_enable(sensor, true));
    ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_DEFAULT));

    vl53l0x_data_t data = {0};

    while (true) {
        // Single-shot ranging: trigger one measurement and print the result once per second.
        vl53l0x_single_measure(sensor, &data);
        print_data(&data);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
