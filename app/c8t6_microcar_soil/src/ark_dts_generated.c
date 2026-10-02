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
/* 由方舟 Studio Rust 工具生成，请勿手工修改。 */
#include "ark_dts_generated.h"

#include <stddef.h>
#include <stdint.h>
#include "ark_dts.h"
#include "ark_hal_bindings.h"
#include "adc.h"
#include "adc38_tracking.h"
#include "adc_sensor.h"
#include "beidou.h"
#include "bh1750.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "control.h"
#include "dht11.h"
#include "encoder.h"
#include "flash.h"
#include "flash_stm32f103.h"
#include "i2c.h"
#include "maixcam.h"
#include "motor.h"
#include "motor_tb6612.h"
#include "oled.h"
#include "servo.h"
#include "stream.h"
#include "tim.h"
#include "usart.h"
#include "ws2812b.h"

void *const ark_hal_adc_handles[2] = {
    &hadc1,
    NULL,
};

void *const ark_hal_can_handles[1] = {
    NULL,
};

void *const ark_hal_encoder_handles[2] = {
    &htim2,
    &htim4,
};

void *const ark_hal_i2c_handles[2] = {
    &hi2c1,
    NULL,
};

void *const ark_hal_pwm_handles[4] = {
    &htim1,
    &htim1,
    NULL,
    NULL,
};

void *const ark_hal_spi_handles[3] = {
    NULL,
    NULL,
    NULL,
};

void *const ark_hal_uart_handles[3] = {
    &huart1,
    &huart2,
    &huart3,
};

void *const ark_hal_watchdog_handle = NULL;
void *const ark_hal_usb_device_handle = NULL;

const uint32_t ark_hal_pwm_channels[4] = {
    TIM_CHANNEL_1,
    TIM_CHANNEL_4,
    0U,
    0U,
};

const ark_hal_soft_i2c_config_t ark_hal_soft_i2c_configs[1] = {{
    .scl = {.port = ARK_HAL_GPIO_PORT_A, .pin = 0U, .active_level = 0U},
    .sda = {.port = ARK_HAL_GPIO_PORT_A, .pin = 0U, .active_level = 0U},
    .delay_us = 0U,
    .deinit_hardware_i2c = false, .hardware_i2c_id = ARK_HAL_I2C_1,
}};

