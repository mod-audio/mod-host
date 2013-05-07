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
#include "socket.h"
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
static unsigned int g_index = 0;
static str_array_t g_commands[PROTOCOL_MAX_COMMANDS];
static void (*g_callbacks[PROTOCOL_MAX_COMMANDS])(void *arg);


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

static int not_is_wildcard(const char *str);


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

static int not_is_wildcard(const char *str)
{
    if (strchr(str, '%') == NULL) return 1;
    return 0;
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void protocol_parse(void *arg)
{
    socket_msg_t *msg = arg;
    protocol_t proto;
    unsigned int i, j;
    int index = NOT_FOUND;

    proto.received = string_split(msg->buffer, ' ');

    /* TODO: few arguments, many arguments, check invalid argument (wildcards) */

    if (proto.received.count > 0)
    {
        for (i = 0; i < g_index; i++)
        {
            /* Checks if counters match */
            if (g_commands[i].count == proto.received.count)
            {
                index = i;

                /* Checks the commands */
                for (j = 0; j < proto.received.count; j++)
                {
                    if (not_is_wildcard(g_commands[i].data[j]) &&
                        strcmp(g_commands[i].data[j], proto.received.data[j]) != 0)
                    {
                        index = NOT_FOUND;
                        break;
                    }
                }

                if (index >= 0) break;
            }
        }

        /* Protocol OK */
        if (index >= 0)
        {
            if (g_callbacks[index])
            {
                g_callbacks[index](&proto);
                socket_send(msg->origin, proto.response, strlen(proto.response) + 1);

                if (g_verbose)
                {
                    printf("received: %s\n", msg->buffer);
                    printf("response: %s\n", proto.response);
                }
            }
        }
        /* Protocol error */
        else
        {
            socket_send(msg->origin, g_error_messages[-index-1], strlen(g_error_messages[-index-1]) + 1);

            if (g_verbose)
            {
                printf("error: %s\n", g_error_messages[-index-1]);
            }
        }

        free_str_array(proto.received);
    }
}


void protocol_add_command(const char *command, void (*callback)(void *arg))
{
    if (g_index >= PROTOCOL_MAX_COMMANDS)
    {
        printf("error: PROTOCOL_MAX_COMMANDS reached (reconfigure it)\n");
        return;
    }

    g_commands[g_index] = string_split(command, ' ');
    g_callbacks[g_index] = callback;
    g_index++;
}


void protocol_remove_commands(void)
{
    unsigned int i;

    for (i = 0; i < g_index; i++)
    {
        free_str_array(g_commands[i]);
    }
}


void protocol_verbose(int verbose)
{
    g_verbose = verbose;
}
