#pragma once

/**
 * @file vl53l0x.h
 * @brief High-level ESP-IDF wrapper API for the ST VL53L0X time-of-flight sensor.
 *
 * Typical flow:
 * 1) `vl53l0x_create()`
 * 2) `vl53l0x_init()`
 * 3) configure mode/profile/limits as needed
 * 4) read data via `vl53l0x_single_measure()` or start/get-ready/get-data loop
 * 5) `vl53l0x_destroy()`
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define VL53L0X_STRING_MAX_LEN 32U

/**
 * @brief Ranging mode wrapper for VL53L0X device modes.
 */
typedef enum {
    VL53L0X_MODE_NONE = 0,
    /** Single-shot measurement. One call/trigger produces one sample. */
    VL53L0X_MODE_SINGLE,
    /** Continuous back-to-back measurements. Sensor runs as fast as allowed by timing budget. */
    VL53L0X_MODE_CONTINUOUS,
    /** Continuous measurements with fixed inter-measurement period (host-configured in ms). */
    VL53L0X_MODE_CONTINUOUS_TIMED,
} vl53l0x_mode_t;

/**
 * @brief Preset profile for common ranging trade-offs.
 */
typedef enum {
    /** Balanced baseline profile (default timing/limits). */
    VL53L0X_PROFILE_DEFAULT = 0,
    /** Higher precision profile (long timing budget, lower update rate). */
    VL53L0X_PROFILE_HIGH_ACCURACY,
    /** Extended-range profile (relaxed signal/sigma limits). */
    VL53L0X_PROFILE_LONG_RANGE,
    /** Faster update profile (minimum timing budget, lower precision). */
    VL53L0X_PROFILE_HIGH_SPEED,
} vl53l0x_profile_t;

/**
 * @brief Limit check identifiers supported by the public wrapper API.
 */
typedef enum {
    VL53L0X_LIMIT_CHECK_SIGMA_FINAL_RANGE = 0,
    VL53L0X_LIMIT_CHECK_SIGNAL_RATE_FINAL_RANGE = 1,
    VL53L0X_LIMIT_CHECK_SIGNAL_REF_CLIP = 2,
    VL53L0X_LIMIT_CHECK_RANGE_IGNORE_THRESHOLD = 3,
    VL53L0X_LIMIT_CHECK_SIGNAL_RATE_MSRC = 4,
    VL53L0X_LIMIT_CHECK_SIGNAL_RATE_PRE_RANGE = 5,
} vl53l0x_limit_check_id_t;

/**
 * @brief GPIO interrupt functionality options.
 */
typedef enum {
    VL53L0X_GPIO_FUNCTION_OFF = 0,
    VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_LOW,
    VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_HIGH,
    VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_OUT,
    VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY,
} vl53l0x_gpio_function_t;

/**
 * @brief GPIO interrupt polarity.
 */
typedef enum {
    VL53L0X_INTERRUPT_POLARITY_LOW = 0,
    VL53L0X_INTERRUPT_POLARITY_HIGH,
} vl53l0x_interrupt_polarity_t;

/**
 * @brief Reference SPAD selection data.
 */
typedef struct {
    /** Number of reference SPADs selected by SPAD management. */
    uint32_t count;
    /** True when selected SPADs are aperture SPADs. */
    bool is_aperture;
} vl53l0x_ref_spad_calibration_t;

/**
 * @brief Reference calibration data (VHV and phase calibration).
 */
typedef struct {
    uint8_t vhv_settings;
    uint8_t phase_cal;
} vl53l0x_ref_calibration_t;

/**
 * @brief Converted ranging result in user-friendly units.
 */
typedef struct {
    /** Measured distance to target in millimeters. */
    uint16_t distance_mm;
    /** Estimated maximum measurable distance for current conditions (mm). */
    uint16_t dmax_mm;
    /** Return signal rate in mega-counts per second (MCPS). */
    float signal_rate_mcps;
    /** Ambient (background) rate in mega-counts per second (MCPS). */
    float ambient_rate_mcps;
    /** Effective SPAD count used for this measurement. */
    float effective_spad_count;
    /** Raw VL53L0X range status code (0 means valid). */
    uint8_t range_status;
    /** Convenience validity flag derived from range_status. */
    bool valid;
    /** Millisecond timestamp taken when result is produced. */
    uint32_t timestamp_ms;
} vl53l0x_data_t;

/**
 * @brief Opaque sensor handle instance. One handle per physical sensor.
 */
typedef struct vl53l0x vl53l0x_t;
typedef vl53l0x_t *vl53l0x_handle_t;