static const uint8_t ark_dts_data[] = {
    0x41U, 0x52U, 0x4BU, 0x20U, 0x43U, 0x52U, 0x45U, 0x57U, 0x20U, 0x4DU, 0x69U, 0x63U,
    0x72U, 0x6FU, 0x43U, 0x61U, 0x72U, 0x20U, 0x53U, 0x6FU, 0x69U, 0x6CU, 0x00U, 0x63U,
    0x38U, 0x74U, 0x36U, 0x5FU, 0x6DU, 0x69U, 0x63U, 0x72U, 0x6FU, 0x63U, 0x61U, 0x72U,
    0x5FU, 0x73U, 0x6FU, 0x69U, 0x6CU, 0x00U, 0x73U, 0x74U, 0x6DU, 0x33U, 0x32U, 0x66U,
    0x31U, 0x30U, 0x33U, 0x63U, 0x38U, 0x00U, 0x73U, 0x69U, 0x6DU, 0x70U, 0x6CU, 0x65U,
    0x2DU, 0x62U, 0x75U, 0x73U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x61U, 0x72U, 0x6BU,
    0x5FU, 0x68U, 0x61U, 0x6CU, 0x5FU, 0x67U, 0x70U, 0x69U, 0x6FU, 0x00U, 0x00U, 0x08U,
    0x01U, 0x40U, 0x00U, 0x04U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU,
    0x01U, 0x40U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x10U, 0x01U, 0x40U, 0x00U, 0x04U,
    0x00U, 0x00U, 0x61U, 0x72U, 0x6BU, 0x5FU, 0x68U, 0x61U, 0x6CU, 0x5FU, 0x74U, 0x69U,
    0x6DU, 0x65U, 0x00U, 0x61U, 0x72U, 0x6BU, 0x5FU, 0x68U, 0x61U, 0x6CU, 0x5FU, 0x61U,
    0x64U, 0x63U, 0x00U, 0x00U, 0x24U, 0x01U, 0x40U, 0x00U, 0x04U, 0x00U, 0x00U, 0x61U,
    0x64U, 0x63U, 0x5FU, 0x73U, 0x65U, 0x6EU, 0x73U, 0x6FU, 0x72U, 0x00U, 0x04U, 0x00U,
    0x00U, 0x00U, 0x6DU, 0x71U, 0x37U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U, 0x6DU, 0x71U,
    0x31U, 0x33U, 0x35U, 0x00U, 0x09U, 0x00U, 0x00U, 0x00U, 0x73U, 0x6FU, 0x69U, 0x6CU,
    0x00U, 0x61U, 0x72U, 0x6BU, 0x5FU, 0x68U, 0x61U, 0x6CU, 0x5FU, 0x65U, 0x6EU, 0x63U,
    0x6FU, 0x64U, 0x65U, 0x72U, 0x00U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x04U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U, 0x40U, 0x00U, 0x04U, 0x00U,
    0x00U, 0x61U, 0x72U, 0x6BU, 0x5FU, 0x68U, 0x61U, 0x6CU, 0x5FU, 0x70U, 0x77U, 0x6DU,
    0x00U, 0x61U, 0x72U, 0x6BU, 0x5FU, 0x68U, 0x61U, 0x6CU, 0x5FU, 0x69U, 0x32U, 0x63U,
    0x00U, 0x00U, 0x54U, 0x00U, 0x40U, 0x00U, 0x04U, 0x00U, 0x00U, 0x6FU, 0x6CU, 0x65U,
    0x64U, 0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x62U, 0x68U, 0x31U, 0x37U, 0x35U, 0x30U,
    0x00U, 0x23U, 0x00U, 0x00U, 0x00U, 0x61U, 0x72U, 0x6BU, 0x5FU, 0x68U, 0x61U, 0x6CU,
    0x5FU, 0x75U, 0x61U, 0x72U, 0x74U, 0x00U, 0x00U, 0x38U, 0x01U, 0x40U, 0x00U, 0x04U,
    0x00U, 0x00U, 0x73U, 0x74U, 0x72U, 0x65U, 0x61U, 0x6DU, 0x00U, 0x62U, 0x6CU, 0x75U,
    0x65U, 0x74U, 0x6FU, 0x6FU, 0x74U, 0x68U, 0x00U, 0x80U, 0x25U, 0x00U, 0x00U, 0x00U,
    0x44U, 0x00U, 0x40U, 0x00U, 0x04U, 0x00U, 0x00U, 0x6DU, 0x61U, 0x69U, 0x78U, 0x63U,
    0x61U, 0x6DU, 0x00U, 0x00U, 0xC2U, 0x01U, 0x00U, 0x00U, 0x48U, 0x00U, 0x40U, 0x00U,
    0x04U, 0x00U, 0x00U, 0x62U, 0x65U, 0x69U, 0x64U, 0x6FU, 0x75U, 0x00U, 0x64U, 0x68U,
    0x74U, 0x31U, 0x31U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x61U, 0x64U, 0x63U, 0x33U, 0x38U, 0x5FU, 0x74U, 0x72U,
    0x61U, 0x63U, 0x6BU, 0x69U, 0x6EU, 0x67U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x08U,
    0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x0DU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x6DU, 0x6FU, 0x74U, 0x6FU, 0x72U, 0x00U, 0x6DU, 0x6FU, 0x74U,
    0x6FU, 0x72U, 0x5FU, 0x74U, 0x62U, 0x36U, 0x36U, 0x31U, 0x32U, 0x00U, 0x07U, 0x00U,
    0x00U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x0DU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x0EU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x0FU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x65U, 0x6EU, 0x63U, 0x6FU, 0x64U, 0x65U,
    0x72U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U, 0x06U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU,
    0xFFU, 0xFFU, 0xDCU, 0x05U, 0x00U, 0x00U, 0x73U, 0x65U, 0x72U, 0x76U, 0x6FU, 0x00U,
    0x01U, 0x00U, 0x00U, 0x00U, 0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x77U, 0x73U, 0x32U, 0x38U, 0x31U, 0x32U, 0x62U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
    0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x73U, 0x6FU, 0x66U, 0x74U,
    0x77U, 0x61U, 0x72U, 0x65U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0x62U, 0x75U, 0x7AU,
    0x7AU, 0x65U, 0x72U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x66U, 0x6CU, 0x61U, 0x73U, 0x68U, 0x00U, 0x66U, 0x6CU,
    0x61U, 0x73U, 0x68U, 0x5FU, 0x73U, 0x74U, 0x6DU, 0x33U, 0x32U, 0x66U, 0x31U, 0x30U,
    0x33U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x80U, 0x00U, 0x00U, 0x00U, 0xA0U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x14U, 0x00U, 0x00U, 0x00U, 0xC8U, 0x00U,
    0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x00U, 0xD0U, 0x07U, 0x00U, 0x00U, 0xE8U, 0x03U,
    0x00U, 0x00U, 0x63U, 0x6FU, 0x6EU, 0x74U, 0x72U, 0x6FU, 0x6CU, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x5EU, 0x01U, 0x00U, 0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x18U, 0xFCU, 0xFFU, 0xFFU, 0xE8U, 0x03U, 0x00U, 0x00U, 0xC0U, 0xF2U,
    0xFCU, 0xFFU, 0x40U, 0x0DU, 0x03U, 0x00U, 0xB4U, 0x00U, 0x00U, 0x00U, 0x2CU, 0x01U,
    0x00U, 0x00U, 0x0AU, 0x00U, 0x00U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x58U, 0x00U,
    0x00U, 0x00U, 0x40U, 0x0AU, 0x00U, 0x00U, 0x7CU, 0x08U, 0x00U, 0x00U, 0x20U, 0x0CU,
    0x00U, 0x00U, 0xE0U, 0x07U, 0x00U, 0x00U, 0xE8U, 0x03U, 0x00U, 0x00U, 0xFBU, 0x07U,
    0x00U, 0x00U, 0x46U, 0x06U, 0x00U, 0x00U, 0xC4U, 0x05U, 0x00U, 0x00U, 0x59U, 0x00U,
    0x00U, 0x00U, 0x54U, 0x00U, 0x00U, 0x00U, 0x57U, 0x00U, 0x00U, 0x00U, 0x5CU, 0x00U,
    0x00U, 0x00U, 0x65U, 0x00U, 0x00U, 0x00U, 0x55U, 0x00U, 0x00U, 0x00U, 0x5DU, 0x00U,
    0x00U, 0x00U, 0x59U, 0x00U, 0x00U, 0x00U, 0xB0U, 0x04U, 0x00U, 0x00U, 0x20U, 0x03U,
    0x00U, 0x00U, 0x40U, 0x1FU, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0xA7U, 0x00U,
    0x00U, 0x00U, 0x78U, 0x05U, 0x00U, 0x00U, 0xA4U, 0x06U, 0x00U, 0x00U, 0x19U, 0x00U,
    0x00U, 0x00U, 0xFAU, 0x00U, 0x00U, 0x00U, 0xC4U, 0x09U, 0x00U, 0x00U, 0x5EU, 0x01U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xD4U, 0xFEU, 0xFFU, 0xFFU, 0x2CU, 0x01U,
    0x00U, 0x00U, 0xC0U, 0xF2U, 0xFCU, 0xFFU, 0x40U, 0x0DU, 0x03U, 0x00U, 0x00U, 0xFCU,
    0xFFU, 0xFFU, 0x9AU, 0xFDU, 0xFFU, 0xFFU, 0x33U, 0xFFU, 0xFFU, 0xFFU, 0xCDU, 0x00U,
    0x00U, 0x00U, 0x66U, 0x02U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0xD7U, 0x00U,
    0x00U, 0x00U, 0xF4U, 0x01U, 0x00U, 0x00U, 0xC2U, 0x01U, 0x00U, 0x00U, 0xBCU, 0x02U,
    0x00U, 0x00U, 0xDCU, 0x00U, 0x00U, 0x00U, 0x41U, 0x00U, 0x00U, 0x00U, 0xB8U, 0x0BU,
    0x00U, 0x00U,
};

