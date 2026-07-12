#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "vl53l0x.h"
#include "vl53l0x_def.h"
#include "hal/i2c_types.h"
#include "vl53l0x_api.h"
#include "vl53l0x_i2c_platform.h"
#include "vl53l0x_platform.h"

/**
 * @file vl53l0x.c
 * @brief ESP-IDF wrapper implementation for VL53L0X PAL API.
 */

struct vl53l0x {
    VL53L0X_Dev_t dev;
    vl53l0x_mode_t mode;
    bool initialized;
    bool measuring;
};

static const char *TAG = "VL53L0X";


/**
 * @brief Convert PAL status to ESP-IDF status and log detailed error text.
 */
static esp_err_t vl53l0x_check_status(const char *step, VL53L0X_Error status)
{
    if (status == VL53L0X_ERROR_NONE) {
        return ESP_OK;
    }

    char err_str[VL53L0X_MAX_STRING_LENGTH] = {0};
    (void)VL53L0X_GetPalErrorString(status, err_str);
    ESP_LOGE(TAG, "%s failed: PAL status %d (%s)", step, (int)status, err_str);
    return ESP_FAIL;
}

/**
 * @brief Validate that sensor handle is non-null and initialized.
 */
static esp_err_t vl53l0x_validate_sensor(const vl53l0x_t *sensor)
{
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/**
 * @brief Common pre-call routine: validate handle and I2C backend context.
 */
static esp_err_t vl53l0x_prepare_sensor(vl53l0x_t *sensor)
{
    esp_err_t err = vl53l0x_validate_sensor(sensor);
    if (err != ESP_OK) {
        return err;
    }

    if (sensor->dev.i2c_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/**
 * @brief Check that limit-check ID is within supported core range.
 */
static bool vl53l0x_is_valid_limit_check_id(vl53l0x_limit_check_id_t id)
{
    return ((uint16_t)id < (uint16_t)VL53L0X_CHECKENABLE_NUMBER_OF_CHECKS);
}

/**
 * @brief Convert wrapper mode enum to PAL mode enum.
 */
static esp_err_t vl53l0x_mode_to_pal(vl53l0x_mode_t mode, VL53L0X_DeviceModes *pal_mode)
{
    if (pal_mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (mode) {
        case VL53L0X_MODE_SINGLE:
            *pal_mode =  VL53L0X_DEVICEMODE_SINGLE_RANGING;
            break;
        case VL53L0X_MODE_CONTINUOUS:
            *pal_mode = VL53L0X_DEVICEMODE_CONTINUOUS_RANGING;
            break;
        case VL53L0X_MODE_CONTINUOUS_TIMED:
            *pal_mode = VL53L0X_DEVICEMODE_CONTINUOUS_TIMED_RANGING;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Convert wrapper GPIO function enum to PAL GPIO functionality value.
 */
static esp_err_t vl53l0x_gpio_function_to_pal(vl53l0x_gpio_function_t gpio_func, VL53L0X_GpioFunctionality *pal_func)
{
    if (pal_func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (gpio_func) {
        case VL53L0X_GPIO_FUNCTION_OFF:
            *pal_func = VL53L0X_GPIOFUNCTIONALITY_OFF;
            break;
        case VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_LOW:
            *pal_func = VL53L0X_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_LOW;
            break;
        case VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_HIGH:
            *pal_func = VL53L0X_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_HIGH;
            break;
        case VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_OUT:
            *pal_func = VL53L0X_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_OUT;
            break;
        case VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY:
            *pal_func = VL53L0X_GPIOFUNCTIONALITY_NEW_MEASURE_READY;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Convert wrapper interrupt polarity enum to PAL polarity value.
 */
static esp_err_t vl53l0x_interrupt_polarity_to_pal(vl53l0x_interrupt_polarity_t polarity, VL53L0X_InterruptPolarity *pal_polarity)
{
    if (pal_polarity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (polarity) {
        case VL53L0X_INTERRUPT_POLARITY_LOW:
            *pal_polarity = VL53L0X_INTERRUPTPOLARITY_LOW;
            break;
        case VL53L0X_INTERRUPT_POLARITY_HIGH:
            *pal_polarity = VL53L0X_INTERRUPTPOLARITY_HIGH;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Convert float value to 16.16 fixed-point format used by VL53L0X API.
 */
static FixPoint1616_t vl53l0x_float_to_fixpoint1616(float value)
{
    if (value <= 0.0f) {
        return 0;
    }
    return (FixPoint1616_t)(value * 65536.0f);
}

/**
 * @brief Convert 16.16 fixed-point value from core API to float.
 */
static float vl53l0x_fixpoint1616_to_float(FixPoint1616_t value)
{
    return ((float)value) / 65536.0f;
}

/**
 * @brief Map raw core measurement structure to simplified public result.
 */
static void vl53l0x_fill_result(vl53l0x_data_t *data, const VL53L0X_RangingMeasurementData_t *raw)
{
    data->distance_mm = raw->RangeMilliMeter;
    data->dmax_mm = raw->RangeDMaxMilliMeter;
    data->signal_rate_mcps = vl53l0x_fixpoint1616_to_float(raw->SignalRateRtnMegaCps);
    data->ambient_rate_mcps = vl53l0x_fixpoint1616_to_float(raw->AmbientRateRtnMegaCps);
    data->effective_spad_count = ((float)raw->EffectiveSpadRtnCount) / 256.0f;
    data->range_status = raw->RangeStatus;
    data->valid = (raw->RangeStatus == 0U);
    data->timestamp_ms = VL53L0X_GetTickCount();
}

/**
 * @brief Create sensor instance and attach it to I2C bus with default address.
 */
esp_err_t vl53l0x_create(vl53l0x_handle_t *out_sensor, i2c_master_bus_handle_t bus)
{
    if ((out_sensor == NULL) || (bus == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (*out_sensor != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t i2c_dev_cfg = {
            .device_address = VL53L0X_I2C_DEFAULT_ADDR_7BIT,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .scl_speed_hz = VL53L0X_I2C_DEFAULT_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle = NULL;

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(
        bus, 
        &i2c_dev_cfg, 
        &dev_handle), 
        TAG, 
        "Failed to add VL53L0X device to I2C bus"
    );

    vl53l0x_handle_t sensor = calloc(1, sizeof(vl53l0x_t));
    if (sensor == NULL) {
        return ESP_ERR_NO_MEM;
    }

    sensor->dev.i2c_dev_handle = dev_handle;
    sensor->initialized = false;
    sensor->measuring = false;
    sensor->mode = VL53L0X_MODE_NONE;
    *out_sensor = sensor;
    return ESP_OK;
}

/**
 * @brief Update sensor I2C address in device and re-register I2C handle on bus.
 */
esp_err_t vl53l0x_set_address(vl53l0x_handle_t sensor, i2c_master_bus_handle_t bus, uint8_t address)
{
    if ((sensor == NULL) || (bus == NULL) || (address == VL53L0X_I2C_DEFAULT_ADDR_7BIT)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        vl53l0x_check_status(
            "VL53L0X_SetDeviceAddress",
            VL53L0X_SetDeviceAddress(&sensor->dev, (uint8_t)(address << 1U))
        ), TAG, "VL53L0X_SetDeviceAddress failed");

    i2c_master_dev_handle_t dev_handle = sensor->dev.i2c_dev_handle;

    if (dev_handle != NULL) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(dev_handle), TAG, "Failed to remove VL53L0X device from I2C bus");
    }

    dev_handle = NULL;

    const i2c_device_config_t i2c_dev_cfg = {
        .device_address = address,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = VL53L0X_I2C_DEFAULT_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(
        bus, 
        &i2c_dev_cfg, 
        &dev_handle), 
        TAG, 
        "Failed to add VL53L0X device to I2C bus"
    );

    sensor->dev.i2c_dev_handle = dev_handle;

    return ESP_OK;
}

/**
 * @brief Run mandatory PAL data/static initialization sequence.
 */
esp_err_t vl53l0x_init(vl53l0x_handle_t sensor)
{
    ESP_RETURN_ON_ERROR(vl53l0x_check_status("VL53L0X_DataInit", VL53L0X_DataInit(&sensor->dev)), TAG, "VL53L0X_DataInit failed");
    ESP_RETURN_ON_ERROR(vl53l0x_check_status("VL53L0X_StaticInit", VL53L0X_StaticInit(&sensor->dev)), TAG, "VL53L0X_StaticInit failed");
    sensor->initialized = true;
    return ESP_OK;
}

/**
 * @brief Stop sensor if needed and clear handle state.
 */
esp_err_t vl53l0x_destroy(vl53l0x_handle_t sensor)
{
    ESP_RETURN_ON_ERROR(vl53l0x_validate_sensor(sensor), TAG, "Invalid sensor handle or sensor is not initialized");

    if (sensor->measuring) {
        ESP_RETURN_ON_ERROR(vl53l0x_stop_measurement(sensor, VL53L0X_DEFAULT_STOP_TIMEOUT_MS), TAG, "Failed to stop measurement before destroying sensor");
    }

    i2c_master_dev_handle_t dev_handle = sensor->dev.i2c_dev_handle;
    if (dev_handle != NULL) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(dev_handle), TAG, "Failed to remove VL53L0X device from I2C bus");
    }

    free(sensor);
    return ESP_OK;
}

/**
 * @brief Execute one blocking single-ranging measurement transaction.
 */
static esp_err_t vl53l0x_perform_single_ranging_measurement(vl53l0x_t *sensor, vl53l0x_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    VL53L0X_RangingMeasurementData_t raw = {0};

    sensor->measuring = true;
    
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_PerformSingleRangingMeasurement",
        VL53L0X_PerformSingleRangingMeasurement(&sensor->dev, &raw)), TAG, "VL53L0X_PerformSingleRangingMeasurement failed");

    sensor->measuring = false;
    vl53l0x_fill_result(data, &raw);
    return ESP_OK;
}

/**
 * @brief High-level helper for one-shot ranging measurement.
 */
esp_err_t vl53l0x_single_measure(vl53l0x_handle_t sensor, vl53l0x_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");
    ESP_RETURN_ON_ERROR(vl53l0x_set_mode(sensor, VL53L0X_MODE_SINGLE), TAG, "Failed to set single ranging mode");
    ESP_RETURN_ON_ERROR(vl53l0x_perform_single_ranging_measurement(sensor, data), TAG, "Failed to perform single ranging measurement");
    return vl53l0x_get_data(sensor, data);
}

/**
 * @brief Change active device mode if requested mode differs from current one.
 */
esp_err_t vl53l0x_set_mode(vl53l0x_t *sensor, vl53l0x_mode_t mode)
{
    VL53L0X_DeviceModes pal_mode;

    if (mode == VL53L0X_MODE_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sensor->mode != mode) {
        ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");
        ESP_RETURN_ON_ERROR(vl53l0x_mode_to_pal(mode, &pal_mode), TAG, "Failed to convert mode to VL53L0X PAL mode");
        ESP_RETURN_ON_ERROR(vl53l0x_check_status("VL53L0X_SetDeviceMode", VL53L0X_SetDeviceMode(&sensor->dev, pal_mode)), TAG, "VL53L0X_SetDeviceMode failed");
        sensor->mode = mode;
    }
    return ESP_OK;
}

/**
 * @brief Clear pending sensor interrupt and reset local measuring flag.
 */
esp_err_t vl53l0x_clear_interrupt_mask(vl53l0x_t *sensor)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");
    ESP_RETURN_ON_ERROR(vl53l0x_check_status("VL53L0X_ClearInterruptMask",
                                VL53L0X_ClearInterruptMask(&sensor->dev, 0 /* Don't used in core library */)), TAG, "VL53L0X_ClearInterruptMask failed");
    sensor->measuring = false;
    return ESP_OK;
}

/**
 * @brief Configure low/high distance thresholds for GPIO threshold interrupt modes.
 */
esp_err_t vl53l0x_set_interrupt_thresholds(vl53l0x_t *sensor,
                                           float threshold_low_mm,
                                           float threshold_high_mm)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_SetInterruptThresholds",
        VL53L0X_SetInterruptThresholds(
            &sensor->dev,
            0, /* Don't used in core library */
            vl53l0x_float_to_fixpoint1616(threshold_low_mm),
            vl53l0x_float_to_fixpoint1616(threshold_high_mm)));
}

/**
 * @brief Configure VL53L0X GPIO interrupt function for currently selected ranging mode.
 */
esp_err_t vl53l0x_set_gpio_config(vl53l0x_t *sensor,
                                  vl53l0x_gpio_function_t functionality,
                                  vl53l0x_interrupt_polarity_t polarity)
{
    VL53L0X_DeviceModes pal_mode;
    VL53L0X_GpioFunctionality pal_func;
    VL53L0X_InterruptPolarity pal_polarity;
    
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->mode == VL53L0X_MODE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_mode_to_pal(sensor->mode, &pal_mode), TAG, "Failed to convert current mode to VL53L0X PAL mode");
    ESP_RETURN_ON_ERROR(vl53l0x_gpio_function_to_pal(functionality, &pal_func), TAG, "Failed to convert GPIO functionality to VL53L0X PAL value");
    ESP_RETURN_ON_ERROR(vl53l0x_interrupt_polarity_to_pal(polarity, &pal_polarity), TAG, "Failed to convert interrupt polarity to VL53L0X PAL value");

    return vl53l0x_check_status(
        "VL53L0X_SetGpioConfig",
        VL53L0X_SetGpioConfig(
            &sensor->dev, 
            0 /* VL53L0X have only pin 0 */, 
            pal_mode, 
            pal_func, 
            pal_polarity));
}

/**
 * @brief Start measurement sequence according to current mode.
 */
esp_err_t vl53l0x_start_measurement(vl53l0x_t *sensor)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor->mode != VL53L0X_MODE_SINGLE) {
        sensor->measuring = true;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_StartMeasurement",
        VL53L0X_StartMeasurement(&sensor->dev)), TAG, "VL53L0X_StartMeasurement failed");
    return ESP_OK;
}

/**
 * @brief Poll data-ready status from sensor.
 */
esp_err_t vl53l0x_get_ready(vl53l0x_t *sensor, bool *ready)
{
    if (ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    uint8_t data_ready = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetMeasurementDataReady",
        VL53L0X_GetMeasurementDataReady(&sensor->dev, &data_ready)), TAG, "VL53L0X_GetMeasurementDataReady failed");
    
    *ready = (data_ready != 0U);
    return ESP_OK;
}

/**
 * @brief Read latest measurement sample and convert PAL fields to wrapper format.
 */
esp_err_t vl53l0x_get_data(vl53l0x_t *sensor, vl53l0x_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    VL53L0X_RangingMeasurementData_t raw = {0};
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetRangingMeasurementData",
        VL53L0X_GetRangingMeasurementData(&sensor->dev, &raw)), TAG, "VL53L0X_GetRangingMeasurementData failed");

    vl53l0x_fill_result(data, &raw);
    return ESP_OK;
}

