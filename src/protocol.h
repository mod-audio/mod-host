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

#ifndef PROTOCOL_H
#define PROTOCOL_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "utils.h"


/*
************************************************************************************************************************
*           DO NOT CHANGE THESE DEFINES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

#define PROTOCOL_MAX_COMMANDS       12
#define MESSAGE_COMMAND_NOT_FOUND   "not found"
#define MESSAGE_MANY_ARGUMENTS      "many arguments"
#define MESSAGE_FEW_ARGUMENTS       "few arguments"
#define MESSAGE_INVALID_ARGUMENT    "invalid argument"


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

typedef struct PROTOCOL_T {
    str_array_t received;
    char response[32];
} protocol_t;


/*
************************************************************************************************************************
*           GLOBAL VARIABLES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           MACRO'S
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

void protocol_parse(void *arg);
void protocol_add_command(const char *command, void (*callback)(void *arg));
void protocol_remove_commands(void);
void protocol_verbose(int verbose);


/*
************************************************************************************************************************
*           CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