/**
 * @brief Create sensor handle and attach VL53L0X I2C device to the specified bus.
 *
 * This call allocates host-side state and registers an I2C device with default
 * VL53L0X address. Sensor boot sequence is not executed yet; call
 * `vl53l0x_init()` after this function succeeds.
 *
 * @param[out] out_sensor Output sensor handle; must point to `NULL` on entry.
 * @param[in]  bus        Initialized ESP-IDF I2C master bus handle.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if arguments are invalid
 * - `ESP_ERR_INVALID_STATE` if `*out_sensor` is not `NULL`
 * - `ESP_ERR_NO_MEM` if allocation fails
 * - other `esp_err_t` returned by I2C bus API
 */
esp_err_t vl53l0x_create(vl53l0x_handle_t *out_sensor, i2c_master_bus_handle_t bus);

/**
 * @brief Change sensor I2C address and rebind device on the bus.
 *
 * @param[in] sensor  Sensor handle.
 * @param[in] bus     I2C master bus used by this sensor.
 * @param[in] address New 7-bit address (must differ from default address).
 *
 * @return `ESP_OK` on success, otherwise error from argument checks, PAL, or I2C APIs.
 */
esp_err_t vl53l0x_set_address(vl53l0x_handle_t sensor, i2c_master_bus_handle_t bus, uint8_t address);

/**
 * @brief Run VL53L0X data/static initialization sequence.
 *
 * Must be called once after `vl53l0x_create()` and before measurements.
 *
 * @param[in] sensor Sensor handle created by `vl53l0x_create()`.
 * @return `ESP_OK` on success, otherwise PAL/I2C related error.
 */
esp_err_t vl53l0x_init(vl53l0x_handle_t sensor);

/**
 * @brief Stop measurements (if running) and destroy handle.
 *
 * @param sensor Sensor handle.
 * @return ESP_OK on success.
 */
esp_err_t vl53l0x_destroy(vl53l0x_handle_t sensor);

/**
 * @brief Perform one blocking single-shot ranging measurement.
 *
 * Internally switches device mode to `VL53L0X_MODE_SINGLE`, triggers ranging,
 * and returns converted data.
 *
 * @param[in]  sensor Sensor handle.
 * @param[out] data   Output measurement structure.
 * @return `ESP_OK` on success, otherwise validation/PAL error.
 */
esp_err_t vl53l0x_single_measure(vl53l0x_handle_t sensor, vl53l0x_data_t *data);

/**
 * @brief Set sensor ranging mode.
 *
 * @param[in] sensor Sensor handle.
 * @param[in] mode   One of `vl53l0x_mode_t` values except `VL53L0X_MODE_NONE`.
 * @return `ESP_OK` on success, `ESP_ERR_INVALID_ARG` for unsupported mode, or PAL error.
 */
esp_err_t vl53l0x_set_mode(vl53l0x_handle_t sensor, vl53l0x_mode_t mode);

/**
 * @brief Clear pending GPIO interrupt condition in sensor.
 *
 * @param[in] sensor Sensor handle.
 * @return `ESP_OK` on success, otherwise PAL/validation error.
 */
esp_err_t vl53l0x_clear_interrupt_mask(vl53l0x_handle_t sensor);

/**
 * @brief Configure low/high interrupt thresholds in millimeters.
 *
 * @param[in] sensor            Sensor handle.
 * @param[in] threshold_low_mm  Low threshold, mm.
 * @param[in] threshold_high_mm High threshold, mm.
 * @return `ESP_OK` on success, otherwise PAL/validation error.
 */
esp_err_t vl53l0x_set_interrupt_thresholds(vl53l0x_handle_t sensor,
                                        float threshold_low_mm,
                                        float threshold_high_mm);

/**
 * @brief Configure GPIO interrupt function and polarity.
 *
 * Device mode must be configured first.
 *
 * @param[in] sensor        Sensor handle.
 * @param[in] functionality GPIO function selection.
 * @param[in] polarity      Active interrupt polarity.
 * @return `ESP_OK` on success, otherwise validation/PAL error.
 */
esp_err_t vl53l0x_set_gpio_config(vl53l0x_handle_t sensor,
                                vl53l0x_gpio_function_t functionality,
                                vl53l0x_interrupt_polarity_t polarity);

/**
 * @brief Start ranging in the currently selected mode.
 *
 * @param[in] sensor Sensor handle.
 * @return `ESP_OK` on success, otherwise `ESP_ERR_INVALID_STATE` or PAL error.
 */
esp_err_t vl53l0x_start_measurement(vl53l0x_handle_t sensor);

/**
 * @brief Query whether a new measurement sample is ready.
 *
 * @param[in]  sensor Sensor handle.
 * @param[out] ready  `true` when data is ready for `vl53l0x_get_data()`.
 * @return `ESP_OK` on success, otherwise validation/PAL error.
 */
esp_err_t vl53l0x_get_ready(vl53l0x_handle_t sensor, bool *ready);