/**
 * @brief Stop ongoing ranging and wait for stop-complete indication.
 */
esp_err_t vl53l0x_stop_measurement(vl53l0x_t *sensor, uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (!sensor->measuring) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_check_status("VL53L0X_StopMeasurement", VL53L0X_StopMeasurement(&sensor->dev)), TAG, "VL53L0X_StopMeasurement failed");

    const uint32_t start_ms = VL53L0X_GetTickCount();
    while (true) {
        uint32_t stop_status = 0;
        ESP_RETURN_ON_ERROR(vl53l0x_check_status(
            "VL53L0X_GetStopCompletedStatus",
            VL53L0X_GetStopCompletedStatus(&sensor->dev, &stop_status)), TAG, "VL53L0X_GetStopCompletedStatus failed");

        if (stop_status != 0U) {
            sensor->measuring = false;
            return ESP_OK;
        }

        const uint32_t now_ms = VL53L0X_GetTickCount();
        if ((now_ms - start_ms) >= timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Set measurement timing budget in microseconds.
 */
esp_err_t vl53l0x_set_timing_budget(vl53l0x_t *sensor, uint32_t us)
{
    if (us < VL53L0X_TIMING_BUDGET_MIN_US) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_SetMeasurementTimingBudgetMicroSeconds",
        VL53L0X_SetMeasurementTimingBudgetMicroSeconds(&sensor->dev, us));
}

/**
 * @brief Get measurement timing budget in microseconds.
 */
esp_err_t vl53l0x_get_timing_budget(vl53l0x_t *sensor, uint32_t *us)
{
    if (us == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_GetMeasurementTimingBudgetMicroSeconds",
        VL53L0X_GetMeasurementTimingBudgetMicroSeconds(&sensor->dev, us));
}

/**
 * @brief Set inter-measurement period in milliseconds.
 */
esp_err_t vl53l0x_set_inter_measurement(vl53l0x_t *sensor, uint32_t ms)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
            "VL53L0X_SetInterMeasurementPeriodMilliSeconds",
            VL53L0X_SetInterMeasurementPeriodMilliSeconds(&sensor->dev, ms));
}

