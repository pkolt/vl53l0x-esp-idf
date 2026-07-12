#pragma once

/** Minimum supported measurement timing budget (us). */
#define VL53L0X_TIMING_BUDGET_MIN_US                    20000U
/** Default measurement timing budget (us). */
#define VL53L0X_DEFAULT_TIMING_BUDGET_US                33000U
/** Default inter-measurement period for back-to-back mode (ms). */
#define VL53L0X_DEFAULT_INTER_MEASUREMENT_PERIOD_MS     0U

/** Default Sigma limit value (mm). */
#define VL53L0X_DEFAULT_SIGMA_LIMIT_MM                  18.0f
/** Default return signal-rate limit value (Mcps). */
#define VL53L0X_DEFAULT_SIGNAL_RATE_LIMIT_MCPS          0.25f
/** Default range-ignore threshold value (Mcps), disabled by default. */
#define VL53L0X_DEFAULT_RANGE_IGNORE_THRESHOLD_MCPS     0.0f

/** Default Sigma limit in FixPoint16.16 representation. */
#define VL53L0X_DEFAULT_SIGMA_LIMIT_FIXPOINT1616        (18U * 65536U)
/** Default signal-rate limit in FixPoint16.16 representation. */
#define VL53L0X_DEFAULT_SIGNAL_RATE_LIMIT_FIXPOINT1616  ((25U * 65536U) / 100U)

/** Default XTalk compensation switch value (disabled). */
#define VL53L0X_DEFAULT_XTALK_COMPENSATION_ENABLE       0U
/** Default timeout for stop-completion polling (ms). */
#define VL53L0X_DEFAULT_STOP_TIMEOUT_MS                 200U

/** Timing budget for High Accuracy profile (us). */
#define VL53L0X_PROFILE_HIGH_ACCURACY_TIMING_BUDGET_US 200000U
/** Timing budget for High Speed profile (us). */
#define VL53L0X_PROFILE_HIGH_SPEED_TIMING_BUDGET_US     20000U
/** Sigma limit for Long Range profile (mm). */
#define VL53L0X_PROFILE_LONG_RANGE_SIGMA_LIMIT_MM       60.0f
/** Signal-rate limit for Long Range profile (Mcps). */
#define VL53L0X_PROFILE_LONG_RANGE_SIGNAL_LIMIT_MCPS    0.10f

#define VL53L0X_I2C_DEFAULT_FREQ_HZ 400000 // 400KHz
#define VL53L0X_I2C_DEFAULT_ADDR_7BIT 0x29 // vl53l0x I2C address