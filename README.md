# VL53L0X for ESP-IDF (ESP32)

## Table of Contents

- [VL53L0X for ESP-IDF (ESP32)](#vl53l0x-for-esp-idf-esp32)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
  - [Calibration](#calibration)
    - [What is SPAD](#what-is-spad)
    - [Which calibrations are needed](#which-calibrations-are-needed)
    - [Recommended order in code](#recommended-order-in-code)
    - [When to run calibrations](#when-to-run-calibrations)
      - [1. `vl53l0x_perform_ref_spad_management()`](#1-vl53l0x_perform_ref_spad_management)
      - [2. `vl53l0x_perform_ref_calibration()`](#2-vl53l0x_perform_ref_calibration)
      - [3. `vl53l0x_perform_offset_calibration()`](#3-vl53l0x_perform_offset_calibration)
      - [4. `vl53l0x_perform_xtalk_calibration()`](#4-vl53l0x_perform_xtalk_calibration)
    - [Using saved values](#using-saved-values)
    - [When to repeat calibration](#when-to-repeat-calibration)
    - [Minimal practical scenario](#minimal-practical-scenario)
  - [Operating profiles](#operating-profiles)
    - [Available profiles](#available-profiles)
    - [How a profile works](#how-a-profile-works)
    - [Usage examples](#usage-examples)
      - [Default profile](#default-profile)
      - [High accuracy](#high-accuracy)
      - [High speed](#high-speed)
      - [Long range](#long-range)
    - [When manual parameter tuning is needed](#when-manual-parameter-tuning-is-needed)
  - [Changing the I2C address](#changing-the-i2c-address)
    - [How to use `XSHUT`](#how-to-use-xshut)
    - [Example](#example)
  - [Operating modes](#operating-modes)
  - [Single measurement (blocking, Single ranging)](#single-measurement-blocking-single-ranging)
    - [How it works](#how-it-works)
    - [When to use](#when-to-use)
    - [Example](#example-1)
  - [Manual readiness polling](#manual-readiness-polling)
    - [How it works](#how-it-works-1)
    - [When to use](#when-to-use-1)
    - [Example](#example-2)
    - [Important notes](#important-notes)
  - [Single measurement (non-blocking, interrupt-driven)](#single-measurement-non-blocking-interrupt-driven)
    - [How it works](#how-it-works-2)
    - [Working example](#working-example)
    - [When to use](#when-to-use-2)
    - [Important notes](#important-notes-1)
  - [Continuous measurement (Continuous ranging)](#continuous-measurement-continuous-ranging)
    - [Working example](#working-example-1)
    - [Important notes](#important-notes-2)
  - [Continuous measurement at time intervals (Continuous Timed)](#continuous-measurement-at-time-intervals-continuous-timed)
    - [Important notes](#important-notes-3)
  - [Threshold measurements by interrupt](#threshold-measurements-by-interrupt)
    - [Interrupt modes](#interrupt-modes)
    - [Important considerations](#important-considerations)
    - [Typical workflow](#typical-workflow)
    - [Working example](#working-example-2)
    - [How to read this example](#how-to-read-this-example)
    - [When to use](#when-to-use-3)
    - [Important notes](#important-notes-4)
  - [API](#api)
    - [Initialization and deinitialization](#initialization-and-deinitialization)
    - [Measurements](#measurements)
    - [Calibration](#calibration-1)
    - [Profile and parameter settings](#profile-and-parameter-settings)
    - [Limit checks](#limit-checks)
    - [Interrupt settings](#interrupt-settings)
    - [Helper functions](#helper-functions)
  - [License](#license)

## Introduction

`VL53L0X` is a Time-of-Flight (ToF) distance sensor from STMicroelectronics (ST). It measures absolute distance in millimeters and returns the result over `I2C`.

This library provides a convenient public API for sensor configuration, starting measurements, reading results, interrupts, and calibration.

Important things to know before use:

- Typical 7-bit I2C address of the sensor: `0x29`. ST documentation may also mention `0x52`; this is the same address in 8-bit representation (`0x29 << 1`).
- Main pins: `SDA`/`SCL` for I2C, `XSHUT` for reset/sleep, and `GPIO1` for interrupts.
- **Important:** On some VL53L0X boards, the `GPIO1` and `XSHUT` pins are already pulled up to the supply through a **10 kOhm** resistor on the module board, but this is **not guaranteed** and depends on the board manufacturer. If your board has no pull-ups, you must add them to the circuit. I2C pins (`SDA`/`SCL`) always require external pull-up resistors, usually 4.7 kOhm. For example, the [VL53L0X breakout](https://www.st.com/resource/en/data_brief/vl53l0x.pdf) board includes pull-ups.
- **Interrupts:** If `GPIO1` is pulled HIGH on your board, for example through 10 kOhm on a breakout board, set `VL53L0X_INTERRUPT_POLARITY_LOW`; an active interrupt will be a low level on the pin. If there are no pull-ups, add them before using interrupts.
- Field of view is about **25°**: as distance increases, the area from which the sensor collects reflected signal also increases.
- The VCSEL emitter operates at **940 nm** and belongs to laser safety **Class 1**, so the sensor is eye-safe. Such sensors are often used in laptops and smartphones: they detect whether a user is in front of the camera and help smartphones focus the camera.
- Range and stability depend on target reflectance, ambient light, selected profile, and measurement time (`timing budget`).
- It is better to evaluate a measurement not only by `distance_mm`, but also by result quality indicators: `valid`, `range_status`, and `signal_rate_mcps`.


## Calibration

Calibration configures the sensor for specific optics, mechanics, and operating conditions so that it measures distance correctly. It is required, especially if the sensor reports an incorrect distance to an object after power-up.

### What is SPAD

`SPAD` stands for `Single Photon Avalanche Diode`, an avalanche photodiode capable of detecting a single photon. This is the receiver used inside the VL53L0X to detect the reflected laser pulse.

Two things are important to understand:

- there is not one SPAD inside the sensor, but a **SPAD array**;
- during initialization and calibration, the sensor selects suitable **Reference SPADs** from this array: working reference SPADs that will be used for measurements.

Some SPADs have a small optical aperture in front of the sensitive area, so the documentation mentions two types:

- **Non-Aperture SPAD** — more sensitive;
- **Aperture SPAD** — has an optical mask that reduces the amount of received light and helps in some optical conditions.

This is why the API includes a separate operation, `vl53l0x_perform_ref_spad_management()`: it selects the number and type of reference SPADs for a particular sensor and its optics.

To prepare the VL53L0X for measurements, use the public API:

1. `vl53l0x_create(&sensor, bus)` — create a handle and associate it with the I2C bus.
2. `vl53l0x_init(sensor)` — perform basic sensor initialization and put it into an operational state.
3. Run the required calibrations in the correct order.
4. Apply previously saved values, if they already exist.
5. Select an operating mode with `vl53l0x_set_mode()` and start measurements.

### Which calibrations are needed

The sensor has several independent calibrations. Some of them are performed only once for a particular product, while others are performed during initialization or when operating conditions change noticeably.

| Calibration | Public API | Purpose | Target | How often to run | What to save |
| --- | --- | --- | --- | --- | --- |
| 1. Reference SPAD management | `vl53l0x_perform_ref_spad_management()` / `vl53l0x_set_reference_spads()` | Selects the number and type of reference SPADs for the sensor | Not required; there should be no reflective objects in front of the sensor | Usually once for a specific device; can be repeated after optical changes or during assembly | `vl53l0x_ref_spad_calibration_t` (`count`, `is_aperture`) |
| 2. Reference calibration | `vl53l0x_perform_ref_calibration()` / `vl53l0x_set_ref_calibration()` | Performs VHV and phase calibration so the sensor operates correctly | Not required; performed without a target in stable conditions | During initial setup and then when temperature changes noticeably, roughly **8°C** | `vl53l0x_ref_calibration_t` (`vhv_settings`, `phase_cal`) |
| 3. Offset calibration | `vl53l0x_perform_offset_calibration()` / `vl53l0x_set_offset_calibration()` | Compensates constant result offset | White target with about **88%** reflectance, distance **100 mm**, dark room | Usually once in production or during device assembly | `offset_um` in micrometers |
| 4. XTalk calibration | `vl53l0x_perform_xtalk_calibration()` / `vl53l0x_set_xtalk_calibration()` + `vl53l0x_set_xtalk_compensation_enable()` | Compensates reflections from protective glass, enclosure, and other parasitic reflections | Gray target with about **17%** reflectance; distance is selected for the protective glass and mechanics | Usually once if the sensor operates through a window/glass; repeat when the glass, gap, or mechanics change | `xtalk_compensation_rate_mcps` |

### Recommended order in code

The recommended initialization and calibration order is shown below:

```c
// Initialize the sensor and perform all required calibrations
ESP_ERROR_CHECK(vl53l0x_create(&sensor, bus));
ESP_ERROR_CHECK(vl53l0x_init(sensor));

// 1. Reference SPAD management - configure reference SPADs (performed once)
// Save the result for later restoration without repeating calibration
vl53l0x_ref_spad_calibration_t ref_spad_calibration = {0};
ESP_ERROR_CHECK(vl53l0x_perform_ref_spad_management(sensor, &ref_spad_calibration));
ESP_ERROR_CHECK(vl53l0x_set_reference_spads(sensor, &ref_spad_calibration));

// 2. Reference calibration - VHV and phase calibration (performed once or when temperature changes by ~8°C)
vl53l0x_ref_calibration_t ref_calibration = {0};
ESP_ERROR_CHECK(vl53l0x_perform_ref_calibration(sensor, &ref_calibration));
ESP_ERROR_CHECK(vl53l0x_set_ref_calibration(sensor, &ref_calibration));

// 3. Offset calibration - compensate constant offset (performed in production or during assembly)
// Requires a white target with 88% reflectance at a distance of 100 mm
int32_t offset_um = 0;
ESP_ERROR_CHECK(vl53l0x_perform_offset_calibration(sensor, 100.0f, &offset_um));
ESP_ERROR_CHECK(vl53l0x_set_offset_calibration(sensor, offset_um));

// 4. XTalk calibration - compensate parasitic reflections from glass/enclosure
// Performed once if the sensor operates through glass
float xtalk_compensation_rate_mcps = 0.0f;
ESP_ERROR_CHECK(vl53l0x_perform_xtalk_calibration(
    sensor,
    calibration_distance_mm,
    &xtalk_compensation_rate_mcps));
ESP_ERROR_CHECK(vl53l0x_set_xtalk_calibration(sensor, xtalk_compensation_rate_mcps));
ESP_ERROR_CHECK(vl53l0x_set_xtalk_compensation_enable(sensor, true));
```

### When to run calibrations

#### 1. `vl53l0x_perform_ref_spad_management()`

This is the basic reference SPAD calibration. It should be performed after sensor initialization and before the first measurement. In a typical application scenario, it does not need to run on every firmware start: it is enough to obtain the values once, save them, and then simply restore them with `vl53l0x_set_reference_spads()`.

If the device is already assembled, optics and mechanics have not changed, and you use saved values, you can call `vl53l0x_set_reference_spads()` immediately at startup.

#### 2. `vl53l0x_perform_ref_calibration()`

This calibration configures VHV and phase. It makes sense to perform it after SPAD management and before the first measurement. It is a blocking operation, so it is run before entering ranging mode.

If you have already saved `vl53l0x_ref_calibration_t`, then during normal startup you can restore the values with `vl53l0x_set_ref_calibration()` and repeat the full calibration only when temperature changes or after rebuilding the device.

#### 3. `vl53l0x_perform_offset_calibration()`

This calibration is needed to remove a constant offset of the result relative to the real distance. It is usually performed once in production or during final device assembly.

How to do it:
- place the sensor in the final enclosure;
- use a white target with about `88%` reflectance;
- set the distance to `100 mm`;
- keep the calibration target in stable conditions;
- save the resulting `offset_um` to host Flash/NVS;
- during normal startup, apply it with `vl53l0x_set_offset_calibration()`.

Example:

```c
// Save offset_um to Flash/NVS for later use
// Later, during normal startup, restore it:
ESP_ERROR_CHECK(vl53l0x_set_offset_calibration(sensor, saved_offset_um));
```

#### 4. `vl53l0x_perform_xtalk_calibration()`

This calibration compensates parasitic reflections from protective glass or an enclosure window. It is needed only if the sensor operates through glass or other optics that add crosstalk.

How to do it:
- use the final mechanics and the same glass that will be used in the product;
- use a gray target with about `17%` reflectance;
- choose a distance selected for your mechanics and protective glass;
- perform calibration once and save `xtalk_compensation_rate_mcps`;
- during normal startup, restore the value with `vl53l0x_set_xtalk_calibration()` and enable compensation with `vl53l0x_set_xtalk_compensation_enable(sensor, true)`.

If there is no protective glass, this calibration is usually not needed.

### Using saved values

If calibration has already been performed earlier, it is enough to restore the saved parameters at each startup:

```c
// Example of restoring calibration data from Flash/NVS (without recalibration)
ESP_ERROR_CHECK(vl53l0x_set_reference_spads(sensor, &saved_ref_spad_calibration));
ESP_ERROR_CHECK(vl53l0x_set_ref_calibration(sensor, &saved_ref_calibration));
ESP_ERROR_CHECK(vl53l0x_set_offset_calibration(sensor, saved_offset_um));
ESP_ERROR_CHECK(vl53l0x_set_xtalk_calibration(sensor, saved_xtalk_mcps));
ESP_ERROR_CHECK(vl53l0x_set_xtalk_compensation_enable(sensor, true));
```

This approach avoids running a full factory calibration at every device startup.

### When to repeat calibration

- After a hardware reset, you must run basic sensor initialization again with `vl53l0x_init()`.
- It makes sense to repeat `vl53l0x_perform_ref_calibration()` when temperature changes noticeably, roughly **8°C**.
- Repeat `vl53l0x_perform_offset_calibration()` when mechanics, power supply, optics, sensor position, or accuracy requirements change.
- Repeat `vl53l0x_perform_xtalk_calibration()` when the glass changes, the air gap changes, the window becomes dirty, or the optical configuration changes in any other way.
- All calibrations are blocking and must be performed before starting continuous measurement or after stopping it with `vl53l0x_stop_measurement()`.

### Minimal practical scenario

If you already have saved calibration data, startup can be as simple as this:

```c
// Minimal scenario when saved calibration data is available
ESP_ERROR_CHECK(vl53l0x_create(&sensor, bus));
ESP_ERROR_CHECK(vl53l0x_init(sensor));

// Restore parameters from Flash/NVS (without recalibration)
ESP_ERROR_CHECK(vl53l0x_set_reference_spads(sensor, &saved_ref_spad_calibration));
ESP_ERROR_CHECK(vl53l0x_set_ref_calibration(sensor, &saved_ref_calibration));
ESP_ERROR_CHECK(vl53l0x_set_offset_calibration(sensor, saved_offset_um));
ESP_ERROR_CHECK(vl53l0x_set_xtalk_calibration(sensor, saved_xtalk_mcps));
ESP_ERROR_CHECK(vl53l0x_set_xtalk_compensation_enable(sensor, true));

// Set the profile and operating mode
ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_DEFAULT));
ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
// Set the interval between measurements (100 ms)
ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 100));
// Start measurements
ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));
```

## Operating profiles

In this library, a measurement profile is a set of parameters that configures the balance between speed, accuracy, and maximum distance. Conceptually, it matches the examples from STMicroelectronics documentation; in code, the easiest way is to apply a ready-made preset with `vl53l0x_set_profile()`.

All profile parameters must be set **before** starting measurement with `vl53l0x_start_measurement()`.

### Available profiles

| Profile | Timing budget | Typical scenario | Notes |
| --- | ---: | --- | --- |
| `VL53L0X_PROFILE_DEFAULT` | `30000 us` | Standard conditions | Balance between speed and accuracy |
| `VL53L0X_PROFILE_HIGH_ACCURACY` | `200000 us` | Precision measurements | Higher accuracy, lower update rate |
| `VL53L0X_PROFILE_LONG_RANGE` | `33000 us` | Range up to ~2 m | Works better with low ambient light and good optics |
| `VL53L0X_PROFILE_HIGH_SPEED` | `20000 us` | Frequent updates | Minimum latency, lower accuracy |

### How a profile works

A profile is not a separate sensor mode, but a combination of settings:

- `timing budget` — how much time the sensor spends on one measurement;
- `limit check` — thresholds used to filter noisy or weak results.

For most tasks, it is enough to call `vl53l0x_set_profile()` and then select the measurement mode with `vl53l0x_set_mode()`.

For `Long range`, signal and noise limits are usually loosened additionally. This allows weaker reflections at longer distances to be detected, but may increase value scatter and the number of erroneous measurements.

### Usage examples

#### Default profile

```c
// Set the default profile (balance between speed and accuracy)
ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_DEFAULT));
ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 100));
ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));
```

#### High accuracy

```c
// Set the high-accuracy profile (long measurement time, lower scatter)
ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_HIGH_ACCURACY));
ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 100));
ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));
```

#### High speed

```c
// Set the high-speed profile (minimum measurement time, more scatter)
ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_HIGH_SPEED));
ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 20));  // More frequent updates
ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));
```

#### Long range

```c
// Set the long-range profile (enhanced reception for weak reflections)
ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_LONG_RANGE));
ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 100));
ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));
```

### When manual parameter tuning is needed

If the ready-made profiles are not enough, parameters can be tuned manually. In that case, focus on two groups of settings:

1. **Measurement timing budget** — the larger the value, the higher the result stability usually is.
2. **Limit checks** — for long-range measurements, these are often loosened, especially for `Long range`.

Example of manual tuning for long-range mode:

```c
// Manually tune parameters for specific conditions
// Set the measurement timing budget (33000 us)
ESP_ERROR_CHECK(vl53l0x_set_timing_budget(sensor, 33000));

// Configure limit checks for long-range measurements
// Enable signal-rate check and set a lower threshold (0.1 Mcps)
ESP_ERROR_CHECK(vl53l0x_set_limit_check_enable(sensor, VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, true));
ESP_ERROR_CHECK(vl53l0x_set_limit_check_value(sensor, VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, 0.1f));
// Enable sigma check and set the threshold to 60.0
ESP_ERROR_CHECK(vl53l0x_set_limit_check_enable(sensor, VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, true));
ESP_ERROR_CHECK(vl53l0x_set_limit_check_value(sensor, VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, 60.0f));

// Start measurements after configuring parameters
ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));
```

## Changing the I2C address

By default, the VL53L0X sensor responds at address `0x52`. If multiple sensors must be used on one bus, each of them needs to be assigned a unique I2C address.

The address change must be performed **immediately after the sensor exits reset and finishes booting**, but **before** calling `vl53l0x_init()`.

The project uses the `vl53l0x_set_address()` function for this. It already performs the required write to the sensor, so no separate platform calls for changing the address are needed.

### How to use `XSHUT`

The `XSHUT` pin is an active-low input. It puts the sensor into hardware standby, effectively holding it in reset.

**Note:** The presence of a pull-up resistor on the `XSHUT` pin depends on the specific VL53L0X board. Some modules, for example ST breakout boards, already have this resistor installed, usually 10 kOhm; others do not. If your board has no pull-up, connect an external 10 kOhm resistor between `XSHUT` and `VDD`. I2C pins (`SDA`/`SCL`) always require external pull-up resistors, usually 4.7 kOhm.

1. Hold **all** sensors in reset by driving all `XSHUT` pins `LOW`.
2. Drive only **one** sensor `HIGH`.
3. Wait for the sensor to finish booting. `tBOOT` is up to **1.2 ms**.
4. Create a sensor object and call `vl53l0x_set_address()` with the new address.
5. Repeat the procedure for the next sensor.

After assigning a unique address, each sensor can be initialized and used separately on the same shared I2C bus.

### Example

```c
#define XSHUT_PIN GPIO_NUM_2

// Function for hardware sensor reset through the XSHUT pin
static void vl53l0x_hard_reset(void)
{
    // Configure the XSHUT pin as an output
    gpio_set_direction(XSHUT_PIN, GPIO_MODE_OUTPUT);
    // Hold in reset (LOW) for 2 ms
    gpio_set_level(XSHUT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    // Exit reset (HIGH) and wait 2 ms
    gpio_set_level(XSHUT_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

// Perform hardware reset
vl53l0x_hard_reset();

// Create a sensor descriptor and set a new I2C address (0x20)
// The address must be unique on the bus and must not conflict with other devices
vl53l0x_handle_t sensor = NULL;
ESP_ERROR_CHECK(vl53l0x_create(&sensor, bus));
ESP_ERROR_CHECK(vl53l0x_set_address(sensor, bus, 0x20));
ESP_ERROR_CHECK(vl53l0x_init(sensor));
```


## Operating modes

## Single measurement (blocking, Single ranging)

Single measurement is a mode where the sensor performs one complete measurement cycle and immediately returns the result in `vl53l0x_data_t`.

This project uses `vl53l0x_single_measure(sensor, &data)` for this. It is convenient when you need to get one distance value “on demand” and do not want to keep the sensor in continuous operating mode.

### How it works

`vl53l0x_single_measure()` performs the measurement as a blocking operation: the call will not finish until the sensor prepares the result. After that, the function fills the result structure and returns an error code.

Internally, this call organizes the full single-measurement cycle:

1. starts measurement,
2. waits for data readiness,
3. reads the result,
4. resets the measurement state for the next call.

### When to use

This mode is suitable if:

- you need a simple “measure → process → wait → measure again” scenario;
- there is no need to keep the sensor in continuous measurement all the time;
- code simplicity is the priority.

### Example

```c
// Function for printing the measurement result to the console
static void print_data(const vl53l0x_data_t *data)
{
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

// Infinite measurement loop
while (true) {
    // Create and zero-initialize the result structure
    vl53l0x_data_t r = {0};

    // Perform a single measurement (blocking operation)
    esp_err_t ret = vl53l0x_single_measure(sensor, &r);
    if (ret == ESP_OK) {
        // If the measurement succeeded, print the result
        print_data(&r);
    }

    // Pause for 500 ms before the next measurement
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

If you need a continuous stream of measurements with less overhead, it is better to use `Continuous ranging` mode.

## Manual readiness polling

### How it works

In manual readiness polling mode, you check yourself whether data from the sensor is ready. Data readiness is checked in a loop using `vl53l0x_get_ready()`.

This approach gives more control over the data acquisition process, but requires constant polling of the sensor state. It is less efficient than using interrupts because the processor spends resources waiting.

### When to use

Manual readiness polling can be useful when:

- other tasks must be performed while waiting for a measurement;
- interrupts cannot be used, for example due to limits on the number of available GPIO pins;
- synchronization with other processes in the system is required.

However, in most cases it is recommended to use interrupts for data-ready notifications, because this is more efficient in terms of CPU resource usage.

### Example

A working example is shown below.

```c
#include <stdbool.h>
#include <stdint.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#include "vl53l0x.h"

static const char *TAG = "VL53L0X_EXAMPLE";

// I2C connection pins (change for your circuit)
#define I2C_PORT_NUM      I2C_NUM_0
#define I2C_SDA_PIN       GPIO_NUM_1
#define I2C_SCL_PIN       GPIO_NUM_0
#define XSHUT_PIN         GPIO_NUM_2

// Function for hardware sensor reset through the XSHUT pin
static void vl53l0x_hard_reset(void)
{
    // Configure the XSHUT pin as an output
    gpio_set_direction(XSHUT_PIN, GPIO_MODE_OUTPUT);
    // Hold in reset (LOW) for 2 ms
    gpio_set_level(XSHUT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    // Exit reset (HIGH) and wait 2 ms
    gpio_set_level(XSHUT_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

// Function for printing the measurement result
static void print_data(const vl53l0x_data_t *data)
{
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

void app_main(void)
{
    // Perform hardware sensor reset
    vl53l0x_hard_reset();

    // Create and initialize the I2C bus
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    // Create the sensor descriptor and initialize it
    vl53l0x_handle_t sensor = NULL;
    ESP_ERROR_CHECK(vl53l0x_create(&sensor, bus));
    ESP_ERROR_CHECK(vl53l0x_init(sensor));

    // Perform calibration
    ESP_ERROR_CHECK(vl53l0x_perform_ref_calibration(sensor, &(vl53l0x_ref_calibration_t){}));

    // Set the measurement profile and single measurement mode
    ESP_ERROR_CHECK(vl53l0x_set_profile(sensor, VL53L0X_PROFILE_DEFAULT));
    ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_SINGLE));

    // Structure for storing the result
    vl53l0x_data_t data = {0};

    // Main loop: start measurements with manual readiness polling
    while (true) {
        // Start a single measurement
        ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));

        bool ready = false;
        // Poll result readiness
        while(true) {
            ESP_ERROR_CHECK(vl53l0x_get_ready(sensor, &ready));
            if (ready) {
                break;
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        // Get the measurement result
        ESP_ERROR_CHECK(vl53l0x_get_data(sensor, &data));
        print_data(&data);

        // Pause for 1000 ms before the next measurement
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Important notes

- This example uses single measurement mode, `VL53L0X_MODE_SINGLE`.
- The polling interval in `vTaskDelay` can be adjusted depending on application requirements.
- With this approach, the processor spends resources continuously polling the sensor, which is less efficient than using interrupts.
- Manual polling is suitable for simple applications or when interrupts are unavailable.

## Single measurement (non-blocking, interrupt-driven)

This variant performs exactly one measurement, but the host does not wait for the result in an active polling loop. Instead, the sensor raises an interrupt on `GPIO1`, and the ESP32 task “wakes up” only when data is ready.

This approach is convenient when you need to save energy and avoid occupying the CPU while waiting for a measurement to finish.

### How it works

The general sequence is:

1. configure the sensor interrupt output with `vl53l0x_set_gpio_config()`;
2. start measurement with `vl53l0x_start_measurement(sensor)`;
3. the ESP32 waits for the signal from `GPIO1`;
4. after the interrupt, the result is read with `vl53l0x_get_data(sensor, &data)`;
5. the interrupt is cleared by calling `vl53l0x_clear_interrupt_mask(sensor)`.

This example uses a FreeRTOS task notification for waiting: the ISR calls `vTaskNotifyGiveFromISR()`, and the task waits with `ulTaskNotifyTake()`.

### Working example

A scenario fragment is shown below. It assumes that `sensor` has already been created and initialized.

```c
#define VL53L0X_GPIO_INT GPIO_NUM_12  // Change for your circuit

// Sensor interrupt handler (runs in ISR context)
static void IRAM_ATTR vl53l0x_gpio_isr(void *arg)
{
    // Get the task handle that needs to be woken
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Wake the task through a FreeRTOS notification
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);

    // If a higher-priority task was woken,
    // request a context switch
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// GPIO initialization for connection to the sensor interrupt output
static void gpio_interrupt_init(void)
{
    // Get the current task handle (this task will be woken from the ISR)
    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // Configure the interrupt pin as input
    gpio_set_direction(VL53L0X_GPIO_INT, GPIO_MODE_INPUT);
    // Disable internal pulls because the module usually already has an external pull-up
    gpio_pullup_dis(VL53L0X_GPIO_INT);
    gpio_pulldown_dis(VL53L0X_GPIO_INT);
    // Interrupt on falling edge (active LOW)
    gpio_set_intr_type(VL53L0X_GPIO_INT, GPIO_INTR_NEGEDGE);

    // Install the interrupt service and register the handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(VL53L0X_GPIO_INT, vl53l0x_gpio_isr, task);
}

// Function for printing the measurement result
static void print_data(const vl53l0x_data_t *data)
{
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

void app_main(void)
{
    // Structure for storing the measurement result
    vl53l0x_data_t data = {0};

    // Set single measurement mode
    ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_SINGLE));
    // Configure GPIO1 to generate an interrupt when a new measurement is ready
    // Use LOW because GPIO1 is pulled HIGH on the sensor board
    ESP_ERROR_CHECK(vl53l0x_set_gpio_config(
        sensor,
        VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY,
        VL53L0X_INTERRUPT_POLARITY_LOW));

    // Initialize GPIO for handling interrupts from the sensor
    gpio_interrupt_init();

    while (true) {
        // Start measurement (does not block execution)
        ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));

        // Wait for the interrupt from the sensor (the task sleeps until the event)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read the measurement result
        ESP_ERROR_CHECK(vl53l0x_get_data(sensor, &data));
        print_data(&data);

        // Clear the interrupt flag so that subsequent events can be received
        ESP_ERROR_CHECK(vl53l0x_clear_interrupt_mask(sensor));
    }
}
```

### When to use

This mode is suitable if:

- you need one measurement without active polling;
- the task can sleep until data is ready;
- saving CPU time or energy is important;
- the project already handles external interrupts.

### Important notes

- After `vl53l0x_start_measurement()`, the sensor must be configured for data-ready interrupts.
- After reading the result, always call `vl53l0x_clear_interrupt_mask(sensor)`, otherwise the next interrupt may not arrive.
- In this example, the measurement is still single: after each event, the loop starts it again.

## Continuous measurement (Continuous ranging)

This mode is suitable when you need a stream of measurements without manually restarting each measurement. Unlike single mode, the sensor continues producing new results on its own, while the host only:

1. starts continuous mode with `vl53l0x_start_measurement(sensor)`;
2. waits for a data-ready event;
3. reads the result with `vl53l0x_get_data(sensor, &data)`;
4. clears the interrupt with `vl53l0x_clear_interrupt_mask(sensor)`;
5. waits for the next measurement.

Important: in this mode, the sensor effectively runs continuously: it keeps performing new measurements, and the IR laser emitter remains active almost all the time. This is convenient for a fast data stream, but can noticeably increase energy consumption compared to single modes.

In the example below, the sensor GPIO1 is configured for the `VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY` event, and the ESP32 task wakes through a FreeRTOS notification. This is convenient when you want to minimize polling and get the result only when it is already ready.

### Working example

A scenario fragment is shown below. It assumes that `sensor` has already been created and initialized.

```c
#define VL53L0X_GPIO_INT GPIO_NUM_12  // Change for your circuit

// Sensor interrupt handler (runs in ISR context)
static void IRAM_ATTR vl53l0x_gpio_isr(void *arg)
{
    // Get the task handle that needs to be woken
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Wake the task through a FreeRTOS notification
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);

    // If a higher-priority task was woken,
    // request a context switch
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// GPIO initialization for connection to the sensor interrupt output
static void gpio_interrupt_init(void)
{
    // Get the current task handle (this task will be woken from the ISR)
    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // Configure the interrupt pin as input
    gpio_set_direction(VL53L0X_GPIO_INT, GPIO_MODE_INPUT);
    // Disable internal pulls because the module usually already has an external pull-up
    gpio_pullup_dis(VL53L0X_GPIO_INT);
    gpio_pulldown_dis(VL53L0X_GPIO_INT);
    // Interrupt on falling edge (active LOW)
    gpio_set_intr_type(VL53L0X_GPIO_INT, GPIO_INTR_NEGEDGE);

    // Install the interrupt service and register the handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(VL53L0X_GPIO_INT, vl53l0x_gpio_isr, task);
}

// Function for printing the measurement result
static void print_data(const vl53l0x_data_t *data)
{
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

void app_main(void)
{
    // Structure for storing the result
    vl53l0x_data_t data = {0};

    // Enable continuous mode
    ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS));
    // GPIO1 will signal readiness of a new measurement
    ESP_ERROR_CHECK(vl53l0x_set_gpio_config(
        sensor,
        VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY,
        VL53L0X_INTERRUPT_POLARITY_LOW));

    // Configure interrupt handling from GPIO1
    gpio_interrupt_init();
    // Start the measurement stream
    ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));

    while (true) {
        // Wait for the interrupt from the sensor (the task sleeps until the event)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read the ready result
        ESP_ERROR_CHECK(vl53l0x_get_data(sensor, &data));
        print_data(&data);

        // Clear the interrupt flag to receive the next event
        ESP_ERROR_CHECK(vl53l0x_clear_interrupt_mask(sensor));
    }
}
```

### Important notes

- `vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS)` enables a continuous measurement stream.
- `vl53l0x_set_gpio_config(..., VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY, ...)` makes GPIO1 a signal for new-result readiness.
- After reading the result, always call `vl53l0x_clear_interrupt_mask(sensor)`, otherwise the next event may not arrive.
- If you need a fixed period between measurements, `Continuous Timed` is usually more suitable.

## Continuous measurement at time intervals (Continuous Timed)

`Continuous Timed` mode is similar to regular `Continuous`, but measurements are started **at a specified time interval**. This is convenient when you need a stable polling period without manually restarting each measurement.

The main practical difference is that the **IR laser turns on only for measurement windows**, and between them the sensor waits for the next interval. Because of this, the mode is usually more energy-efficient than continuous measurement without pauses.

The workflow is:

1. initialize the sensor and I2C;
2. select a profile and `VL53L0X_MODE_CONTINUOUS_TIMED` mode;
3. set the period between measurements with `vl53l0x_set_inter_measurement(sensor, ...)`;
4. configure GPIO1 for the `VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY` event;
5. start measurements;
6. wait for an interrupt, read the result, and clear the interrupt mask.

A scenario fragment is shown below. It assumes that `sensor` has already been created and initialized.

```c
#define VL53L0X_GPIO_INT GPIO_NUM_12  // Change for your circuit

// Sensor interrupt handler (runs in ISR context)
static void IRAM_ATTR vl53l0x_gpio_isr(void *arg)
{
    // Get the task handle that needs to be woken
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Wake the task through a FreeRTOS notification
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);

    // If a higher-priority task was woken,
    // request a context switch
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// GPIO initialization for connection to the sensor interrupt output
static void gpio_interrupt_init(void)
{
    // Get the current task handle (this task will be woken from the ISR)
    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // Configure the interrupt pin as input
    gpio_set_direction(VL53L0X_GPIO_INT, GPIO_MODE_INPUT);
    // Disable internal pulls because the module usually already has an external pull-up
    gpio_pullup_dis(VL53L0X_GPIO_INT);
    gpio_pulldown_dis(VL53L0X_GPIO_INT);
    // Interrupt on falling edge (active LOW)
    gpio_set_intr_type(VL53L0X_GPIO_INT, GPIO_INTR_NEGEDGE);

    // Install the interrupt service and register the handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(VL53L0X_GPIO_INT, vl53l0x_gpio_isr, task);
}

// Function for printing the measurement result
static void print_data(const vl53l0x_data_t *data)
{
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

void app_main(void)
{
    // Structure for storing the result
    vl53l0x_data_t data = {0};

    // Enable continuous measurements with an interval
    ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
    // Interval between measurements: 1000 ms
    ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 1000));
    // GPIO1 will signal readiness of a new measurement
    ESP_ERROR_CHECK(vl53l0x_set_gpio_config(
        sensor,
        VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY,
        VL53L0X_INTERRUPT_POLARITY_LOW));

    // Configure interrupt handling from GPIO1
    gpio_interrupt_init();
    // Start measurements
    ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));

    while (true) {
        // Wait for the interrupt from the sensor
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read the ready result
        ESP_ERROR_CHECK(vl53l0x_get_data(sensor, &data));
        print_data(&data);

        // Clear the interrupt flag
        ESP_ERROR_CHECK(vl53l0x_clear_interrupt_mask(sensor));
    }
}
```

### Important notes

- `vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED)` enables continuous measurements with a specified interval.
- `vl53l0x_set_inter_measurement(sensor, 1000)` sets the period between measurements in milliseconds.
- `vl53l0x_set_gpio_config(..., VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY, ...)` allows the task to be woken when a new result is ready.
- After reading the result, always call `vl53l0x_clear_interrupt_mask(sensor)`, otherwise the next event may not arrive.

If you need a measurement stream with pauses between measurements, `Continuous Timed` is usually more convenient and more energy-efficient than regular `Continuous`.


## Threshold measurements by interrupt

This mode is needed when the sensor should not merely report that a new measurement is ready, but should signal only when specified distance limits are reached. Then the host can “sleep” and wake only at the right moment: for example, when an object comes closer than a specified threshold, leaves an allowed window, or when a new measurement needs to be processed without constant polling.

Two settings are used for this:

1. `vl53l0x_set_interrupt_thresholds(sensor, low_mm, high_mm)` — sets the lower and upper limits.
2. `vl53l0x_set_gpio_config(sensor, function, polarity)` — selects which event activates the `GPIO1` output.

### Interrupt modes

The library provides several interrupt output modes:

- `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_LOW` — interrupt on lower threshold. Triggers when the measured distance becomes less than `thresh_low`.
- `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_HIGH` — interrupt on upper threshold. Triggers when the distance becomes greater than `thresh_high`.
- `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_OUT` — “outside window” interrupt. Triggers if the value is below the lower limit or above the upper limit: `value < thresh_low || value > thresh_high`.
- `VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY` — interrupt when a new measurement is ready. This mode is useful if you only need a signal when another measurement has completed.

Threshold logic usually uses `THRESHOLD_CROSSED_LOW`, `THRESHOLD_CROSSED_HIGH`, or `THRESHOLD_CROSSED_OUT`.

### Important considerations

- In single measurement mode (`VL53L0X_MODE_SINGLE`), the maximum programmable threshold value is limited to **254 mm**.
- In continuous modes (`VL53L0X_MODE_CONTINUOUS` and `VL53L0X_MODE_CONTINUOUS_TIMED`), thresholds can be greater than 254 mm.
- If upper-threshold mode is selected and no target is detected, the interrupt may not arrive.
- After handling an event, the interrupt must be cleared with `vl53l0x_clear_interrupt_mask(sensor)`, otherwise the next event will not trigger.
- If an external handler on `GPIO1` is used, the sensor itself must already be switched to the required mode and measurement must be started.

### Typical workflow

1. Initialize I2C and create a sensor object.
2. Perform calibrations and select a measurement profile.
3. Set thresholds with `vl53l0x_set_interrupt_thresholds()`.
4. Configure `GPIO1` with `vl53l0x_set_gpio_config()`.
5. Set up the interrupt handler on the ESP32 side.
6. Start measurement with `vl53l0x_start_measurement()`.
7. Wait for the signal from the sensor.
8. Read the result with `vl53l0x_get_data()`.
9. Clear the interrupt flag with `vl53l0x_clear_interrupt_mask()`.

### Working example

A scenario fragment is shown below. It assumes that `sensor` has already been created and initialized.

```c
#define VL53L0X_GPIO_INT GPIO_NUM_12  // Change for your circuit

// Sensor interrupt handler (runs in ISR context)
static void IRAM_ATTR vl53l0x_gpio_isr(void *arg)
{
    // Get the task handle that needs to be woken
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Wake the task through a FreeRTOS notification
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);

    // If a higher-priority task was woken,
    // request a context switch
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// GPIO initialization for connection to the sensor interrupt output
static void gpio_interrupt_init(void)
{
    // Get the current task handle (this task will be woken from the ISR)
    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // Configure the interrupt pin as input
    gpio_set_direction(VL53L0X_GPIO_INT, GPIO_MODE_INPUT);
    // Disable internal pulls because the module usually already has an external pull-up
    gpio_pullup_dis(VL53L0X_GPIO_INT);
    gpio_pulldown_dis(VL53L0X_GPIO_INT);
    // Interrupt on falling edge (active LOW)
    gpio_set_intr_type(VL53L0X_GPIO_INT, GPIO_INTR_NEGEDGE);

    // Install the interrupt service and register the handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(VL53L0X_GPIO_INT, vl53l0x_gpio_isr, task);
}

// Function for printing the measurement result
static void print_data(const vl53l0x_data_t *data) 
{
    ESP_LOGI(TAG,
        "distance=%u mm, valid=%s, status=%u (%s), signal=%.3f Mcps, ambient=%.3f Mcps",
        (unsigned)data->distance_mm,
        data->valid ? "true" : "false",
        (unsigned)data->range_status,
        vl53l0x_range_status_str(data->range_status),
        (double)data->signal_rate_mcps,
        (double)data->ambient_rate_mcps);
}

void app_main(void)
{
    // Structure for storing the result
    vl53l0x_data_t data = {0};

    // Continuous mode with a 250 ms interval
    ESP_ERROR_CHECK(vl53l0x_set_mode(sensor, VL53L0X_MODE_CONTINUOUS_TIMED));
    ESP_ERROR_CHECK(vl53l0x_set_inter_measurement(sensor, 250));
    // Lower threshold 400 mm: event when distance is below the threshold
    ESP_ERROR_CHECK(vl53l0x_set_interrupt_thresholds(sensor, 400.0, 0.0));
    // GPIO1 is activated when the lower limit is crossed
    ESP_ERROR_CHECK(vl53l0x_set_gpio_config(
        sensor,
        VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_LOW,
        VL53L0X_INTERRUPT_POLARITY_LOW));

    // Configure interrupt handling from GPIO1
    gpio_interrupt_init();
    // Start measurements
    ESP_ERROR_CHECK(vl53l0x_start_measurement(sensor));

    while (true) {
        // Wait for the threshold event to trigger
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read the measurement result
        ESP_ERROR_CHECK(vl53l0x_get_data(sensor, &data));
        print_data(&data);

        // Clear the interrupt flag
        ESP_ERROR_CHECK(vl53l0x_clear_interrupt_mask(sensor));
    }
}
```

### How to read this example

- `vl53l0x_set_interrupt_thresholds(sensor, 400.0, 0.0)` sets the lower trigger threshold to 400 mm.
- `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_LOW` enables reaction to lower-limit crossing.
- `VL53L0X_INTERRUPT_POLARITY_LOW` is selected because the `GPIO1` interrupt output is active low. This is possible because on most VL53L0X boards, `GPIO1` is already pulled up to the supply through a 10 kOhm resistor. If your board has no pull-up, add it to the circuit.

### When to use

This mode is suitable if:

- you need to track an object approaching or moving away by a specified boundary;
- the system should sleep until an important event occurs;
- polling is unsuitable because of CPU load or power consumption requirements;
- it is important to process only meaningful changes, not every measurement in sequence.

### Important notes

- For “less than threshold” logic, use `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_LOW`.
- For “greater than threshold” logic, use `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_HIGH`.
- For inside/outside window monitoring, use `VL53L0X_GPIO_FUNCTION_THRESHOLD_CROSSED_OUT`.
- After handling an event, always call `vl53l0x_clear_interrupt_mask(sensor)`.
- If you only need a data-ready signal rather than threshold logic, choose `VL53L0X_GPIO_FUNCTION_NEW_MEASURE_READY`.

## API

### Initialization and deinitialization

- `esp_err_t vl53l0x_create(vl53l0x_handle_t *out_sensor, i2c_master_bus_handle_t bus)` - Initializes and calibrates a VL53L0X instance
- `esp_err_t vl53l0x_init(vl53l0x_handle_t sensor)` - Initializes the sensor
- `esp_err_t vl53l0x_destroy(vl53l0x_handle_t sensor)` - Stops measurements, if running, and destroys the descriptor
- `esp_err_t vl53l0x_reset(vl53l0x_handle_t sensor)` - Resets the sensor and waits for the device reboot to complete
- `esp_err_t vl53l0x_set_address(vl53l0x_handle_t sensor, i2c_master_bus_handle_t bus, uint8_t address)` - Sets the sensor I2C address

### Measurements

- `esp_err_t vl53l0x_single_measure(vl53l0x_handle_t sensor, vl53l0x_data_t *data)` - Performs a single distance measurement (blocking function)
- `esp_err_t vl53l0x_set_mode(vl53l0x_handle_t sensor, vl53l0x_mode_t mode)` - Sets the measurement mode (single, continuous, timed)
- `esp_err_t vl53l0x_start_measurement(vl53l0x_handle_t sensor)` - Starts measurement (non-blocking function)
- `esp_err_t vl53l0x_stop_measurement(vl53l0x_handle_t sensor, uint32_t timeout_ms)` - Stops the current measurement and waits for the stop to complete
- `esp_err_t vl53l0x_get_ready(vl53l0x_handle_t sensor, bool *ready)` - Checks measurement data readiness
- `esp_err_t vl53l0x_get_data(vl53l0x_handle_t sensor, vl53l0x_data_t *data)` - Gets distance measurement data

### Calibration

- `esp_err_t vl53l0x_perform_ref_spad_management(vl53l0x_handle_t sensor, vl53l0x_ref_spad_calibration_t *out)` - Performs blocking reference SPAD management and returns values for host-side storage
- `esp_err_t vl53l0x_set_reference_spads(vl53l0x_handle_t sensor, const vl53l0x_ref_spad_calibration_t *calibration)` - Applies previously saved reference SPAD calibration data
- `esp_err_t vl53l0x_get_reference_spads(vl53l0x_handle_t sensor, vl53l0x_ref_spad_calibration_t *out)` - Reads current reference SPAD calibration data
- `esp_err_t vl53l0x_perform_ref_calibration(vl53l0x_handle_t sensor, vl53l0x_ref_calibration_t *out)` - Performs blocking VHV/phase calibration and returns values for host-side storage
- `esp_err_t vl53l0x_set_ref_calibration(vl53l0x_handle_t sensor, const vl53l0x_ref_calibration_t *calibration)` - Applies previously saved VHV/phase calibration data
- `esp_err_t vl53l0x_get_ref_calibration(vl53l0x_handle_t sensor, vl53l0x_ref_calibration_t *out)` - Reads current VHV/phase calibration data
- `esp_err_t vl53l0x_perform_offset_calibration(vl53l0x_handle_t sensor, float calibration_distance_mm, int32_t *out_offset_um)` - Performs blocking offset calibration at a known target distance
- `esp_err_t vl53l0x_set_offset_calibration(vl53l0x_handle_t sensor, int32_t offset_um)` - Applies previously saved measurement offset calibration in micrometers
- `esp_err_t vl53l0x_get_offset_calibration(vl53l0x_handle_t sensor, int32_t *offset_um)` - Reads current measurement offset calibration in micrometers
- `esp_err_t vl53l0x_perform_xtalk_calibration(vl53l0x_handle_t sensor, float calibration_distance_mm, float *out_xtalk_compensation_rate_mcps)` - Performs blocking crosstalk calibration at a known target distance
- `esp_err_t vl53l0x_set_xtalk_calibration(vl53l0x_handle_t sensor, float xtalk_compensation_rate_mcps)` - Applies previously saved crosstalk compensation rate in MCPS and enables compensation
- `esp_err_t vl53l0x_get_xtalk_calibration(vl53l0x_handle_t sensor, float *xtalk_compensation_rate_mcps)` - Reads current crosstalk compensation rate in MCPS
- `esp_err_t vl53l0x_set_xtalk_compensation_enable(vl53l0x_handle_t sensor, bool enable)` - Enables or disables crosstalk compensation
- `esp_err_t vl53l0x_get_xtalk_compensation_enable(vl53l0x_handle_t sensor, bool *enabled)` - Reads crosstalk compensation enable state

### Profile and parameter settings

- `esp_err_t vl53l0x_set_profile(vl53l0x_handle_t sensor, vl53l0x_profile_t profile)` - Applies one of the predefined profiles
- `esp_err_t vl53l0x_set_timing_budget(vl53l0x_handle_t sensor, uint32_t us)` - Sets the measurement timing budget in microseconds
- `esp_err_t vl53l0x_get_timing_budget(vl53l0x_handle_t sensor, uint32_t *us)` - Gets the current measurement timing budget in microseconds
- `esp_err_t vl53l0x_set_inter_measurement(vl53l0x_handle_t sensor, uint32_t ms)` - Sets the period between measurements in milliseconds
- `esp_err_t vl53l0x_get_inter_measurement(vl53l0x_handle_t sensor, uint32_t *ms)` - Gets the period between measurements in milliseconds

### Limit checks

- `esp_err_t vl53l0x_set_limit_check_enable(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, bool enable)` - Enables/disables the selected limit check
- `esp_err_t vl53l0x_get_limit_check_enable(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, bool *enabled)` - Reads the enable state of the selected limit check
- `esp_err_t vl53l0x_set_limit_check_value(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, float value)` - Sets the value of the selected limit check, in float units
- `esp_err_t vl53l0x_get_limit_check_value(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, float *value)` - Gets the configured value of the selected limit check
- `esp_err_t vl53l0x_get_limit_check_current(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, float *value)` - Gets the current measured value of the selected limit check
- `esp_err_t vl53l0x_get_limit_check_status(vl53l0x_handle_t sensor, vl53l0x_limit_check_id_t id, bool *passed)` - Gets pass/fail status of the selected limit check for the last sample
- `esp_err_t vl53l0x_set_range_ignore_threshold(vl53l0x_handle_t sensor, bool enable, float threshold_mcps)` - Convenience wrapper for controlling the range-ignore threshold
- `esp_err_t vl53l0x_get_range_ignore_threshold(vl53l0x_handle_t sensor, bool *enabled, float *threshold_mcps)` - Reads the range-ignore threshold state and value

### Interrupt settings

- `esp_err_t vl53l0x_clear_interrupt_mask(vl53l0x_handle_t sensor)` - Clears the interrupt mask
- `esp_err_t vl53l0x_set_interrupt_thresholds(vl53l0x_handle_t sensor, float threshold_low_mm, float threshold_high_mm)` - Sets interrupt thresholds, lower and upper, in millimeters
- `esp_err_t vl53l0x_set_gpio_config(vl53l0x_handle_t sensor, vl53l0x_gpio_function_t functionality, vl53l0x_interrupt_polarity_t polarity)` - Configures interrupt GPIO settings

### Helper functions

- `const char *vl53l0x_range_status_str(uint8_t status)` - Converts a range status code to human-readable text

## License

[MIT](./LICENSE)
