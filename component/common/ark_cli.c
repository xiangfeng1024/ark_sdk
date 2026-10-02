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

#define ARK_CLI_IMPLEMENTATION
#include "ark_cli.h"

#include <stdio.h>
#include <string.h>

#define ARK_CLI_MAX_ARGUMENTS 8

static ark_cli_command_t *ark_cli_head;
static ark_cli_command_t *ark_cli_tail;

bool ark_cli_register(ark_cli_command_t *command) {
    const ark_cli_command_t *current_command;

    if ((command == NULL) || (command->name == NULL) ||
        (command->name[0] == '\0') || (command->usage == NULL) ||
        (command->description == NULL) || (command->handler == NULL) ||
        (command->next != NULL)) {
        return false;
    }
    for (current_command = ark_cli_head;
         current_command != NULL;
         current_command = current_command->next) {
        if (strcmp(current_command->name, command->name) == 0) {
            return false;
        }
    }
    if (ark_cli_tail == NULL) {
        ark_cli_head = command;
    } else {
        ark_cli_tail->next = command;
    }
    ark_cli_tail = command;
    return true;
}

const ark_cli_command_t *ark_cli_find(const char *name) {
    const ark_cli_command_t *current_command;

    if (name == NULL) {
        return NULL;
    }
    for (current_command = ark_cli_head;
         current_command != NULL;
         current_command = current_command->next) {
        if (strcmp(current_command->name, name) == 0) {
            return current_command;
        }
    }
    return NULL;
}

size_t ark_cli_count(void) {
    const ark_cli_command_t *current_command;
    size_t count = 0U;

    for (current_command = ark_cli_head;
         current_command != NULL;
         current_command = current_command->next) {
        ++count;
    }
    return count;
}

void ark_cli_print_help(void) {
    const ark_cli_command_t *current_command;

    printf("commands:\r\n");
    for (current_command = ark_cli_head;
         current_command != NULL;
         current_command = current_command->next) {
        printf("  %-24s %s\r\n",
               current_command->usage,
               current_command->description);
    }
}

bool ark_cli_execute(char *line) {
    char *argv[ARK_CLI_MAX_ARGUMENTS];
    const ark_cli_command_t *command;
    int argc = 0;
    char *token;

    if (line == NULL) {
        return false;
    }
    token = strtok(line, " \t");
    while ((token != NULL) && (argc < ARK_CLI_MAX_ARGUMENTS)) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    if (argc == 0) {
        return true;
    }
    if (token != NULL) {
        printf("too many arguments\r\n");
        return true;
    }
    command = ark_cli_find(argv[0]);
    if (command == NULL) {
        printf("unknown command: %s\r\n", argv[0]);
        return false;
    }
    command->handler(argc, argv);
    return true;
}
