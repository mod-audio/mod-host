/*
 * This file is part of mod-host.
 *
 * mod-host is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mod-host is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mod-host.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
************************************************************************************************************************
*
************************************************************************************************************************
*/

#ifndef HOST_SCENARIO_H
#define HOST_SCENARIO_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stddef.h>
#include "host-backend.h"


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

#define HOST_SCENARIO_MAX_LINES     128
#define HOST_SCENARIO_MAX_VARS      16
#define HOST_SCENARIO_MAX_ABSENT    16
#define HOST_SCENARIO_REPLY_SIZE    256


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

/* One $NAME the scenario file refers to and its value for the host under test. */
typedef struct HOST_SCENARIO_VAR_T {
    const char *name;
    const char *value;
} host_scenario_var_t;

/* One line of the file after substitution: the command, the '|'-separated replies accepted, and the
   verb whose absence (an earlier "resp -902") skips the line, or NULL. */
typedef struct HOST_SCENARIO_T {
    char *command;
    char *reply;
    char *guard;
} host_scenario_t;

typedef struct HOST_SCENARIO_LIST_T {
    host_scenario_t lines[HOST_SCENARIO_MAX_LINES];
    int count;
} host_scenario_list_t;

/* Sends one command and stores its NUL-terminated reply. Returns 0, or -1 when the host went away. */
typedef int (*host_scenario_exchange_t)(void *ctx, const char *command, char *reply, size_t size);

/* A backend in this process driven through the protocol parser over a socketpair. */
typedef struct HOST_SCENARIO_DIRECT_T {
    int fd[2];
    const host_backend_t *backend;
} host_scenario_direct_t;


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

int host_scenario_load(const char *path, const host_scenario_var_t *vars, int var_count, host_scenario_list_t *list);
void host_scenario_free(host_scenario_list_t *list);

/* Runs every line, printing one line per exchange. Returns the number of failures, or -1 when the exchange
   broke. When 'table' is given, a "resp -902" from a verb the table serves, or any other reply from a verb it
   leaves NULL, is a failure too. */
int host_scenario_run(const host_scenario_list_t *list, host_scenario_exchange_t exchange, void *ctx,
                      const host_backend_t *table);

int host_scenario_socket_open(const char *host, int port);
int host_scenario_socket_exchange(void *ctx, const char *command, char *reply, size_t size);

int host_scenario_direct_open(host_scenario_direct_t *direct, const host_backend_t *backend);
int host_scenario_direct_exchange(void *ctx, const char *command, char *reply, size_t size);
void host_scenario_direct_close(host_scenario_direct_t *direct);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
