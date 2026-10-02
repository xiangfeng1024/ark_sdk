/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
 *
 * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
 *
 * This source code, together with its associated documentation, examples,
 * configuration files, and related materials, is collectively referred to
 * as the "Software".
 *
 * Subject to the complete terms set forth in the LICENSE file, Ark Crew
 * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
 * right to access, reproduce, and modify the Software solely for personal
 * study, classroom education, academic research, and non-commercial evaluation.
 *
 * Commercial use of the Software, in whole or in part, is strictly prohibited
 * without prior written authorization from Ark Crew. Prohibited activities
 * include, without limitation, sale, sublicensing, paid distribution, use in
 * paid consulting or training, incorporation into any commercial product or
 * service, and internal development intended for commercial deployment.
 *
 * Except for the limited rights expressly granted under the applicable
 * License, no license or other right, whether express, implied, by estoppel,
 * or otherwise, is granted under any copyright, patent, trademark, trade
 * secret, mask work, or other intellectual property right belonging to
 * Ark Crew or any third party.
 *
 * Delivery or disclosure of the Software does not convey permission to use
 * the Ark Crew name, trademarks, logos, visual identity, or other branding,
 * except where strictly necessary to preserve the original attribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
 * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
 * NON-INFRINGEMENT.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
 * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
 *
 * This notice shall be retained in all authorized copies or substantial
 * portions of the Software. Removal, concealment, or unauthorized alteration
 * of this notice is prohibited.
 *
 * See the LICENSE file in the root directory of this repository for the
 * complete and controlling license terms.
 */

#include "icm20602.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_gpio.h"
#include "ark_hal_i2c.h"

typedef struct {
    ark_hal_i2c_id_t i2c_id;
    uint8_t address;
    ark_hal_gpio_config_t INT;
} icm20602_config_t;

#define ICM20602_REG_SMPLRT_DIV 0x19U
#define ICM20602_REG_CONFIG 0x1AU
#define ICM20602_REG_GYRO_CONFIG 0x1BU
#define ICM20602_REG_ACCEL_CONFIG 0x1CU
#define ICM20602_REG_ACCEL_CONFIG2 0x1DU
#define ICM20602_REG_INT_PIN_CFG 0x37U
#define ICM20602_REG_INT_ENABLE 0x38U
#define ICM20602_REG_INT_STATUS 0x3AU
#define ICM20602_REG_ACCEL_XOUT_H 0x3BU
#define ICM20602_REG_PWR_MGMT_1 0x6BU
#define ICM20602_REG_PWR_MGMT_2 0x6CU
#define ICM20602_REG_WHO_AM_I 0x75U

#define ICM20602_WHO_AM_I_VALUE 0x12U
#define ICM20602_COMPATIBLE_WHO_AM_I_VALUE 0x2EU
#define ICM20602_TRANSFER_TIMEOUT_MS 20U
#define ICM20602_RESET_DELAY_MS 100U
#define ICM20602_GYRO_STARTUP_MS 100U
#define ICM20602_SELF_TEST_BUDGET_MS 100U
#define ICM20602_SAMPLE_SIZE 14U
#define ICM20602_GYRO_CALIBRATION_SAMPLES 64U
#define ICM20602_GYRO_CALIBRATION_ATTEMPTS 192U
#define ICM20602_RAD_TO_DEG 57.2957795F
#define ICM20602_FILTER_TIME_CONSTANT_S 0.5F
#define ICM20602_SAMPLE_FREQUENCY_HZ 200.0F
#define ICM20602_STATIONARY_ENTER_GYRO_MDPS 2000L
#define ICM20602_STATIONARY_EXIT_GYRO_MDPS 5000L
#define ICM20602_STATIONARY_ENTER_ACCEL_MIN_MG 850L
#define ICM20602_STATIONARY_ENTER_ACCEL_MAX_MG 1150L
#define ICM20602_STATIONARY_EXIT_ACCEL_MIN_MG 750L
#define ICM20602_STATIONARY_EXIT_ACCEL_MAX_MG 1250L
#define ICM20602_STATIONARY_CONFIRM_SAMPLES 100U
#define ICM20602_MOTION_CONFIRM_SAMPLES 10U
#define ICM20602_BIAS_FILTER_DIVISOR 1024L
#define ICM20602_TASK_STACK_DEPTH 384U

