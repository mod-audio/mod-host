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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>

/* Jack */
#include <jack/jack.h>
#include <jack/midiport.h>

/* LV2 and Lilv */
#include <lilv/lilv.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/uri-map/uri-map.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/event/event.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/parameters/parameters.h>

/* Local */
#include "effects.h"
#include "monitor.h"
#include "uridmap.h"
#include "lv2_evbuf.h"
#include "worker.h"
#include "symap.h"
#include "atomic.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define REMOVE_ALL      (-1)


/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/

enum PortFlow {
    FLOW_UNKNOWN,
    FLOW_INPUT,
    FLOW_OUTPUT
};

enum PortType {
    TYPE_UNKNOWN,
    TYPE_CONTROL,
    TYPE_AUDIO,
    TYPE_EVENT
};

enum {
    URI_MAP_FEATURE,
    URID_MAP_FEATURE,
    URID_UNMAP_FEATURE,
    OPTIONS_FEATURE,
    WORKER_FEATURE,
    BUF_SIZE_POWER2_FEATURE,
    BUF_SIZE_FIXED_FEATURE,
    BUF_SIZE_BOUNDED_FEATURE,
    FEATURE_TERMINATOR
};


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct PORT_T {
    uint32_t index;
    enum PortType type;
    enum PortFlow flow;
    jack_port_t *jack_port;
    const LilvPort *lilv_port;
    float *buffer;
    uint32_t buffer_count;
    LV2_Evbuf *evbuf;
    bool old_ev_api;
} port_t;

typedef struct MONITOR_T {
    int port_id;
    int op;
    float value;
    float last_notified_value;
} monitor_t;


typedef struct EFFECT_T {
    int instance;
    jack_client_t *jack_client;
    LilvInstance *lilv_instance;
    const LilvPlugin *lilv_plugin;
    const LV2_Feature** features;

    port_t **ports;
    uint32_t ports_count;

    port_t **audio_ports;
    uint32_t audio_ports_count;
    port_t **input_audio_ports;
    uint32_t input_audio_ports_count;
    port_t **output_audio_ports;
    uint32_t output_audio_ports_count;

    port_t **event_ports;
    uint32_t event_ports_count;
    port_t **input_event_ports;
    uint32_t input_event_ports_count;
    port_t **output_event_ports;
    uint32_t output_event_ports_count;

    port_t **control_ports;
    uint32_t control_ports_count;

    monitor_t **monitors;
    uint32_t monitors_count;

    worker_t worker;

    int bypass;
} effect_t;

typedef struct URIDS_T {
	LV2_URID atom_Float;
	LV2_URID atom_Int;
	LV2_URID atom_eventTransfer;
	LV2_URID bufsz_maxBlockLength;
	LV2_URID bufsz_minBlockLength;
	LV2_URID bufsz_sequenceSize;
	LV2_URID log_Trace;
	LV2_URID midi_MidiEvent;
	LV2_URID param_sampleRate;
	LV2_URID patch_Set;
	LV2_URID patch_property;
	LV2_URID patch_value;
	LV2_URID time_Position;
	LV2_URID time_bar;
	LV2_URID time_barBeat;
	LV2_URID time_beatUnit;
	LV2_URID time_beatsPerBar;
	LV2_URID time_beatsPerMinute;
	LV2_URID time_frame;
	LV2_URID time_speed;
} urids_t;


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

#define UNUSED_PARAM(var)           do { (void)(var); } while (0)
#define IS_HW_INPUT_PORT(port)      (1 - (port % 2))
#define IS_HW_OUTPUT_PORT(port)     (port % 2)
#define RELOAD_PLUGINS()            lilv_world_load_all(LV2_Data);                  \
                                    Plugins = lilv_world_get_all_plugins(LV2_Data);
#define INSTANCE_IS_VALID(id)       ((id >= 0) && (id < MAX_INSTANCES))


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static effect_t Effects[MAX_INSTANCES];

/* Jack */
static jack_client_t *Jack_Global_Client;
static jack_nframes_t Sample_Rate, BlockLength;
static size_t MidiBufferSize;
volatile int xruns;

/* LV2 and Lilv */
static LilvWorld *LV2_Data;
static const LilvPlugins *Plugins;

/* Global features */
static Symap* symap;
static urids_t urids;
static LV2_URI_Map_Feature uri_map;
static LV2_URID_Map urid_map;
static LV2_URID_Unmap urid_unmap;
static LV2_Options_Option options[5];

static LV2_Feature uri_map_feature = {LV2_URI_MAP_URI, &uri_map};
static LV2_Feature urid_map_feature = {LV2_URID__map, &urid_map};
static LV2_Feature urid_unmap_feature = {LV2_URID__unmap, &urid_unmap};
static LV2_Feature options_feature = {LV2_OPTIONS__options, &options};
static LV2_Feature buf_size_features[3] = {
	{ LV2_BUF_SIZE__powerOf2BlockLength, NULL },
	{ LV2_BUF_SIZE__fixedBlockLength, NULL },
	{ LV2_BUF_SIZE__boundedBlockLength, NULL }
    };


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

static void InstanceDelete(int effect_id);
static int InstanceExist(int effect_id);
static void AllocatePortBuffers(effect_t* effect);
static int BufferSize(jack_nframes_t nframes, void* data);
static int ProcessAudio(jack_nframes_t nframes, void *arg);
static void GetFeatures(effect_t *effect);
static int XRun(void* data);


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

