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
#include <jack/jack.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>

#ifdef HAVE_FFTW335
#include <fftw3.h>
#endif

#ifdef HAVE_NE10
#include <NE10.h>
#endif

#ifdef HAVE_NEW_LILV
#include <lilv/lilv.h>
#else
#define lilv_free(x) free(x)
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

/* Wherever we should be running */
static volatile int running;
/* Thread that calls socket_run() for the JACK internal client */
static pthread_t intclient_socket_thread;

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
    if (effects_preset_show(proto->list[1], &state_str) == SUCCESS)
    {
        if (state_str)
        {
            protocol_response(state_str, proto);
            lilv_free(state_str);
            return;
        }
    }
    protocol_response("", proto);
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

static void effects_licensee_cb(proto_t *proto)
{
    char *licensee = NULL;

    if (effects_licensee(atoi(proto->list[1]), &licensee) == SUCCESS)
    {
        if (licensee)
        {
            protocol_response(licensee, proto);
            free(licensee);
            return;
        }
    }

    protocol_response("", proto);
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

static void monitor_output_cb(proto_t *proto)
{
    int resp;
    resp = !effects_monitor_output_parameter(atoi(proto->list[1]), proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void midi_learn_cb(proto_t *proto)
{
    int resp;
    resp = !effects_midi_learn(atoi(proto->list[1]), proto->list[2], atof(proto->list[3]), atof(proto->list[4]));

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void midi_map_cb(proto_t *proto)
{
    int resp;
    resp = !effects_midi_map(atoi(proto->list[1]), proto->list[2],
                             atoi(proto->list[3]), atoi(proto->list[4]),
                             atof(proto->list[5]), atof(proto->list[6]));

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void midi_unmap_cb(proto_t *proto)
{
    int resp;
    resp = !effects_midi_unmap(atoi(proto->list[1]), proto->list[2]);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void midi_program_listen_cb(proto_t *proto)
{
    effects_midi_program_listen(atoi(proto->list[1]), atoi(proto->list[2]));
    protocol_response("resp 0", proto);
}

static void cc_map_cb(proto_t *proto)
{
    int resp;
    int scalepoints_count = atoi(proto->list[11]);
    scalepoint_t *scalepoints;

    if (scalepoints_count != 0 && (scalepoints_count == 1 || proto->list_count < (uint32_t)(12+2*scalepoints_count)))
    {
        char buffer[128];
        sprintf(buffer, "resp %i", ERR_ASSIGNMENT_INVALID_OP);
        protocol_response(buffer, proto);
        return;
    }

    if (scalepoints_count >= 2)
    {
        scalepoints = malloc(sizeof(scalepoint_t)*scalepoints_count);

        if (scalepoints != NULL)
        {
            for (int i = 0; i < scalepoints_count; i++)
            {
                scalepoints[i].label = proto->list[12+i*2];
                scalepoints[i].value = atof(proto->list[12+i*2+1]);
            }
        }
        else
        {
            char buffer[128];
            sprintf(buffer, "resp %i", ERR_MEMORY_ALLOCATION);
            protocol_response(buffer, proto);
            return;
        }
    }
    else
    {
        scalepoints = NULL;
    }

    resp = effects_cc_map(atoi(proto->list[1]), // effect_id
                               proto->list[2],  // control_symbol
                          atoi(proto->list[3]), // device_id
                          atoi(proto->list[4]), // actuator_id
                               proto->list[5],  // label
                          atof(proto->list[6]), // value
                          atof(proto->list[7]), // minimum
                          atof(proto->list[8]), // maximum
                          atoi(proto->list[9]), // steps
                               proto->list[10], // unit
                          scalepoints_count, scalepoints);

    free(scalepoints);

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void cc_unmap_cb(proto_t *proto)
{
    int resp;
    resp = effects_cc_unmap(atoi(proto->list[1]), proto->list[2]);

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

static void bundle_add(proto_t *proto)
{
    effects_bundle_add(proto->list[1]);
    protocol_response("resp 0", proto);
}

static void bundle_remove(proto_t *proto)
{
    effects_bundle_remove(proto->list[1]);
    protocol_response("resp 0", proto);
}

static void feature_enable(proto_t *proto)
{
    const char* feature = proto->list[1];
    int enabled = atoi(proto->list[2]);
    int resp;

    if (!strcmp(feature, "link"))
        resp = effects_link_enable(enabled);
    else if (!strcmp(feature, "processing"))
        resp = effects_processing_enable(enabled);
    else
        resp = ERR_INVALID_OPERATION;

    char buffer[128];
    sprintf(buffer, "resp %i", resp);
    protocol_response(buffer, proto);
}

static void transport(proto_t *proto)
{
    effects_transport(atoi(proto->list[1]), atof(proto->list[2]), atof(proto->list[3]));
    protocol_response("resp 0", proto);
}

static void output_data_ready(proto_t *proto)
{
    effects_output_data_ready();
    protocol_response("resp 0", proto);
}

static void help_cb(proto_t *proto)
{
    proto->response = 0;

    size_t i, len = strlen(help_msg);
    for (i = 0; i < len; i++)
        printf("%c", help_msg[i]);
    fflush(stdout);
}

static void quit_cb(proto_t *proto)
{
    protocol_response("resp 0", proto);

    protocol_remove_commands();
    socket_finish();
    effects_finish(1);
    exit(EXIT_SUCCESS);
}

static void interactive_mode(void)
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

static void term_signal(int sig)
{
    running = 0;
    socket_finish();

    /* unused */
    return; (void)sig;
}

static int mod_host_init(jack_client_t* client, int socket_port, int feedback_port)
{
#ifdef HAVE_FFTW335
    /* Make fftw thread-safe */
    fftw_make_planner_thread_safe();
    fftwf_make_planner_thread_safe();
#endif
#ifdef HAVE_NE10
    /* Initialize ne10 */
    ne10_init();
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
    protocol_add_command(EFFECT_LICENSEE, effects_licensee_cb);
    protocol_add_command(MONITOR_ADDR_SET, monitor_addr_set_cb);
    protocol_add_command(MONITOR_OUTPUT, monitor_output_cb);
    protocol_add_command(MIDI_LEARN, midi_learn_cb);
    protocol_add_command(MIDI_MAP, midi_map_cb);
    protocol_add_command(MIDI_UNMAP, midi_unmap_cb);
    protocol_add_command(MIDI_PROGRAM_LISTEN, midi_program_listen_cb);
    protocol_add_command(CC_MAP, cc_map_cb);
    protocol_add_command(CC_UNMAP, cc_unmap_cb);
    protocol_add_command(CPU_LOAD, cpu_load_cb);
    protocol_add_command(LOAD_COMMANDS, load_cb);
    protocol_add_command(SAVE_COMMANDS, save_cb);
    protocol_add_command(BUNDLE_ADD, bundle_add);
    protocol_add_command(BUNDLE_REMOVE, bundle_remove);
    protocol_add_command(FEATURE_ENABLE, feature_enable);
    protocol_add_command(TRANSPORT, transport);
    protocol_add_command(OUTPUT_DATA_READY, output_data_ready);

    /* skip help and quit for internal client */
    if (client == NULL)
    {
        protocol_add_command(HELP, help_cb);
        protocol_add_command(QUIT, quit_cb);
    }

    /* Startup the effects */
    if (effects_init(client))
        return -1;

    /* Setup the socket */
    if (socket_start(socket_port, feedback_port, SOCKET_MSG_BUFFER_SIZE) < 0)
        return -1;

    socket_set_receive_cb(protocol_parse);

    return 0;
}

static void* intclient_socket_run(void* ptr)
{
    while (running)
        socket_run(0);

    /* unused */
    return NULL;
    (void)ptr;
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
        {"feedback-port", required_argument, 0, 'f'},
        {"interactive", no_argument, 0, 'i'},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, opt_index = 0;

    /* parse command line options */
    int nofork = 0, verbose = 0,  interactive = 0;
    int socket_port = SOCKET_DEFAULT_PORT, feedback_port = 0;
    while ((opt = getopt_long(argc, argv, "nvp:f:iVh", long_options, &opt_index)) != -1)
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

            case 'f':
                feedback_port = atoi(optarg);
                break;

            case 'i':
                interactive = 1;
                nofork = 1;
                break;

            case 'V':
                printf(
                    "%s version: %s\n"
                    "source code: https://github.com/moddevices/mod-host\n",
                argv[0], version);

                exit(EXIT_SUCCESS);
                break;

            case 'h':
                printf(
                    "Usage: %s [-vih] [-p <port>]\n"
                    "  -v, --verbose                  verbose messages\n"
                    "  -p, --socket-port=<port>       socket port definition\n"
                    "  -f, --feedback-port=<port>     feedback port definition\n"
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

    if (mod_host_init(NULL, socket_port, feedback_port) != 0)
    {
        exit(EXIT_FAILURE);
        return 1;
    }

    /* Interactive mode */
    if (interactive)
    {
        interactive_mode();
        return 0;
    }
    else
    {
        struct sigaction sig;
        memset(&sig, 0, sizeof(sig));

        sig.sa_handler = term_signal;
        sig.sa_flags   = SA_RESTART;
        sigemptyset(&sig.sa_mask);
        sigaction(SIGTERM, &sig, NULL);
        sigaction(SIGINT, &sig, NULL);
    }

    /* Verbose */
    protocol_verbose(verbose);

    /* Report ready */
    printf("mod-host ready!\n");
    fflush(stdout);

    running = 1;
    while (running) socket_run(interactive);

    socket_finish();
    effects_finish(1);
    protocol_remove_commands();

    return 0;
}

__attribute__ ((visibility("default")))
int jack_initialize(jack_client_t* client, const char* load_init);

int jack_initialize(jack_client_t* client, const char* load_init)
{
    int socket_port = SOCKET_DEFAULT_PORT;

    if (load_init != NULL && load_init[0] != '\0')
        socket_port = atoi(load_init);

    if (mod_host_init(client, socket_port, socket_port+1) != 0)
        return 1;

    running = 1;
    pthread_create(&intclient_socket_thread, NULL, intclient_socket_run, NULL);

    return 0;
}

__attribute__ ((visibility("default")))
void jack_finish(void* arg);

void jack_finish(void* arg)
{
    running = 0;
    socket_finish();
    pthread_join(intclient_socket_thread, NULL);
    effects_finish(0);
    protocol_remove_commands();

    // unused
    return; (void)arg;
}