/**
 * @brief Get inter-measurement period in milliseconds.
 */
esp_err_t vl53l0x_get_inter_measurement(vl53l0x_t *sensor, uint32_t *ms)
{
    if (ms == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetInterMeasurementPeriodMilliSeconds",
        VL53L0X_GetInterMeasurementPeriodMilliSeconds(&sensor->dev, ms)), TAG, "VL53L0X_GetInterMeasurementPeriodMilliSeconds failed");
    return ESP_OK;
}

/**
 * @brief Run reference SPAD management calibration and return selected SPAD info.
 */
esp_err_t vl53l0x_perform_ref_spad_management(vl53l0x_t *sensor,
                                               vl53l0x_ref_spad_calibration_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t count = 0;
    uint8_t is_aperture = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_PerformRefSpadManagement",
        VL53L0X_PerformRefSpadManagement(&sensor->dev, &count, &is_aperture)), TAG, "VL53L0X_PerformRefSpadManagement failed");

    out->count = count;
    out->is_aperture = (is_aperture != 0U);
    return ESP_OK;
}

/**
 * @brief Apply saved reference SPAD calibration settings.
 */
esp_err_t vl53l0x_set_reference_spads(vl53l0x_t *sensor,
                                      const vl53l0x_ref_spad_calibration_t *calibration)
{
    if (calibration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    return vl53l0x_check_status(
        "VL53L0X_SetReferenceSpads",
        VL53L0X_SetReferenceSpads(
            &sensor->dev,
            calibration->count,
            calibration->is_aperture ? 1U : 0U));
}

/**
 * @brief Read current reference SPAD calibration settings.
 */
esp_err_t vl53l0x_get_reference_spads(vl53l0x_t *sensor,
                                      vl53l0x_ref_spad_calibration_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    uint32_t count = 0;
    uint8_t is_aperture = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetReferenceSpads",
        VL53L0X_GetReferenceSpads(&sensor->dev, &count, &is_aperture)), TAG, "VL53L0X_GetReferenceSpads failed");

    out->count = count;
    out->is_aperture = (is_aperture != 0U);
    return ESP_OK;
}