static void InstanceDelete(int effect_id)
{
    if (INSTANCE_IS_VALID(effect_id))
    {
        Effects[effect_id].lilv_instance = NULL;
    }
}


static int InstanceExist(int effect_id)
{
    if (INSTANCE_IS_VALID(effect_id))
    {
        return (int)(Effects[effect_id].lilv_instance != NULL);
    }

    return 0;
}


static void AllocatePortBuffers(effect_t* effect)
{
    uint32_t i;

    MidiBufferSize = jack_port_type_get_buffer_size(effect->jack_client, JACK_DEFAULT_MIDI_TYPE);
    for (i = 0; i < effect->event_ports_count; i++)
    {
        lv2_evbuf_free(effect->event_ports[i]->evbuf);
        effect->event_ports[i]->evbuf = lv2_evbuf_new(
            MidiBufferSize,
            effect->event_ports[i]->old_ev_api ? LV2_EVBUF_EVENT : LV2_EVBUF_ATOM,
            urid_map.map(urid_map.handle, lilv_node_as_string(lilv_new_uri(LV2_Data, LV2_ATOM__Chunk))),
            urid_map.map(urid_map.handle, lilv_node_as_string(lilv_new_uri(LV2_Data, LV2_ATOM__Sequence))));

        LV2_Atom_Sequence *buf;
        buf = lv2_evbuf_get_buffer(effect->event_ports[i]->evbuf);
        lilv_instance_connect_port(effect->lilv_instance, effect->event_ports[i]->index, buf);
	}
}


static int BufferSize(jack_nframes_t nframes, void* data)
{
    effect_t *effect = data;
    BlockLength = nframes;
    MidiBufferSize = jack_port_type_get_buffer_size(effect->jack_client, JACK_DEFAULT_MIDI_TYPE);
    AllocatePortBuffers(effect);
	return SUCCESS;
}

static int XRun(void* data)
{
    INC_ATOMIC(data);
    return SUCCESS;
}

static int ProcessAudio(jack_nframes_t nframes, void *arg)
{
    effect_t *effect;
    float *buffer_in, *buffer_out;
    unsigned int i;

    if (arg == NULL) return SUCCESS;
    effect = arg;

    /* Prepare midi/event ports */
    for (i = 0; i < effect->input_event_ports_count; i++)
    {
        lv2_evbuf_reset(effect->input_event_ports[i]->evbuf, true);
        LV2_Evbuf_Iterator iter = lv2_evbuf_begin(effect->input_event_ports[i]->evbuf);

        /* Write Jack MIDI input */
        void* buf = jack_port_get_buffer(effect->input_event_ports[i]->jack_port, nframes);
        uint32_t j;
        for (j = 0; j < jack_midi_get_event_count(buf); ++j)
        {
            jack_midi_event_t ev;
            jack_midi_event_get(&ev, buf, j);
            if (!lv2_evbuf_write(&iter, ev.time, 0, urids.midi_MidiEvent, ev.size, ev.buffer))
            {
                fprintf(stderr, "lv2 evbuf write failed\n");
            }
        }
    }

    for (i = 0; i < effect->output_event_ports_count; i++)
    {
        lv2_evbuf_reset(effect->output_event_ports[i]->evbuf, false);
    }

    /* Bypass */
    if (effect->bypass)
    {
        if (effect->input_audio_ports_count > 0)
        {
            for (i = 0; i < effect->output_audio_ports_count; i++)
            {
                if (i < effect->input_audio_ports_count)
                {
                    buffer_in = jack_port_get_buffer(effect->input_audio_ports[i]->jack_port, nframes);
                    memset(effect->input_audio_ports[i]->buffer, 0, (sizeof(float) * nframes));
                }
                else
                {
                    buffer_in = jack_port_get_buffer(effect->input_audio_ports[effect->input_audio_ports_count - 1]->jack_port, nframes);
                }
                buffer_out = jack_port_get_buffer(effect->output_audio_ports[i]->jack_port, nframes);
                memcpy(buffer_out, buffer_in, (sizeof(float) * nframes));

                /* Run the plugin with zero buffer to avoid 'pause behavior' in delay plugins */
                lilv_instance_run(effect->lilv_instance, nframes);

                memset(effect->output_audio_ports[i]->buffer, 0, (sizeof(float) * nframes));
            }
        }
        else // generator plugins
        {
            for (i = 0; i < effect->output_audio_ports_count; i++)
            {
                buffer_out = jack_port_get_buffer(effect->output_audio_ports[i]->jack_port, nframes);
                memset(buffer_out, 0, (sizeof(float) * nframes));
                lilv_instance_run(effect->lilv_instance, nframes);
            }
        }
    }
    /* Effect process */
    else
    {
        /* Copy the input buffers audio */
        for (i = 0; i < effect->input_audio_ports_count; i++)
        {
            buffer_in = jack_port_get_buffer(effect->input_audio_ports[i]->jack_port, nframes);
            memcpy(effect->input_audio_ports[i]->buffer, buffer_in, (sizeof(float) * nframes));
        }

        /* Run the effect */
        lilv_instance_run(effect->lilv_instance, nframes);

        /* Notify the plugin the run() cycle is finished */
        if (effect->worker.iface)
        {
            /* Process any replies from the worker. */
            worker_emit_responses(&effect->worker);
            if (effect->worker.iface->end_run) {
                effect->worker.iface->end_run(effect->lilv_instance->lv2_handle);
            }
        }

        /* Copy the output buffers audio */
        for (i = 0; i < effect->output_audio_ports_count; i++)
        {
            buffer_out = jack_port_get_buffer(effect->output_audio_ports[i]->jack_port, nframes);
            memcpy(buffer_out, effect->output_audio_ports[i]->buffer, (sizeof(float) * nframes));
        }

        for (i = 0; i < effect->monitors_count; i++) {
            int port_id = effect->monitors[i]->port_id;
            float value = *(effect->ports[port_id]->buffer);
            if (monitor_check_condition(effect->monitors[i]->op, effect->monitors[i]->value, value) &&
                value != effect->monitors[i]->last_notified_value) {
                const LilvNode *symbol_node = lilv_port_get_symbol(effect->lilv_plugin, effect->ports[port_id]->lilv_port);
                const char *symbol = lilv_node_as_string(symbol_node);
                if (monitor_send(effect->instance, symbol, value) >= 0)
                    effect->monitors[i]->last_notified_value = value;
            }
        }
    }

    /* MIDI out events */
    uint32_t p;
	for (p = 0; p < effect->output_event_ports_count; p++)
    {
		port_t *port = effect->output_event_ports[p];
		if (port->jack_port && port->flow == FLOW_OUTPUT && port->type == TYPE_EVENT)
        {
			void* buf = jack_port_get_buffer(port->jack_port, nframes);
			jack_midi_clear_buffer(buf);

            LV2_Evbuf_Iterator i;
			for (i = lv2_evbuf_begin(port->evbuf); lv2_evbuf_is_valid(i); i = lv2_evbuf_next(i))
            {
				uint32_t frames, subframes, type, size;
				uint8_t* body;
				lv2_evbuf_get(i, &frames, &subframes, &type, &size, &body);
				if (type == urids.midi_MidiEvent)
                {
					jack_midi_event_write(buf, frames, body, size);
				}
			}
		}
    }
    return SUCCESS;
}


