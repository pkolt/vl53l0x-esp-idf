/*
 * COPYRIGHT (C) STMicroelectronics 2015. All rights reserved.
 *
 * This software is the confidential and proprietary information of
 * STMicroelectronics ("Confidential Information").  You shall not
 * disclose such Confidential Information and shall use it only in
 * accordance with the terms of the license agreement you entered into
 * with STMicroelectronics
 *
 * Programming Golden Rule: Keep it Simple!
 *
 */

/*!
 * \file   VL53L0X_platform_log.c
 * \brief  Platform logging implementation for ESP-IDF
 *
 */

#include <stdarg.h>
#include <stdint.h>

#include "esp_log.h"
#include "vl53l0x_i2c_platform.h"
#include "vl53l0x_platform_log.h"

static const char *TAG = "VL53L0X_LOG";

char debug_string[VL53L0X_MAX_STRING_LENGTH_PLT];

uint32_t _trace_level = TRACE_LEVEL_WARNING;
uint32_t _trace_modules = TRACE_MODULE_NONE;
uint32_t _trace_functions = TRACE_FUNCTION_NONE;



static esp_log_level_t vl53l0x_trace_to_esp_level(uint32_t level)
{
    switch (level) {
        case TRACE_LEVEL_ERRORS:
            return ESP_LOG_ERROR;
        case TRACE_LEVEL_WARNING:
            return ESP_LOG_WARN;
        case TRACE_LEVEL_INFO:
            return ESP_LOG_INFO;
        case TRACE_LEVEL_DEBUG:
        case TRACE_LEVEL_ALL:
            return ESP_LOG_DEBUG;
        case TRACE_LEVEL_NONE:
        case TRACE_LEVEL_IGNORE:
        default:
            return ESP_LOG_VERBOSE;
    }
}

void trace_print_module_function(uint32_t module, uint32_t level, uint32_t function, const char *format, ...)
{
    if ((((level <= _trace_level) && ((module & _trace_modules) > 0)) ||
         ((function & _trace_functions) > 0)) &&
        (format != NULL)) {
        va_list arg_list;
        va_start(arg_list, format);
        esp_log_writev(vl53l0x_trace_to_esp_level(level), TAG, format, arg_list);
        va_end(arg_list);
    }
}
