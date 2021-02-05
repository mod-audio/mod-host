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
#include <unistd.h>

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

typedef struct FAKE_INPUT_CLIENT_T {
    jack_client_t *client;
    jack_port_t *ports[PORT_COUNT];
    bool in1_connected;
    bool in2_connected;
} fake_input_client_t;

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
    fake_input_client_t *const fak = arg;

    memcpy(jack_port_get_buffer(fak->ports[PORT_OUT1], nframes),
           jack_port_get_buffer(fak->ports[PORT_IN1], nframes),
           sizeof(float)*nframes);

    memcpy(jack_port_get_buffer(fak->ports[PORT_OUT2], nframes),
           jack_port_get_buffer(fak->ports[PORT_IN2], nframes),
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
        fprintf(stderr, "loading 2 instances of fake-input client is not allowed\n");
        return 1;
    }

    /* allocate fake-input client */
    fake_input_client_t *const fak = malloc(sizeof(fake_input_client_t));

    if (!fak)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    fak->client = client;

    /* Register jack ports */
    fak->ports[PORT_IN1 ] = jack_port_register(client, "source_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    fak->ports[PORT_IN2 ] = jack_port_register(client, "source_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    fak->ports[PORT_OUT1] = jack_port_register(client, "fake_capture_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal|JackPortIsPhysical, 0);
    fak->ports[PORT_OUT2] = jack_port_register(client, "fake_capture_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal|JackPortIsPhysical, 0);

    for (int i=0; i<PORT_COUNT; ++i)
    {
        if (! fak->ports[i])
        {
            fprintf(stderr, "can't register jack ports\n");
            free(fak);
            return 1;
        }
    }

    /* Set jack callbacks */
    jack_set_process_callback(client, ProcessMonitor, fak);

    /* Activate the jack client */
    if (jack_activate(client) != 0)
    {
        fprintf(stderr, "can't activate jack client\n");
        free(fak);
        return 1;
    }

    g_active = true;

    return 0;

    // unused
    (void)load_init;
}

__attribute__ ((visibility("default")))
void jack_finish(void* arg);

void jack_finish(void* arg)
{
    fake_input_client_t *const fak = arg;

    jack_deactivate(fak->client);

    g_active = false;

    for (int i=0; i<PORT_COUNT; ++i)
        jack_port_unregister(fak->client, fak->ports[i]);

    free(fak);
}