static void GetFeatures(effect_t *effect)
{
    const LV2_Feature **features = (const LV2_Feature**) calloc(FEATURE_TERMINATOR+1, sizeof(LV2_Feature*));

    /* URI and URID Features are the same for all instances (global declaration) */

    /* Options Feature is the same for all instances (global declaration) */

    /* Worker Feature */
    LV2_Feature *work_schedule_feature = (LV2_Feature *) malloc(sizeof(LV2_Feature));
    work_schedule_feature->URI = LV2_WORKER__schedule;
    work_schedule_feature->data = NULL;

    LilvNode *lilv_worker_schedule = lilv_new_uri(LV2_Data, LV2_WORKER__schedule);
	if (lilv_plugin_has_feature(effect->lilv_plugin, lilv_worker_schedule))
    {
        LV2_Worker_Schedule *schedule = (LV2_Worker_Schedule*) malloc(sizeof(LV2_Worker_Schedule));
        schedule->handle = &effect->worker;
        schedule->schedule_work = worker_schedule;
        work_schedule_feature->data = schedule;
	}
    lilv_node_free(lilv_worker_schedule);

    /* Buf-size Feature is the same for all instances (global declaration) */

    /* Features array */
    features[URI_MAP_FEATURE]           = &uri_map_feature;
    features[URID_MAP_FEATURE]          = &urid_map_feature;
    features[URID_UNMAP_FEATURE]        = &urid_unmap_feature;
    features[OPTIONS_FEATURE]           = &options_feature;
    features[WORKER_FEATURE]            = work_schedule_feature;
    features[BUF_SIZE_POWER2_FEATURE]   = &buf_size_features[0];
    features[BUF_SIZE_FIXED_FEATURE]    = &buf_size_features[1];
    features[BUF_SIZE_BOUNDED_FEATURE]  = &buf_size_features[2];
    features[FEATURE_TERMINATOR]        = NULL;

    effect->features = features;
}


