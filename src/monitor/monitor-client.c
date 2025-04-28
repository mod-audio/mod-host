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
#include "../mod-semaphore.h"
#include "../utils.h"
#include "../dsp/compressor_core.h"

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#if defined(_MOD_DEVICE_DUO) || defined(_MOD_DEVICE_DUOX) || defined(_MOD_DEVICE_DWARF)
#define MOD_MONITOR_STEREO_HANDLING
#endif

#if defined(_MOD_DEVICE_DUOX) || defined(_MOD_DEVICE_DWARF)
#define MOD_IO_PROCESSING_ENABLED
#endif

// used for local stack variables
#define MAX_CHAR_BUF_SIZE 255

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

typedef struct MONITOR_CLIENT_T {
    jack_client_t *client;
    jack_port_t **in_ports;
    jack_port_t **out_ports;
    sem_t wait_volume_sem;
    uint64_t connected;
    uint32_t numports;
    float volume, smooth_volume, step_volume;
   #ifdef MOD_IO_PROCESSING_ENABLED
    sf_compressor_state_st compressor;
   #endif
   #ifdef _MOD_DEVICE_DUOX
    sf_compressor_state_st compressor2;
    bool extra_active;
   #endif
   #ifdef MOD_MONITOR_STEREO_HANDLING
    bool mono_copy;
   #endif
    bool apply_compressor;
    bool apply_volume, apply_smoothing;
    bool muted;
    bool wait_volume;
} monitor_client_t;

/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

#define UNUSED_PARAM(var)           do { (void)(var); } while (0)

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

#ifdef MOD_MONITOR_STEREO_HANDLING
static float ProcessMonitorLoopStereo(monitor_client_t *const mon, jack_nframes_t nframes, uint32_t offset)
{
    const float *const bufIn1  = jack_port_get_buffer(mon->in_ports[offset], nframes);
    const float *const bufIn2  = jack_port_get_buffer(mon->in_ports[offset + 1], nframes);
    /* */ float *const bufOut1 = jack_port_get_buffer(mon->out_ports[offset], nframes);
    /* */ float *const bufOut2 = jack_port_get_buffer(mon->out_ports[offset + 1], nframes);

   #ifdef MOD_IO_PROCESSING_ENABLED
    const bool apply_compressor = mon->apply_compressor;
   #endif
    const bool apply_smoothing = mon->apply_smoothing;
    const bool apply_volume = mon->apply_volume;

    const float volume = mon->volume;
    const float step_volume = mon->step_volume;
    float smooth_volume = mon->smooth_volume;

    const bool in1_connected = mon->connected & (1 << offset);
    const bool in2_connected = mon->connected & (1 << (offset + 1));

   #ifdef _MOD_DEVICE_DUOX
    sf_compressor_state_st* const compressor = offset == 2 ? &mon->compressor2 : &mon->compressor;
   #else
    sf_compressor_state_st* const compressor = &mon->compressor;
   #endif

    if (in1_connected && in2_connected)
    {
        if (bufOut1 != bufIn1)
            memcpy(bufOut1, bufIn1, sizeof(float)*nframes);
        if (bufOut2 != bufIn2)
            memcpy(bufOut2, bufIn2, sizeof(float)*nframes);

       #ifdef MOD_IO_PROCESSING_ENABLED
        // input1 and input2 have connections
        if (apply_compressor)
        {
            compressor_process(compressor, nframes, bufOut1, bufOut2);

            if (apply_volume)
            {
                float dy;

                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                    {
                        dy = volume - smooth_volume;
                        smooth_volume += copysignf(fminf(fabsf(dy), step_volume), dy);
                    }

                    bufOut1[i] *= smooth_volume;
                    bufOut2[i] *= smooth_volume;
                }
            }
        }
        else
       #endif
        {
            if (apply_volume)
            {
                float dy;

                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                    {
                        dy = volume - smooth_volume;
                        smooth_volume += copysignf(fminf(fabsf(dy), step_volume), dy);
                    }

                    bufOut1[i] *= smooth_volume;
                    bufOut2[i] *= smooth_volume;
                }
            }
        }
    }
    else if (in1_connected || in2_connected)
    {
        // only one input has connections
        const float *const bufInR  = in1_connected ? bufIn1 : bufIn2;
        /* */ float *const bufOutR = in1_connected ? bufOut1 : bufOut2;
        /* */ float *const bufOutC = in1_connected ? bufOut2 : bufOut1;

        if (bufOutR != bufInR)
            memcpy(bufOutR, bufInR, sizeof(float)*nframes);

       #ifdef MOD_IO_PROCESSING_ENABLED
        if (apply_compressor)
        {
            compressor_process_mono(compressor, nframes, bufOutR);

            if (apply_volume)
            {
                float dy;

                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                    {
                        dy = volume - smooth_volume;
                        smooth_volume += copysignf(fminf(fabsf(dy), step_volume), dy);
                    }

                    bufOutR[i] *= smooth_volume;
                }
            }
        }
        else
       #endif
        {
            if (apply_volume)
            {
                float dy;

                for (jack_nframes_t i=0; i<nframes; ++i)
                {
                    if (apply_smoothing)
                    {
                        dy = volume - smooth_volume;
                        smooth_volume += copysignf(fminf(fabsf(dy), step_volume), dy);
                    }

                    bufOutR[i] *= smooth_volume;
                }
            }
        }

        if (offset == 0 && mon->mono_copy)
            memcpy(bufOutC, bufInR, sizeof(float)*nframes);
        else
            memset(bufOutC, 0, sizeof(float)*nframes);
    }
    else
    {
        // nothing connected in input1 or input2
        memset(bufOut1, 0, sizeof(float)*nframes);
        memset(bufOut2, 0, sizeof(float)*nframes);
    }

    return smooth_volume;
}
#endif

