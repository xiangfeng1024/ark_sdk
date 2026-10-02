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

#ifndef ARK_COMPONENT_BLUETOOTH_H
#define ARK_COMPONENT_BLUETOOTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLUETOOTH_CONTROL_DATA_SIZE 5U
#define BLUETOOTH_PACKET_UINT8_CAPACITY 8U
#define BLUETOOTH_PACKET_SHORT_CAPACITY 4U
#define BLUETOOTH_PACKET_INT_CAPACITY 4U
#define BLUETOOTH_PACKET_FLOAT_CAPACITY 8U

typedef struct {
    uint8_t uint8_count;
    uint8_t short_count;
    uint8_t int_count;
    uint8_t float_count;
    uint8_t uint8_values[BLUETOOTH_PACKET_UINT8_CAPACITY];
    int16_t short_values[BLUETOOTH_PACKET_SHORT_CAPACITY];
    int32_t int_values[BLUETOOTH_PACKET_INT_CAPACITY];
    float float_values[BLUETOOTH_PACKET_FLOAT_CAPACITY];
} bluetooth_packet_builder_t;

typedef struct {
    uint8_t data[BLUETOOTH_CONTROL_DATA_SIZE];
    uint32_t sequence;
    uint32_t tick;
} bluetooth_control_t;

bool bluetooth_register(void);
int32_t bluetooth_read(uint8_t *data, size_t size);
void bluetooth_packet_reset(bluetooth_packet_builder_t *builder);
bool bluetooth_packet_add_uint8(bluetooth_packet_builder_t *builder, uint8_t value);
bool bluetooth_packet_add_short(bluetooth_packet_builder_t *builder, int16_t value);
bool bluetooth_packet_add_int(bluetooth_packet_builder_t *builder, int32_t value);
bool bluetooth_packet_add_float(bluetooth_packet_builder_t *builder, float value);
bool bluetooth_packet_send(const bluetooth_packet_builder_t *builder);
bool bluetooth_get_control(bluetooth_control_t *control);

#endif /* ARK_COMPONENT_BLUETOOTH_H */
