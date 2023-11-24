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

#include "monitor-client.h"
#include "../utils.h"
#include "../dsp/compressor_core.h"

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
    PORT_COUNT,
#ifdef _MOD_DEVICE_DUOX
    PORT_EXTRA_IN3 = PORT_COUNT,
    PORT_EXTRA_IN4,
    PORT_EXTRA_OUT3,
    PORT_EXTRA_OUT4,
    PORT_EXTRA_COUNT
#else
    PORT_EXTRA_COUNT = PORT_COUNT
#endif
};

/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct MONITOR_CLIENT_T {
    jack_client_t *client;
    jack_port_t *ports[PORT_EXTRA_COUNT];
    bool mono_copy;
    bool in1_connected;
    bool in2_connected;
#ifdef _MOD_DEVICE_DUOX
    bool extra_active;
    bool in3_connected;
    bool in4_connected;
#endif
    bool apply_compressor;
    bool apply_volume, apply_smoothing;
    bool muted;
    sf_compressor_state_st compressor;
#ifdef _MOD_DEVICE_DUOX
    sf_compressor_state_st compressor2;
#endif
    float volume, smooth_volume;
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
static monitor_client_t* g_monitor_handle = NULL;

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

static inline float db2lin(const float db)
{
    return powf(10.0f, 0.05f * db);
}

#ifdef _MOD_DEVICE_DUOX
static void ProcessMonitorExtra(monitor_client_t *const mon, jack_nframes_t nframes);
#endif

static int ProcessMonitor(jack_nframes_t nframes, void *arg)
{
    monitor_client_t *const mon = arg;
    const float *const bufIn1  = jack_port_get_buffer(mon->ports[PORT_IN1], nframes);
    const float *const bufIn2  = jack_port_get_buffer(mon->ports[PORT_IN2], nframes);
    /* */ float *const bufOut1 = jack_port_get_buffer(mon->ports[PORT_OUT1], nframes);
    /* */ float *const bufOut2 = jack_port_get_buffer(mon->ports[PORT_OUT2], nframes);

    if (mon->muted)
    {
        memset(bufOut1, 0, sizeof(float)*nframes);
        memset(bufOut2, 0, sizeof(float)*nframes);
#ifdef _MOD_DEVICE_DUOX
        if (mon->extra_active)
        {
            float *const bufOut3 = jack_port_get_buffer(mon->ports[PORT_EXTRA_OUT3], nframes);
            float *const bufOut4 = jack_port_get_buffer(mon->ports[PORT_EXTRA_OUT4], nframes);
            memset(bufOut3, 0, sizeof(float)*nframes);
            memset(bufOut4, 0, sizeof(float)*nframes);
        }
#endif
        return 0;
    }

    const float new_volume_weight = 0.001f;
    const float old_volume_weight = 1.f - new_volume_weight;

    const float volume = mon->volume;

    float smooth_volume = mon->smooth_volume;

    if (floats_differ_enough(volume, smooth_volume)) {
        mon->apply_volume = true;
        mon->apply_smoothing = true;
    }

    const bool apply_compressor = mon->apply_compressor;
    const bool apply_smoothing = mon->apply_smoothing;
    const bool apply_volume = mon->apply_volume;

#ifdef _MOD_DEVICE_DUOX
    if (mon->extra_active)
        ProcessMonitorExtra(mon, nframes);
#endif

    if (mon->in1_connected && mon->in2_connected)
    {
        if (bufOut1 != bufIn1)
            memcpy(bufOut1, bufIn1, sizeof(float)*nframes);
        if (bufOut2 != bufIn2)
            memcpy(bufOut2, bufIn2, sizeof(float)*nframes);

        // input1 and input2 have connections
        if (apply_compressor)
        {
            compressor_process(&mon->compressor, nframes, bufOut1, bufOut2);

            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOut1[i] *= smooth_volume;
                    bufOut2[i] *= smooth_volume;
                }
            }
        }
        else
        {
            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOut1[i] *= smooth_volume;
                    bufOut2[i] *= smooth_volume;
                }
            }
        }

        mon->apply_volume = floats_differ_enough(smooth_volume, 1.0f);
        mon->smooth_volume = smooth_volume;
        return 0;
    }

    if (mon->in1_connected || mon->in2_connected)
    {
        // only one input has connections
        const float *const bufInR  = mon->in1_connected ? bufIn1 : bufIn2;
        /* */ float *const bufOutR = mon->in1_connected ? bufOut1 : bufOut2;
        /* */ float *const bufOutC = mon->in1_connected ? bufOut2 : bufOut1;

        if (bufOutR != bufInR)
            memcpy(bufOutR, bufInR, sizeof(float)*nframes);

        if (apply_compressor)
        {
            compressor_process_mono(&mon->compressor, nframes, bufOutR);

            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOutR[i] *= smooth_volume;
                }
            }
        }
        else
        {
            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOutR[i] *= smooth_volume;
                }
            }
        }

        if (mon->mono_copy)
            memcpy(bufOutC, bufInR, sizeof(float)*nframes);
        else
            memset(bufOutC, 0, sizeof(float)*nframes);

        mon->apply_volume = floats_differ_enough(smooth_volume, 1.0f);
        mon->smooth_volume = smooth_volume;
        return 0;
    }

    // nothing connected in input1 or input2
    memset(bufOut1, 0, sizeof(float)*nframes);
    memset(bufOut2, 0, sizeof(float)*nframes);
    return 0;
}