#define ICM20602_PWR_RESET 0x80U
#define ICM20602_CLOCK_PLL 0x01U
#define ICM20602_SAMPLE_DIV_200_HZ 0x04U
#define ICM20602_GYRO_DLPF_41_HZ 0x03U
#define ICM20602_GYRO_500_DPS 0x08U
#define ICM20602_ACCEL_4_G 0x08U
#define ICM20602_ACCEL_DLPF_44_HZ 0x03U
#define ICM20602_INT_ACTIVE_HIGH_PULSE 0x00U
#define ICM20602_DATA_READY_ENABLE 0x01U

static StaticSemaphore_t data_ready_control;
static SemaphoreHandle_t data_ready_semaphore;
static volatile uint32_t interrupt_count;
static icm20602_diagnostics_t icm20602_diagnostics;
static icm20602_motion_t icm20602_motion;
static float icm20602_euler_deg[3];
static int32_t icm20602_previous_gyro_mdps[3];
static int32_t icm20602_bias_error_accumulator[3];
static uint32_t icm20602_motion_samples;
static TickType_t icm20602_previous_sample_tick;
static bool icm20602_fusion_initialized;
static TaskHandle_t icm20602_task_handle;
static icm20602_config_t icm20602_config;

static bool icm20602_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "icm20602");
    uint8_t i2c_id;
    uint32_t address;

    if ((node == NULL) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_I2C, &i2c_id) != 0) ||
        (ark_of_property_read_u32(node, "reg", &address) != 0) ||
        (address > 0x7FU) ||
        (ark_of_get_named_gpio(node, "int-gpios", 0U,
                              &icm20602_config.INT) != 0)) {
        return false;
    }
    icm20602_config.i2c_id = (ark_hal_i2c_id_t)i2c_id;
    icm20602_config.address = (uint8_t)address;
    return true;
}

static void icm20602_task(void *argument) {
    (void)argument;

    printf("[icm20602] keep sensor stationary; calibrating gyro...\r\n");
    if (!icm20602_calibrate()) {
        printf("[icm20602] continuing without startup gyro bias\r\n");
    }
    for (;;) {
        icm20602_update();
    }
}

static int32_t icm20602_round_float(float value) {
    return (int32_t)(value + ((value >= 0.0F) ? 0.5F : -0.5F));
}

static int32_t icm20602_abs_i32(int32_t value) {
    return (value < 0L) ? -value : value;
}