static int ProcessMonitor(jack_nframes_t nframes, void *arg)
{
    monitor_client_t *const mon = arg;

    if (mon->muted)
    {
        for (uint32_t i=0; i < mon->numports; ++i)
            memset(jack_port_get_buffer(mon->out_ports[i], nframes), 0, sizeof(float)*nframes);

        return 0;
    }

   #ifndef MOD_MONITOR_STEREO_HANDLING
    const float step_volume = mon->step_volume;
   #endif
    const float volume = mon->volume;
    float smooth_volume = mon->smooth_volume;

    if (floats_differ_enough(volume, smooth_volume))
    {
        mon->apply_volume = true;
        mon->apply_smoothing = true;
    }

  #ifdef MOD_MONITOR_STEREO_HANDLING
    smooth_volume = ProcessMonitorLoopStereo(mon, nframes, 0);

   #ifdef _MOD_DEVICE_DUOX
    if (mon->extra_active)
        ProcessMonitorLoopStereo(mon, nframes, 2);
   #endif
  #else
    const float* bufIn[mon->numports];
    /* */ float* bufOut[mon->numports];

    const bool apply_smoothing = mon->apply_smoothing;
    const bool apply_volume = mon->apply_volume;
    const uint64_t connected = mon->connected;

    for (uint32_t i=0; i < mon->numports; ++i)
    {
        bufIn[i] = jack_port_get_buffer(mon->in_ports[i], nframes);
        bufOut[i] = jack_port_get_buffer(mon->out_ports[i], nframes);

        if (connected & (1 << i))
        {
            if (bufOut[i] != bufIn[i])
                memcpy(bufOut[i], bufIn[i], sizeof(float)*nframes);
        }
        else
        {
            memset(bufOut[i], 0, sizeof(float)*nframes);
        }
    }

    if (apply_volume)
    {
        float dy;

        for (jack_nframes_t i=0; i<nframes; ++i)
        {
            if (apply_smoothing)
            {
                dy = volume - smooth_volume;
                smooth_volume += copysignf(fminf(fabsf(dy), step_volume), dy);
            }

            for (uint32_t j=0; j<mon->numports; ++j)
                bufOut[j][i] *= smooth_volume;
        }
    }
  #endif

    mon->apply_volume = floats_differ_enough(smooth_volume, 1.0f);
    mon->smooth_volume = smooth_volume;
    mon->muted = smooth_volume <= db2lin(MOD_MONITOR_VOLUME_MUTE) ||
               ! floats_differ_enough(smooth_volume, db2lin(MOD_MONITOR_VOLUME_MUTE));

    if (mon->wait_volume && fabsf(volume - smooth_volume) < 0.000001f)
    {
        mon->wait_volume = false;
        sem_post(&mon->wait_volume_sem);
    }

    return 0;
}