#ifdef _MOD_DEVICE_DUOX
static void ProcessMonitorExtra(monitor_client_t *const mon, jack_nframes_t nframes)
{
    const float *const bufIn3  = jack_port_get_buffer(mon->ports[PORT_EXTRA_IN3], nframes);
    const float *const bufIn4  = jack_port_get_buffer(mon->ports[PORT_EXTRA_IN4], nframes);
    /* */ float *const bufOut3 = jack_port_get_buffer(mon->ports[PORT_EXTRA_OUT3], nframes);
    /* */ float *const bufOut4 = jack_port_get_buffer(mon->ports[PORT_EXTRA_OUT4], nframes);

    const float new_volume_weight = 0.001f;
    const float old_volume_weight = 1.f - new_volume_weight;

    const float volume = mon->volume;

    const bool apply_compressor = mon->apply_compressor;
    const bool apply_smoothing = mon->apply_smoothing;
    const bool apply_volume = mon->apply_volume;

    float smooth_volume = mon->smooth_volume;

    if (mon->in3_connected && mon->in4_connected)
    {
        if (bufOut3 != bufIn3)
            memcpy(bufOut3, bufIn3, sizeof(float)*nframes);
        if (bufOut4 != bufIn4)
            memcpy(bufOut4, bufIn4, sizeof(float)*nframes);

        // input3 and input4 have connections
        if (apply_compressor)
        {
            compressor_process(&mon->compressor2, nframes, bufOut3, bufOut4);

            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOut3[i] *= smooth_volume;
                    bufOut4[i] *= smooth_volume;
                }
            }
        }
        else
        {
            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOut3[i] *= smooth_volume;
                    bufOut4[i] *= smooth_volume;
                }
            }
        }
        return;
    }

    if (mon->in3_connected || mon->in4_connected)
    {
        // only one input has connections
        const float *const bufInR  = mon->in3_connected ? bufIn3 : bufIn4;
        /* */ float *const bufOutR = mon->in3_connected ? bufOut3 : bufOut4;
        /* */ float *const bufOutC = mon->in3_connected ? bufOut4 : bufOut3;

        if (bufOutR != bufInR)
            memcpy(bufOutR, bufInR, sizeof(float)*nframes);

        if (apply_compressor)
        {
            compressor_process_mono(&mon->compressor2, nframes, bufOutR);

            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOutR[i] *= smooth_volume;
                }
            }
        }
        else
        {
            if (apply_volume)
            {
                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                        smooth_volume = new_volume_weight * volume + old_volume_weight * smooth_volume;
                    bufOutR[i] *= smooth_volume;
                }
            }
        }
        memset(bufOutC, 0, sizeof(float)*nframes);
        return;
    }

    // nothing connected in input3 or input4
    memset(bufOut3, 0, sizeof(float)*nframes);
    memset(bufOut4, 0, sizeof(float)*nframes);
}
#endif

static int GraphOrder(void* arg)
{
    monitor_client_t *const mon = arg;

    mon->in1_connected = jack_port_connected(mon->ports[PORT_IN1]) > 0;
    mon->in2_connected = jack_port_connected(mon->ports[PORT_IN2]) > 0;

#ifdef _MOD_DEVICE_DUOX
    if (mon->extra_active)
    {
        mon->in3_connected = jack_port_connected(mon->ports[PORT_EXTRA_IN3]) > 0;
        mon->in4_connected = jack_port_connected(mon->ports[PORT_EXTRA_IN4]) > 0;
    }
#endif
    return 0;
}