/**
 * @brief Run reference VHV/phase calibration and return resulting values.
 */
esp_err_t vl53l0x_perform_ref_calibration(vl53l0x_t *sensor,
                                          vl53l0x_ref_calibration_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t vhv_settings = 0;
    uint8_t phase_cal = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_PerformRefCalibration",
        VL53L0X_PerformRefCalibration(&sensor->dev, &vhv_settings, &phase_cal)), TAG, "VL53L0X_PerformRefCalibration failed");

    out->vhv_settings = vhv_settings;
    out->phase_cal = phase_cal;
    return ESP_OK;
}

/**
 * @brief Apply saved reference calibration values (VHV + phase).
 */
esp_err_t vl53l0x_set_ref_calibration(vl53l0x_t *sensor,
                                      const vl53l0x_ref_calibration_t *calibration)
{
    if (calibration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    return vl53l0x_check_status(
        "VL53L0X_SetRefCalibration",
        VL53L0X_SetRefCalibration(&sensor->dev, calibration->vhv_settings, calibration->phase_cal));
}

/**
 * @brief Read current reference calibration values (VHV + phase).
 */
esp_err_t vl53l0x_get_ref_calibration(vl53l0x_t *sensor,
                                      vl53l0x_ref_calibration_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_GetRefCalibration",
        VL53L0X_GetRefCalibration(&sensor->dev, &out->vhv_settings, &out->phase_cal));
}

