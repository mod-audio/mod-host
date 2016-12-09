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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jack/jack.h>

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

// used for local stack variables
#define MAX_CHAR_BUF_SIZE 255

/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/

enum Ports {
    PORT_IN1,
    PORT_IN2,
    PORT_OUT1,
    PORT_OUT2,
    PORT_COUNT
};

/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct MONITOR_CLIENT_T {
    jack_client_t *client;
    jack_port_t *ports[PORT_COUNT];
} monitor_client_t;

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

static bool g_active = false;

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

/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

static int ProcessMonitor(jack_nframes_t nframes, void *arg)
{
    monitor_client_t *const mon = arg;

    memcpy(jack_port_get_buffer(mon->ports[PORT_OUT1], nframes),
           jack_port_get_buffer(mon->ports[PORT_IN1], nframes),
           sizeof(float)*nframes);

    memcpy(jack_port_get_buffer(mon->ports[PORT_OUT2], nframes),
           jack_port_get_buffer(mon->ports[PORT_IN2], nframes),
           sizeof(float)*nframes);

    return 0;
}

__attribute__ ((visibility("default")))
int jack_initialize(jack_client_t* client, const char* load_init);

int jack_initialize(jack_client_t* client, const char* load_init)
{
    /* can only be run once */
    if (g_active)
    {
        fprintf(stderr, "loading 2 instances of monitor client is not allowed\n");
        return 1;
    }

    /* allocate monitor client */
    monitor_client_t *const mon = malloc(sizeof(monitor_client_t));

    if (!mon)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    mon->client = client;

    /* Register jack ports */
    mon->ports[PORT_IN1 ] = jack_port_register(client, "in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    mon->ports[PORT_IN2 ] = jack_port_register(client, "in_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    mon->ports[PORT_OUT1] = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    mon->ports[PORT_OUT1] = jack_port_register(client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    for (int i=0; i<PORT_COUNT; ++i)
    {
        if (! mon->ports[i])
        {
            fprintf(stderr, "can't register jack ports\n");
            return 1;
        }
    }

    /* Set jack callbacks */
    jack_set_process_callback(client, ProcessMonitor, mon);

    /* Activate the jack client */
    if (jack_activate(client) != 0)
    {
        fprintf(stderr, "can't activate jack client\n");
        return 1;
    }

    g_active = true;

    /* Connect output ports */
    char ourportname[MAX_CHAR_BUF_SIZE+1];
    ourportname[MAX_CHAR_BUF_SIZE] = '\0';

    const char* const ourclientname = jack_get_client_name(client);

    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_1", ourclientname);
    jack_connect(client, ourportname, "system:playback_1");

    if (jack_port_by_name(client, "mod-peakmeter:in_3") != NULL)
        jack_connect(client, ourportname, "mod-peakmeter:in_3");

    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_2", ourclientname);
    jack_connect(client, ourportname, "system:playback_2");

    if (jack_port_by_name(client, "mod-peakmeter:in_4") != NULL)
        jack_connect(client, ourportname, "mod-peakmeter:in_4");

    return 0;

    // unused
    (void)load_init;
}

__attribute__ ((visibility("default")))
void jack_finish(void* arg);

void jack_finish(void* arg)
{
    monitor_client_t *const mon = arg;

    jack_deactivate(mon->client);

    g_active = false;

    for (int i=0; i<PORT_COUNT; ++i)
        jack_port_unregister(mon->client, mon->ports[i]);

    free(mon);
}