static const ark_of_property_t ark_dts_properties[] = {
    {54993U, 0U, 23U, ARK_OF_PROP_STRING},
    {10916U, 23U, 31U, ARK_OF_PROP_STRING},
    {10916U, 54U, 11U, ARK_OF_PROP_STRING},
    {52956U, 65U, 4U, ARK_OF_PROP_U32},
    {19181U, 65U, 4U, ARK_OF_PROP_U32},
    {26644U, 69U, 0U, ARK_OF_PROP_BOOL},
    {10916U, 69U, 13U, ARK_OF_PROP_STRING},
    {50785U, 82U, 8U, ARK_OF_PROP_U32},
    {22506U, 69U, 0U, ARK_OF_PROP_BOOL},
    {43040U, 90U, 4U, ARK_OF_PROP_U32},
    {10916U, 69U, 13U, ARK_OF_PROP_STRING},
    {50785U, 94U, 8U, ARK_OF_PROP_U32},
    {22506U, 69U, 0U, ARK_OF_PROP_BOOL},
    {43040U, 90U, 4U, ARK_OF_PROP_U32},
    {10916U, 69U, 13U, ARK_OF_PROP_STRING},
    {50785U, 102U, 8U, ARK_OF_PROP_U32},
    {22506U, 69U, 0U, ARK_OF_PROP_BOOL},
    {43040U, 90U, 4U, ARK_OF_PROP_U32},
    {10916U, 110U, 13U, ARK_OF_PROP_STRING},
    {10916U, 123U, 12U, ARK_OF_PROP_STRING},
    {50785U, 135U, 8U, ARK_OF_PROP_U32},
    {57623U, 65U, 4U, ARK_OF_PROP_U32},
    {10916U, 143U, 11U, ARK_OF_PROP_STRING},
    {50785U, 154U, 4U, ARK_OF_PROP_U32},
    {57706U, 158U, 4U, ARK_OF_PROP_STRING},
    {10916U, 143U, 11U, ARK_OF_PROP_STRING},
    {50785U, 162U, 4U, ARK_OF_PROP_U32},
    {57706U, 166U, 6U, ARK_OF_PROP_STRING},
    {10916U, 143U, 11U, ARK_OF_PROP_STRING},
    {50785U, 172U, 4U, ARK_OF_PROP_U32},
    {57706U, 176U, 5U, ARK_OF_PROP_STRING},
    {10916U, 181U, 16U, ARK_OF_PROP_STRING},
    {50785U, 197U, 8U, ARK_OF_PROP_U32},
    {21693U, 205U, 4U, ARK_OF_PROP_U32},
    {10916U, 181U, 16U, ARK_OF_PROP_STRING},
    {50785U, 209U, 8U, ARK_OF_PROP_U32},
    {21693U, 205U, 4U, ARK_OF_PROP_U32},
    {10916U, 217U, 12U, ARK_OF_PROP_STRING},
    {18496U, 205U, 4U, ARK_OF_PROP_U32},
    {10916U, 217U, 12U, ARK_OF_PROP_STRING},
    {18496U, 205U, 4U, ARK_OF_PROP_U32},
    {10916U, 229U, 12U, ARK_OF_PROP_STRING},
    {50785U, 241U, 8U, ARK_OF_PROP_U32},
    {52956U, 65U, 4U, ARK_OF_PROP_U32},
    {19181U, 205U, 4U, ARK_OF_PROP_U32},
    {10916U, 249U, 5U, ARK_OF_PROP_STRING},
    {50785U, 254U, 4U, ARK_OF_PROP_U32},
    {10916U, 258U, 7U, ARK_OF_PROP_STRING},
    {50785U, 265U, 4U, ARK_OF_PROP_U32},
    {10916U, 269U, 13U, ARK_OF_PROP_STRING},
    {50785U, 282U, 8U, ARK_OF_PROP_U32},
    {10916U, 290U, 7U, ARK_OF_PROP_STRING},
    {28073U, 297U, 10U, ARK_OF_PROP_STRING},
    {34022U, 307U, 4U, ARK_OF_PROP_U32},
    {10916U, 297U, 10U, ARK_OF_PROP_STRING},
    {10916U, 269U, 13U, ARK_OF_PROP_STRING},
    {50785U, 311U, 8U, ARK_OF_PROP_U32},
    {10916U, 290U, 7U, ARK_OF_PROP_STRING},
    {28073U, 319U, 8U, ARK_OF_PROP_STRING},
    {34022U, 327U, 4U, ARK_OF_PROP_U32},
    {10916U, 319U, 8U, ARK_OF_PROP_STRING},
    {10916U, 269U, 13U, ARK_OF_PROP_STRING},
    {50785U, 331U, 8U, ARK_OF_PROP_U32},
    {10916U, 290U, 7U, ARK_OF_PROP_STRING},
    {28073U, 339U, 7U, ARK_OF_PROP_STRING},
    {34022U, 307U, 4U, ARK_OF_PROP_U32},
    {10916U, 339U, 7U, ARK_OF_PROP_STRING},
    {10916U, 346U, 6U, ARK_OF_PROP_STRING},
    {38559U, 352U, 12U, ARK_OF_PROP_U32},
    {10916U, 364U, 15U, ARK_OF_PROP_STRING},
    {7295U, 379U, 8U, ARK_OF_PROP_U32},
    {46958U, 387U, 36U, ARK_OF_PROP_U32},
    {10916U, 423U, 6U, ARK_OF_PROP_STRING},
    {10916U, 429U, 13U, ARK_OF_PROP_STRING},
    {44720U, 442U, 8U, ARK_OF_PROP_U32},
    {43243U, 450U, 12U, ARK_OF_PROP_U32},
    {12778U, 462U, 12U, ARK_OF_PROP_U32},
    {26851U, 474U, 12U, ARK_OF_PROP_U32},
    {30987U, 486U, 12U, ARK_OF_PROP_U32},
    {45549U, 65U, 4U, ARK_OF_PROP_U32},
    {36975U, 65U, 4U, ARK_OF_PROP_U32},
    {10916U, 498U, 8U, ARK_OF_PROP_STRING},
    {672U, 506U, 8U, ARK_OF_PROP_U32},
    {45549U, 514U, 4U, ARK_OF_PROP_U32},
    {36975U, 65U, 4U, ARK_OF_PROP_U32},
    {45589U, 518U, 4U, ARK_OF_PROP_U32},
    {10916U, 522U, 6U, ARK_OF_PROP_STRING},
    {26656U, 69U, 0U, ARK_OF_PROP_BOOL},
    {38559U, 528U, 12U, ARK_OF_PROP_U32},
    {10916U, 540U, 8U, ARK_OF_PROP_STRING},
    {38559U, 548U, 12U, ARK_OF_PROP_U32},
    {38821U, 560U, 9U, ARK_OF_PROP_STRING},
    {58204U, 569U, 4U, ARK_OF_PROP_U32},
    {10916U, 573U, 7U, ARK_OF_PROP_STRING},
    {24041U, 580U, 12U, ARK_OF_PROP_U32},
    {10916U, 592U, 6U, ARK_OF_PROP_STRING},
    {10916U, 598U, 16U, ARK_OF_PROP_STRING},
    {33715U, 614U, 4U, ARK_OF_PROP_U32},
    {6759U, 618U, 4U, ARK_OF_PROP_U32},
    {48393U, 622U, 4U, ARK_OF_PROP_U32},
    {4546U, 622U, 4U, ARK_OF_PROP_U32},
    {20797U, 626U, 4U, ARK_OF_PROP_U32},
    {33763U, 90U, 4U, ARK_OF_PROP_U32},
    {42678U, 65U, 4U, ARK_OF_PROP_U32},
    {21197U, 65U, 4U, ARK_OF_PROP_U32},
    {57298U, 90U, 4U, ARK_OF_PROP_U32},
    {6556U, 630U, 4U, ARK_OF_PROP_U32},
    {2046U, 634U, 4U, ARK_OF_PROP_U32},
    {31965U, 638U, 4U, ARK_OF_PROP_U32},
    {44023U, 630U, 4U, ARK_OF_PROP_U32},
    {13475U, 642U, 4U, ARK_OF_PROP_U32},
    {44510U, 646U, 4U, ARK_OF_PROP_U32},
    {10916U, 650U, 8U, ARK_OF_PROP_STRING},
    {9351U, 630U, 4U, ARK_OF_PROP_U32},
    {9835U, 658U, 32U, ARK_OF_PROP_U32},
    {35058U, 658U, 32U, ARK_OF_PROP_U32},
    {50849U, 69U, 0U, ARK_OF_PROP_BOOL},
    {16650U, 690U, 4U, ARK_OF_PROP_U32},
    {39434U, 694U, 4U, ARK_OF_PROP_U32},
    {40564U, 698U, 4U, ARK_OF_PROP_U32},
    {40426U, 205U, 4U, ARK_OF_PROP_U32},
    {41147U, 518U, 4U, ARK_OF_PROP_U32},
    {43943U, 702U, 4U, ARK_OF_PROP_U32},
    {24308U, 706U, 4U, ARK_OF_PROP_U32},
    {13821U, 710U, 32U, ARK_OF_PROP_U32},
    {4456U, 742U, 32U, ARK_OF_PROP_U32},
    {29380U, 90U, 4U, ARK_OF_PROP_U32},
    {18794U, 698U, 4U, ARK_OF_PROP_U32},
    {9351U, 630U, 4U, ARK_OF_PROP_U32},
    {47648U, 774U, 4U, ARK_OF_PROP_U32},
    {42839U, 778U, 4U, ARK_OF_PROP_U32},
    {36283U, 782U, 4U, ARK_OF_PROP_U32},
    {14536U, 786U, 4U, ARK_OF_PROP_U32},
    {22077U, 646U, 4U, ARK_OF_PROP_U32},
    {49610U, 790U, 4U, ARK_OF_PROP_U32},
    {8177U, 782U, 4U, ARK_OF_PROP_U32},
    {27109U, 154U, 4U, ARK_OF_PROP_U32},
    {25203U, 694U, 4U, ARK_OF_PROP_U32},
    {24598U, 646U, 4U, ARK_OF_PROP_U32},
    {2316U, 646U, 4U, ARK_OF_PROP_U32},
    {10304U, 794U, 4U, ARK_OF_PROP_U32},
    {15456U, 786U, 4U, ARK_OF_PROP_U32},
    {30135U, 162U, 4U, ARK_OF_PROP_U32},
    {33555U, 798U, 4U, ARK_OF_PROP_U32},
    {29723U, 802U, 4U, ARK_OF_PROP_U32},
    {26081U, 806U, 4U, ARK_OF_PROP_U32},
    {13842U, 810U, 4U, ARK_OF_PROP_U32},
    {59056U, 814U, 4U, ARK_OF_PROP_U32},
    {37347U, 786U, 4U, ARK_OF_PROP_U32},
    {61976U, 818U, 32U, ARK_OF_PROP_U32},
    {62624U, 850U, 24U, ARK_OF_PROP_U32},
    {30832U, 874U, 4U, ARK_OF_PROP_U32},
    {30951U, 638U, 4U, ARK_OF_PROP_U32},
    {33970U, 786U, 4U, ARK_OF_PROP_U32},
    {36578U, 786U, 4U, ARK_OF_PROP_U32},
    {17089U, 786U, 4U, ARK_OF_PROP_U32},
    {47718U, 786U, 4U, ARK_OF_PROP_U32},
    {58064U, 154U, 4U, ARK_OF_PROP_U32},
    {20219U, 162U, 4U, ARK_OF_PROP_U32},
    {10287U, 65U, 4U, ARK_OF_PROP_U32},
    {1584U, 514U, 4U, ARK_OF_PROP_U32},
    {706U, 690U, 4U, ARK_OF_PROP_U32},
    {52059U, 878U, 4U, ARK_OF_PROP_U32},
    {17249U, 882U, 4U, ARK_OF_PROP_U32},
    {20001U, 886U, 4U, ARK_OF_PROP_U32},
    {30169U, 890U, 4U, ARK_OF_PROP_U32},
    {36286U, 65U, 4U, ARK_OF_PROP_U32},
    {19036U, 694U, 4U, ARK_OF_PROP_U32},
    {60102U, 806U, 4U, ARK_OF_PROP_U32},
    {7671U, 786U, 4U, ARK_OF_PROP_U32},
    {61490U, 786U, 4U, ARK_OF_PROP_U32},
    {14131U, 162U, 4U, ARK_OF_PROP_U32},
    {58993U, 894U, 4U, ARK_OF_PROP_U32},
    {24399U, 65U, 4U, ARK_OF_PROP_U32},
    {48546U, 694U, 4U, ARK_OF_PROP_U32},
    {61945U, 642U, 4U, ARK_OF_PROP_U32},
    {4122U, 694U, 4U, ARK_OF_PROP_U32},
    {14058U, 898U, 4U, ARK_OF_PROP_U32},
    {42470U, 569U, 4U, ARK_OF_PROP_U32},
    {57363U, 634U, 4U, ARK_OF_PROP_U32},
    {50692U, 786U, 4U, ARK_OF_PROP_U32},
};

