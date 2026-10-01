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

#ifndef HOST_DISPATCH_H
#define HOST_DISPATCH_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "protocol.h"
#include "host-backend.h"

#include <stdint.h>


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

#define HOST_INFO_STRING_SIZE 256
#define HOST_REMOTE_PAGE_SLOTS 8

/* what param_info answers: unit an LV2 unit name ("db", "hz", ...) or "none", scale "linear", "log" or
   "stepped", step 0 for continuous, stable_symbol the name the parameter keeps across hosts */
typedef struct HOST_PARAM_INFO_T {
    const char *unit;
    const char *scale;
    double min, max, def, step;
    const char *stable_symbol;
} host_param_info_t;

/* one page of remote controls: eight parameter symbols, "-" for an empty slot */
typedef struct HOST_REMOTE_PAGE_T {
    uint32_t id;
    char section[HOST_INFO_STRING_SIZE];
    char name[HOST_INFO_STRING_SIZE];
    char slots[HOST_REMOTE_PAGE_SLOTS][HOST_INFO_STRING_SIZE];
} host_remote_page_t;

/* What a host can tell about a plugin. A NULL entry answers ERR_INVALID_OPERATION.
   track_info gets a checked name (may be ""), a color "#RRGGBB" or "-", and a kind "bus", "return", "master"
   or NULL for an input channel. remote_pages answers the page count. */
typedef struct HOST_PLUGIN_INFO_T {
    int (*track_info)(int instance, const char *name, const char *color, const char *kind);
    int (*remote_pages)(int instance);
    int (*remote_page_get)(int instance, int page, host_remote_page_t *page_out);
    int (*param_info)(int instance, const char *symbol, host_param_info_t *info);
} host_plugin_info_t;


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

void host_dispatch_register(const host_backend_t *backend);
/* Registers monitor_output, answered as mod-host does; NULL answers ERR_INVALID_OPERATION.
   Call it before host_dispatch_register_unsupported(). */
void host_dispatch_register_monitor_output(int (*monitor_output)(int instance, const char *symbol));
/* Registers track_info, remote_pages, remote_page_get and param_info.
   Call it before host_dispatch_register_unsupported(). */
void host_dispatch_register_plugin_info(const host_plugin_info_t *info);
void host_dispatch_register_unsupported(void);
void host_dispatch_unsupported_cb(proto_t *proto);
/* Sends OUTPUT_SET on the feedback socket; -1 without a feedback client or for a symbol too long. */
int host_dispatch_output_set(int instance, const char *symbol, float value);
/* Sends REMOTE_PAGES_CHANGED on the feedback socket; -1 without a feedback client. */
int host_dispatch_remote_pages_changed(int instance);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
