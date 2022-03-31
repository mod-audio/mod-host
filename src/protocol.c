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
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "utils.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define NOT_FOUND           (-1)
#define MANY_ARGUMENTS      (-2)
#define FEW_ARGUMENTS       (-3)
#define INVALID_ARGUMENT    (-4)


/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/

const char *g_error_messages[] = {
    MESSAGE_COMMAND_NOT_FOUND,
    MESSAGE_MANY_ARGUMENTS,
    MESSAGE_FEW_ARGUMENTS,
    MESSAGE_INVALID_ARGUMENT
};


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct CMD_T {
    char* command;
    char** list;
    uint32_t count;
    void (*callback)(proto_t *proto);
} cmd_t;


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static int g_verbose = 0;
static unsigned int g_command_count = 0;
static cmd_t g_commands[PROTOCOL_MAX_COMMANDS];


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static int is_wildcard(const char *str)
{
    if (str && strchr(str, '%') != NULL) return 1;
    return 0;
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void protocol_parse(msg_t *msg)
{
    uint32_t i, j;
    int32_t index;
    proto_t proto;

    proto.list = strarr_split(msg->data);
    proto.list_count = strarr_length(proto.list);
    proto.response = NULL;

    if (g_verbose)
    {
        printf("PROTOCOL: received '%s'", msg->data);
        for (i = 1; i < proto.list_count; i++)
            printf(" '%s'", proto.list[i]);
        printf("\n");
    }

    // TODO: check invalid argumets (wildcards)

    if (proto.list_count == 0) return;

    unsigned int match, variable_arguments = 0;

    index = NOT_FOUND;

    // loop all registered commands
    for (i = 0; i < g_command_count; i++)
    {
        match = 0;

        // checks received protocol
        for (j = 0; j < proto.list_count && j < g_commands[i].count; j++)
        {
            if (strcmp(g_commands[i].list[j], proto.list[j]) == 0)
            {
                match++;
            }
            else if (match > 0)
            {
                if (is_wildcard(g_commands[i].list[j]))
                {
                    match++;
                }
                else if (strcmp(g_commands[i].list[j], "...") == 0)
                {
                    match++;
                    variable_arguments = 1;
                }
            }
        }

        if (match > 0)
        {
            // checks if the last argument is ...
            if (j < g_commands[i].count)
            {
                if (strcmp(g_commands[i].list[j], "...") == 0) variable_arguments = 1;
            }

            // few arguments
            if (proto.list_count < (g_commands[i].count - variable_arguments))
            {
                index = FEW_ARGUMENTS;
            }

            // many arguments
            else if (proto.list_count > g_commands[i].count && !variable_arguments)
            {
                index = MANY_ARGUMENTS;
            }

            // arguments match
            else if (match == proto.list_count || variable_arguments)
            {
                index = i;
            }

            // not found
            else
            {
                index = NOT_FOUND;
            }

            break;
        }
    }

    // Protocol OK
    if (index >= 0)
    {
        if (g_commands[index].callback)
        {
            g_commands[index].callback(&proto);
            if (proto.response)
            {
                SEND_TO_SENDER(msg->sender_id, proto.response, proto.response_size);
                if (g_verbose) printf("PROTOCOL: response '%s'\n", proto.response);

                FREE(proto.response);
            }
        }
    }
    // Protocol error
    else
    {
        SEND_TO_SENDER(msg->sender_id, g_error_messages[-index-1], strlen(g_error_messages[-index-1]));
        if (g_verbose) printf("PROTOCOL: error '%s'\n", g_error_messages[-index-1]);
    }

    FREE(proto.list);
}


void protocol_add_command(const char *command, void (*callback)(proto_t *proto))
{
    if (g_command_count >= PROTOCOL_MAX_COMMANDS)
    {
        printf("error: PROTOCOL_MAX_COMMANDS reached (reconfigure it)\n");
        return;
    }

    char *cmd = str_duplicate(command);
    g_commands[g_command_count].command = cmd;
    g_commands[g_command_count].list = strarr_split(cmd);
    g_commands[g_command_count].count = strarr_length(g_commands[g_command_count].list);
    g_commands[g_command_count].callback = callback;
    g_command_count++;
}


void protocol_response(const char *response, proto_t *proto)
{
    proto->response_size = strlen(response);
    proto->response = MALLOC(proto->response_size + 1);
    strcpy(proto->response, response);
}


void protocol_response_int(int resp, proto_t *proto)
{
    char buffer[32];
    snprintf(buffer, 32, "resp %i", resp);
    buffer[31] = '\0';
    protocol_response(buffer, proto);
}


void protocol_remove_commands(void)
{
    unsigned int i;

    for (i = 0; i < g_command_count; i++)
    {
        FREE(g_commands[i].command);
        FREE(g_commands[i].list);
    }
}


void protocol_verbose(int verbose)
{
    g_verbose = verbose;
}