static const ark_of_node_t ark_dts_nodes[] = {
    {48466U, 255U, 1U, 255U, 0U, 2U, 0U},
    {54967U, 0U, 2U, 36U, 2U, 4U, 0U},
    {57068U, 1U, 255U, 3U, 6U, 4U, 1U},
    {56665U, 1U, 255U, 4U, 10U, 4U, 2U},
    {56266U, 1U, 255U, 5U, 14U, 4U, 3U},
    {24737U, 1U, 255U, 6U, 18U, 1U, 0U},
    {45869U, 1U, 7U, 10U, 19U, 3U, 4U},
    {49013U, 6U, 255U, 8U, 22U, 3U, 0U},
    {47622U, 6U, 255U, 9U, 25U, 3U, 0U},
    {24034U, 6U, 255U, 255U, 28U, 3U, 0U},
    {32721U, 1U, 255U, 11U, 31U, 3U, 5U},
    {28732U, 1U, 255U, 12U, 34U, 3U, 6U},
    {39838U, 1U, 255U, 13U, 37U, 2U, 7U},
    {39171U, 1U, 255U, 14U, 39U, 2U, 8U},
    {3113U, 1U, 15U, 17U, 41U, 4U, 9U},
    {24106U, 14U, 255U, 16U, 45U, 2U, 0U},
    {21841U, 14U, 255U, 255U, 47U, 2U, 0U},
    {24329U, 1U, 18U, 20U, 49U, 2U, 10U},
    {52786U, 17U, 19U, 255U, 51U, 3U, 11U},
    {18371U, 18U, 255U, 255U, 54U, 1U, 0U},
    {22780U, 1U, 21U, 23U, 55U, 2U, 12U},
    {54376U, 20U, 22U, 255U, 57U, 3U, 13U},
    {60142U, 21U, 255U, 255U, 60U, 1U, 0U},
    {22627U, 1U, 24U, 26U, 61U, 2U, 14U},
    {3935U, 23U, 25U, 255U, 63U, 3U, 15U},
    {35088U, 24U, 255U, 255U, 66U, 1U, 0U},
    {50317U, 1U, 255U, 27U, 67U, 2U, 0U},
    {27942U, 1U, 255U, 28U, 69U, 3U, 0U},
    {16742U, 1U, 29U, 30U, 72U, 1U, 0U},
    {5412U, 28U, 255U, 255U, 73U, 8U, 0U},
    {37747U, 1U, 255U, 31U, 81U, 5U, 0U},
    {8693U, 1U, 255U, 32U, 86U, 3U, 0U},
    {29395U, 1U, 255U, 33U, 89U, 4U, 0U},
    {36531U, 1U, 255U, 34U, 93U, 2U, 0U},
    {16891U, 1U, 35U, 255U, 95U, 1U, 0U},
    {34217U, 34U, 255U, 255U, 96U, 2U, 0U},
    {51211U, 0U, 37U, 255U, 98U, 0U, 0U},
    {46517U, 36U, 255U, 38U, 98U, 14U, 0U},
    {60966U, 36U, 255U, 39U, 112U, 8U, 0U},
    {4459U, 36U, 255U, 40U, 120U, 1U, 0U},
    {6338U, 36U, 255U, 41U, 121U, 3U, 0U},
    {18656U, 36U, 255U, 42U, 124U, 4U, 0U},
    {54676U, 36U, 255U, 43U, 128U, 23U, 0U},
    {34375U, 36U, 255U, 44U, 151U, 21U, 0U},
    {8414U, 36U, 255U, 255U, 172U, 9U, 0U},
};

