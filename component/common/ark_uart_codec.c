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

#include "ark_uart_codec.h"

#include <stdbool.h>
#include <string.h>

uint8_t ark_uart_checksum_sum(const uint8_t *data, size_t size) {
    uint8_t checksum = 0U;
    size_t index;

    if (data == NULL) {
        return 0U;
    }
    for (index = 0U; index < size; ++index) {
        checksum = (uint8_t)(checksum + data[index]);
    }
    return checksum;
}

uint8_t ark_uart_checksum_xor(const uint8_t *data, size_t size) {
    uint8_t checksum = 0U;
    size_t index;

    if (data == NULL) {
        return 0U;
    }
    for (index = 0U; index < size; ++index) {
        checksum ^= data[index];
    }
    return checksum;
}

static bool ark_uart_host_is_little_endian(void) {
    const uint16_t marker = 1U;

    return *((const uint8_t *)&marker) == 1U;
}

uint16_t ark_uart_host_to_le16(uint16_t value) {
    return ark_uart_host_is_little_endian() ? value :
        (uint16_t)((value << 8U) | (value >> 8U));
}

uint16_t ark_uart_le16_to_host(uint16_t value) {
    return ark_uart_host_to_le16(value);
}

uint32_t ark_uart_host_to_le32(uint32_t value) {
    if (ark_uart_host_is_little_endian()) {
        return value;
    }
    return ((value & 0x000000FFUL) << 24U) |
           ((value & 0x0000FF00UL) << 8U) |
           ((value & 0x00FF0000UL) >> 8U) |
           ((value & 0xFF000000UL) >> 24U);
}

uint32_t ark_uart_le32_to_host(uint32_t value) {
    return ark_uart_host_to_le32(value);
}

uint32_t ark_uart_float_to_le(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return ark_uart_host_to_le32(bits);
}

float ark_uart_float_from_le(uint32_t value) {
    uint32_t bits = ark_uart_le32_to_host(value);
    float result;

    memcpy(&result, &bits, sizeof(result));
    return result;
}
