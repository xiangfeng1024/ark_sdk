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

#ifndef ARK_COMPONENT_ICM20602_H
#define ARK_COMPONENT_ICM20602_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t accel_raw[3];
    int16_t gyro_raw[3];
    int16_t temperature_raw;
    int32_t accel_mg[3];
    int32_t gyro_mdps[3];
    int32_t temperature_cdeg;
} icm20602_sample_t;

typedef struct {
    icm20602_sample_t raw;
    int32_t euler_cdeg[3];
    int32_t gyro_bias_mdps[3];
    int32_t gyro_mdps[3];
    int32_t angular_accel_mdps2[3];
    uint32_t update_count;
    uint32_t stationary_samples;
    bool calibrated;
    bool stationary;
} icm20602_motion_t;

typedef struct {
    uint8_t who_am_i;
    uint32_t irq_count;
    uint32_t sample_count;
    uint32_t read_failures;
    uint32_t data_ready_timeouts;
    uint32_t bus_recoveries;
} icm20602_diagnostics_t;

bool icm20602_register(void);
bool icm20602_wait_data_ready(uint32_t timeout_ms);
/* Read one unfiltered sensor sample without changing the attitude solution. */
bool icm20602_read_sample(icm20602_sample_t *sample);
bool icm20602_calibrate(void);
bool icm20602_update(void);
bool icm20602_get_motion(icm20602_motion_t *motion);
bool icm20602_get_diagnostics(icm20602_diagnostics_t *diagnostics);
void icm20602_scan(void);

#endif /* ARK_COMPONENT_ICM20602_H */