void FreeFeatures(effect_t *effect)
{
    worker_finish(&effect->worker);

    if (effect->features[WORKER_FEATURE]->data)
        free(effect->features[WORKER_FEATURE]->data);

    if (effect->features[WORKER_FEATURE])
        free((void*)effect->features[WORKER_FEATURE]);

    if (effect->features)
        free(effect->features);
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

int effects_init(void)
{
    /* This global client is for connections / disconnections */
    Jack_Global_Client = jack_client_open("Global", JackNoStartServer, NULL);


    if (Jack_Global_Client == NULL)
    {
        return ERR_JACK_CLIENT_CREATION;
    }

    /* Initialise XRun Info */
    xruns = 0;
    jack_reset_max_delayed_usecs(Jack_Global_Client);

    /* Get sample rate */
    Sample_Rate = jack_get_sample_rate(Jack_Global_Client);

    /* Get buffers size */
    BlockLength = jack_get_buffer_size(Jack_Global_Client);
    MidiBufferSize = jack_port_type_get_buffer_size(Jack_Global_Client, JACK_DEFAULT_MIDI_TYPE);

    /* Load all LV2 data */
    LV2_Data = lilv_world_new();
    lilv_world_load_all(LV2_Data);
    Plugins = lilv_world_get_all_plugins(LV2_Data);

    /* URI and URID Feature initialization */
    urid_sem_init();
    symap = symap_new();

    uri_map.callback_data = symap;
    uri_map.uri_to_id = &uri_to_id;

    urid_map.handle = symap;
    urid_map.map = urid_to_id;
    urid_unmap.handle = symap;
    urid_unmap.unmap = id_to_urid;

	urids.atom_Float           = urid_to_id(symap, LV2_ATOM__Float);
	urids.atom_Int             = urid_to_id(symap, LV2_ATOM__Int);
	urids.atom_eventTransfer   = urid_to_id(symap, LV2_ATOM__eventTransfer);
	urids.bufsz_maxBlockLength = urid_to_id(symap, LV2_BUF_SIZE__maxBlockLength);
	urids.bufsz_minBlockLength = urid_to_id(symap, LV2_BUF_SIZE__minBlockLength);
	urids.bufsz_sequenceSize   = urid_to_id(symap, LV2_BUF_SIZE__sequenceSize);
	urids.midi_MidiEvent       = urid_to_id(symap, LV2_MIDI__MidiEvent);
	urids.param_sampleRate     = urid_to_id(symap, LV2_PARAMETERS__sampleRate);
	urids.patch_Set            = urid_to_id(symap, LV2_PATCH__Set);
	urids.patch_property       = urid_to_id(symap, LV2_PATCH__property);
	urids.patch_value          = urid_to_id(symap, LV2_PATCH__value);
	urids.time_Position        = urid_to_id(symap, LV2_TIME__Position);
	urids.time_bar             = urid_to_id(symap, LV2_TIME__bar);
	urids.time_barBeat         = urid_to_id(symap, LV2_TIME__barBeat);
	urids.time_beatUnit        = urid_to_id(symap, LV2_TIME__beatUnit);
	urids.time_beatsPerBar     = urid_to_id(symap, LV2_TIME__beatsPerBar);
	urids.time_beatsPerMinute  = urid_to_id(symap, LV2_TIME__beatsPerMinute);
	urids.time_frame           = urid_to_id(symap, LV2_TIME__frame);
	urids.time_speed           = urid_to_id(symap, LV2_TIME__speed);

    /* Options Feature initialization */
    options[0].context = LV2_OPTIONS_INSTANCE;
    options[0].subject = 0;
    options[0].key = urids.param_sampleRate;
    options[0].size = sizeof(float);
    options[0].type = urids.atom_Int;
    options[0].value = &BlockLength;

    options[1].context = LV2_OPTIONS_INSTANCE;
    options[1].subject = 0;
    options[1].key = urids.bufsz_minBlockLength;
    options[1].size = sizeof(int32_t);
    options[1].type = urids.atom_Int;
    options[1].value = &BlockLength;

    options[2].context = LV2_OPTIONS_INSTANCE;
    options[2].subject = 0;
    options[2].key = urids.bufsz_maxBlockLength;
    options[2].size = sizeof(int32_t);
    options[2].type = urids.atom_Int;
    options[2].value = &BlockLength;

    options[3].context = LV2_OPTIONS_INSTANCE;
    options[3].subject = 0;
    options[3].key = urids.bufsz_sequenceSize;
    options[3].size = sizeof(int32_t);
    options[3].type = urids.atom_Int;
    options[3].value = &MidiBufferSize;

    options[4].context = LV2_OPTIONS_INSTANCE;
    options[4].subject = 0;
    options[4].key = 0;
    options[4].size = 0;
    options[4].type = 0;
    options[4].value = NULL;

    /* Init lilv_instance as NULL for all plugins */
    int i;
    for (i = 0; i < MAX_INSTANCES; i++)
    {
        Effects[i].lilv_instance = NULL;
    }

    jack_set_xrun_callback(Jack_Global_Client, &XRun, &xruns);

    /* Try activate the Jack client, this is needed for the xrun callback */
    if (jack_activate(Jack_Global_Client) != 0)
    {
        fprintf(stderr, "can't activate Jack_Global_Client\n");
        return ERR_JACK_CLIENT_ACTIVATION;
    }

    return SUCCESS;
}


int effects_finish(void)
{
    effects_remove(REMOVE_ALL);
    jack_client_close(Jack_Global_Client);
    symap_free(symap);
    lilv_world_free(LV2_Data);

    return SUCCESS;
}


int effects_add(const char *uid, int instance)
{
    unsigned int i, ports_count;
    char effect_name[32], port_name[256];
    float *audio_buffer, *control_buffer;
    jack_port_t *jack_port;
    uint32_t audio_ports_count, input_audio_ports_count, output_audio_ports_count;
    uint32_t control_ports_count, event_ports_count, input_event_ports_count, output_event_ports_count;
    effect_t *effect;
    int32_t error;

    /* Jack */
    jack_client_t *jack_client;
    jack_status_t jack_status;
    unsigned long jack_flags = 0;

    /* Lilv */
    const LilvPlugin *plugin;
    LilvInstance *lilv_instance;
    LilvNode *plugin_uri;
    LilvNode *lilv_input, *lilv_output, *lilv_control, *lilv_audio, *lilv_event, *lilv_midi;
    LilvNode *lilv_default, *lilv_minimum, *lilv_maximum, *lilv_atom_port, *lilv_worker_interface;
    const LilvPort *lilv_port;
    const LilvNode *symbol_node;

    if (!uid) return ERR_LV2_INVALID_URI;
    if (!INSTANCE_IS_VALID(instance)) return ERR_INSTANCE_INVALID;
    if (InstanceExist(instance)) return ERR_INSTANCE_ALREADY_EXISTS;

    effect = &Effects[instance];

    /* Init the struct */
    effect->instance = instance;
    effect->jack_client = NULL;
    effect->lilv_instance = NULL;
    effect->ports = NULL;
    effect->audio_ports = NULL;
    effect->input_audio_ports = NULL;
    effect->output_audio_ports = NULL;
    effect->control_ports = NULL;

    /* Init the pointers */
    lilv_instance = NULL;
    lilv_input = NULL;
    lilv_output = NULL;
    lilv_control = NULL;
    lilv_audio = NULL;
    lilv_midi = NULL;
    lilv_default = NULL;
    lilv_minimum = NULL;
    lilv_maximum = NULL;
    lilv_event = NULL;
    lilv_atom_port = NULL;
    lilv_worker_interface = NULL;

    /* Create a client to Jack */
    sprintf(effect_name, "effect_%i", instance);
    jack_client = jack_client_open(effect_name, JackNoStartServer, &jack_status);

    if (!jack_client)
    {
        fprintf(stderr, "can't get jack client\n");
        error = ERR_JACK_CLIENT_CREATION;
        goto error;
    }
    effect->jack_client = jack_client;

    /* Get the plugin */
    plugin_uri = lilv_new_uri(LV2_Data, uid);
    plugin = lilv_plugins_get_by_uri(Plugins, plugin_uri);
    lilv_node_free(plugin_uri);

    if (!plugin)
    {
        /* If the plugin are not found reload all plugins */
        RELOAD_PLUGINS();

        /* Try get the plugin again */
        plugin_uri = lilv_new_uri(LV2_Data, uid);
        plugin = lilv_plugins_get_by_uri(Plugins, plugin_uri);
        lilv_node_free(plugin_uri);

        if (!plugin)
        {
            fprintf(stderr, "can't get plugin\n");
            error = ERR_LV2_INVALID_URI;
            goto error;
        }
    }

    effect->lilv_plugin = plugin;

    /* Features */
    GetFeatures(effect);

    /* Create and activate the plugin instance */
    lilv_instance = lilv_plugin_instantiate(plugin, Sample_Rate, effect->features);

    if (!lilv_instance)
    {
        fprintf(stderr, "can't get lilv instance\n");
        error = ERR_LILV_INSTANTIATION;
        goto error;
    }
    effect->lilv_instance = lilv_instance;

    /* Worker */
    effect->worker.instance = lilv_instance;
    zix_sem_init(&effect->worker.sem, 0);
	effect->worker.iface = NULL;
	effect->worker.requests  = NULL;
	effect->worker.responses = NULL;
	effect->worker.response  = NULL;

    lilv_worker_interface = lilv_new_uri(LV2_Data, LV2_WORKER__interface);
	if (lilv_plugin_has_extension_data(effect->lilv_plugin, lilv_worker_interface))
    {
        LV2_Worker_Interface * worker_interface;
        worker_interface =
            (LV2_Worker_Interface*) lilv_instance_get_extension_data(effect->lilv_instance, LV2_WORKER__interface);

        worker_init(&effect->worker, worker_interface);
	}

    /* Create the URI for identify the ports */
    ports_count = lilv_plugin_get_num_ports(plugin);
    lilv_audio = lilv_new_uri(LV2_Data, LILV_URI_AUDIO_PORT);
    lilv_control = lilv_new_uri(LV2_Data, LILV_URI_CONTROL_PORT);
    lilv_input = lilv_new_uri(LV2_Data, LILV_URI_INPUT_PORT);
    lilv_output = lilv_new_uri(LV2_Data, LILV_URI_OUTPUT_PORT);
    lilv_event = lilv_new_uri(LV2_Data, LILV_URI_EVENT_PORT);
    lilv_atom_port = lilv_new_uri(LV2_Data, LV2_ATOM__AtomPort);
    lilv_midi = lilv_new_uri(LV2_Data, LILV_URI_MIDI_EVENT);

    /* Allocate memory to ports */
    audio_ports_count  = 0;
    input_audio_ports_count  = 0;
    output_audio_ports_count  = 0;
    control_ports_count  = 0;
    event_ports_count = 0;
    input_event_ports_count = 0;
    output_event_ports_count = 0;
    effect->monitors_count = 0;
    effect->monitors = NULL;
    effect->ports_count = ports_count;
    effect->ports = (port_t **) calloc(ports_count, sizeof(port_t *));
    for (i = 0; i < ports_count; i++) effect->ports[i] = NULL;

    for (i = 0; i < ports_count; i++)
    {
        /* Allocate memory to current port */
        effect->ports[i] = (port_t *) malloc(sizeof(port_t));
        effect->ports[i]->jack_port = NULL;
        effect->ports[i]->buffer = NULL;
        effect->ports[i]->evbuf = NULL;

        /* Lilv port */
        lilv_port = lilv_plugin_get_port_by_index(plugin, i);
        effect->ports[i]->index = i;
        effect->ports[i]->lilv_port = lilv_port;

        /* Port flow */
        effect->ports[i]->flow = FLOW_UNKNOWN;
        if (lilv_port_is_a(plugin, lilv_port, lilv_input))
        {
            jack_flags = JackPortIsInput;
            effect->ports[i]->flow = FLOW_INPUT;
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_output))
        {
            jack_flags = JackPortIsOutput;
            effect->ports[i]->flow = FLOW_OUTPUT;
        }

        symbol_node = lilv_port_get_symbol(plugin, lilv_port);
        sprintf(port_name, "%s", lilv_node_as_string(symbol_node));

        effect->ports[i]->type = TYPE_UNKNOWN;
        if (lilv_port_is_a(plugin, lilv_port, lilv_audio))
        {
            effect->ports[i]->type = TYPE_AUDIO;

            /* Allocate memory to audio buffer */
            audio_buffer = (float *) calloc(Sample_Rate, sizeof(float));

            if (!audio_buffer)
            {
                fprintf(stderr, "can't get audio buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }
            effect->ports[i]->buffer = audio_buffer;
            effect->ports[i]->buffer_count = Sample_Rate;
            lilv_instance_connect_port(lilv_instance, i, audio_buffer);

            /* Jack port creation */
            jack_port = jack_port_register(jack_client, port_name, JACK_DEFAULT_AUDIO_TYPE, jack_flags, 0);
            if (jack_port == NULL)
            {
                fprintf(stderr, "can't get jack port\n");
                error = ERR_JACK_PORT_REGISTER;
                goto error;
            }
            effect->ports[i]->jack_port = jack_port;

            audio_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, lilv_input)) input_audio_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output)) output_audio_ports_count++;
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_control))
        {
            effect->ports[i]->type = TYPE_CONTROL;

            /* Allocate memory to control port */
            control_buffer = (float *) malloc(sizeof(float));
            if (!control_buffer)
            {
                fprintf(stderr, "can't get control buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }
            effect->ports[i]->buffer = control_buffer;
            effect->ports[i]->buffer_count = 1;
            lilv_instance_connect_port(lilv_instance, i, control_buffer);

            /* Set the default value of control */
            lilv_port_get_range(plugin, lilv_port, &lilv_default, &lilv_minimum, &lilv_maximum);
            if (lilv_node_is_float(lilv_default) || lilv_node_is_int(lilv_default) || lilv_node_is_bool(lilv_default))
            {
                (*control_buffer) = lilv_node_as_float(lilv_default);
            }

            effect->ports[i]->jack_port = NULL;
            control_ports_count++;
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_event) ||
                    lilv_port_is_a(plugin, lilv_port, lilv_atom_port))
        {
            effect->ports[i]->old_ev_api = lilv_port_is_a(plugin, lilv_port, lilv_event) ? true : false;
            effect->ports[i]->type = TYPE_EVENT;
            jack_port = jack_port_register(jack_client, port_name, JACK_DEFAULT_MIDI_TYPE, jack_flags, 0);
            if (jack_port == NULL)
            {
                fprintf(stderr, "can't get jack port\n");
                error = ERR_JACK_PORT_REGISTER;
                goto error;
            }
            effect->ports[i]->jack_port = jack_port;
            event_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, lilv_input)) input_event_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output)) output_event_ports_count++;
        }
    }

    /* Allocate memory to indexes */
    effect->audio_ports_count = audio_ports_count;
    effect->audio_ports = (port_t **) calloc(audio_ports_count, sizeof(port_t *));
    effect->input_audio_ports_count = input_audio_ports_count;
    effect->input_audio_ports = (port_t **) calloc(input_audio_ports_count, sizeof(port_t *));
    effect->output_audio_ports_count = output_audio_ports_count;
    effect->output_audio_ports = (port_t **) calloc(output_audio_ports_count, sizeof(port_t *));
    effect->control_ports_count = control_ports_count;
    effect->control_ports = (port_t **) calloc(control_ports_count, sizeof(port_t *));
    effect->event_ports_count = event_ports_count;
    effect->event_ports = (port_t **) calloc(event_ports_count, sizeof(port_t *));
    effect->input_event_ports_count = input_event_ports_count;
    effect->input_event_ports = (port_t **) calloc(input_event_ports_count, sizeof(port_t *));
    effect->output_event_ports_count = output_event_ports_count;
    effect->output_event_ports = (port_t **) calloc(output_event_ports_count, sizeof(port_t *));

    /* Index the audio, control and event ports */
    audio_ports_count = 0;
    input_audio_ports_count = 0;
    output_audio_ports_count = 0;
    control_ports_count = 0;
    event_ports_count = 0;
    input_event_ports_count = 0;
    output_event_ports_count = 0;

    for (i = 0; i < ports_count; i++)
    {
        lilv_port = lilv_plugin_get_port_by_index(plugin, i);
        if (lilv_port_is_a(plugin, lilv_port, lilv_audio))
        {
            effect->audio_ports[audio_ports_count] = effect->ports[i];
            audio_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, lilv_input))
            {
                effect->input_audio_ports[input_audio_ports_count] = effect->ports[i];
                input_audio_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output))
            {
                effect->output_audio_ports[output_audio_ports_count] = effect->ports[i];
                output_audio_ports_count++;
            }
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_control))
        {
            effect->control_ports[control_ports_count] = effect->ports[i];
            control_ports_count++;
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_event) ||
                    lilv_port_is_a(plugin, lilv_port, lilv_atom_port))
        {
            effect->event_ports[event_ports_count] = effect->ports[i];
            event_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, lilv_input))
            {
                effect->input_event_ports[input_event_ports_count] = effect->ports[i];
                input_event_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output))
            {
                effect->output_event_ports[output_event_ports_count] = effect->ports[i];
                output_event_ports_count++;
            }
        }
    }

    AllocatePortBuffers(effect);

    /* Default value of bypass */
    effect->bypass = 0;

    lilv_node_free(lilv_audio);
    lilv_node_free(lilv_control);
    lilv_node_free(lilv_input);
    lilv_node_free(lilv_output);
    lilv_node_free(lilv_event);
    lilv_node_free(lilv_midi);
    lilv_node_free(lilv_atom_port);
    lilv_node_free(lilv_worker_interface);

    /* Jack callbacks */
    jack_set_process_callback(jack_client, &ProcessAudio, effect);
    jack_set_buffer_size_callback(jack_client, &BufferSize, effect);

    lilv_instance_activate(lilv_instance);

    /* Try activate the Jack client */
    if (jack_activate(jack_client) != 0)
    {
        fprintf(stderr, "can't activate jack_client\n");
        error = ERR_JACK_CLIENT_ACTIVATION;
        goto error;
    }

    return instance;

    error:
        lilv_node_free(lilv_audio);
        lilv_node_free(lilv_control);
        lilv_node_free(lilv_input);
        lilv_node_free(lilv_output);
        lilv_node_free(lilv_event);
        lilv_node_free(lilv_midi);
        lilv_node_free(lilv_atom_port);
        lilv_node_free(lilv_worker_interface);

        effects_remove(instance);

    return error;
}