#ifdef STANDALONE_MONITOR_CLIENT
__attribute__ ((visibility("default")))
int jack_initialize(jack_client_t* client, const char* load_init);
#else
static
#endif
int jack_initialize(jack_client_t* client, const char* load_init)
{
    /* can only be run once */
    if (g_active)
    {
        fprintf(stderr, "loading 2 instances of monitor client is not allowed\n");
        return 1;
    }

    /* allocate monitor client */
    monitor_client_t *const mon = calloc(sizeof(monitor_client_t), 1);

    if (!mon)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    mon->client = client;
    mon->in1_connected = false;
    mon->in2_connected = false;
    mon->mono_copy = (load_init && !strcmp(load_init, "1")) || access("/data/jack-mono-copy", F_OK) != -1;
#ifdef _MOD_DEVICE_DUOX
    mon->extra_active = access("/data/separate-spdif-outs", F_OK) != -1;
#endif

    mon->apply_compressor = false;
    mon->apply_volume = false;
    mon->muted = false;
    mon->volume = 1.0f;

    compressor_init(&mon->compressor, jack_get_sample_rate(client));
#ifdef _MOD_DEVICE_DUOX
    if (mon->extra_active)
        compressor_init(&mon->compressor2, jack_get_sample_rate(client));
#endif

    /* Register jack ports */
    mon->ports[PORT_IN1 ] = jack_port_register(client, "in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    mon->ports[PORT_IN2 ] = jack_port_register(client, "in_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    mon->ports[PORT_OUT1] = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    mon->ports[PORT_OUT2] = jack_port_register(client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    jack_port_tie(mon->ports[PORT_IN1], mon->ports[PORT_OUT1]);
    jack_port_tie(mon->ports[PORT_IN2], mon->ports[PORT_OUT2]);

    for (int i=0; i<PORT_COUNT; ++i)
    {
        if (! mon->ports[i])
        {
            fprintf(stderr, "can't register jack ports\n");
            free(mon);
            return 1;
        }
    }

#ifdef _MOD_DEVICE_DUOX
    if (mon->extra_active)
    {
        /* Register extra jack ports */
        mon->ports[PORT_EXTRA_IN3 ] = jack_port_register(client, "in_3", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        mon->ports[PORT_EXTRA_IN4 ] = jack_port_register(client, "in_4", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        mon->ports[PORT_EXTRA_OUT3] = jack_port_register(client, "out_3", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        mon->ports[PORT_EXTRA_OUT4] = jack_port_register(client, "out_4", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

        jack_port_tie(mon->ports[PORT_EXTRA_IN3], mon->ports[PORT_EXTRA_OUT3]);
        jack_port_tie(mon->ports[PORT_EXTRA_IN4], mon->ports[PORT_EXTRA_OUT4]);

        for (int i=PORT_COUNT; i<PORT_EXTRA_COUNT; ++i)
        {
            if (! mon->ports[i])
            {
                fprintf(stderr, "can't register jack ports\n");
                free(mon);
                return 1;
            }
        }
    }
#endif

    /* Set jack callbacks */
    jack_set_graph_order_callback(client, GraphOrder, mon);
    jack_set_process_callback(client, ProcessMonitor, mon);

    /* Activate the jack client */
    if (jack_activate(client) != 0)
    {
        fprintf(stderr, "can't activate jack client\n");
        free(mon);
        return 1;
    }

    g_active = true;
    g_monitor_handle = mon;

    /* Connect output ports */
    char ourportname[MAX_CHAR_BUF_SIZE+1];
    ourportname[MAX_CHAR_BUF_SIZE] = '\0';

    const char* const ourclientname = jack_get_client_name(client);

    if (jack_port_by_name(client, "system:playback_1") != NULL)
    {
       #ifndef _MOD_DEVICE_DWARF
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_1", ourclientname);
       #else
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_2", ourclientname);
       #endif
        jack_connect(client, ourportname, "system:playback_1");
    }

    if (jack_port_by_name(client, "system:playback_2") != NULL)
    {
       #ifndef _MOD_DEVICE_DWARF
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_2", ourclientname);
       #else
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_1", ourclientname);
       #endif
        jack_connect(client, ourportname, "system:playback_2");
    }

   #ifdef _MOD_DEVICE_DUOX
    if (jack_port_by_name(client, "system:playback_3") != NULL)
    {
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, mon->extra_active ? "%s:out_3" : "%s:out_1", ourclientname);
        jack_connect(client, ourportname, "system:playback_3");
    }

    if (jack_port_by_name(client, "system:playback_4") != NULL)
    {
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, mon->extra_active ? "%s:out_4" : "%s:out_2", ourclientname);
        jack_connect(client, ourportname, "system:playback_4");
    }
   #endif

    return 0;
}

#ifdef STANDALONE_MONITOR_CLIENT
__attribute__ ((visibility("default")))
void jack_finish(void* arg);
#else
static
#endif
void jack_finish(void* arg)
{
    monitor_client_t *const mon = arg;

    jack_deactivate(mon->client);

    g_monitor_handle = NULL;
    g_active = false;

    for (int i=0; i<PORT_COUNT; ++i)
        jack_port_unregister(mon->client, mon->ports[i]);

    free(mon);
}

bool monitor_client_init(void)
{
    jack_client_t *const client = jack_client_open("mod-monitor", JackNoStartServer|JackUseExactName, NULL);

    if (!client)
    {
        fprintf(stderr, "failed to open mod-monitor client\n");
        return false;
    }

    if (jack_initialize(client, NULL) != 0)
    {
        jack_client_close(client);
        return false;
    }

    return true;
}

bool monitor_client_setup_compressor(int mode, float release)
{
    monitor_client_t *const mon = g_monitor_handle;

    if (!mon)
    {
        fprintf(stderr, "asked to setup compressor while monitor client is not active\n");
        return false;
    }

    switch (mode)
    {
    case 1:
        compressor_set_params(&mon->compressor, -12.f, 12.f, 2.f, 0.0001f, release / 1000, -3.f);
#ifdef _MOD_DEVICE_DUOX
        if (mon->extra_active)
            compressor_set_params(&mon->compressor2, -12.f, 12.f, 2.f, 0.0001f, release / 1000, -3.f);
#endif
        break;
    case 2:
        compressor_set_params(&mon->compressor, -12.f, 12.f, 3.f, 0.0001f, release / 1000, -3.f);
#ifdef _MOD_DEVICE_DUOX
        if (mon->extra_active)
            compressor_set_params(&mon->compressor2, -12.f, 12.f, 3.f, 0.0001f, release / 1000, -3.f);
#endif
        break;
    case 3:
        compressor_set_params(&mon->compressor, -15.f, 15.f, 4.f, 0.0001f, release / 1000, -3.f);
#ifdef _MOD_DEVICE_DUOX
        if (mon->extra_active)
            compressor_set_params(&mon->compressor2, -15.f, 15.f, 4.f, 0.0001f, release / 1000, -3.f);
#endif
        break;
    case 4:
        compressor_set_params(&mon->compressor, -25.f, 15.f, 10.f, 0.0001f, release / 1000, -6.f);
#ifdef _MOD_DEVICE_DUOX
        if (mon->extra_active)
            compressor_set_params(&mon->compressor2, -25.f, 15.f, 10.f, 0.0001f, release / 1000, -6.f);
#endif
        break;
    }

    mon->apply_compressor = mode != 0;
    return true;
}

bool monitor_client_setup_volume(float volume)
{
    monitor_client_t *const mon = g_monitor_handle;

    if (!mon)
    {
        fprintf(stderr, "asked to setup volume while monitor client is not active\n");
        return false;
    }

    // local variables for calculations before changing the real struct values
    const float final_volume = db2lin(volume);
    const bool apply_volume = floats_differ_enough(final_volume, 1.0f);
    const bool muted = !floats_differ_enough(volume, -30.0f);

    mon->volume = final_volume;
    mon->apply_volume = apply_volume;
    mon->muted = muted;
    return true;
}

void monitor_client_stop(void)
{
    monitor_client_t *const mon = g_monitor_handle;

    if (!mon)
    {
        fprintf(stderr, "failed to close mod-monitor client\n");
        return;
    }

    jack_client_t *const client = mon->client;

    jack_finish(mon);
    jack_client_close(client);
}
