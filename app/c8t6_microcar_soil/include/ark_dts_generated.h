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
#ifndef ARK_DTS_GENERATED_H
#define ARK_DTS_GENERATED_H

#include <stdbool.h>

#define ARK_DTS_APP_NAME "c8t6_microcar_soil"
#define ARK_DTS_HAS_ADC38_TRACKING 1
#define ARK_DTS_HAS_ADC_SENSOR 1
#define ARK_DTS_HAS_BEIDOU 1
#define ARK_DTS_HAS_BH1750 1
#define ARK_DTS_HAS_BUZZER 1
#define ARK_DTS_HAS_BLUETOOTH 1
#define ARK_DTS_HAS_CONTROL 1
#define ARK_DTS_HAS_DHT11 1
#define ARK_DTS_HAS_FLASH 1
#define ARK_DTS_HAS_FLASH_STM32F103 1
#define ARK_DTS_HAS_FLASH_W25Q16 0
#define ARK_DTS_HAS_ICM20602 0
#define ARK_DTS_HAS_JSON 0
#define ARK_DTS_HAS_LCD 0
#define ARK_DTS_HAS_MAIXCAM 1
#define ARK_DTS_HAS_LED 0
#define ARK_DTS_HAS_ENCODER 1
#define ARK_DTS_HAS_MOTOR 1
#define ARK_DTS_HAS_MOTOR_TB6612 1
#define ARK_DTS_HAS_MOTOR_CAN 0
#define ARK_DTS_HAS_SERVO 1
#define ARK_DTS_HAS_OLED 1
#define ARK_DTS_HAS_WIFI 0
#define ARK_DTS_HAS_WIFI_ESP_AT 0
#define ARK_DTS_HAS_ARK_NET 0
#define ARK_DTS_HAS_STREAM 1
#define ARK_DTS_HAS_UART_CLI 0
#define ARK_DTS_HAS_USB 0
#define ARK_DTS_HAS_WATCHDOG 0
#define ARK_DTS_HAS_WS2812B 1
#define ARK_DTS_OLED_DIRTY_REFRESH 0
#define ARK_DTS_WS2812B_PWM_ENABLED 0
#define ARK_DTS_WS2812B_SOFTWARE_ENABLED 1

bool ark_dts_register_components(void);

#endif
