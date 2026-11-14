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


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

void host_dispatch_register(const host_backend_t *backend);
/* Registers monitor_output, answered as mod-host does; NULL answers ERR_INVALID_OPERATION.
   Call it before host_dispatch_register_unsupported(). */
void host_dispatch_register_monitor_output(int (*monitor_output)(int instance, const char *symbol));
void host_dispatch_register_unsupported(void);
void host_dispatch_unsupported_cb(proto_t *proto);
/* Sends OUTPUT_SET on the feedback socket; -1 without a feedback client or for a symbol too long. */
int host_dispatch_output_set(int instance, const char *symbol, float value);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