int effects_remove(int effect_id)
{
    uint32_t i;
    int j, start, end;
    effect_t *effect;

    if (effect_id == REMOVE_ALL)
    {
        uint32_t j;
        char portA[64], portB[64];

        for (i = 1; i <= AUDIO_INPUT_PORTS; i++)
        {
            sprintf(portA, "system:capture_%i", i);

            for (j = 1; j <= AUDIO_INPUT_PORTS; j++)
            {
                sprintf(portB, "system:playback_%i", j);
                effects_disconnect(portA, portB);
            }
        }

        start = 0;
        end = MAX_INSTANCES - 10; // TODO: better way to exclude the Tools (10)
    }
    else
    {
        start = effect_id;
        end = start + 1;
    }

    for (j = start; j < end; j++)
    {
        if (InstanceExist(j))
        {
            effect = &Effects[j];
            FreeFeatures(effect);

            if (jack_deactivate(effect->jack_client) != 0) return ERR_JACK_CLIENT_DEACTIVATION;

            if (effect->ports)
            {
                for (i = 0; i < effect->ports_count; i++)
                {
                    if (effect->ports[i])
                    {
                        if (effect->ports[i]->buffer) free(effect->ports[i]->buffer);
                        free(effect->ports[i]);
                    }
                }
                free(effect->ports);
            }

            if (effect->lilv_instance) lilv_instance_deactivate(effect->lilv_instance);
            lilv_instance_free(effect->lilv_instance);
            if (effect->jack_client) jack_client_close(effect->jack_client);

            if (effect->audio_ports) free(effect->audio_ports);
            if (effect->input_audio_ports) free(effect->input_audio_ports);
            if (effect->output_audio_ports) free(effect->output_audio_ports);
            if (effect->control_ports) free(effect->control_ports);

            InstanceDelete(j);
        }
    }

    return SUCCESS;
}