/**
 * @brief Run offset calibration at known target distance.
 */
esp_err_t vl53l0x_perform_offset_calibration(vl53l0x_t *sensor,
                                                float calibration_distance_mm,
                                                int32_t *out_offset_um)
{
    if ((out_offset_um == NULL) || (calibration_distance_mm <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    return vl53l0x_check_status(
        "VL53L0X_PerformOffsetCalibration",
        VL53L0X_PerformOffsetCalibration(
            &sensor->dev,
            vl53l0x_float_to_fixpoint1616(calibration_distance_mm),
            out_offset_um));
}

/**
 * @brief Apply saved offset calibration value in micrometers.
 */
esp_err_t vl53l0x_set_offset_calibration(vl53l0x_t *sensor, int32_t offset_um)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    return vl53l0x_check_status(
        "VL53L0X_SetOffsetCalibrationDataMicroMeter",
        VL53L0X_SetOffsetCalibrationDataMicroMeter(&sensor->dev, offset_um));
}

/**
 * @brief Read current offset calibration value in micrometers.
 */
esp_err_t vl53l0x_get_offset_calibration(vl53l0x_t *sensor, int32_t *offset_um)
{
    if (offset_um == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_GetOffsetCalibrationDataMicroMeter",
        VL53L0X_GetOffsetCalibrationDataMicroMeter(&sensor->dev, offset_um));
}

/**
 * @brief Run cross-talk calibration at known target distance.
 */
esp_err_t vl53l0x_perform_xtalk_calibration(vl53l0x_t *sensor,
                                            float calibration_distance_mm,
                                            float *out_xtalk_compensation_rate_mcps)
{
    if ((out_xtalk_compensation_rate_mcps == NULL) || (calibration_distance_mm <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    FixPoint1616_t xtalk = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_PerformXTalkCalibration",
        VL53L0X_PerformXTalkCalibration(
            &sensor->dev,
            vl53l0x_float_to_fixpoint1616(calibration_distance_mm),
            &xtalk)), TAG, "VL53L0X_PerformXTalkCalibration failed");

    *out_xtalk_compensation_rate_mcps = vl53l0x_fixpoint1616_to_float(xtalk);
    return ESP_OK;
}

/**
 * @brief Apply saved cross-talk compensation rate (MCPS).
 */
esp_err_t vl53l0x_set_xtalk_calibration(vl53l0x_t *sensor, float xtalk_compensation_rate_mcps)
{
    if (xtalk_compensation_rate_mcps < 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    return vl53l0x_check_status(
        "VL53L0X_SetXTalkCompensationRateMegaCps",
        VL53L0X_SetXTalkCompensationRateMegaCps(
            &sensor->dev,
            vl53l0x_float_to_fixpoint1616(xtalk_compensation_rate_mcps)));
}

/**
 * @brief Read current cross-talk compensation rate (MCPS).
 */
esp_err_t vl53l0x_get_xtalk_calibration(vl53l0x_t *sensor, float *xtalk_compensation_rate_mcps)
{
    if (xtalk_compensation_rate_mcps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    FixPoint1616_t xtalk = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetXTalkCompensationRateMegaCps",
        VL53L0X_GetXTalkCompensationRateMegaCps(&sensor->dev, &xtalk)), TAG, "VL53L0X_GetXTalkCompensationRateMegaCps failed");

    *xtalk_compensation_rate_mcps = vl53l0x_fixpoint1616_to_float(xtalk);
    return ESP_OK;
}

/**
 * @brief Enable or disable cross-talk compensation.
 */
esp_err_t vl53l0x_set_xtalk_compensation_enable(vl53l0x_t *sensor, bool enable)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    if (sensor->measuring) {
        return ESP_ERR_INVALID_STATE;
    }

    return vl53l0x_check_status(
        "VL53L0X_SetXTalkCompensationEnable",
        VL53L0X_SetXTalkCompensationEnable(&sensor->dev, enable ? 1U : 0U));
}