/**
 * @brief Read latest ranging data from sensor and convert to user-friendly units.
 *
 * @param[in]  sensor Sensor handle.
 * @param[out] out    Output data structure.
 * @return `ESP_OK` on success, otherwise validation/PAL error.
 */
esp_err_t vl53l0x_get_data(vl53l0x_handle_t sensor, vl53l0x_data_t *out);

/**
 * @brief Stop ongoing measurement and wait until stop is acknowledged by device.
 *
 * @param[in] sensor     Sensor handle.
 * @param[in] timeout_ms Maximum wait time in milliseconds.
 * @return `ESP_OK` on success, `ESP_ERR_TIMEOUT` on timeout, or PAL/validation error.
 */
esp_err_t vl53l0x_stop_measurement(vl53l0x_handle_t sensor, uint32_t timeout_ms);

/**
 * @brief Set measurement timing budget in microseconds.
 *
 * @param[in] sensor Sensor handle.
 * @param[in] us     Timing budget in microseconds (must satisfy device minimum).
 * @return `ESP_OK` on success, otherwise argument/PAL/validation error.
 */
esp_err_t vl53l0x_set_timing_budget(vl53l0x_handle_t sensor, uint32_t us);

/**
 * @brief Get current measurement timing budget in microseconds.
 *
 * @param[in]  sensor Sensor handle.
 * @param[out] us     Output timing budget in microseconds.
 * @return `ESP_OK` on success, otherwise argument/PAL/validation error.
 */
esp_err_t vl53l0x_get_timing_budget(vl53l0x_handle_t sensor, uint32_t *us);

/**
 * @brief Set inter-measurement period in milliseconds.
 *
 * Used by `VL53L0X_MODE_CONTINUOUS_TIMED` mode.
 *
 * @param[in] sensor Sensor handle.
 * @param[in] ms     Period in milliseconds.
 * @return `ESP_OK` on success, otherwise PAL/validation error.
 */
esp_err_t vl53l0x_set_inter_measurement(vl53l0x_handle_t sensor, uint32_t ms);

/**
 * @brief Run blocking reference SPAD management and return values to persist on host.
 *
 * The procedure changes the device mode to single ranging. It must not be called
 * while continuous measurement is running.
 */
esp_err_t vl53l0x_perform_ref_spad_management(vl53l0x_handle_t sensor,
                                               vl53l0x_ref_spad_calibration_t *out);

/**
 * @brief Apply previously saved reference SPAD calibration data.
 */
esp_err_t vl53l0x_set_reference_spads(vl53l0x_handle_t sensor,
                                      const vl53l0x_ref_spad_calibration_t *calibration);

/**
 * @brief Read current reference SPAD calibration data.
 */
esp_err_t vl53l0x_get_reference_spads(vl53l0x_handle_t sensor,
                                      vl53l0x_ref_spad_calibration_t *out);

/**
 * @brief Run blocking VHV/phase reference calibration and return values to persist on host.
 *
 * ST recommends repeating this calibration if operating temperature changes by about 8°C
 * from the last calibration point. It must not be called while continuous measurement is running.
 */
esp_err_t vl53l0x_perform_ref_calibration(vl53l0x_handle_t sensor,
                                          vl53l0x_ref_calibration_t *out);

/**
 * @brief Apply previously saved VHV/phase reference calibration data.
 */
esp_err_t vl53l0x_set_ref_calibration(vl53l0x_handle_t sensor,
                                      const vl53l0x_ref_calibration_t *calibration);

/**
 * @brief Read current VHV/phase reference calibration data.
 */
esp_err_t vl53l0x_get_ref_calibration(vl53l0x_handle_t sensor,
                                      vl53l0x_ref_calibration_t *out);

/**
 * @brief Run blocking ranging offset calibration at a known target distance.
 *
 * @param calibration_distance_mm Known target distance in millimeters, commonly 100 mm.
 * @param out_offset_um Computed offset in micrometers; save this value and restore it later
 *        with vl53l0x_set_offset_calibration() or cfg.offset_calibration_um.
 */
esp_err_t vl53l0x_perform_offset_calibration(vl53l0x_handle_t sensor,
                                             float calibration_distance_mm,
                                             int32_t *out_offset_um);

/**
 * @brief Apply previously saved ranging offset calibration in micrometers.
 */
esp_err_t vl53l0x_set_offset_calibration(vl53l0x_handle_t sensor, int32_t offset_um);

/**
 * @brief Read current ranging offset calibration in micrometers.
 */
esp_err_t vl53l0x_get_offset_calibration(vl53l0x_handle_t sensor, int32_t *offset_um);

/**
 * @brief Run blocking cross-talk calibration at a known target distance.
 *
 * The procedure applies and enables the computed compensation. Save the returned MCPS
 * value and restore it later with vl53l0x_set_xtalk_calibration() or
 * cfg.xtalk_compensation_rate_mcps.
 */
