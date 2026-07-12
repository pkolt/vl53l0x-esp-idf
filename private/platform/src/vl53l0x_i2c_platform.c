/*
 * COPYRIGHT (C) STMicroelectronics 2014. All rights reserved.
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
 * \file   VL53L0X_i2c_platform.c
 * \brief  Code function definitions for ESP32 ESP-IDF platform
 *
 */

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

#include "vl53l0x_types.h"
#include "vl53l0x_i2c_platform.h"

#define I2C_XFER_TIMEOUT_MS            1000

#define STATUS_OK                      0x00
#define STATUS_FAIL                    0x01

static const char *TAG = "VL53L0X_I2C";

uint32_t VL53L0X_GetTickCount(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

int32_t VL53L0X_write_multi(i2c_master_dev_handle_t dev_handle,
                            uint8_t index,
                            uint8_t *pdata,
                            int32_t count)
{
    if ((dev_handle == NULL) || (pdata == NULL) || (count <= 0)) {
        return STATUS_FAIL;
    }

    if (count > COMMS_BUFFER_SIZE) {
        ESP_LOGE(TAG, "I2C write size too large: %ld", (long)count);
        return STATUS_FAIL;
    }

    uint8_t tx_buffer[COMMS_BUFFER_SIZE + 1U];
    tx_buffer[0] = index;
    memcpy(&tx_buffer[1], pdata, (size_t)count);

    esp_err_t ret = i2c_master_transmit(dev_handle, tx_buffer, (size_t)count + 1U, I2C_XFER_TIMEOUT_MS);
    if (ret == ESP_OK) {
        return STATUS_OK;
    }

    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    return STATUS_FAIL;
}

int32_t VL53L0X_read_multi(i2c_master_dev_handle_t dev_handle,
                           uint8_t index,
                           uint8_t *pdata,
                           int32_t count)
{
    if ((dev_handle == NULL) || (pdata == NULL) || (count <= 0)) {
        return STATUS_FAIL;
    }

    if (count > COMMS_BUFFER_SIZE) {
        ESP_LOGE(TAG, "I2C read size too large: %ld", (long)count);
        return STATUS_FAIL;
    }

    const uint8_t reg = index;
    esp_err_t ret = i2c_master_transmit_receive(
        dev_handle,
        &reg,
        1,
        pdata,
        (size_t)count,
        I2C_XFER_TIMEOUT_MS);

    if (ret == ESP_OK) {
        return STATUS_OK;
    }

    ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    return STATUS_FAIL;
}

int32_t VL53L0X_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t index, uint8_t data)
{
    return VL53L0X_write_multi(dev_handle, index, &data, 1);
}

int32_t VL53L0X_write_word(i2c_master_dev_handle_t dev_handle, uint8_t index, uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(data >> 8);
    buf[1] = (uint8_t)(data & 0xFF);
    return VL53L0X_write_multi(dev_handle, index, buf, 2);
}

int32_t VL53L0X_write_dword(i2c_master_dev_handle_t dev_handle,
                            uint8_t index,
                            uint32_t data)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)((data >> 24) & 0xFF);
    buf[1] = (uint8_t)((data >> 16) & 0xFF);
    buf[2] = (uint8_t)((data >> 8) & 0xFF);
    buf[3] = (uint8_t)(data & 0xFF);
    return VL53L0X_write_multi(dev_handle, index, buf, 4);
}

int32_t VL53L0X_read_byte(i2c_master_dev_handle_t dev_handle, uint8_t index, uint8_t *data)
{
    return VL53L0X_read_multi(dev_handle, index, data, 1);
}

int32_t VL53L0X_read_word(i2c_master_dev_handle_t dev_handle, uint8_t index, uint16_t *data)
{
    uint8_t buf[2];
    int32_t ret = VL53L0X_read_multi(dev_handle, index, buf, 2);
    if ((ret == STATUS_OK) && (data != NULL)) {
        *data = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
    }
    return ret;
}

int32_t VL53L0X_read_dword(i2c_master_dev_handle_t dev_handle, uint8_t index, uint32_t *data)
{
    uint8_t buf[4];
    int32_t ret = VL53L0X_read_multi(dev_handle, index, buf, 4);
    if ((ret == STATUS_OK) && (data != NULL)) {
        *data = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) |
                (uint32_t)buf[3];
    }
    return ret;
}

int32_t VL53L0X_wait_ms(int32_t wait_ms)
{
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    return STATUS_OK;
}
