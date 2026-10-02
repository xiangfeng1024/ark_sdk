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

#ifndef ARK_HAL_UART_H
#define ARK_HAL_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ARK_HAL_UART_1 = 0,
    ARK_HAL_UART_2,
    ARK_HAL_UART_3,
    ARK_HAL_UART_COUNT
} ark_hal_uart_id_t;

typedef struct {
    bool (*is_ready)(ark_hal_uart_id_t id);
    bool (*rx_dma_is_circular)(ark_hal_uart_id_t id);
    int32_t (*transmit)(
        ark_hal_uart_id_t id,
        const uint8_t *data,
        size_t size,
        uint32_t timeout_ms);
    int32_t (*receive)(
        ark_hal_uart_id_t id,
        uint8_t *data,
        size_t size,
        uint32_t timeout_ms);
    bool (*transmit_dma)(ark_hal_uart_id_t id, const uint8_t *data, size_t size);
    bool (*receive_it)(ark_hal_uart_id_t id, uint8_t *data, size_t size);
    bool (*receive_dma)(ark_hal_uart_id_t id, uint8_t *data, size_t size);
    bool (*receive_to_idle_dma)(ark_hal_uart_id_t id, uint8_t *data, size_t size);
    bool (*abort)(ark_hal_uart_id_t id);
} ark_hal_uart_driver_t;

extern const ark_hal_uart_driver_t ark_hal_uart;

#endif /* ARK_HAL_UART_H */
