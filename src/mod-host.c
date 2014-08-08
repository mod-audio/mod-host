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
    adds a lv2 effect to pedalboard (jack session)\n\
    e.g.: add http://lv2plug.in/plugins/eg-amp 0\n\
    instance_number must be any value between 0 ~ 9999, inclusively\n\
\n\
remove <instance_number>\n\
    removes a lv2 effect from pedalboard\n\
    e.g.: remove 0\n\
\n\
connect <origin_port> <destination_port>\n\
    connects two ports of effects, hardware or MIDI\n\
    e.g.: connect system:capture_1 effect_0:in\n\
\n\
disconnect <origin_port> <destination_port>\n\
    disconnects two ports of effects, hardware or MIDI\n\
    e.g.: disconnect system:capture_1 effect_0:in\n\
\n\
preset <instance_number> <preset_name>\n\
    e.g.: preset 0 \"Invert CC Value\"\n\
\n\
param_set <instance_number> <param_symbol> <param_value>\n\
    change the value of a parameter\n\
    e.g.: param_set 0 gain 2.50\n\
\n\
param_get <instance_number> <param_symbol>\n\
    show the value of a parameter\n\
    e.g.: param_get 0 gain\n\
\n\
param_monitor <instance_number> <param_symbol> <cond_op> <value>\n\
    defines a parameter to be monitored\n\
    e.g: param_monitor 0 gain > 2.50\n\
\n\
monitor <addr> <port> <status>\n\
    controls the monitoring of parameters\n\
    e.g: monitor localhost 12345 1\n\
    if status = 1 start monitoring\n\
    if status = 0 stop monitoring\n\
\n\
map <instance_number> <param_symbol>\n\
    maps a MIDI controller to control a parameter\n\
    e.g.: map 0 gain\n\
\n\
unmap <instance_number> <param_symbol>\n\
    unmaps a MIDI controller\n\
    e.g.: unmap 0 gain\n\
\n\
bypass <instance_number> <bypass_value>\n\
    process or bypass an effect\n\
    e.g.: bypass 0 1\n\
    if bypass_value = 1 bypass the effect\n\
    if bypass_value = 0 process the effect\n\
\n\
load <filename>\n\
    loads the history of typed commands\n\
    e.g.: load my_preset\n\
\n\
save <filename>\n\
    saves the history of typed commands\n\
    e.g.: save my_preset\n\
\n\
cpu_load\n\
    shows the current jack cpu load\n\
    e.g: cpu_load\n\
\n\
help\n\
    show a help message\n\
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

static int g_verbose, g_interactive;


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