static int GraphOrder(void* arg)
{
    monitor_client_t *const mon = arg;

    for (uint32_t i=0; i<mon->numports; ++i)
    {
        const uint64_t flag = 1 << i;

        if (jack_port_connected(mon->in_ports[i]) > 0)
            mon->connected |= flag;
        else
            mon->connected &= ~flag;
    }

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
    monitor_client_t *const mon = calloc(1, sizeof(monitor_client_t));

    if (!mon)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    mon->client = client;
    mon->connected = 0;

   #ifdef MOD_IO_PROCESSING_ENABLED
    mon->apply_compressor = false;
    compressor_init(&mon->compressor, jack_get_sample_rate(client));
   #endif

   #ifdef _MOD_DEVICE_DUOX
    mon->extra_active = access("/data/separate-spdif-outs", F_OK) != -1;

    if (mon->extra_active)
        compressor_init(&mon->compressor2, jack_get_sample_rate(client));
   #endif

   #ifdef MOD_MONITOR_STEREO_HANDLING
    mon->mono_copy = (load_init && !strcmp(load_init, "1")) || access("/data/jack-mono-copy", F_OK) != -1;
   #endif

    mon->apply_volume = false;
    mon->muted = false;
    mon->wait_volume = false;
    mon->volume = mon->smooth_volume = 1.0f;
    mon->step_volume = 0.f;

    /* Register jack ports */
   #if defined(_MOD_DEVICE_DUO) || defined(_MOD_DEVICE_DWARF)
    const uint32_t numports = 2;
   #elif defined(_MOD_DEVICE_DUOX)
    const uint32_t numports = mon->extra_active ? 4 : 2;
   #elif defined(_DARKGLASS_DEVICE_PABLITO)
    const uint32_t numports = 10;
   #else
    uint32_t numports = 0;

    const char** const sysports = jack_get_ports(client, "system:playback_", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    if (sysports != NULL)
    {
        while (sysports[numports])
            ++numports;
        jack_free(sysports);
    }

    if (numports == 0)
    {
        fprintf(stderr, "no jack playback ports\n");
        free(mon);
        return 1;
    }
   #endif

    mon->numports = numports;
    mon->in_ports = malloc(sizeof(jack_port_t*) * numports);
    mon->out_ports = malloc(sizeof(jack_port_t*) * numports);

    if (!mon->in_ports || !mon->out_ports)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    char portname[MAX_CHAR_BUF_SIZE+1];
    portname[MAX_CHAR_BUF_SIZE] = '\0';

    for (uint32_t i=0; i<numports; ++i)
    {
        snprintf(portname, MAX_CHAR_BUF_SIZE, "in_%d", i + 1);
        mon->in_ports[i] = jack_port_register(client, portname, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

        snprintf(portname, MAX_CHAR_BUF_SIZE, "out_%d", i + 1);
        mon->out_ports[i] = jack_port_register(client, portname, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

        if (!mon->in_ports[i] || !mon->out_ports[i])
        {
            fprintf(stderr, "can't register jack ports\n");
            free(mon);
            return 1;
        }

        jack_port_tie(mon->in_ports[i], mon->out_ports[i]);
    }

    sem_init(&mon->wait_volume_sem, 0, 0);

    /* Set jack callbacks */
    jack_set_graph_order_callback(client, GraphOrder, mon);
    jack_set_process_callback(client, ProcessMonitor, mon);

    /* Activate the jack client */
    if (jack_activate(client) != 0)
    {
        fprintf(stderr, "can't activate jack client\n");
        sem_destroy(&mon->wait_volume_sem);
        free(mon);
        return 1;
    }

    g_active = true;
    g_monitor_handle = mon;

    /* Connect output ports */
    char ourportname[MAX_CHAR_BUF_SIZE+1];
    ourportname[MAX_CHAR_BUF_SIZE] = '\0';

    const char* const ourclientname = jack_get_client_name(client);

   #if defined(_DARKGLASS_DEVICE_PABLITO)
    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:in_9", ourclientname);
    jack_connect(client, "anagram-output:out1", ourportname);

    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:in_10", ourclientname);
    jack_connect(client, "anagram-output:out2", ourportname);

    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_5", ourclientname);
    jack_connect(client, ourportname, "system:playback_5");

    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_6", ourclientname);
    jack_connect(client, ourportname, "system:playback_6");
   #elif defined(_MOD_DEVICE_DWARF)
    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_2", ourclientname);
    jack_connect(client, ourportname, "system:playback_1");

    snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_1", ourclientname);
    jack_connect(client, ourportname, "system:playback_2");
   #else
    for (uint32_t i=0; i<numports; ++i)
    {
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_%d", ourclientname, i + 1);
        snprintf(portname, MAX_CHAR_BUF_SIZE, "system:playback_%d", i + 1);
        jack_connect(client, ourportname, portname);
    }
   #endif

   #ifdef _MOD_DEVICE_DUOX
    if (!mon->extra_active)
    {
        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_1", ourclientname);
        jack_connect(client, ourportname, "system:playback_3");

        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:out_2", ourclientname);
        jack_connect(client, ourportname, "system:playback_4");
    }
   #endif

    return 0;

   #ifndef MOD_MONITOR_STEREO_HANDLING
    UNUSED_PARAM(load_init);
   #endif
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
    sem_destroy(&mon->wait_volume_sem);

    g_monitor_handle = NULL;
    g_active = false;

    for (uint32_t i=0; i<mon->numports; ++i)
    {
        jack_port_unregister(mon->client, mon->in_ports[i]);
        jack_port_unregister(mon->client, mon->out_ports[i]);
    }

    free(mon->in_ports);
    free(mon->out_ports);
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

bool monitor_client_setup_compressor(int mode, float release)
{
   #ifdef MOD_IO_PROCESSING_ENABLED
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
   #else
    fprintf(stderr, "asked to setup compressor while IO processing is not enabled\n");
    return false;

    UNUSED_PARAM(mode);
    UNUSED_PARAM(release);
   #endif
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
    const float step_volume = fabsf(final_volume - mon->smooth_volume)
                            / (MOD_MONITOR_VOLUME_WAIT * jack_get_sample_rate(mon->client));
    const bool apply_volume = floats_differ_enough(final_volume, 1.0f);
    const bool unmute = volume > MOD_MONITOR_VOLUME_MUTE;

    mon->volume = final_volume;
    mon->step_volume = step_volume;
    mon->apply_volume = apply_volume;

    if (unmute)
        mon->muted = false;

    return true;
}

bool monitor_client_flush_volume(void)
{
    monitor_client_t *const mon = g_monitor_handle;

    if (!mon)
    {
        fprintf(stderr, "asked to flush volume while monitor client is not active\n");
        return false;
    }

    mon->smooth_volume = mon->volume;
    return true;
}

bool monitor_client_wait_volume(void)
{
    monitor_client_t *const mon = g_monitor_handle;

    if (!mon)
    {
        fprintf(stderr, "asked to wait for volume while monitor client is not active\n");
        return false;
    }

    mon->wait_volume = true;
    return sem_timedwait_secs(&mon->wait_volume_sem, 1) == 0;
}
