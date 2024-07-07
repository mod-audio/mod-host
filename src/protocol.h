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
#include "socket.h"

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

#define PROTOCOL_MAX_COMMANDS       64

// error messages configuration
#define MESSAGE_COMMAND_NOT_FOUND   "not found"
#define MESSAGE_MANY_ARGUMENTS      "many arguments"
#define MESSAGE_FEW_ARGUMENTS       "few arguments"
#define MESSAGE_INVALID_ARGUMENT    "invalid argument"

// defines the function to send responses to sender
#define SEND_TO_SENDER(id,msg,len)  if(id==1)fprintf(stdin,"%s\n",msg); else socket_send((id),(msg),(len)+1)


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

// This struct is used on callbacks argument
typedef struct PROTO_T {
    char **list;
    uint32_t list_count;
    char *response;
    uint32_t response_size;
} proto_t;


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

void protocol_parse(msg_t *msg);
void protocol_add_command(const char *command, void (*callback)(proto_t *proto));
void protocol_response(const char *response, proto_t *proto);
void protocol_response_int(int resp, proto_t *proto);
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