/**
 * @brief Read cross-talk compensation enable state.
 */
esp_err_t vl53l0x_get_xtalk_compensation_enable(vl53l0x_t *sensor, bool *enabled)
{
    if (enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    uint8_t xtalk_enabled = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetXTalkCompensationEnable",
        VL53L0X_GetXTalkCompensationEnable(&sensor->dev, &xtalk_enabled)), TAG, "VL53L0X_GetXTalkCompensationEnable failed");

    *enabled = (xtalk_enabled != 0U);
    return ESP_OK;
}

/**
 * @brief Apply predefined profile settings for speed/accuracy/range trade-off.
 */
esp_err_t vl53l0x_set_profile(vl53l0x_t *sensor, vl53l0x_profile_t profile)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    switch (profile) {
        case VL53L0X_PROFILE_DEFAULT:
            ESP_RETURN_ON_ERROR(vl53l0x_set_timing_budget(sensor, VL53L0X_DEFAULT_TIMING_BUDGET_US), TAG, "Failed to apply profile timing budget");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, true), TAG, "Failed to enable SIGMA_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, true), TAG, "Failed to enable SIGNAL_RATE_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, VL53L0X_DEFAULT_SIGMA_LIMIT_MM), TAG, "Failed to configure SIGMA_FINAL_RANGE limit value");
            return vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, VL53L0X_DEFAULT_SIGNAL_RATE_LIMIT_MCPS);

        case VL53L0X_PROFILE_HIGH_ACCURACY:
            ESP_RETURN_ON_ERROR(vl53l0x_set_timing_budget(sensor, VL53L0X_PROFILE_HIGH_ACCURACY_TIMING_BUDGET_US), TAG, "Failed to apply high-accuracy timing budget");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, true), TAG, "Failed to enable SIGMA_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, true), TAG, "Failed to enable SIGNAL_RATE_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, VL53L0X_DEFAULT_SIGMA_LIMIT_MM), TAG, "Failed to configure SIGMA_FINAL_RANGE limit value");
            return vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, VL53L0X_DEFAULT_SIGNAL_RATE_LIMIT_MCPS);

        case VL53L0X_PROFILE_LONG_RANGE:
            ESP_RETURN_ON_ERROR(vl53l0x_set_timing_budget(sensor, VL53L0X_DEFAULT_TIMING_BUDGET_US), TAG, "Failed to apply profile timing budget");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, true), TAG, "Failed to enable SIGMA_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, true), TAG, "Failed to enable SIGNAL_RATE_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, VL53L0X_PROFILE_LONG_RANGE_SIGMA_LIMIT_MM), TAG, "Failed to configure long-range SIGMA_FINAL_RANGE limit value");
            return vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, VL53L0X_PROFILE_LONG_RANGE_SIGNAL_LIMIT_MCPS);

        case VL53L0X_PROFILE_HIGH_SPEED:
            ESP_RETURN_ON_ERROR(vl53l0x_set_timing_budget(sensor, VL53L0X_PROFILE_HIGH_SPEED_TIMING_BUDGET_US), TAG, "Failed to apply high-speed timing budget");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, true), TAG, "Failed to enable SIGMA_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, true), TAG, "Failed to enable SIGNAL_RATE_FINAL_RANGE limit check");
            ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE, VL53L0X_DEFAULT_SIGMA_LIMIT_MM), TAG, "Failed to configure SIGMA_FINAL_RANGE limit value");
            return vl53l0x_set_limit_check_value(sensor, VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE, VL53L0X_DEFAULT_SIGNAL_RATE_LIMIT_MCPS);

        default:
            return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief Enable or disable a specific limit check.
 */