int effects_connect(const char *portA, const char *portB)
{
    int ret;

    ret = jack_connect(Jack_Global_Client, portA, portB);
    if (ret != 0) ret = jack_connect(Jack_Global_Client, portB, portA);
    if (ret == EEXIST) ret = 0;

    if (ret != 0) return ERR_JACK_PORT_CONNECTION;

    return ret;
}


int effects_disconnect(const char *portA, const char *portB)
{
    int ret;

    ret = jack_disconnect(Jack_Global_Client, portA, portB);
    if (ret != 0) ret = jack_disconnect(Jack_Global_Client, portB, portA);
    if (ret == EEXIST) ret = 0;

    if (ret != 0) return ERR_JACK_PORT_DISCONNECTION;

    return ret;
}


int effects_set_parameter(int effect_id, const char *control_symbol, float value)
{
    uint32_t i;
    const char *symbol;
    const LilvPlugin *lilv_plugin;
    const LilvPort *lilv_port;
    const LilvNode *symbol_node;
    LilvNode *lilv_default, *lilv_minimum, *lilv_maximum;
    float min = 0.0, max = 0.0;

    if (InstanceExist(effect_id))
    {
        for (i = 0; i < Effects[effect_id].control_ports_count; i++)
        {
            lilv_plugin = Effects[effect_id].lilv_plugin;
            lilv_port = Effects[effect_id].control_ports[i]->lilv_port;
            symbol_node = lilv_port_get_symbol(lilv_plugin, lilv_port);
            symbol = lilv_node_as_string(symbol_node);

            if (strcmp(control_symbol, symbol) == 0)
            {
                lilv_port_get_range(lilv_plugin, lilv_port, &lilv_default, &lilv_minimum, &lilv_maximum);

                if (lilv_node_is_float(lilv_minimum) || lilv_node_is_int(lilv_minimum) || lilv_node_is_bool(lilv_minimum))
                    min = lilv_node_as_float(lilv_minimum);

                if (lilv_node_is_float(lilv_maximum) || lilv_node_is_int(lilv_maximum) || lilv_node_is_bool(lilv_maximum))
                    max = lilv_node_as_float(lilv_maximum);

                if (value > max) value = max;
                else if (value < min) value = min;
                *(Effects[effect_id].control_ports[i]->buffer) = value;

                return SUCCESS;
            }
        }

        return ERR_LV2_INVALID_PARAM_SYMBOL;
    }

    return ERR_INSTANCE_NON_EXISTS;
}

