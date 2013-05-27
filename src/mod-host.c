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
#include "completer.h"
#include "monitor.h"


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

const char HELP_MESSAGE[] = {
"\n\
Valid commands:\n\n\
add <lv2_uri> <instance_number>\n\
    e.g.: add http://lv2plug.in/plugins/eg-amp 0\n\
    instance_number must be any value between 0 ~ 9999, inclusively\n\
\n\
remove <instance_number>\n\
    e.g.: remove 0\n\
\n\
connect <origin_port> <destination_port>\n\
    e.g.: connect system:capture_1 effect_0:in\n\
\n\
disconnect <origin_port> <destination_port>\n\
    e.g.: disconnect system:capture_1 effect_0:in\n\
\n\
param_set <instance_number> <param_symbol> <param_value>\n\
    e.g.: param_set 0 gain 2.50\n\
\n\
param_get <instance_number> <param_symbol>\n\
    e.g.: param_get 0 gain\n\
\n\
bypass <instance_number> <bypass_value>\n\
    e.g.: bypass 0 1\n\
    bypass_value = 1 bypass the effect and bypass_value = 0 process the effect\n\
\n\
help\n\
    show this message\n\
\n\
quit\n\
    bye!\n"
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
    if (resp >= 0)
        sprintf(proto->response, "resp %i %.04f", resp, value);
    else
        sprintf(proto->response, "resp %i", resp);
}

static void effects_monitor_param_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    if (monitor_status()) 
        resp = effects_monitor_parameter(atoi(proto->received.data[1]), proto->received.data[2], 
                                     proto->received.data[3], (float)strtof(proto->received.data[4], NULL));
    else 
        resp = -1;
    sprintf(proto->response, "resp %i", resp);
}

static void monitor_addr_set_cb(void *arg)
{
    protocol_t *proto = arg;

    int resp;
    if (atoi(proto->received.data[3]) == 1) {
        resp = monitor_start(proto->received.data[1], atoi(proto->received.data[2]));
    } else {
        resp = monitor_stop();
    }
    sprintf(proto->response, "resp %i", resp);
}

static void help_cb(void *arg)
{
    protocol_t *proto = arg;
    *(proto->response) = 0;

    fprintf(stdout, HELP_MESSAGE);
    fflush(stdout);
}

static void quit_cb(void *arg)
{
    protocol_t *proto = arg;
    *(proto->response) = 0;

    protocol_remove_commands();
    socket_finish();
    effects_finish();
    exit(EXIT_SUCCESS);
}

void interactive_mode(void)
{
    socket_msg_t msg;
    char *input;

    completer_init();

    msg.origin = STDOUT_FILENO;

    while (1)
    {
        input = readline("mod-host> ");
        rl_bind_key('\t', rl_complete);

        if (input)
        {
            msg.size = strlen(input);
            msg.buffer = input;
            protocol_parse(&msg);
            printf("\n");

            add_history(input);
            free(input);
            input = NULL;
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
    protocol_add_command(EFFECT_PARAM_MON, effects_monitor_param_cb);
    protocol_add_command(MONITOR_ADDR_SET, monitor_addr_set_cb);
    protocol_add_command(HELP, help_cb);
    protocol_add_command(QUIT, quit_cb);

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