static const ark_of_hal_ref_t ark_dts_hal_refs[] = {
    {2U, ARK_OF_HAL_GPIO, 0U},
    {3U, ARK_OF_HAL_GPIO, 1U},
    {4U, ARK_OF_HAL_GPIO, 2U},
    {6U, ARK_OF_HAL_ADC, 0U},
    {10U, ARK_OF_HAL_ENCODER, 0U},
    {11U, ARK_OF_HAL_ENCODER, 1U},
    {12U, ARK_OF_HAL_PWM, 0U},
    {13U, ARK_OF_HAL_PWM, 1U},
    {14U, ARK_OF_HAL_I2C, 0U},
    {17U, ARK_OF_HAL_UART, 0U},
    {20U, ARK_OF_HAL_UART, 1U},
    {23U, ARK_OF_HAL_UART, 2U},
};

const ark_of_blob_t ark_of_blob = {
    ark_dts_nodes, sizeof(ark_dts_nodes) / sizeof(ark_dts_nodes[0]),
    ark_dts_properties, sizeof(ark_dts_properties) / sizeof(ark_dts_properties[0]),
    ark_dts_data,
    ark_dts_hal_refs, sizeof(ark_dts_hal_refs) / sizeof(ark_dts_hal_refs[0]),
};

bool ark_dts_register_components(void)
{
    bool ok = true;
    ok = adc_sensor_register() && ok;
    ok = oled_register() && ok;
    ok = bh1750_register() && ok;
    ok = stream_register() && ok;
    ok = bluetooth_register() && ok;
    ok = maixcam_register() && ok;
    ok = beidou_register() && ok;
    ok = dht11_register() && ok;
    ok = adc38_tracking_register() && ok;
    ok = motor_register() && ok;
    ok = motor_tb6612_register() && ok;
    ok = encoder_register() && ok;
    ok = servo_register() && ok;
    ok = ws2812b_register() && ok;
    ok = buzzer_register() && ok;
    ok = flash_register() && ok;
    ok = flash_stm32f103_register() && ok;
    ok = control_register() && ok;
    return ok;
}