int effects_monitor_parameter(int effect_id, const char *control_symbol, const char *op, float value)
{
    float v;
    int ret = effects_get_parameter(effect_id, control_symbol, &v);

    if (ret != SUCCESS)
        return ret;

    int iop;
    if(strcmp(op, ">") == 0)
        iop = 0;
    else if(strcmp(op, ">=") == 0)
        iop = 1;
    else if(strcmp(op, "<") == 0)
        iop = 2;
    else if(strcmp(op, "<=") == 0)
        iop = 3;
    else if(strcmp(op, "==") == 0)
        iop = 4;
    else if(strcmp(op, "!=") == 0)
        iop = 5;
    else
        return -1;


    const LilvNode *symbol = lilv_new_string(LV2_Data, control_symbol);
    const LilvPort *port = lilv_plugin_get_port_by_symbol(Effects[effect_id].lilv_plugin, symbol);

    int port_id = lilv_port_get_index(Effects[effect_id].lilv_plugin, port);

    Effects[effect_id].monitors_count++;
    Effects[effect_id].monitors = (monitor_t**)realloc(Effects[effect_id].monitors, sizeof(monitor_t *) * Effects[effect_id].monitors_count);
    int idx = Effects[effect_id].monitors_count - 1;
    Effects[effect_id].monitors[idx] = (monitor_t*)malloc(sizeof(monitor_t));
    Effects[effect_id].monitors[idx]->port_id = port_id;
    Effects[effect_id].monitors[idx]->op = iop;
    Effects[effect_id].monitors[idx]->value = value;
    Effects[effect_id].monitors[idx]->last_notified_value = 0.0;
    return 0;
}