esp_err_t vl53l0x_set_limit_check_enable(vl53l0x_t *sensor, vl53l0x_limit_check_id_t id, bool enable)
{
    if (!vl53l0x_is_valid_limit_check_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_SetLimitCheckEnable",
        VL53L0X_SetLimitCheckEnable(&sensor->dev, (uint16_t)id, enable ? 1U : 0U));
}

/**
 * @brief Read enabled state of a specific limit check.
 */
esp_err_t vl53l0x_get_limit_check_enable(vl53l0x_t *sensor, vl53l0x_limit_check_id_t id, bool *enabled)
{
    if ((enabled == NULL) || !vl53l0x_is_valid_limit_check_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetLimitCheckEnable",
        VL53L0X_GetLimitCheckEnable(&sensor->dev, (uint16_t)id, &value)), TAG, "VL53L0X_GetLimitCheckEnable failed");

    *enabled = (value != 0U);
    return ESP_OK;
}

/**
 * @brief Set threshold value for a specific limit check.
 */
esp_err_t vl53l0x_set_limit_check_value(vl53l0x_t *sensor, vl53l0x_limit_check_id_t id, float value)
{
    if ((value < 0.0f) || !vl53l0x_is_valid_limit_check_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    return vl53l0x_check_status(
        "VL53L0X_SetLimitCheckValue",
        VL53L0X_SetLimitCheckValue(&sensor->dev, (uint16_t)id, vl53l0x_float_to_fixpoint1616(value)));
}

/**
 * @brief Get configured threshold value for a specific limit check.
 */
esp_err_t vl53l0x_get_limit_check_value(vl53l0x_t *sensor, vl53l0x_limit_check_id_t id, float *value)
{
    if ((value == NULL) || !vl53l0x_is_valid_limit_check_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    FixPoint1616_t raw = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetLimitCheckValue",
        VL53L0X_GetLimitCheckValue(&sensor->dev, (uint16_t)id, &raw)), TAG, "VL53L0X_GetLimitCheckValue failed");

    *value = vl53l0x_fixpoint1616_to_float(raw);
    return ESP_OK;
}

