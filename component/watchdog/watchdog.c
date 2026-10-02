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

#include "watchdog.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_dts_generated.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_hal_watchdog.h"

#define WATCHDOG_TASK_STACK_DEPTH     96U
#define WATCHDOG_REFRESH_PERIOD_MS    250U
#define WATCHDOG_RESET_NOTICE_MS      20U
#define WATCHDOG_RESET_WAIT_PERIOD_MS 1000U

static volatile bool watchdog_refresh_enabled;
static TaskHandle_t watchdog_task_handle;

static void watchdog_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        if (watchdog_refresh_enabled) {
            ark_hal_watchdog.refresh();
        }
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(WATCHDOG_REFRESH_PERIOD_MS));
    }
}

static ark_component_result_t watchdog_init(void) {
    if (!ark_hal_watchdog.is_ready() || !ark_hal_watchdog.refresh()) {
        return ARK_COMPONENT_ERROR;
    }

    watchdog_refresh_enabled = true;
    if (xTaskCreate(
            watchdog_task,
            "watchdog",
            WATCHDOG_TASK_STACK_DEPTH,
            NULL,
            tskIDLE_PRIORITY + 3U,
            &watchdog_task_handle) != pdPASS) {
        watchdog_refresh_enabled = false;
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static ark_component_result_t watchdog_self_test(void) {
    return ark_hal_watchdog.refresh() ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static const ark_component_t watchdog_component = {
    .name = "watchdog",
    .init = watchdog_init,
    .self_test = watchdog_self_test,
    .self_test_expected_ms = 20U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_1,
};

#if ARK_DTS_HAS_UART_CLI
static int watchdog_reset_cli(int argc, char *argv[]) {
    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    printf("resetting by watchdog...\r\n");
    vTaskDelay(pdMS_TO_TICKS(WATCHDOG_RESET_NOTICE_MS));
    watchdog_request_reset();
    return 0;
}

static ark_cli_command_t watchdog_reset_command = {
    .name = "reset",
    .usage = "reset",
    .description = "reset by stopping IWDG refresh",
    .handler = watchdog_reset_cli,
};
#endif

bool watchdog_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&watchdog_reset_command);
#endif
    return ark_component_register(&watchdog_component);
}

void watchdog_request_reset(void) {
    watchdog_refresh_enabled = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_RESET_WAIT_PERIOD_MS));
    }
}