int effects_get_parameter(int effect_id, const char *control_symbol, float *value)
{
    uint32_t i;
    const char *symbol;
    const LilvPlugin *lilv_plugin;
    const LilvPort *lilv_port;
    const LilvNode *symbol_node;
    LilvNode *lilv_default, *lilv_minimum, *lilv_maximum;
    float min = 0.0, max = 0.0;

    if (InstanceExist(effect_id))
    {
        for (i = 0; i < Effects[effect_id].control_ports_count; i++)
        {
            lilv_plugin = Effects[effect_id].lilv_plugin;
            lilv_port = Effects[effect_id].control_ports[i]->lilv_port;
            symbol_node = lilv_port_get_symbol(lilv_plugin, lilv_port);
            symbol = lilv_node_as_string(symbol_node);

            if (strcmp(control_symbol, symbol) == 0)
            {
                lilv_port_get_range(lilv_plugin, lilv_port, &lilv_default, &lilv_minimum, &lilv_maximum);

                if (lilv_node_is_float(lilv_minimum) || lilv_node_is_int(lilv_minimum) || lilv_node_is_bool(lilv_minimum))
                    min = lilv_node_as_float(lilv_minimum);

                if (lilv_node_is_float(lilv_maximum) || lilv_node_is_int(lilv_maximum) || lilv_node_is_bool(lilv_maximum))
                    max = lilv_node_as_float(lilv_maximum);

                (*value) = *(Effects[effect_id].control_ports[i]->buffer);
                if ((*value) > max) (*value) = max;
                else if ((*value) < min) (*value) = min;
                return SUCCESS;
            }
        }

        return ERR_LV2_INVALID_PARAM_SYMBOL;
    }

    return ERR_INSTANCE_NON_EXISTS;
}


int effects_bypass(int effect_id, int value)
{
    if (InstanceExist(effect_id))
    {
        Effects[effect_id].bypass = value;
        return SUCCESS;
    }

    return ERR_INSTANCE_NON_EXISTS;
}


int effects_get_controls_symbols(int effect_id, char** symbols)
{
    if (!InstanceExist(effect_id))
    {
        symbols = NULL;
        return ERR_INSTANCE_NON_EXISTS;
    }

    uint32_t i;
    effect_t *effect = &Effects[effect_id];
    const LilvNode* symbol_node;

    for (i = 0; i < effect->control_ports_count; i++)
    {
        symbol_node = lilv_port_get_symbol(effect->lilv_plugin, effect->control_ports[i]->lilv_port);
        symbols[i] = (char *) lilv_node_as_string(symbol_node);
    }

    symbols[i] = NULL;

    return SUCCESS;
}

int effects_get_xruns(int *number_of_xruns, float *max_delay)
{
    *number_of_xruns = xruns;
    *max_delay = jack_get_max_delayed_usecs (Jack_Global_Client);
    RESET_ATOMIC(&xruns);
    jack_reset_max_delayed_usecs(Jack_Global_Client);
    return SUCCESS;
}