static void effects_add_cb(proto_t *proto)
{
    int resp;
    resp = effects_add(proto->list[1], atoi(proto->list[2]));

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_remove_cb(proto_t *proto)
{
    int resp;
    resp = effects_remove(atoi(proto->list[1]));

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_preset_cb(proto_t *proto)
{
    int resp;
    resp = effects_preset(atoi(proto->list[1]), proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_connect_cb(proto_t *proto)
{
    int resp;
    resp = effects_connect(proto->list[1], proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_disconnect_cb(proto_t *proto)
{
    int resp;
    resp = effects_disconnect(proto->list[1], proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_bypass_cb(proto_t *proto)
{
    int resp;
    resp = effects_bypass(atoi(proto->list[1]), atoi(proto->list[2]));

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_set_param_cb(proto_t *proto)
{
    int resp;
    resp = effects_set_parameter(atoi(proto->list[1]), proto->list[2], atof(proto->list[3]));

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_get_param_cb(proto_t *proto)
{
    int resp;
    float value;
    resp = effects_get_parameter(atoi(proto->list[1]), proto->list[2], &value);

    char buffer[128];
    if (resp >= 0)
        sprintf(buffer, "resp %i %.04f", resp, value);
    else
        sprintf(buffer, "resp %i", resp);

    protocol_response(buffer, proto);
}

static void effects_monitor_param_cb(proto_t *proto)
{
    int resp;
    if (monitor_status())
        resp = effects_monitor_parameter(atoi(proto->list[1]), proto->list[2],
                                         proto->list[3], atof(proto->list[4]));
    else
        resp = -1;

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void monitor_addr_set_cb(proto_t *proto)
{
    int resp;
    if (atoi(proto->list[3]) == 1)
        resp = monitor_start(proto->list[1], atoi(proto->list[2]));
    else
        resp = monitor_stop();

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_map_cb(proto_t *proto)
{
    int resp;
    resp = effects_map_parameter(atoi(proto->list[1]), proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);

    if (resp == 0 && (g_verbose || g_interactive))
    {
        strcat(buffer, "\nMIDI learning: move the controller to assign it to parameter");
    }

    protocol_response(buffer, proto);
}

static void effects_unmap_cb(proto_t *proto)
{
    int resp;
    resp = effects_unmap_parameter(atoi(proto->list[1]), proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void cpu_load_cb(proto_t *proto)
{
    float value = effects_jack_cpu_load();
    char buffer[128];
    sprintf(buffer, "resp 0 %.04f", value);

    protocol_response(buffer, proto);
}

static void load_cb(proto_t *proto)
{
    FILE *fp;

    fp = fopen(proto->list[1], "r");
    if (fp)
    {
        char line[1024];
        msg_t msg;
        msg.sender_id = STDOUT_FILENO;

        while (fgets(line, sizeof(line), fp))
        {
            printf(line);

            /* removes the \n at end of line */
            line[strlen(line)-1] = 0;

            /* fills the message struct and parse it */
            msg.data = line;
            msg.data_size = strlen(line);
            protocol_parse(&msg);

            printf("\n");
        }

        fclose(fp);
    }
    else
    {
        protocol_response("error: can't open the file", proto);
    }
}

static void save_cb(proto_t *proto)
{
    HIST_ENTRY *entry;

    if (history_length > 0)
    {
        /* removes the save command from history */
        entry = remove_history(history_length-1);
        free_history_entry(entry);

        /* saves the history in the file */
        write_history(proto->list[1]);
        protocol_response("resp 0", proto);
    }
}

static void help_cb(proto_t *proto)
{
    proto->response = 0;
    fprintf(stdout, HELP_MESSAGE);
    fflush(stdout);
}

static void quit_cb(proto_t *proto)
{
    protocol_response("resp 0", proto);

    protocol_remove_commands();
    socket_finish();
    effects_finish();
    exit(EXIT_SUCCESS);
}

void interactive_mode(void)
{
    msg_t msg;
    char *input;

    completer_init();

    msg.sender_id = STDOUT_FILENO;

    while (1)
    {
        input = readline("mod-host> ");
        rl_bind_key('\t', rl_complete);

        if (input)
        {
            if (*input)
            {
                /* adds the line on history */
                add_history(input);

                /* fills the message struct and parse it */
                msg.data = input;
                msg.data_size = strlen(input);
                protocol_parse(&msg);
                printf("\n");

                free(input);
                input = NULL;
            }
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
    int socket_port;

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

        g_verbose = _verbose->count;
        socket_port = _socket->ival[0];
        g_interactive = _interactive->count;
    }
    else
    {
        arg_print_errors(stderr, _end, argv[0]);
        exit(EXIT_FAILURE);
    }

    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));

    /* If verbose or interactive, don't fork */
    if (!g_verbose && !g_interactive)
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
    protocol_add_command(EFFECT_PRESET, effects_preset_cb);
    protocol_add_command(EFFECT_CONNECT, effects_connect_cb);
    protocol_add_command(EFFECT_DISCONNECT, effects_disconnect_cb);
    protocol_add_command(EFFECT_BYPASS, effects_bypass_cb);
    protocol_add_command(EFFECT_PARAM_SET, effects_set_param_cb);
    protocol_add_command(EFFECT_PARAM_GET, effects_get_param_cb);
    protocol_add_command(EFFECT_PARAM_MON, effects_monitor_param_cb);
    protocol_add_command(MONITOR_ADDR_SET, monitor_addr_set_cb);
    protocol_add_command(MAP_COMMANDS, effects_map_cb);
    protocol_add_command(UNMAP_COMMANDS, effects_unmap_cb);
    protocol_add_command(CPU_LOAD, cpu_load_cb);
    protocol_add_command(LOAD_COMMANDS, load_cb);
    protocol_add_command(SAVE_COMMANDS, save_cb);
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
    if (g_interactive) interactive_mode();

    /* Verbose */
    protocol_verbose(g_verbose);

    while (1) socket_run();

    protocol_remove_commands();
    socket_finish();
    effects_finish();

    return 0;
}