static int16_t icm20602_decode_i16(const uint8_t *bytes) {
    return (int16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static bool icm20602_write_register(uint8_t register_address, uint8_t value) {
    return ark_hal_i2c.mem_write(
               icm20602_config.i2c_id,
               icm20602_config.address,
               register_address,
               &value,
               1U,
               ICM20602_TRANSFER_TIMEOUT_MS) == 1;
}

static bool icm20602_read_register(uint8_t register_address, uint8_t *value) {
    return ark_hal_i2c.mem_read(
               icm20602_config.i2c_id,
               icm20602_config.address,
               register_address,
               value,
               1U,
               ICM20602_TRANSFER_TIMEOUT_MS) == 1;
}

static bool icm20602_identity_supported(uint8_t who_am_i) {
    return (who_am_i == ICM20602_WHO_AM_I_VALUE) ||
           (who_am_i == ICM20602_COMPATIBLE_WHO_AM_I_VALUE);
}

static void icm20602_data_ready_irq(void *context) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    ++interrupt_count;
    if (data_ready_semaphore != NULL) {
        xSemaphoreGiveFromISR(
            data_ready_semaphore,
            &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static ark_component_result_t icm20602_init(void) {
    uint8_t interrupt_status;

    memset(&icm20602_diagnostics, 0, sizeof(icm20602_diagnostics));
    memset(&icm20602_motion, 0, sizeof(icm20602_motion));
    memset(icm20602_euler_deg, 0, sizeof(icm20602_euler_deg));
    memset(icm20602_previous_gyro_mdps, 0, sizeof(icm20602_previous_gyro_mdps));
    memset(icm20602_bias_error_accumulator, 0, sizeof(icm20602_bias_error_accumulator));
    icm20602_motion_samples = 0U;
    icm20602_previous_sample_tick = xTaskGetTickCount();
    icm20602_fusion_initialized = false;
    interrupt_count = 0U;
    data_ready_semaphore = xSemaphoreCreateBinaryStatic(&data_ready_control);
    if ((data_ready_semaphore == NULL) ||
        (icm20602_config.address < 0x68U) ||
        (icm20602_config.address > 0x69U) ||
        !ark_hal_i2c.is_ready(icm20602_config.i2c_id) ||
        !ark_hal_i2c.is_device_ready(
            icm20602_config.i2c_id,
            icm20602_config.address,
            ICM20602_TRANSFER_TIMEOUT_MS) ||
        !ark_hal_gpio.register_irq(
            icm20602_config.INT.port,
            icm20602_config.INT.pin,
            icm20602_data_ready_irq,
            NULL) ||
        !icm20602_write_register(ICM20602_REG_PWR_MGMT_1, ICM20602_PWR_RESET)) {
        return ARK_COMPONENT_ERROR;
    }
    vTaskDelay(pdMS_TO_TICKS(ICM20602_RESET_DELAY_MS));
    if (!icm20602_write_register(ICM20602_REG_PWR_MGMT_1, ICM20602_CLOCK_PLL)) {
        return ARK_COMPONENT_ERROR;
    }
    vTaskDelay(pdMS_TO_TICKS(ICM20602_GYRO_STARTUP_MS));
    if (!icm20602_write_register(ICM20602_REG_PWR_MGMT_2, 0x00U) ||
        !icm20602_write_register(ICM20602_REG_SMPLRT_DIV, ICM20602_SAMPLE_DIV_200_HZ) ||
        !icm20602_write_register(ICM20602_REG_CONFIG, ICM20602_GYRO_DLPF_41_HZ) ||
        !icm20602_write_register(ICM20602_REG_GYRO_CONFIG, ICM20602_GYRO_500_DPS) ||
        !icm20602_write_register(ICM20602_REG_ACCEL_CONFIG, ICM20602_ACCEL_4_G) ||
        !icm20602_write_register(ICM20602_REG_ACCEL_CONFIG2, ICM20602_ACCEL_DLPF_44_HZ) ||
        !icm20602_write_register(ICM20602_REG_INT_PIN_CFG, ICM20602_INT_ACTIVE_HIGH_PULSE) ||
        !icm20602_read_register(ICM20602_REG_INT_STATUS, &interrupt_status) ||
        !icm20602_write_register(ICM20602_REG_INT_ENABLE, ICM20602_DATA_READY_ENABLE) ||
        !icm20602_read_register(ICM20602_REG_WHO_AM_I, &icm20602_diagnostics.who_am_i) ||
        !icm20602_identity_supported(icm20602_diagnostics.who_am_i)) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static ark_component_result_t icm20602_self_test(void) {
    icm20602_sample_t sample;
    uint8_t who_am_i = 0U;

    if (!icm20602_read_register(ICM20602_REG_WHO_AM_I, &who_am_i) ||
        !icm20602_identity_supported(who_am_i) ||
        !icm20602_read_sample(&sample)) {
        return ARK_COMPONENT_ERROR;
    }
    if ((icm20602_task_handle == NULL) &&
        (xTaskCreate(
             icm20602_task,
             "icm20602",
             ICM20602_TASK_STACK_DEPTH,
             NULL,
             tskIDLE_PRIORITY + 2U,
             &icm20602_task_handle) != pdPASS)) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static const ark_component_t icm20602_component = {
    .name = "icm20602",
    .init = icm20602_init,
    .self_test = icm20602_self_test,
    .self_test_expected_ms = ICM20602_SELF_TEST_BUDGET_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

#if ARK_DTS_HAS_UART_CLI
static int icm20602_scan_cli(int argc, char *argv[]) {
    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    icm20602_scan();
    return 0;
}

static ark_cli_command_t icm20602_scan_command = {
    .name = "imu_scan",
    .usage = "imu_scan",
    .description = "scan ICM20602 addresses and registers",
    .handler = icm20602_scan_cli,
};
#endif

bool icm20602_register(void) {
    if (!icm20602_load_config()) {
        return false;
    }
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&icm20602_scan_command);
#endif
    return ark_component_register(&icm20602_component);
}

bool icm20602_wait_data_ready(uint32_t timeout_ms) {
    return (data_ready_semaphore != NULL) &&
           (xSemaphoreTake(
                data_ready_semaphore,
                pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

bool icm20602_read_sample(icm20602_sample_t *sample) {
    uint8_t sample_data[ICM20602_SAMPLE_SIZE];
    int32_t read_result;
    size_t axis;

    if (sample == NULL) {
        return false;
    }
    read_result = ark_hal_i2c.mem_read(
        icm20602_config.i2c_id,
        icm20602_config.address,
        ICM20602_REG_ACCEL_XOUT_H,
        sample_data,
        sizeof(sample_data),
        ICM20602_TRANSFER_TIMEOUT_MS);
    if ((read_result != (int32_t)sizeof(sample_data)) &&
        ark_hal_i2c.recover(
            icm20602_config.i2c_id,
            ICM20602_TRANSFER_TIMEOUT_MS)) {
        ++icm20602_diagnostics.bus_recoveries;
        read_result = ark_hal_i2c.mem_read(
            icm20602_config.i2c_id,
            icm20602_config.address,
            ICM20602_REG_ACCEL_XOUT_H,
            sample_data,
            sizeof(sample_data),
            ICM20602_TRANSFER_TIMEOUT_MS);
    }
    if (read_result != (int32_t)sizeof(sample_data)) {
        ++icm20602_diagnostics.read_failures;
        return false;
    }
    for (axis = 0U; axis < 3U; ++axis) {
        sample->accel_raw[axis] = icm20602_decode_i16(&sample_data[axis * 2U]);
        sample->gyro_raw[axis] = icm20602_decode_i16(&sample_data[8U + axis * 2U]);
        sample->accel_mg[axis] =
            (int32_t)sample->accel_raw[axis] * 1000L / 8192L;
        sample->gyro_mdps[axis] =
            (int32_t)sample->gyro_raw[axis] * 2000L / 131L;
    }
    sample->temperature_raw = icm20602_decode_i16(&sample_data[6]);
    sample->temperature_cdeg = 2500L + (int32_t)sample->temperature_raw * 1000L / 3268L;
    ++icm20602_diagnostics.sample_count;
    return true;
}

bool icm20602_calibrate(void) {
    int64_t gyro_sum_mdps[3] = {0, 0, 0};
    uint32_t accepted_sample_count = 0U;
    uint32_t attempt_count = 0U;
    size_t axis;

    while ((accepted_sample_count < ICM20602_GYRO_CALIBRATION_SAMPLES) &&
           (attempt_count < ICM20602_GYRO_CALIBRATION_ATTEMPTS)) {
        icm20602_sample_t sample;

        ++attempt_count;
        if (!icm20602_wait_data_ready(ICM20602_TRANSFER_TIMEOUT_MS) ||
            !icm20602_read_sample(&sample)) {
            continue;
        }
        for (axis = 0U; axis < 3U; ++axis) {
            gyro_sum_mdps[axis] += sample.gyro_mdps[axis];
        }
        ++accepted_sample_count;
    }
    if (accepted_sample_count != ICM20602_GYRO_CALIBRATION_SAMPLES) {
        return false;
    }
    taskENTER_CRITICAL();
    for (axis = 0U; axis < 3U; ++axis) {
        icm20602_motion.gyro_bias_mdps[axis] =
            (int32_t)(gyro_sum_mdps[axis] / (int64_t)accepted_sample_count);
    }
    icm20602_motion.calibrated = true;
    taskEXIT_CRITICAL();
    icm20602_previous_sample_tick = xTaskGetTickCount();
    return true;
}

bool icm20602_update(void) {
    icm20602_sample_t sample;
    icm20602_motion_t updated;
    TickType_t sample_tick;
    float dt_seconds;
    float accel_y;
    float accel_z;
    float roll_accel;
    float pitch_accel;
    float alpha;
    int32_t corrected_gyro_mdps[3];
    int32_t gyro_limit;
    int32_t accel_min;
    int32_t accel_max;
    int64_t accel_norm_squared;
    bool stationary_candidate = true;
    bool bias_update_allowed = false;
    bool first_update;
    size_t axis;

    if (!icm20602_wait_data_ready(ICM20602_TRANSFER_TIMEOUT_MS)) {
        ++icm20602_diagnostics.data_ready_timeouts;
        return false;
    }
    if (!icm20602_read_sample(&sample)) {
        return false;
    }
    sample_tick = xTaskGetTickCount();
    dt_seconds = (float)(sample_tick - icm20602_previous_sample_tick) /
                 (float)configTICK_RATE_HZ;
    icm20602_previous_sample_tick = sample_tick;
    if ((dt_seconds <= 0.0F) || (dt_seconds > 0.1F)) {
        dt_seconds = 1.0F / ICM20602_SAMPLE_FREQUENCY_HZ;
    }
    accel_y = (float)sample.accel_raw[1];
    accel_z = (float)sample.accel_raw[2];
    roll_accel = atan2f(accel_y, accel_z) * ICM20602_RAD_TO_DEG;
    pitch_accel = atan2f(
                      -(float)sample.accel_raw[0],
                      sqrtf(accel_y * accel_y + accel_z * accel_z)) *
                  ICM20602_RAD_TO_DEG;
    alpha = ICM20602_FILTER_TIME_CONSTANT_S /
            (ICM20602_FILTER_TIME_CONSTANT_S + dt_seconds);

    taskENTER_CRITICAL();
    updated = icm20602_motion;
    taskEXIT_CRITICAL();
    gyro_limit = updated.stationary ? ICM20602_STATIONARY_EXIT_GYRO_MDPS :
                                      ICM20602_STATIONARY_ENTER_GYRO_MDPS;
    accel_min = updated.stationary ? ICM20602_STATIONARY_EXIT_ACCEL_MIN_MG :
                                     ICM20602_STATIONARY_ENTER_ACCEL_MIN_MG;
    accel_max = updated.stationary ? ICM20602_STATIONARY_EXIT_ACCEL_MAX_MG :
                                     ICM20602_STATIONARY_ENTER_ACCEL_MAX_MG;
    accel_norm_squared =
        (int64_t)sample.accel_mg[0] * sample.accel_mg[0] +
        (int64_t)sample.accel_mg[1] * sample.accel_mg[1] +
        (int64_t)sample.accel_mg[2] * sample.accel_mg[2];
    for (axis = 0U; axis < 3U; ++axis) {
        corrected_gyro_mdps[axis] =
            sample.gyro_mdps[axis] - updated.gyro_bias_mdps[axis];
        if (icm20602_abs_i32(corrected_gyro_mdps[axis]) > gyro_limit) {
            stationary_candidate = false;
        }
    }
    if ((accel_norm_squared < (int64_t)accel_min * accel_min) ||
        (accel_norm_squared > (int64_t)accel_max * accel_max)) {
        stationary_candidate = false;
    }
    if (updated.stationary) {
        if (stationary_candidate) {
            icm20602_motion_samples = 0U;
            bias_update_allowed = true;
        } else {
            if (icm20602_motion_samples < ICM20602_MOTION_CONFIRM_SAMPLES) {
                ++icm20602_motion_samples;
            }
            if (icm20602_motion_samples >= ICM20602_MOTION_CONFIRM_SAMPLES) {
                updated.stationary_samples = 0U;
                updated.stationary = false;
                icm20602_motion_samples = 0U;
            }
        }
    } else {
        icm20602_motion_samples = 0U;
        if (stationary_candidate) {
            if (updated.stationary_samples < ICM20602_STATIONARY_CONFIRM_SAMPLES) {
                ++updated.stationary_samples;
            }
            if (updated.stationary_samples >= ICM20602_STATIONARY_CONFIRM_SAMPLES) {
                updated.stationary = true;
                updated.calibrated = true;
                bias_update_allowed = true;
            }
        } else {
            updated.stationary_samples = 0U;
        }
    }
    if (updated.stationary && bias_update_allowed) {
        for (axis = 0U; axis < 3U; ++axis) {
            icm20602_bias_error_accumulator[axis] +=
                sample.gyro_mdps[axis] - updated.gyro_bias_mdps[axis];
            updated.gyro_bias_mdps[axis] +=
                icm20602_bias_error_accumulator[axis] /
                ICM20602_BIAS_FILTER_DIVISOR;
            icm20602_bias_error_accumulator[axis] %= ICM20602_BIAS_FILTER_DIVISOR;
        }
    }
    if (updated.stationary) {
        for (axis = 0U; axis < 3U; ++axis) {
            corrected_gyro_mdps[axis] = 0L;
        }
    }
    first_update = !icm20602_fusion_initialized;
    if (first_update) {
        icm20602_euler_deg[0] = roll_accel;
        icm20602_euler_deg[1] = pitch_accel;
        icm20602_euler_deg[2] = 0.0F;
        icm20602_fusion_initialized = true;
    } else {
        icm20602_euler_deg[0] = alpha *
            (icm20602_euler_deg[0] +
             ((float)corrected_gyro_mdps[0] / 1000.0F) *
             dt_seconds) +
            (1.0F - alpha) * roll_accel;
        icm20602_euler_deg[1] = alpha *
            (icm20602_euler_deg[1] +
             ((float)corrected_gyro_mdps[1] / 1000.0F) *
             dt_seconds) +
            (1.0F - alpha) * pitch_accel;
        icm20602_euler_deg[2] +=
            ((float)corrected_gyro_mdps[2] / 1000.0F) *
            dt_seconds;
    }
    if (icm20602_euler_deg[2] > 180.0F) {
        icm20602_euler_deg[2] -= 360.0F;
    } else if (icm20602_euler_deg[2] < -180.0F) {
        icm20602_euler_deg[2] += 360.0F;
    }
    updated.raw = sample;
    for (axis = 0U; axis < 3U; ++axis) {
        int32_t corrected_gyro = corrected_gyro_mdps[axis];

        updated.euler_cdeg[axis] =
            icm20602_round_float(icm20602_euler_deg[axis] * 100.0F);
        updated.gyro_mdps[axis] = corrected_gyro;
        updated.angular_accel_mdps2[axis] = first_update ? 0L : icm20602_round_float(
            (float)(corrected_gyro - icm20602_previous_gyro_mdps[axis]) / dt_seconds);
        icm20602_previous_gyro_mdps[axis] = corrected_gyro;
    }
    ++updated.update_count;
    taskENTER_CRITICAL();
    icm20602_motion = updated;
    taskEXIT_CRITICAL();
    return true;
}

bool icm20602_get_motion(icm20602_motion_t *motion) {
    if (motion == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *motion = icm20602_motion;
    taskEXIT_CRITICAL();
    return true;
}

bool icm20602_get_diagnostics(icm20602_diagnostics_t *diagnostics) {
    if (diagnostics == NULL) {
        return false;
    }
    *diagnostics = icm20602_diagnostics;
    diagnostics->irq_count = interrupt_count;
    return true;
}

void icm20602_scan(void) {
    static const uint8_t registers[] = {
        ICM20602_REG_WHO_AM_I,
        ICM20602_REG_PWR_MGMT_1,
        ICM20602_REG_SMPLRT_DIV,
        ICM20602_REG_CONFIG,
        ICM20602_REG_GYRO_CONFIG,
        ICM20602_REG_ACCEL_CONFIG,
        ICM20602_REG_ACCEL_CONFIG2,
        ICM20602_REG_INT_PIN_CFG,
        ICM20602_REG_INT_ENABLE,
    };
    static const char *const names[] = {
        "WHO", "PWR1", "DIV", "CFG", "GYRO", "ACCEL", "ACCEL2", "INTCFG", "INTEN",
    };
    uint8_t address;

    for (address = 0x68U; address <= 0x69U; ++address) {
        size_t register_index;

        if (!ark_hal_i2c.is_device_ready(
                icm20602_config.i2c_id,
                address,
                ICM20602_TRANSFER_TIMEOUT_MS)) {
            printf("ICM20602 address 0x%02X: no response\r\n", address);
            continue;
        }
        printf("ICM20602 address 0x%02X:", address);
        for (register_index = 0U;
             register_index < sizeof(registers);
             ++register_index) {
            uint8_t value = 0U;
            int32_t result = ark_hal_i2c.mem_read(
                icm20602_config.i2c_id,
                address,
                registers[register_index],
                &value,
                1U,
                ICM20602_TRANSFER_TIMEOUT_MS);

            if (result == 1) {
                printf(" %s=%02X", names[register_index], value);
            } else {
                printf(" %s=ERR", names[register_index]);
            }
        }
        printf("\r\n");
    }
}
