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

#ifndef ARK_HAL_CAN_H
#define ARK_HAL_CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ARK_HAL_CAN_1 = 0,
    ARK_HAL_CAN_COUNT
} ark_hal_can_id_t;

typedef struct {
    uint32_t id;
    bool extended;
    bool remote;
    uint8_t length;
    uint8_t data[8];
} ark_hal_can_frame_t;

typedef struct {
    uint32_t hal_state;
    uint32_t hal_error;
    uint32_t esr;
    uint32_t tsr;
    uint32_t rf1r;
    uint32_t tx_mailboxes_free;
    uint32_t received;
    uint32_t dropped;
} ark_hal_can_diagnostics_t;

typedef struct {
    bool (*is_ready)(ark_hal_can_id_t id);
    int32_t (*configure_std_filter)(ark_hal_can_id_t id, uint16_t standard_id);
    int32_t (*set_loopback)(ark_hal_can_id_t id, bool enabled);
    int32_t (*start)(ark_hal_can_id_t id);
    int32_t (*transmit)(
        ark_hal_can_id_t id,
        const ark_hal_can_frame_t *frame,
        uint32_t timeout_ms);
    int32_t (*receive)(
        ark_hal_can_id_t id,
        ark_hal_can_frame_t *frame,
        uint32_t timeout_ms);
    bool (*get_diagnostics)(ark_hal_can_id_t id, ark_hal_can_diagnostics_t *diagnostics);
} ark_hal_can_driver_t;

extern const ark_hal_can_driver_t ark_hal_can;

#endif /* ARK_HAL_CAN_H */
