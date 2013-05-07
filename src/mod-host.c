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


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <argtable2.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "mod-host.h"
#include "effects.h"
#include "socket.h"
#include "protocol.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define PID_FILE        "/tmp/mod-host.pid"


/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/


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

static void effects_add_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    resp = effects_add(proto->received.data[1], atoi(proto->received.data[2]));
    sprintf(proto->response, "resp %i", resp);
}

static void effects_remove_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    resp = effects_remove(atoi(proto->received.data[1]));
    sprintf(proto->response, "resp %i", resp);
}

static void effects_connect_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    resp = effects_connect(proto->received.data[1], proto->received.data[2]);
    sprintf(proto->response, "resp %i", resp);
}

static void effects_disconnect_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    resp = effects_disconnect(proto->received.data[1], proto->received.data[2]);
    sprintf(proto->response, "resp %i", resp);
}

static void effects_bypass_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    resp = effects_bypass(atoi(proto->received.data[1]), atoi(proto->received.data[2]));
    sprintf(proto->response, "resp %i", resp);
}

static void effects_set_param_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    resp = effects_set_parameter(atoi(proto->received.data[1]), proto->received.data[2], atof(proto->received.data[3]));
    sprintf(proto->response, "resp %i", resp);
}

static void effects_get_param_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    float value;
    resp = effects_get_parameter(atoi(proto->received.data[1]), proto->received.data[2], &value);
    sprintf(proto->response, "resp %i %.04f", resp, value);
}

void interactive_mode(void)
{
    socket_msg_t msg;
    char buffer[SOCKET_MSG_BUFFER_SIZE];

    msg.origin = STDOUT_FILENO;

    while (1)
    {
        msg.buffer = readline(">>> ");

        if (msg.buffer)
        {
            msg.size = strlen(buffer);
            protocol_parse(&msg);
            add_history(msg.buffer);
            printf("\n");
        }
        else break;
    }
}


/*
************************************************************************************************************************
*           MAIN FUNCTION
************************************************************************************************************************
*/

int main(int argc, char **argv)
{
    int verbose, socket_port, interactive;

    /* Command line options */
    struct arg_lit *_verbose = arg_lit0("v", "verbose,debug", "verbose messages");
    struct arg_int *_socket = arg_int0("p", "socket-port", "<port>", "socket port definition");
    struct arg_lit *_interactive = arg_lit0("i", "interactive", "interactive mode");
    struct arg_lit *_help = arg_lit0("h", "help", "print this help and exit");
    struct arg_end *_end = arg_end(20);
    void *argtable[] = {_verbose, _socket, _interactive, _help, _end};

    if (arg_nullcheck(argtable))
    {
        fprintf(stderr, "argtable error: insufficient memory\n");
        exit(EXIT_FAILURE);
    }

    /* Default value of command line arguments */
    _socket->ival[0] = SOCKET_DEFAULT_PORT;

    /* Run the argument parser */
    if (arg_parse(argc, argv, argtable) == 0)
    {
        if (_help->count > 0)
        {
            fprintf(stdout, "Usage: %s", argv[0]);
            arg_print_syntax(stdout, argtable, "\n");
            arg_print_glossary(stdout, argtable, "  %-30s %s\n");
            exit(EXIT_SUCCESS);
        }

        verbose = _verbose->count;
        socket_port = _socket->ival[0];
        interactive = _interactive->count;
    }
    else
    {
        arg_print_errors(stderr, _end, argv[0]);
        exit(EXIT_FAILURE);
    }

    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));

    /* If verbose or interactive, don't fork */
    if (!verbose && !interactive)
    {
        int pid;
        pid = fork();
        if (pid != 0)
        {
            printf("Forking... child PID: %d\n", pid);

            FILE *fd;
            fd = fopen(PID_FILE, "w");
            if (fd == NULL)
            {
                fprintf(stderr, "can't open PID File\n");
            }
            else
            {
                fprintf(fd, "%d\n", pid);
                fclose(fd);
            }
            exit(EXIT_SUCCESS);
        }
    }

    /* Setup the protocol */
    protocol_add_command(EFFECT_ADD, effects_add_cb);
    protocol_add_command(EFFECT_REMOVE, effects_remove_cb);
    protocol_add_command(EFFECT_CONNECT, effects_connect_cb);
    protocol_add_command(EFFECT_DISCONNECT, effects_disconnect_cb);
    protocol_add_command(EFFECT_BYPASS, effects_bypass_cb);
    protocol_add_command(EFFECT_PARAM_SET, effects_set_param_cb);
    protocol_add_command(EFFECT_PARAM_GET, effects_get_param_cb);

    /* Startup the effects */
    if (effects_init()) return -1;

    /* Setup the socket */
    if (socket_start(socket_port, SOCKET_MSG_BUFFER_SIZE) < 0) {
        exit(EXIT_FAILURE);
    }
    socket_set_receive_cb(protocol_parse);

    /* Interactice mode */
    if (interactive) interactive_mode();

    /* Verbose */
    protocol_verbose(verbose);

    while (1) socket_run();

    protocol_remove_commands();
    socket_finish();
    effects_finish();

    return 0;
}