/**
 * @brief Get current measured value used by a specific limit check.
 */
esp_err_t vl53l0x_get_limit_check_current(vl53l0x_t *sensor, vl53l0x_limit_check_id_t id, float *value)
{
    if ((value == NULL) || !vl53l0x_is_valid_limit_check_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    FixPoint1616_t raw = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetLimitCheckCurrent",
        VL53L0X_GetLimitCheckCurrent(&sensor->dev, (uint16_t)id, &raw)), TAG, "VL53L0X_GetLimitCheckCurrent failed");

    *value = vl53l0x_fixpoint1616_to_float(raw);
    return ESP_OK;
}

/**
 * @brief Get pass/fail status of a specific limit check.
 */
esp_err_t vl53l0x_get_limit_check_status(vl53l0x_t *sensor, vl53l0x_limit_check_id_t id, bool *passed)
{
    if ((passed == NULL) || !vl53l0x_is_valid_limit_check_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");

    uint8_t raw = 0;
    ESP_RETURN_ON_ERROR(vl53l0x_check_status(
        "VL53L0X_GetLimitCheckStatus",
        VL53L0X_GetLimitCheckStatus(&sensor->dev, (uint16_t)id, &raw)), TAG, "VL53L0X_GetLimitCheckStatus failed");

    *passed = (raw == 0U);
    return ESP_OK;
}

/**
 * @brief Convenience setter for Range Ignore Threshold check.
 */
esp_err_t vl53l0x_set_range_ignore_threshold(vl53l0x_t *sensor, bool enable, float threshold_mcps)
{
    if (threshold_mcps < 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");
    ESP_RETURN_ON_ERROR(vl53l0x_set_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_RANGE_IGNORE_THRESHOLD, enable), TAG, "Failed to update RANGE_IGNORE_THRESHOLD enable state");

    if (threshold_mcps > 0.0f) {
        return vl53l0x_set_limit_check_value(
            sensor,
            VL53L0X_LIMIT_CHECK_RANGE_IGNORE_THRESHOLD,
            threshold_mcps);
    }
    return ESP_OK;
}

/**
 * @brief Convenience getter for Range Ignore Threshold check.
 */
esp_err_t vl53l0x_get_range_ignore_threshold(vl53l0x_t *sensor, bool *enabled, float *threshold_mcps)
{
    if ((enabled == NULL) || (threshold_mcps == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");
    ESP_RETURN_ON_ERROR(vl53l0x_get_limit_check_enable(sensor, VL53L0X_LIMIT_CHECK_RANGE_IGNORE_THRESHOLD, enabled), TAG, "Failed to read RANGE_IGNORE_THRESHOLD enable state");

    return vl53l0x_get_limit_check_value(
        sensor,
        VL53L0X_LIMIT_CHECK_RANGE_IGNORE_THRESHOLD,
        threshold_mcps);
}

/**
 * @brief Reset the sensor through core API and restore local state flags.
 */
esp_err_t vl53l0x_reset(vl53l0x_t *sensor)
{
    ESP_RETURN_ON_ERROR(vl53l0x_prepare_sensor(sensor), TAG, "Sensor is not ready (not initialized or invalid I2C handle)");
    ESP_RETURN_ON_ERROR(vl53l0x_check_status("VL53L0X_ResetDevice", VL53L0X_ResetDevice(&sensor->dev)), TAG, "VL53L0X_ResetDevice failed");

    sensor->measuring = false;
    sensor->mode = VL53L0X_MODE_NONE;
    return ESP_OK;
}

/**
 * @brief Return human-readable string for known range status codes.
 */
const char *vl53l0x_range_status_str(uint8_t status)
{
    switch (status) {
        case 0:
            return "Range Valid";
        case 1:
            return "Sigma Fail";
        case 2:
            return "Signal Fail";
        case 3:
            return "Min Range Fail";
        case 4:
            return "Phase Fail";
        case 5:
            return "Hardware Fail";
        case 255:
            return "No Update";
        default:
            return "Unknown";
    }
}