esp_err_t vl53l0x_perform_xtalk_calibration(vl53l0x_handle_t sensor,
                                            float calibration_distance_mm,
                                            float *out_xtalk_compensation_rate_mcps);

/**
 * @brief Apply previously saved cross-talk compensation rate in MCPS and enable compensation.
 */
esp_err_t vl53l0x_set_xtalk_calibration(vl53l0x_handle_t sensor, float xtalk_compensation_rate_mcps);

/**
 * @brief Read current cross-talk compensation rate in MCPS.
 */
esp_err_t vl53l0x_get_xtalk_calibration(vl53l0x_handle_t sensor, float *xtalk_compensation_rate_mcps);

/**
 * @brief Enable or disable cross-talk compensation.
 */
esp_err_t vl53l0x_set_xtalk_compensation_enable(vl53l0x_handle_t sensor, bool enable);

/**
 * @brief Read cross-talk compensation enable state.
 */
esp_err_t vl53l0x_get_xtalk_compensation_enable(vl53l0x_handle_t sensor, bool *enabled);

/**
 * @brief Get inter-measurement period in milliseconds.
 *
 * @param[in]  sensor Sensor handle.
 * @param[out] ms     Output period in milliseconds.
 * @return `ESP_OK` on success, otherwise argument/PAL/validation error.
 */
esp_err_t vl53l0x_get_inter_measurement(vl53l0x_handle_t sensor, uint32_t *ms);

/**
 * @brief Apply one of predefined profile presets.
 *
 * The preset updates timing budget and selected limit-check configuration.
 *
 * @param[in] sensor  Sensor handle.
 * @param[in] profile Preset profile.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_set_profile(vl53l0x_handle_t sensor, vl53l0x_profile_t profile);

/**
 * @brief Enable/disable a selected limit check.
 *
 * @param[in] sensor Sensor handle.
 * @param[in] id     Limit-check identifier.
 * @param[in] enable New enable state.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_set_limit_check_enable(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, bool enable);

/**
 * @brief Read enable state of selected limit check.
 *
 * @param[in]  sensor  Sensor handle.
 * @param[in]  id      Limit-check identifier.
 * @param[out] enabled Output enable flag.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_get_limit_check_enable(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, bool *enabled);

/**
 * @brief Set value of selected limit check (float units).
 *
 * @param[in] sensor Sensor handle.
 * @param[in] id     Limit-check identifier.
 * @param[in] value  Threshold value in PAL float units for the selected check.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_set_limit_check_value(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, float value);

/**
 * @brief Get configured value of selected limit check.
 *
 * @param[in]  sensor Sensor handle.
 * @param[in]  id     Limit-check identifier.
 * @param[out] value  Output threshold value.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_get_limit_check_value(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, float *value);

/**
 * @brief Get current measured value of selected limit check.
 *
 * @param[in]  sensor Sensor handle.
 * @param[in]  id     Limit-check identifier.
 * @param[out] value  Output measured value for latest measurement context.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_get_limit_check_current(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, float *value);

/**
 * @brief Get pass/fail status of selected limit check for latest sample.
 *
 * @param[in]  sensor Sensor handle.
 * @param[in]  id     Limit-check identifier.
 * @param[out] passed `true` when check passed.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_get_limit_check_status(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, bool *passed);

/**
 * @brief Convenience wrapper for Range Ignore Threshold control.
 *
 * @param[in] sensor          Sensor handle.
 * @param[in] enable          Enable/disable Range Ignore Threshold check.
 * @param[in] threshold_mcps  Threshold in MCPS (kept unchanged when set to 0).
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_set_range_ignore_threshold(vl53l0x_handle_t sensor, bool enable, float threshold_mcps);

/**
 * @brief Read Range Ignore Threshold state and value.
 *
 * @param[in]  sensor         Sensor handle.
 * @param[out] enabled        Output enable state.
 * @param[out] threshold_mcps Output threshold in MCPS.
 * @return `ESP_OK` on success, otherwise argument/validation/PAL error.
 */
esp_err_t vl53l0x_get_range_ignore_threshold(vl53l0x_handle_t sensor, bool *enabled, float *threshold_mcps);

/**
 * @brief Reset sensor through PAL API and clear wrapper runtime state.
 *
 * After reset, mode is set to `VL53L0X_MODE_NONE`; reconfigure mode/settings as needed.
 *
 * @param[in] sensor Sensor handle.
 * @return `ESP_OK` on success, otherwise validation/PAL error.
 */
esp_err_t vl53l0x_reset(vl53l0x_handle_t sensor);

/**
 * @brief Convert VL53L0X range status code to human-readable text.
 *
 * @param[in] status Raw status code from `vl53l0x_data_t::range_status`.
 * @return Pointer to static string literal.
 */
const char *vl53l0x_range_status_str(uint8_t status);
