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
#include <getopt.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef HAVE_FFTW335
#include <fftw3.h>
#endif

#include "mod-host.h"
#include "effects.h"
#include "socket.h"
#include "protocol.h"
#include "completer.h"
#include "monitor.h"
#include "info.h"


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

/* The text of help message is extracted from README file */
extern const char help_msg[];
/* The version is extracted from git history */
extern const char version[];


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

static void effects_preset_save_cb(proto_t *proto)
{
    int resp;
    resp = effects_preset_save(atoi(proto->list[1]), proto->list[3], proto->list[4], proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_preset_load_cb(proto_t *proto)
{
    int resp;
    resp = effects_preset_load(atoi(proto->list[1]), proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void effects_preset_show_cb(proto_t *proto)
{
    char *state_str = NULL;
    if (effects_preset_show(atoi(proto->list[1]), proto->list[2], &state_str) == SUCCESS)
    {
        if (state_str)
        {
            printf("%s", state_str);
            free(state_str);
        }
    }
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
    if (resp != SUCCESS)
    {
        resp = effects_set_property(atoi(proto->list[1]), proto->list[2], proto->list[3]);
    }

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
            printf("%s", line);

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

    unsigned int i;
    for (i = 0; i < sizeof(help_msg); i++)
        printf("%c", help_msg[i]);
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
    /* Command line options */
    static struct option long_options[] = {
        {"nofork", no_argument, 0, 'n'},
        {"verbose", no_argument, 0, 'v'},
        {"socket-port", required_argument, 0, 'p'},
        {"interactive", no_argument, 0, 'i'},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, opt_index = 0;

    /* parse command line options */
    int nofork = 0, verbose = 0, socket_port = SOCKET_DEFAULT_PORT, interactive = 0;
    while ((opt = getopt_long(argc, argv, "nvp:iVh", long_options, &opt_index)) != -1)
    {
        switch (opt)
        {
            case 'n':
                nofork = 1;
                break;

            case 'v':
                verbose = 1;
                nofork = 1;
                break;

            case 'p':
                socket_port = atoi(optarg);
                break;

            case 'i':
                interactive = 1;
                nofork = 1;
                break;

            case 'V':
                printf(
                    "%s version: %s\n"
                    "source code: https://github.com/portalmod/mod-host\n",
                argv[0], version);

                exit(EXIT_SUCCESS);
                break;

            case 'h':
                printf(
                    "Usage: %s [-vih] [-p <port>]\n"
                    "  -v, --verbose                  verbose messages\n"
                    "  -p, --socket-port=<port>       socket port definition\n"
                    "  -i, --interactive              interactive mode\n"
                    "  -V, --version                  print program version and exit\n"
                    "  -h, --help                     print this help and exit\n",
                argv[0]);

                exit(EXIT_SUCCESS);
        }
    }

    if (! nofork)
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

#ifdef HAVE_FFTW335
    /* Make fftw thread-safe */
    fftw_make_planner_thread_safe();
    fftwf_make_planner_thread_safe();
#endif

    /* Setup the protocol */
    protocol_add_command(EFFECT_ADD, effects_add_cb);
    protocol_add_command(EFFECT_REMOVE, effects_remove_cb);
    protocol_add_command(EFFECT_PRESET_LOAD, effects_preset_load_cb);
    protocol_add_command(EFFECT_PRESET_SAVE, effects_preset_save_cb);
    protocol_add_command(EFFECT_PRESET_SHOW, effects_preset_show_cb);
    protocol_add_command(EFFECT_CONNECT, effects_connect_cb);
    protocol_add_command(EFFECT_DISCONNECT, effects_disconnect_cb);
    protocol_add_command(EFFECT_BYPASS, effects_bypass_cb);
    protocol_add_command(EFFECT_PARAM_SET, effects_set_param_cb);
    protocol_add_command(EFFECT_PARAM_GET, effects_get_param_cb);
    protocol_add_command(EFFECT_PARAM_MON, effects_monitor_param_cb);
    protocol_add_command(MONITOR_ADDR_SET, monitor_addr_set_cb);
    protocol_add_command(CPU_LOAD, cpu_load_cb);
    protocol_add_command(LOAD_COMMANDS, load_cb);
    protocol_add_command(SAVE_COMMANDS, save_cb);
    protocol_add_command(HELP, help_cb);
    protocol_add_command(QUIT, quit_cb);

    /* Startup the effects */
    if (effects_init()) return -1;

    /* Setup the socket */
    if (socket_start(socket_port, SOCKET_MSG_BUFFER_SIZE) < 0)
    {
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
