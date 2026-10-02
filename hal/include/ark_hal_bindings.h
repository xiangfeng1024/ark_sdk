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

#ifndef ARK_HAL_BINDINGS_H
#define ARK_HAL_BINDINGS_H

#include "ark_hal_i2c.h"
#include "ark_hal_adc.h"
#include "ark_hal_can.h"
#include "ark_hal_encoder.h"
#include "ark_hal_pwm.h"
#include "ark_hal_soft_i2c.h"
#include "ark_hal_spi.h"
#include "ark_hal_uart.h"

extern void *const ark_hal_i2c_handles[ARK_HAL_I2C_COUNT];
extern void *const ark_hal_adc_handles[ARK_HAL_ADC_COUNT];
extern void *const ark_hal_spi_handles[ARK_HAL_SPI_COUNT];
extern void *const ark_hal_uart_handles[ARK_HAL_UART_COUNT];
extern void *const ark_hal_can_handles[ARK_HAL_CAN_COUNT];
extern void *const ark_hal_pwm_handles[ARK_HAL_PWM_COUNT];
extern void *const ark_hal_encoder_handles[ARK_HAL_ENCODER_COUNT];
extern const uint32_t ark_hal_pwm_channels[ARK_HAL_PWM_COUNT];
extern void *const ark_hal_watchdog_handle;
extern void *const ark_hal_usb_device_handle;

#endif /* ARK_HAL_BINDINGS_H */
