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
#include <limits.h>
#include <math.h>
#include <pthread.h>

/* Jack */
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/transport.h>

/* LV2 and Lilv */
#include <lilv/lilv.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/uri-map/uri-map.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/event/event.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/presets/presets.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/ext/port-props/port-props.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/parameters/parameters.h>
#include "mod-license.h"

#ifdef HAVE_CONTROLCHAIN
/* Control Chain */
#include <cc/cc_client.h>
#endif

#ifdef HAVE_HYLIA
/* Hylia / Link */
#include <hylia.h>
#endif

#ifndef HAVE_NEW_LILV
#define lilv_free(x) free(x)
#warning Your current lilv version does not support loading or unloading bundles
#endif

#ifndef LV2_BUF_SIZE__nominalBlockLength
#define LV2_BUF_SIZE__nominalBlockLength  LV2_BUF_SIZE_PREFIX "nominalBlockLength"
#endif

#ifndef LILV_URI_CV_PORT
#define LILV_URI_CV_PORT "http://lv2plug.in/ns/lv2core#CVPort"
#endif

#ifndef LV2_CORE__enabled
#define LV2_CORE__enabled LV2_CORE_PREFIX "enabled"
#endif

#define LILV_NS_MOD "http://moddevices.com/ns/mod#"

#define KX_TIME__TicksPerBeat "http://kxstudio.sf.net/ns/lv2ext/props#TimePositionTicksPerBeat"

// custom jack flag used for cv
// needed because we prefer jack2 which doesn't have metadata yet
#define JackPortIsControlVoltage 0x100

/* Local */
#include "effects.h"
#include "monitor.h"
#include "socket.h"
#include "uridmap.h"
#include "lv2_evbuf.h"
#include "worker.h"
#include "symap.h"
#include "sha1/sha1.h"
#include "rtmempool/list.h"
#include "rtmempool/rtmempool.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define REMOVE_ALL        (-1)
#define GLOBAL_EFFECT_ID  (9995)

#define ASSIGNMENT_UNUSED -1 // item was used before, so there might other valid items after this one
#define ASSIGNMENT_NULL   -2 // item never used before, thus signaling the end of valid items

#define BYPASS_PORT_SYMBOL  ":bypass"
#define PRESETS_PORT_SYMBOL ":presets"
#define BPB_PORT_SYMBOL     ":bpb"
#define BPM_PORT_SYMBOL     ":bpm"
#define ROLLING_PORT_SYMBOL ":rolling"

// use pitchbend as midi cc, with an invalid MIDI controller number
#define MIDI_PITCHBEND_AS_CC 131

// transport defaults
#define TRANSPORT_TICKS_PER_BEAT 1920.0


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
    TYPE_CV,
    TYPE_EVENT
};

enum PortHints {
    // controls
    HINT_ENUMERATION   = 1 << 0,
    HINT_INTEGER       = 1 << 1,
    HINT_TOGGLE        = 1 << 2,
    HINT_TRIGGER       = 1 << 3,
    HINT_LOGARITHMIC   = 1 << 4,
    HINT_MONITORED     = 1 << 5, // outputs only
    // events
    HINT_TRANSPORT     = 1 << 0,
    HINT_OLD_EVENT_API = 1 << 1,
};

enum PluginHints {
    //HINT_TRANSPORT     = 1 << 0,
    HINT_TRIGGERS        = 1 << 1,
    HINT_OUTPUT_MONITORS = 1 << 2,
};

enum {
    URI_MAP_FEATURE,
    URID_MAP_FEATURE,
    URID_UNMAP_FEATURE,
    OPTIONS_FEATURE,
    WORKER_FEATURE,
    LICENSE_FEATURE,
    BUF_SIZE_POWER2_FEATURE,
    BUF_SIZE_FIXED_FEATURE,
    BUF_SIZE_BOUNDED_FEATURE,
    FEATURE_TERMINATOR
};

enum PostPonedEventType {
    POSTPONED_PARAM_SET,
    POSTPONED_OUTPUT_MONITOR,
    POSTPONED_PROGRAM_LISTEN,
    POSTPONED_MIDI_MAP,
    POSTPONED_TRANSPORT
};

enum UpdatePositionFlag {
    UPDATE_POSTION_SKIP,
    UPDATE_POSTION_IF_CHANGED,
    UPDATE_POSTION_FORCED,
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
    enum PortHints hints;
    const char* symbol;
    jack_port_t *jack_port;
    float *buffer;
    uint32_t buffer_count;
    LV2_Evbuf *evbuf;
    float min_value;
    float max_value;
    float def_value;
    float prev_value;
    LilvScalePoints* scale_points;
} port_t;

typedef struct PROPERTY_T {
    const LilvNode* label;
    const LilvNode* property;
} property_t;

typedef struct PROPERTY_EVENT_T {
    uint32_t size;
    uint8_t  body[];
} property_event_t;

typedef struct PRESET_T {
    const LilvNode* uri;
} preset_t;

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

    property_t **properties;
    uint32_t properties_count;

    port_t **audio_ports;
    uint32_t audio_ports_count;
    port_t **input_audio_ports;
    uint32_t input_audio_ports_count;
    port_t **output_audio_ports;
    uint32_t output_audio_ports_count;

    port_t **control_ports;
    uint32_t control_ports_count;
    port_t **input_control_ports;
    uint32_t input_control_ports_count;
    port_t **output_control_ports;
    uint32_t output_control_ports_count;

    port_t **cv_ports;
    uint32_t cv_ports_count;
    port_t **input_cv_ports;
    uint32_t input_cv_ports_count;
    port_t **output_cv_ports;
    uint32_t output_cv_ports_count;

    port_t **event_ports;
    uint32_t event_ports_count;
    port_t **input_event_ports;
    uint32_t input_event_ports_count;
    port_t **output_event_ports;
    uint32_t output_event_ports_count;

    // indexes of specially designated ports (-1 if unavailable)
    int32_t control_index; // control/event input
    int32_t enabled_index;
    int32_t freewheel_index;
    int32_t bpb_index;
    int32_t bpm_index;
    int32_t speed_index;

    preset_t **presets;
    uint32_t presets_count;

    monitor_t **monitors;
    uint32_t monitors_count;

    worker_t worker;
    const MOD_License_Interface* license_iface;

    jack_ringbuffer_t *events_buffer;

    // previous transport state
    bool transport_rolling;
    uint32_t transport_frame;
    double transport_bpb;
    double transport_bpm;

    // current and previous bypass state
    port_t bypass_port;
    float bypass;
    bool was_bypassed;

    // cached plugin information, avoids itenerating controls each cycle
    enum PluginHints hints;

    // virtual presets port
    port_t presets_port;
    float preset_value;
} effect_t;

typedef struct URIDS_T {
    LV2_URID atom_Float;
    LV2_URID atom_Double;
    LV2_URID atom_Int;
    LV2_URID atom_Long;
    LV2_URID atom_eventTransfer;
    LV2_URID bufsz_maxBlockLength;
    LV2_URID bufsz_minBlockLength;
    LV2_URID bufsz_nomimalBlockLength;
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
    LV2_URID time_beat;
    LV2_URID time_beatUnit;
    LV2_URID time_beatsPerBar;
    LV2_URID time_beatsPerMinute;
    LV2_URID time_ticksPerBeat;
    LV2_URID time_frame;
    LV2_URID time_speed;
} urids_t;

typedef struct MIDI_CC_T {
    int8_t channel;
    uint8_t controller;
    float minimum;
    float maximum;
    int effect_id;
    const char* symbol;
    const port_t* port;
} midi_cc_t;

typedef struct ASSIGNMENT_T {
    int effect_id;
    const port_t *port;
    int device_id;
    int assignment_id;
} assignment_t;

typedef struct POSTPONED_PARAMETER_EVENT_T {
    int effect_id;
    const char* symbol;
    float value;
} postponed_parameter_event_t;

typedef struct POSTPONED_PROGRAM_EVENT_T {
    int8_t value;
} postponed_program_event_t;

typedef struct POSTPONED_MIDI_MAP_EVENT_T {
    int effect_id;
    const char* symbol;
    int8_t channel;
    uint8_t controller;
    float value;
    float minimum;
    float maximum;
} postponed_midi_map_event_t;

typedef struct POSTPONED_TRANSPORT_EVENT_T {
    bool rolling;
    float bpb;
    float bpm;
} postponed_transport_event_t;

typedef struct POSTPONED_EVENT_T {
    enum PostPonedEventType type;
    union {
        postponed_parameter_event_t parameter;
        postponed_program_event_t program;
        postponed_midi_map_event_t midi_map;
        postponed_transport_event_t transport;
    };
} postponed_event_t;

typedef struct POSTPONED_EVENT_LIST_DATA {
    postponed_event_t event;
    struct list_head siblings;
} postponed_event_list_data;

typedef struct POSTPONED_CACHED_SYMBOL_LIST_DATA {
    int effect_id;
    char symbol[MAX_CHAR_BUF_SIZE+1];
    struct list_head siblings;
} postponed_cached_symbol_list_data;

typedef struct POSTPONED_CACHED_EVENTS {
    int last_effect_id;
    char last_symbol[MAX_CHAR_BUF_SIZE+1];
    postponed_cached_symbol_list_data symbols;
} postponed_cached_events;


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

#define UNUSED_PARAM(var)           do { (void)(var); } while (0)
#define INSTANCE_IS_VALID(id)       ((id >= 0) && (id < MAX_INSTANCES))


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static effect_t g_effects[MAX_INSTANCES];
static midi_cc_t g_midi_cc_list[MAX_MIDI_CC_ASSIGN], *g_midi_learning;

#ifdef HAVE_CONTROLCHAIN
/* Control Chain */
static cc_client_t *g_cc_client = NULL;
static assignment_t g_assignments_list[CC_MAX_DEVICES][CC_MAX_ASSIGNMENTS];
#endif

static struct list_head g_rtsafe_list;
static RtMemPool_Handle g_rtsafe_mem_pool;
static pthread_mutex_t  g_rtsafe_mutex;

static volatile int  g_postevents_running; // 0: stopped, 1: running, -1: stopped & about to close mod-host
static volatile bool g_postevents_ready;
static sem_t         g_postevents_semaphore;
static pthread_t     g_postevents_thread;

/* Jack */
static jack_client_t *g_jack_global_client;
static jack_nframes_t g_sample_rate, g_block_length;
static const char **g_capture_ports, **g_playback_ports;
static size_t g_midi_buffer_size;
static jack_port_t *g_midi_in_port;
static jack_position_t g_jack_pos;
static bool g_jack_rolling;
static volatile double g_transport_bpb;
static volatile double g_transport_bpm;
static volatile bool g_transport_reset;
static double g_transport_tick;
static bool g_processing_enabled;

/* LV2 and Lilv */
static LilvWorld *g_lv2_data;
static const LilvPlugins *g_plugins;
static LilvNode *g_sample_rate_node;

/* Global features */
static Symap* g_symap;
static urids_t g_urids;
static LV2_Atom_Forge g_lv2_atom_forge;
static LV2_URI_Map_Feature g_uri_map;
static LV2_URID_Map g_urid_map;
static LV2_URID_Unmap g_urid_unmap;
static LV2_Options_Option g_options[6];
static MOD_License_Feature g_license;

static LV2_Feature g_uri_map_feature = {LV2_URI_MAP_URI, &g_uri_map};
static LV2_Feature g_urid_map_feature = {LV2_URID__map, &g_urid_map};
static LV2_Feature g_urid_unmap_feature = {LV2_URID__unmap, &g_urid_unmap};
static LV2_Feature g_options_feature = {LV2_OPTIONS__options, &g_options};
static LV2_Feature g_license_feature = {MOD_LICENSE__feature, &g_license};
static LV2_Feature g_buf_size_features[3] = {
    { LV2_BUF_SIZE__powerOf2BlockLength, NULL },
    { LV2_BUF_SIZE__fixedBlockLength, NULL },
    { LV2_BUF_SIZE__boundedBlockLength, NULL }
    };

/* MIDI Learn */
static int g_midi_program_listen;
static pthread_mutex_t g_midi_learning_mutex;

#ifdef HAVE_HYLIA
static volatile bool hylia_enabled;
static hylia_t* g_hylia_instance;
static hylia_time_info_t g_hylia_timeinfo;
#endif

static const char* const g_bypass_port_symbol = BYPASS_PORT_SYMBOL;
static const char* const g_presets_port_symbol = PRESETS_PORT_SYMBOL;
static const char* const g_bpb_port_symbol = BPB_PORT_SYMBOL;
static const char* const g_bpm_port_symbol = BPM_PORT_SYMBOL;
static const char* const g_rolling_port_symbol = ROLLING_PORT_SYMBOL;


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

static void InstanceDelete(int effect_id);
static int InstanceExist(int effect_id);
static void AllocatePortBuffers(effect_t* effect);
static int BufferSize(jack_nframes_t nframes, void* data);
static void FreeWheelMode(int starting, void* data);
static void RunPostPonedEvents(int ignored_effect_id);
static void* PostPonedEventsThread(void* arg);
static int ProcessPlugin(jack_nframes_t nframes, void *arg);
static float UpdateValueFromMidi(midi_cc_t* mcc, uint16_t mvalue, bool highres);
static void UpdateGlobalJackPosition(enum UpdatePositionFlag flag);
static int ProcessMidi(jack_nframes_t nframes, void *arg);
static void JackTimebase(jack_transport_state_t state, jack_nframes_t nframes,
                         jack_position_t* pos, int new_pos, void* arg);
static void JackThreadInit(void *arg);
static void GetFeatures(effect_t *effect);
static property_t *FindEffectPropertyByLabel(effect_t *effect, const char *label);
static port_t *FindEffectInputPortBySymbol(effect_t *effect, const char *control_symbol);
static port_t *FindEffectOutputPortBySymbol(effect_t *effect, const char *control_symbol);
static const void* GetPortValueForState(const char* symbol, void* user_data, uint32_t* size, uint32_t* type);
static int LoadPresets(effect_t *effect);
static void FreeFeatures(effect_t *effect);
static char* GetLicenseFile(MOD_License_Handle handle, const char *license_uri);
static void FreeLicenseData(MOD_License_Handle handle, char *license);
#ifdef HAVE_CONTROLCHAIN
static void CCDataUpdate(void* arg);
static void InitializeControlChainIfNeeded(void);
#endif
#ifdef HAVE_HYLIA
static uint32_t GetHyliaOutputLatency(void);
#endif


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
        g_effects[effect_id].jack_client = NULL;
    }
}

static int InstanceExist(int effect_id)
{
    if (INSTANCE_IS_VALID(effect_id))
    {
        return (int)(g_effects[effect_id].jack_client != NULL);
    }

    return 0;
}

static void AllocatePortBuffers(effect_t* effect)
{
    uint32_t i;

    for (i = 0; i < effect->event_ports_count; i++)
    {
        lv2_evbuf_free(effect->event_ports[i]->evbuf);
        effect->event_ports[i]->evbuf = lv2_evbuf_new(
            g_midi_buffer_size,
            (effect->event_ports[i]->hints & HINT_OLD_EVENT_API) ? LV2_EVBUF_EVENT : LV2_EVBUF_ATOM,
            g_urid_map.map(g_urid_map.handle, LV2_ATOM__Chunk),
            g_urid_map.map(g_urid_map.handle, LV2_ATOM__Sequence));

        LV2_Atom_Sequence *buf;
        buf = lv2_evbuf_get_buffer(effect->event_ports[i]->evbuf);
        lilv_instance_connect_port(effect->lilv_instance, effect->event_ports[i]->index, buf);
    }
}

static int BufferSize(jack_nframes_t nframes, void* data)
{
    g_block_length = nframes;
    g_midi_buffer_size = jack_port_type_get_buffer_size(g_jack_global_client, JACK_DEFAULT_MIDI_TYPE);

    if (data)
    {
        effect_t *effect = data;
        AllocatePortBuffers(effect);
    }
#ifdef HAVE_HYLIA
    else if (g_hylia_instance)
    {
        hylia_set_output_latency(g_hylia_instance, GetHyliaOutputLatency());
    }
#endif

    return SUCCESS;
}

static void FreeWheelMode(int starting, void* data)
{
    effect_t *effect = data;
    if (effect->freewheel_index >= 0)
    {
        *(effect->ports[effect->freewheel_index]->buffer) = starting ? 1.0f : 0.0f;
    }
}

static bool ShouldIgnorePostPonedEvent(postponed_parameter_event_t* ev, postponed_cached_events* cached_events)
{
    // symbol must not be null
    if (ev->symbol == NULL)
        return false;

    if (ev->effect_id == cached_events->last_effect_id &&
        strncmp(ev->symbol, cached_events->last_symbol, MAX_CHAR_BUF_SIZE) == 0)
    {
        // already received this event, like just now
        return true;
    }

    // we received some events, but last one was different
    // we might be getting interleaved events, so let's check if it's there
    struct list_head *it;
    list_for_each(it, &cached_events->symbols.siblings)
    {
        postponed_cached_symbol_list_data* const psymbol = list_entry(it, postponed_cached_symbol_list_data, siblings);

        if (ev->effect_id == psymbol->effect_id &&
            strncmp(ev->symbol, psymbol->symbol, MAX_CHAR_BUF_SIZE) == 0)
        {
            // haha! found you little bastard!
            return true;
        }
    }

    // we'll process this event because it's the "last" of its type
    // also add it to the list of received events
    postponed_cached_symbol_list_data* const psymbol = malloc(sizeof(postponed_cached_symbol_list_data));

    if (psymbol)
    {
        psymbol->effect_id = ev->effect_id;
        strncpy(psymbol->symbol, ev->symbol, MAX_CHAR_BUF_SIZE);
        psymbol->symbol[MAX_CHAR_BUF_SIZE] = '\0';
        list_add_tail(&psymbol->siblings, &cached_events->symbols.siblings);
    }

    return false;
}

static void RunPostPonedEvents(int ignored_effect_id)
{
    // local queue to where we'll save rtsafe list
    struct list_head queue;
    INIT_LIST_HEAD(&queue);

    // move rtsafe list to our local queue, and clear it
    pthread_mutex_lock(&g_rtsafe_mutex);
    list_splice_init(&g_rtsafe_list, &queue);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    if (list_empty(&queue))
    {
        // nothing to do
        return;
    }

    // local buffer
    char buf[MAX_CHAR_BUF_SIZE+1];
    buf[MAX_CHAR_BUF_SIZE] = '\0';

    // cached data, to make sure we only handle similar events once
    bool got_midi_program = false;
    bool got_transport = false;
    postponed_cached_events cached_param_set, cached_output_mon;
    postponed_cached_symbol_list_data *psymbol;

    cached_param_set.last_effect_id = -1;
    cached_output_mon.last_effect_id = -1;
    cached_param_set.last_symbol[0] = '\0';
    cached_param_set.last_symbol[MAX_CHAR_BUF_SIZE] = '\0';
    cached_output_mon.last_symbol[0] = '\0';
    cached_output_mon.last_symbol[MAX_CHAR_BUF_SIZE] = '\0';
    INIT_LIST_HEAD(&cached_param_set.symbols.siblings);
    INIT_LIST_HEAD(&cached_output_mon.symbols.siblings);

    // itenerate backwards
    struct list_head *it, *it2;
    postponed_event_list_data* eventptr;

    list_for_each_prev(it, &queue)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);

        switch (eventptr->event.type)
        {
        case POSTPONED_PARAM_SET:
            if (eventptr->event.parameter.effect_id == ignored_effect_id)
                continue;
            if (ShouldIgnorePostPonedEvent(&eventptr->event.parameter, &cached_param_set))
                continue;

            snprintf(buf, MAX_CHAR_BUF_SIZE, "param_set %i %s %f", eventptr->event.parameter.effect_id,
                                                                   eventptr->event.parameter.symbol,
                                                                   eventptr->event.parameter.value);
            socket_send_feedback(buf);

            // save for fast checkup next time
            cached_param_set.last_effect_id = eventptr->event.parameter.effect_id;
            strncpy(cached_param_set.last_symbol, eventptr->event.parameter.symbol, MAX_CHAR_BUF_SIZE);
            break;

        case POSTPONED_OUTPUT_MONITOR:
            if (eventptr->event.parameter.effect_id == ignored_effect_id)
                continue;
            if (ShouldIgnorePostPonedEvent(&eventptr->event.parameter, &cached_output_mon))
                continue;

            snprintf(buf, MAX_CHAR_BUF_SIZE, "output_set %i %s %f", eventptr->event.parameter.effect_id,
                                                                    eventptr->event.parameter.symbol,
                                                                    eventptr->event.parameter.value);
            socket_send_feedback(buf);

            // save for fast checkup next time
            cached_output_mon.last_effect_id = eventptr->event.parameter.effect_id;
            strncpy(cached_output_mon.last_symbol, eventptr->event.parameter.symbol, MAX_CHAR_BUF_SIZE);
            break;

        case POSTPONED_MIDI_MAP:
            if (eventptr->event.midi_map.effect_id == ignored_effect_id)
                continue;
            snprintf(buf, MAX_CHAR_BUF_SIZE, "midi_mapped %i %s %i %i %f %f %f", eventptr->event.midi_map.effect_id,
                                                                                 eventptr->event.midi_map.symbol,
                                                                                 eventptr->event.midi_map.channel,
                                                                                 eventptr->event.midi_map.controller,
                                                                                 eventptr->event.midi_map.value,
                                                                                 eventptr->event.midi_map.minimum,
                                                                                 eventptr->event.midi_map.maximum);
            socket_send_feedback(buf);
            break;

        case POSTPONED_PROGRAM_LISTEN:
            if (got_midi_program)
                continue;

            snprintf(buf, MAX_CHAR_BUF_SIZE, "midi_program %i", eventptr->event.program.value);
            socket_send_feedback(buf);

            // ignore older midi program changes
            got_midi_program = true;
            break;

        case POSTPONED_TRANSPORT:
            if (got_transport)
                continue;

            snprintf(buf, MAX_CHAR_BUF_SIZE, "transport %i %f %f", eventptr->event.transport.rolling ? 1 : 0,
                                                                   eventptr->event.transport.bpb,
                                                                   eventptr->event.transport.bpm);
            socket_send_feedback(buf);

            // ignore older transport changes
            got_transport = true;
            break;
        }
    }

    // cleanup memory
    list_for_each_safe(it, it2, &cached_param_set.symbols.siblings)
    {
        psymbol = list_entry(it, postponed_cached_symbol_list_data, siblings);
        free(psymbol);
    }
    list_for_each_safe(it, it2, &cached_output_mon.symbols.siblings)
    {
        psymbol = list_entry(it, postponed_cached_symbol_list_data, siblings);
        free(psymbol);
    }

    // cleanup memory of rt queue, safely
    list_for_each_safe(it, it2, &queue)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);
        rtsafe_memory_pool_deallocate(g_rtsafe_mem_pool, eventptr);
    }

    if (g_postevents_ready)
    {
        // report data finished to server
        g_postevents_ready = false;
        socket_send_feedback("data_finish");
    }
}

static void* PostPonedEventsThread(void* arg)
{
    while (g_postevents_running == 1)
    {
        if (sem_timedwait_secs(&g_postevents_semaphore, 1) != 0)
            continue;

        if (g_postevents_running == 1 && g_postevents_ready)
            RunPostPonedEvents(-3); // as all effects are valid we set ignored_effect_id to -3
    }

    return NULL;

    UNUSED_PARAM(arg);
}

static int ProcessPlugin(jack_nframes_t nframes, void *arg)
{
    effect_t *effect;
    const float *buffer_in;
    float *buffer_out;
    unsigned int i;

    if (arg == NULL) return 0;
    effect = arg;

    if (!g_processing_enabled)
    {
        port_t *port;
        for (i = 0; i < effect->output_audio_ports_count; i++)
        {
            memset(jack_port_get_buffer(effect->output_audio_ports[i]->jack_port, nframes),
                   0, (sizeof(float) * nframes));
        }
        for (i = 0; i < effect->output_cv_ports_count; i++)
        {
            memset(jack_port_get_buffer(effect->output_cv_ports[i]->jack_port, nframes),
                   0, (sizeof(float) * nframes));
        }
        for (i = 0; i < effect->output_event_ports_count; i++)
        {
            port = effect->output_event_ports[i];
            if (port->jack_port && port->flow == FLOW_OUTPUT && port->type == TYPE_EVENT)
                jack_midi_clear_buffer(jack_port_get_buffer(port->jack_port, nframes));
        }
        return 0;
    }

    /* transport */
    uint8_t pos_buf[256];
    LV2_Atom* lv2_pos = (LV2_Atom*)pos_buf;

    if (effect->hints & HINT_TRANSPORT)
    {
        jack_position_t pos;
        const bool rolling = (jack_transport_query(effect->jack_client, &pos) == JackTransportRolling);

        if ((pos.valid & JackPositionBBT) == 0)
        {
            pos.beats_per_bar    = g_transport_bpb;
            pos.beats_per_minute = g_transport_bpm;
        }

        if (effect->transport_rolling != rolling ||
            effect->transport_frame != pos.frame ||
            effect->transport_bpb != (double)pos.beats_per_bar ||
            effect->transport_bpm != pos.beats_per_minute ||
            (effect->bypass < 0.5f && effect->was_bypassed))
        {
            effect->transport_rolling = rolling;
            effect->transport_frame = pos.frame;
            effect->transport_bpb = pos.beats_per_bar;
            effect->transport_bpm = pos.beats_per_minute;

            LV2_Atom_Forge forge = g_lv2_atom_forge;
            lv2_atom_forge_set_buffer(&forge, pos_buf, sizeof(pos_buf));

            LV2_Atom_Forge_Frame frame;
            lv2_atom_forge_object(&forge, &frame, 0, g_urids.time_Position);

            lv2_atom_forge_key(&forge, g_urids.time_speed);
            lv2_atom_forge_float(&forge, rolling ? 1.0f : 0.0f);

            lv2_atom_forge_key(&forge, g_urids.time_frame);
            lv2_atom_forge_long(&forge, pos.frame);

            if (pos.valid & JackPositionBBT)
            {
                lv2_atom_forge_key(&forge, g_urids.time_bar);
                lv2_atom_forge_long(&forge, pos.bar - 1);

                lv2_atom_forge_key(&forge, g_urids.time_barBeat);
                lv2_atom_forge_float(&forge, pos.beat - 1 + (pos.tick / pos.ticks_per_beat));

                lv2_atom_forge_key(&forge, g_urids.time_beat);
                lv2_atom_forge_double(&forge, pos.beat - 1);

                lv2_atom_forge_key(&forge, g_urids.time_beatUnit);
                lv2_atom_forge_int(&forge, pos.beat_type);

                lv2_atom_forge_key(&forge, g_urids.time_beatsPerBar);
                lv2_atom_forge_float(&forge, pos.beats_per_bar);

                lv2_atom_forge_key(&forge, g_urids.time_beatsPerMinute);
                lv2_atom_forge_float(&forge, pos.beats_per_minute);

                lv2_atom_forge_key(&forge, g_urids.time_ticksPerBeat);
                lv2_atom_forge_double(&forge, pos.ticks_per_beat);
            }

            lv2_atom_forge_pop(&forge, &frame);
        }
        else
        {
            lv2_pos->size = 0;
        }
    }
    if (effect->bpb_index >= 0)
    {
        *(effect->ports[effect->bpb_index]->buffer) = g_transport_bpb;
    }
    if (effect->bpm_index >= 0)
    {
        *(effect->ports[effect->bpm_index]->buffer) = g_transport_bpm;
    }
    if (effect->speed_index >= 0)
    {
        *(effect->ports[effect->speed_index]->buffer) = g_jack_rolling ? 1.0f : 0.0f;
    }

    /* Prepare midi/event ports */
    for (i = 0; i < effect->input_event_ports_count; i++)
    {
        lv2_evbuf_reset(effect->input_event_ports[i]->evbuf, true);

        if (effect->bypass > 0.5f)
        {
            // effect is now bypassed, but wasn't before
            if (!effect->was_bypassed)
            {
                LV2_Evbuf_Iterator iter = lv2_evbuf_begin(effect->input_event_ports[i]->evbuf);

                uint8_t bufNotesOff[3] = {
                    0xB0, // CC
                    0x7B, // all-notes-off
                    0
                };
                uint8_t bufSoundOff[3] = {
                    0xB0, // CC
                    0x78, // all-sound-off
                    0
                };

                int j=0;

                do {
                    if (!lv2_evbuf_write(&iter, 0, 0, g_urids.midi_MidiEvent, 3, bufNotesOff))
                        break;
                    if (!lv2_evbuf_write(&iter, 0, 0, g_urids.midi_MidiEvent, 3, bufSoundOff))
                        break;
                    bufNotesOff[0] += 1;
                    bufSoundOff[0] += 1;
                } while (++j < 16);
            }
        }
        else
        {
            // non-bypassed, processing
            LV2_Evbuf_Iterator iter = lv2_evbuf_begin(effect->input_event_ports[i]->evbuf);

            /* Write Jack MIDI input */
            void* buf = jack_port_get_buffer(effect->input_event_ports[i]->jack_port, nframes);
            uint32_t j;
            for (j = 0; j < jack_midi_get_event_count(buf); ++j)
            {
                jack_midi_event_t ev;
                jack_midi_event_get(&ev, buf, j);
                if (!lv2_evbuf_write(&iter, ev.time, 0, g_urids.midi_MidiEvent, ev.size, ev.buffer))
                {
                    fprintf(stderr, "lv2 evbuf write failed\n");
                }
            }

            /* Write time position */
            if (lv2_pos->size > 0 && (effect->input_event_ports[i]->hints & HINT_TRANSPORT) != 0)
            {
                lv2_evbuf_write(&iter, 0, 0, lv2_pos->type, lv2_pos->size, LV2_ATOM_BODY_CONST(lv2_pos));
            }
        }
    }

    for (i = 0; i < effect->output_event_ports_count; i++)
        lv2_evbuf_reset(effect->output_event_ports[i]->evbuf, false);

    /* control in events */
    if (effect->events_buffer && effect->control_index >= 0)
    {
        const size_t space = jack_ringbuffer_read_space(effect->events_buffer);
        LV2_Atom atom;
        size_t j;
        for (j = 0; j < space; j += sizeof(atom) + atom.size)
        {
            jack_ringbuffer_read(effect->events_buffer, (char*)&atom, sizeof(atom));
            char fatom[sizeof(atom)+atom.size];
            memcpy(&fatom, (char*)&atom, sizeof(atom));
            char *body = &fatom[sizeof(atom)];
            jack_ringbuffer_read(effect->events_buffer, body, atom.size);
            const port_t *port = effect->ports[effect->control_index];
            LV2_Evbuf_Iterator e = lv2_evbuf_end(port->evbuf);
            const LV2_Atom* const ratom = (const LV2_Atom*)fatom;
            lv2_evbuf_write(&e, nframes - 1, 0, ratom->type, ratom->size,
                                LV2_ATOM_BODY_CONST(ratom));
        }
    }

    /* Bypass */
    if (effect->bypass > 0.5f && effect->enabled_index < 0)
    {
        /* Plugins with audio inputs */
        if (effect->input_audio_ports_count > 0)
        {
            /* prepare jack buffers (bypass copy) */
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
            }

            /* silence any remaining audio inputs */
            for (; i < effect->input_audio_ports_count; i++)
                memset(effect->input_audio_ports[i]->buffer, 0, (sizeof(float) * nframes));

            /* reset cv inputs */
            for (i = 0; i < effect->input_cv_ports_count; i++)
                memset(effect->input_cv_ports[i]->buffer, 0, (sizeof(float) * nframes));

            /* Run the plugin with zero buffer to avoid 'pause behavior' in delay plugins */
            lilv_instance_run(effect->lilv_instance, nframes);

            /* no need to silence plugin audio or cv, they are unused during bypass */
        }
        /* Plugins without audio inputs */
        else
        {
            /* prepare jack buffers (silent) */
            for (i = 0; i < effect->output_audio_ports_count; i++)
            {
                buffer_out = jack_port_get_buffer(effect->output_audio_ports[i]->jack_port, nframes);
                memset(buffer_out, 0, (sizeof(float) * nframes));
            }
            for (i = 0; i < effect->output_cv_ports_count; i++)
            {
                buffer_out = jack_port_get_buffer(effect->output_cv_ports[i]->jack_port, nframes);
                memset(buffer_out, 0, (sizeof(float) * nframes));
            }

            /* reset cv inputs */
            for (i = 0; i < effect->input_cv_ports_count; i++)
                memset(effect->input_cv_ports[i]->buffer, 0, (sizeof(float) * nframes));

            /* Run the plugin with default cv buffers and without midi events */
            lilv_instance_run(effect->lilv_instance, nframes);

            /* no need to silence plugin audio or cv, they are unused during bypass */
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

        /* Copy the input buffers cv */
        for (i = 0; i < effect->input_cv_ports_count; i++)
        {
            buffer_in = jack_port_get_buffer(effect->input_cv_ports[i]->jack_port, nframes);
            memcpy(effect->input_cv_ports[i]->buffer, buffer_in, (sizeof(float) * nframes));
        }

        /* Run the effect */
        lilv_instance_run(effect->lilv_instance, nframes);

        /* Notify the plugin the run() cycle is finished */
        if (effect->worker.iface)
        {
            /* Process any replies from the worker. */
            worker_emit_responses(&effect->worker);
            if (effect->worker.iface->end_run)
            {
                effect->worker.iface->end_run(effect->lilv_instance->lv2_handle);
            }
        }

        /* Copy the output buffers audio */
        for (i = 0; i < effect->output_audio_ports_count; i++)
        {
            buffer_out = jack_port_get_buffer(effect->output_audio_ports[i]->jack_port, nframes);
            memcpy(buffer_out, effect->output_audio_ports[i]->buffer, (sizeof(float) * nframes));
        }

        /* Copy the output buffers cv */
        for (i = 0; i < effect->output_cv_ports_count; i++)
        {
            buffer_out = jack_port_get_buffer(effect->output_cv_ports[i]->jack_port, nframes);
            memcpy(buffer_out, effect->output_cv_ports[i]->buffer, (sizeof(float) * nframes));
        }

        for (i = 0; i < effect->monitors_count; i++)
        {
            int port_id = effect->monitors[i]->port_id;
            float value = *(effect->ports[port_id]->buffer);
            if (monitor_check_condition(effect->monitors[i]->op, effect->monitors[i]->value, value) &&
                floats_differ_enough(value, effect->monitors[i]->last_notified_value)) {
                if (monitor_send(effect->instance, effect->ports[port_id]->symbol, value) >= 0)
                    effect->monitors[i]->last_notified_value = value;
            }
        }
    }

    /* MIDI out events */
    uint32_t p;
    for (p = 0; p < effect->output_event_ports_count; p++)
    {
        port_t *port = effect->output_event_ports[p];
        if (port->jack_port &&
            port->flow == FLOW_OUTPUT &&
            port->type == TYPE_EVENT)
        {
            void* buf = jack_port_get_buffer(port->jack_port, nframes);
            jack_midi_clear_buffer(buf);

            if (effect->bypass > 0.5f)
            {
                // effect is now bypassed, but wasn't before
                if (!effect->was_bypassed)
                {
                    jack_midi_data_t bufNotesOff[3] = {
                        0xB0, // CC
                        0x7B, // all-notes-off
                        0
                    };

                    int j=0;

                    do {
                        if (jack_midi_event_write(buf, 0, bufNotesOff, 3) != 0)
                            break;
                        bufNotesOff[0] += 1;
                    } while (++j < 16);
                }

                if (effect->input_audio_ports_count == 0 &&
                    effect->output_audio_ports_count == 0 &&
                    effect->input_event_ports_count == effect->output_event_ports_count)
                {
                    void* bufIn = jack_port_get_buffer(effect->input_event_ports[p]->jack_port, nframes);
                    jack_midi_event_t ev;

                    for (i = 0; i < jack_midi_get_event_count(bufIn); ++i)
                    {
                        if (jack_midi_event_get(&ev, bufIn, i) == 0)
                        {
                            if (jack_midi_event_write(buf, ev.time, ev.buffer, ev.size) != 0)
                                break;
                        }
                    }
                }
            }
            else
            {
                LV2_Evbuf_Iterator it;
                for (it = lv2_evbuf_begin(port->evbuf); lv2_evbuf_is_valid(it); it = lv2_evbuf_next(it))
                {
                    uint32_t frames, subframes, type, size;
                    uint8_t* body;
                    lv2_evbuf_get(it, &frames, &subframes, &type, &size, &body);
                    if (type == g_urids.midi_MidiEvent)
                    {
                        jack_midi_event_write(buf, frames, body, size);
                    }
                }
            }
        }
    }

    if (effect->hints & HINT_TRIGGERS)
    {
        for (i = 0; i < effect->input_control_ports_count; i++)
        {
            if (effect->input_control_ports[i]->hints & HINT_TRIGGER)
                *(effect->input_control_ports[i]->buffer) = effect->input_control_ports[i]->def_value;
        }
    }

    if (effect->hints & HINT_OUTPUT_MONITORS)
    {
        bool needs_post = false;
        float value;

        for (i = 0; i < effect->output_control_ports_count; i++)
        {
            if ((effect->output_control_ports[i]->hints & HINT_MONITORED) == 0)
                continue;

            value = *(effect->output_control_ports[i]->buffer);

            if (! floats_differ_enough(effect->output_control_ports[i]->prev_value, value))
                continue;

            postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

            if (posteventptr == NULL)
                continue;

            effect->output_control_ports[i]->prev_value = value;

            posteventptr->event.type = POSTPONED_OUTPUT_MONITOR;
            posteventptr->event.parameter.effect_id = effect->instance;
            posteventptr->event.parameter.symbol    = effect->output_control_ports[i]->symbol;
            posteventptr->event.parameter.value     = value;

            pthread_mutex_lock(&g_rtsafe_mutex);
            list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
            pthread_mutex_unlock(&g_rtsafe_mutex);

            needs_post = true;
        }

        if (needs_post)
            sem_post(&g_postevents_semaphore);
    }

    effect->was_bypassed = effect->bypass > 0.5f;

    return 0;
}

static float UpdateValueFromMidi(midi_cc_t* mcc, uint16_t mvalue, bool highres)
{
    const uint16_t mvaluediv = highres ? 8192 : 64;

    if (!strcmp(mcc->symbol, g_bypass_port_symbol))
    {
        effect_t *effect = &g_effects[mcc->effect_id];

        const bool bypassed = mvalue < mvaluediv;
        effect->bypass = bypassed ? 1.0f : 0.0f;

        if (effect->enabled_index >= 0)
        {
            *(effect->ports[effect->enabled_index]->buffer) = bypassed ? 0.0f : 1.0f;
        }

        return bypassed ? 1.0f : 0.0f;
    }

    const port_t* port = mcc->port;
    float value;

    if (port->hints & HINT_TRIGGER)
    {
        // now triggered, always maximum
        value = port->max_value;
    }
    else if (port->hints & HINT_TOGGLE)
    {
        // toggle, always min or max
        value = mvalue >= mvaluediv ? port->max_value : port->min_value;

        if (mcc->effect_id == GLOBAL_EFFECT_ID && !strcmp(mcc->symbol, g_rolling_port_symbol))
        {
            if (mvalue >= mvaluediv)
            {
                jack_transport_start(g_jack_global_client);
            }
            else
            {
                jack_transport_stop(g_jack_global_client);
                jack_transport_locate(g_jack_global_client, 0);
            }
            g_transport_reset = true;
        }
    }
    else
    {
        // get percentage by dividing by max MIDI value
        value = (float)mvalue;

        if (highres)
            value /= 16383.0f;
        else
            value /= 127.0f;

        // make sure bounds are correct
        if (value <= 0.0f)
        {
            value = mcc->minimum;
        }
        else if (value >= 1.0f)
        {
            value = mcc->maximum;
        }
        else
        {
            if (port->hints & HINT_LOGARITHMIC)
            {
                // FIXME: calculate value properly (don't do log on custom scale, use port min/max then adjust)
                value = mcc->minimum * powf(mcc->maximum/mcc->minimum, value);
            }
            else
            {
                value = mcc->minimum + (mcc->maximum - mcc->minimum) * value;
            }

            if (port->hints & HINT_INTEGER)
                value = rintf(value);
        }

        if (mcc->effect_id == GLOBAL_EFFECT_ID)
        {
            if (!strcmp(mcc->symbol, g_bpb_port_symbol))
                g_transport_bpb = value;
            else if (!strcmp(mcc->symbol, g_bpm_port_symbol))
                g_transport_bpm = value;
        }
    }

    // set param value
    *(port->buffer) = value;
    return value;
}

static void UpdateGlobalJackPosition(enum UpdatePositionFlag flag)
{
    bool old_rolling;
    double old_bpb, old_bpm;

    if (flag != UPDATE_POSTION_SKIP)
    {
        old_rolling = g_jack_rolling;
        old_bpb = g_transport_bpb;
        old_bpm = g_transport_bpm;
    }

    g_jack_rolling = (jack_transport_query(g_jack_global_client, &g_jack_pos) == JackTransportRolling);

    if ((g_jack_pos.valid & JackPositionBBT) == 0x0)
    {
        g_jack_pos.beats_per_bar    = g_transport_bpb;
        g_jack_pos.beats_per_minute = g_transport_bpm;
    }

    if (flag == UPDATE_POSTION_SKIP)
        return;
    if (flag == UPDATE_POSTION_IF_CHANGED &&
        old_rolling == g_jack_rolling && old_bpb == g_transport_bpb && old_bpm == g_transport_bpm)
        return;

    postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

    if (!posteventptr)
        return;

    posteventptr->event.type = POSTPONED_TRANSPORT;
    posteventptr->event.transport.rolling = g_jack_rolling;
    posteventptr->event.transport.bpb     = g_transport_bpb;
    posteventptr->event.transport.bpm     = g_transport_bpm;

    pthread_mutex_lock(&g_rtsafe_mutex);
    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    sem_post(&g_postevents_semaphore);
}

static int ProcessMidi(jack_nframes_t nframes, void *arg)
{
    jack_midi_event_t event;
    uint8_t channel, controller, status;
    uint16_t mvalue;
    float value;
    bool handled, highres, needs_post = false;
    enum UpdatePositionFlag pos_flag = UPDATE_POSTION_IF_CHANGED;

#ifdef HAVE_HYLIA
    if (hylia_enabled)
    {
        hylia_process(g_hylia_instance, nframes, &g_hylia_timeinfo);
        const double new_bpb = g_hylia_timeinfo.beatsPerBar;
        const double new_bpm = g_hylia_timeinfo.beatsPerMinute;

        if (new_bpb > 0.0 && (g_transport_bpb != new_bpb))
        {
            g_transport_bpb = new_bpb;
            pos_flag = UPDATE_POSTION_FORCED;
        }
        if (new_bpm > 0.0 && (g_transport_bpm != new_bpm))
        {
            g_transport_bpm = new_bpm;
            pos_flag = UPDATE_POSTION_FORCED;
        }
    }
#endif

    UpdateGlobalJackPosition(pos_flag);

    void *const port_buf = jack_port_get_buffer(g_midi_in_port, nframes);
    const jack_nframes_t event_count = jack_midi_get_event_count(port_buf);

    for (jack_nframes_t i = 0 ; i < event_count; i++)
    {
        if (jack_midi_event_get(&event, port_buf, i) != 0)
            break;

        status = event.buffer[0] & 0xF0;

        // check if it's a program event
        if (status == 0xC0)
        {
            if (event.size != 2 || g_midi_program_listen != (event.buffer[0] & 0x0F))
                continue;

            postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

            if (posteventptr)
            {
                posteventptr->event.type = POSTPONED_PROGRAM_LISTEN;
                posteventptr->event.program.value = event.buffer[1];

                pthread_mutex_lock(&g_rtsafe_mutex);
                list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                pthread_mutex_unlock(&g_rtsafe_mutex);

                needs_post = true;
            }
        }

        if (event.size != 3)
            continue;

        // check if it's a CC or Pitchbend event
        if (status == 0xB0)
        {
            controller = event.buffer[1];
            mvalue     = event.buffer[2];
            highres    = false;
        }
        else if (status == 0xE0)
        {
            controller = MIDI_PITCHBEND_AS_CC;
            mvalue     = (event.buffer[2] << 7) | event.buffer[1];
            highres    = true;
        }
        else
        {
            continue;
        }

        handled = false;
        channel = (event.buffer[0] & 0x0F);

        for (int j = 0; j < MAX_MIDI_CC_ASSIGN; j++)
        {
            if (g_midi_cc_list[j].effect_id == ASSIGNMENT_NULL)
                break;
            if (g_midi_cc_list[j].effect_id == ASSIGNMENT_UNUSED)
                continue;

            // TODO: avoid race condition against effects_midi_unmap
            if (g_midi_cc_list[j].channel    == channel &&
                g_midi_cc_list[j].controller == controller)
            {
                handled = true;
                value = UpdateValueFromMidi(&g_midi_cc_list[j], mvalue, highres);

                postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                if (posteventptr)
                {
                    posteventptr->event.type = POSTPONED_PARAM_SET;
                    posteventptr->event.parameter.effect_id = g_midi_cc_list[j].effect_id;
                    posteventptr->event.parameter.symbol    = g_midi_cc_list[j].symbol;
                    posteventptr->event.parameter.value     = value;

                    pthread_mutex_lock(&g_rtsafe_mutex);
                    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                    pthread_mutex_unlock(&g_rtsafe_mutex);

                    needs_post = true;
                }

                break;
            }
        }

        if (! handled)
        {
            int effect_id;
            const char* symbol;
            float minimum, maximum;

            pthread_mutex_lock(&g_midi_learning_mutex);
            if (g_midi_learning != NULL)
            {
                effect_id = g_midi_learning->effect_id;
                symbol    = g_midi_learning->symbol;
                minimum   = g_midi_learning->minimum;
                maximum   = g_midi_learning->maximum;
                value     = UpdateValueFromMidi(g_midi_learning, mvalue, highres);
                g_midi_learning->channel    = channel;
                g_midi_learning->controller = controller;
                g_midi_learning = NULL;
            }
            else
            {
                effect_id = -1;
            }
            pthread_mutex_unlock(&g_midi_learning_mutex);

            if (effect_id != -1)
            {
                postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                if (posteventptr)
                {
                    posteventptr->event.type = POSTPONED_MIDI_MAP;
                    posteventptr->event.midi_map.effect_id  = effect_id;
                    posteventptr->event.midi_map.symbol     = symbol;
                    posteventptr->event.midi_map.channel    = channel;
                    posteventptr->event.midi_map.controller = controller;
                    posteventptr->event.midi_map.value      = value;
                    posteventptr->event.midi_map.minimum    = minimum;
                    posteventptr->event.midi_map.maximum    = maximum;

                    pthread_mutex_lock(&g_rtsafe_mutex);
                    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                    pthread_mutex_unlock(&g_rtsafe_mutex);

                    needs_post = true;
                }
            }
        }
    }

    if (needs_post)
        sem_post(&g_postevents_semaphore);

    return 0;

    UNUSED_PARAM(arg);
}

static void JackTimebase(jack_transport_state_t state, jack_nframes_t nframes,
                         jack_position_t *pos, int new_pos, void *arg)
{
    double tick;

    pos->beats_per_bar = g_transport_bpb;
    pos->beats_per_minute = g_transport_bpm;

    const int32_t beats_per_bar_int = (int32_t)(pos->beats_per_bar + 0.5f);

    if (new_pos || g_transport_reset)
    {
        pos->valid = JackPositionBBT;
        pos->beat_type = 4.0f;
        pos->ticks_per_beat = TRANSPORT_TICKS_PER_BEAT;

        double abs_beat, abs_tick;

#ifdef HAVE_HYLIA
        if (hylia_enabled)
        {
            if (g_hylia_timeinfo.beat >= 0.0)
            {
                const double beat = g_hylia_timeinfo.beat;
                abs_beat = floor(beat);
                abs_tick = beat * TRANSPORT_TICKS_PER_BEAT;
            }
            else
            {
                abs_beat = 0.0;
                abs_tick = 0.0;
                g_jack_rolling = false;
            }
        }
        else
#endif
        {
            const double min = (double)pos->frame / (double)(g_sample_rate * 60);
            abs_tick = min * pos->beats_per_minute * TRANSPORT_TICKS_PER_BEAT;
            abs_beat = abs_tick / TRANSPORT_TICKS_PER_BEAT;
            g_transport_reset = false;
        }

        pos->bar  = (int32_t)(floor(abs_beat / pos->beats_per_bar) + 0.5);
        pos->beat = (int32_t)(abs_beat - (double)(pos->bar * beats_per_bar_int) + 1.5);
        pos->bar_start_tick = pos->bar * pos->beats_per_bar * TRANSPORT_TICKS_PER_BEAT;
        ++pos->bar;

        tick = abs_tick - (abs_beat * pos->ticks_per_beat);
    }
    else
    {
        tick = g_transport_tick +
              (nframes * TRANSPORT_TICKS_PER_BEAT * pos->beats_per_minute / (double)(g_sample_rate * 60));

        while (tick >= TRANSPORT_TICKS_PER_BEAT)
        {
            tick -= TRANSPORT_TICKS_PER_BEAT;

            if (++pos->beat > beats_per_bar_int)
            {
                pos->beat = 1;
                pos->bar_start_tick += pos->beats_per_bar * TRANSPORT_TICKS_PER_BEAT;
                ++pos->bar;
            }
        }
    }

    pos->tick = (int32_t)(tick + 0.5);
    g_transport_tick = tick;
    return;

    UNUSED_PARAM(state);
    UNUSED_PARAM(arg);
}

static void JackThreadInit(void* arg)
{
/* Disable denormal numbers in floating point calculation.
 * Denormal numbers happen often in IIR filters, and it can be very slow.
 */
/* Taken from cras/src/dsp/dsp_util.c in Chromium OS code.
 * Copyright (c) 2013 The Chromium OS Authors. */
#if defined(__i386__) || defined(__x86_64__)
        unsigned int mxcsr;
        mxcsr = __builtin_ia32_stmxcsr();
        __builtin_ia32_ldmxcsr(mxcsr | 0x8040);
#elif defined(__aarch64__)
        uint64_t cw;
        __asm__ __volatile__ (
                "mrs    %0, fpcr                            \n"
                "orr    %0, %0, #0x1000000                  \n"
                "msr    fpcr, %0                            \n"
                "isb                                        \n"
                : "=r"(cw) :: "memory");
#elif defined(__arm__)
        uint32_t cw;
        __asm__ __volatile__ (
                "vmrs   %0, fpscr                           \n"
                "orr    %0, %0, #0x1000000                  \n"
                "vmsr   fpscr, %0                           \n"
                : "=r"(cw) :: "memory");
#else
#warning "Don't know how to disable denormals. Performace may suffer."
#endif
    return;

    UNUSED_PARAM(arg);
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

    LilvNode *lilv_worker_schedule = lilv_new_uri(g_lv2_data, LV2_WORKER__schedule);
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
    features[URI_MAP_FEATURE]           = &g_uri_map_feature;
    features[URID_MAP_FEATURE]          = &g_urid_map_feature;
    features[URID_UNMAP_FEATURE]        = &g_urid_unmap_feature;
    features[OPTIONS_FEATURE]           = &g_options_feature;
    features[WORKER_FEATURE]            = work_schedule_feature;
    features[LICENSE_FEATURE]           = &g_license_feature;
    features[BUF_SIZE_POWER2_FEATURE]   = &g_buf_size_features[0];
    features[BUF_SIZE_FIXED_FEATURE]    = &g_buf_size_features[1];
    features[BUF_SIZE_BOUNDED_FEATURE]  = &g_buf_size_features[2];
    features[FEATURE_TERMINATOR]        = NULL;

    effect->features = features;
}

static property_t *FindEffectPropertyByLabel(effect_t *effect, const char *label)
{
    // TODO: index properties to make it faster

    uint32_t i;

    for (i = 0; i < effect->properties_count; i++)
    {
        if (strcmp(label, lilv_node_as_string(effect->properties[i]->label)) == 0)
            return effect->properties[i];
    }
    return NULL;
}

static port_t *FindEffectInputPortBySymbol(effect_t *effect, const char *control_symbol)
{
    if (!strcmp(control_symbol, g_bypass_port_symbol))
        return &effect->bypass_port;
    if (!strcmp(control_symbol, g_presets_port_symbol))
        return &effect->presets_port;

    for (uint32_t i = 0; i < effect->input_control_ports_count; i++)
    {
        if (strcmp(effect->input_control_ports[i]->symbol, control_symbol) == 0)
            return effect->input_control_ports[i];
    }
    return NULL;
}

static port_t *FindEffectOutputPortBySymbol(effect_t *effect, const char *control_symbol)
{
    for (uint32_t i = 0; i < effect->output_control_ports_count; i++)
    {
        if (strcmp(effect->output_control_ports[i]->symbol, control_symbol) == 0)
            return effect->output_control_ports[i];
    }
    return NULL;
}

static void SetParameterFromState(const char* symbol, void* user_data,
                                  const void* value, uint32_t size,
                                  uint32_t type)
{
    effect_t *effect = (effect_t*)user_data;
    float realvalue;

    if (type == g_urids.atom_Float)
    {
        if (size != sizeof(float))
            return;
        realvalue = *((const float*)value);
    }
    else if (type == g_urids.atom_Double)
    {
        if (size != sizeof(double))
            return;
        realvalue = *((const double*)value);
    }
    else if (type == g_urids.atom_Int)
    {
        if (size != sizeof(int32_t))
            return;
        realvalue = *((const int32_t*)value);
    }
    else if (type == g_urids.atom_Long)
    {
        if (size != sizeof(int64_t))
            return;
        realvalue = *((const int64_t*)value);
    }
    else
    {
        fprintf(stderr, "SetParameterFromState called with unknown type: %u %u\n", type, size);
        return;
    }

    effects_set_parameter(effect->instance, symbol, realvalue);
}

static const void* GetPortValueForState(const char* symbol, void* user_data, uint32_t* size, uint32_t* type)
{
    effect_t *effect = (effect_t*)user_data;
    port_t *port = FindEffectInputPortBySymbol(effect, symbol);
    if (port)
    {
        *size = sizeof(float);
        *type = g_urids.atom_Float;
        return port->buffer;
    }
    return NULL;
}

static int LoadPresets(effect_t *effect)
{
    LilvNode *preset_uri = lilv_new_uri(g_lv2_data, LV2_PRESETS__Preset);
    LilvNodes* presets = lilv_plugin_get_related(effect->lilv_plugin, preset_uri);
    uint32_t presets_count = lilv_nodes_size(presets);
    effect->presets_count = presets_count;
    // allocate for presets
    effect->presets = (preset_t **) calloc(presets_count, sizeof(preset_t *));
    uint32_t j = 0;
    for (j = 0; j < presets_count; j++) effect->presets[j] = NULL;
    j = 0;
    LILV_FOREACH(nodes, i, presets)
    {
        const LilvNode* preset = lilv_nodes_get(presets, i);
        effect->presets[j] = (preset_t *) malloc(sizeof(preset_t));
        effect->presets[j]->uri = lilv_node_duplicate(preset);
        j++;
    }
    lilv_nodes_free(presets);
    lilv_node_free(preset_uri);

    return 0;
}

// ignore const cast for this function, need to free const features array
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"

static void FreeFeatures(effect_t *effect)
{
    worker_finish(&effect->worker);

    if (effect->features)
    {
        free(effect->features[WORKER_FEATURE]->data);
        free((void*)effect->features[WORKER_FEATURE]);
        free(effect->features);
    }
}

// back to normal
#pragma GCC diagnostic pop

static char* GetLicenseFile(MOD_License_Handle handle, const char *license_uri)
{
    if (!license_uri || *license_uri == '\0')
        return NULL;

    // get keys path. note: assumes trailing separator
    const char* const keyspath = getenv("MOD_KEYS_PATH");
    if (!keyspath || *keyspath == '\0')
        return NULL;

    // get path length, check trailing separator
    const size_t keyspathlen = strlen(keyspath);
    if (keyspath[keyspathlen-1] != '/')
        return NULL;

    // local vars
    uint8_t* hashenc;
    char hashdec[HASH_LENGTH*2+1];
    FILE* file;
    long filesize;
    size_t filenamesize;
    char* filename;
    char* filebuffer = NULL;
    sha1nfo sha1;

    // hash the uri
    sha1_init(&sha1);
    sha1_write(&sha1, license_uri, strlen(license_uri));
    hashenc = sha1_result(&sha1);

    for (int i=0; i<HASH_LENGTH; i++) {
        sprintf(hashdec+(i*2), "%02x", hashenc[i]);
    }
    hashdec[HASH_LENGTH*2] = '\0';

    // join path
    filenamesize = strlen(keyspath) + HASH_LENGTH*2;
    filename = malloc(filenamesize+1);

    if (! filename)
        return NULL;

    memcpy(filename, keyspath, keyspathlen);
    memcpy(filename+keyspathlen, hashdec, HASH_LENGTH*2);
    filename[filenamesize] = '\0';

    // open file
    file = fopen(filename, "r");

    // cleanup
    free(filename);

    if (!file)
        return NULL;

    // get size
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    if (filesize <= 0 || filesize > 5*1024*1024) // 5Mb
        goto end;

    // allocate file buffer
    filebuffer = (char*)calloc(1, filesize+1);
    if (! filebuffer)
        goto end;

    // read file
    fseek(file, 0, SEEK_SET);
    if (fread(filebuffer, 1, filesize, file) != (size_t)filesize)
        filebuffer = NULL;

end:
    fclose(file);
    return filebuffer;

    UNUSED_PARAM(handle);
}

static void FreeLicenseData(MOD_License_Handle handle, char *license)
{
    return free(license);

    UNUSED_PARAM(handle);
}

#ifdef HAVE_CONTROLCHAIN
static void CCDataUpdate(void* arg)
{
    cc_update_list_t *updates = arg;

    bool is_bypass, needs_post = false;
    const int device_id = updates->device_id;

    for (int i = 0; i < updates->count; i++)
    {
        cc_update_data_t *data = &updates->list[i];
        assignment_t *assignment =
            &g_assignments_list[device_id][data->assignment_id];

        if (!assignment->port)
            continue;

        // invert value if bypass
        if ((is_bypass = !strcmp(assignment->port->symbol, g_bypass_port_symbol)))
            data->value = 1.0f - data->value;

        // ignore requests for same value
        if (!floats_differ_enough(*(assignment->port->buffer), data->value))
            continue;

        if (is_bypass)
        {
            effect_t *effect = &g_effects[assignment->effect_id];
            if (effect->enabled_index >= 0)
                *(effect->ports[effect->enabled_index]->buffer) = (data->value > 0.5f) ? 0.0f : 1.0f;
        }
        else if (assignment->effect_id == GLOBAL_EFFECT_ID)
        {
            if (!strcmp(assignment->port->symbol, g_bpb_port_symbol))
            {
                g_transport_bpb = data->value;
            }
            else if (!strcmp(assignment->port->symbol, g_bpm_port_symbol))
            {
                g_transport_bpm = data->value;
            }
            else if (!strcmp(assignment->port->symbol, g_rolling_port_symbol))
            {
                if (data->value > 0.5f)
                {
                    jack_transport_start(g_jack_global_client);
                }
                else
                {
                    jack_transport_stop(g_jack_global_client);
                    jack_transport_locate(g_jack_global_client, 0);
                }
                g_transport_reset = true;
            }
        }

        *(assignment->port->buffer) = data->value;

        postponed_event_list_data* const posteventptr =
            rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

        if (posteventptr == NULL)
            continue;

        posteventptr->event.type = POSTPONED_PARAM_SET;
        posteventptr->event.parameter.effect_id = assignment->effect_id;
        posteventptr->event.parameter.symbol    = assignment->port->symbol;
        posteventptr->event.parameter.value     = data->value;

        pthread_mutex_lock(&g_rtsafe_mutex);
        list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
        pthread_mutex_unlock(&g_rtsafe_mutex);

        needs_post = true;
    }

    if (needs_post)
        sem_post(&g_postevents_semaphore);
}

static void InitializeControlChainIfNeeded(void)
{
    if (g_cc_client != NULL)
        return;

    if ((g_cc_client = cc_client_new("/tmp/control-chain.sock")) != NULL)
        cc_client_data_update_cb(g_cc_client, CCDataUpdate);
}
#endif

#ifdef HAVE_HYLIA
static uint32_t GetHyliaOutputLatency(void)
{
    const long long int latency = llround(1.0e6 * g_block_length / g_sample_rate);

    if (latency >= 0 && latency < UINT32_MAX)
        return (uint32_t)latency;

    return 0;
}
#endif

/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

int effects_init(void* client)
{
    /* This global client is for connections / disconnections and midi-learn */
    if (client != NULL)
    {
        g_jack_global_client = (jack_client_t*)client;
    }
    else
    {
        g_jack_global_client = jack_client_open("mod-host", JackNoStartServer, NULL);
    }

    if (g_jack_global_client == NULL)
    {
        return ERR_JACK_CLIENT_CREATION;
    }

    INIT_LIST_HEAD(&g_rtsafe_list);

    if (!rtsafe_memory_pool_create(&g_rtsafe_mem_pool, "mod-host", sizeof(postponed_event_list_data),
                                   MAX_POSTPONED_EVENTS))
    {
        fprintf(stderr, "can't allocate realtime-safe memory pool\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_MEMORY_ALLOCATION;
    }

    pthread_mutexattr_t atts;
    pthread_mutexattr_init(&atts);
#ifdef __MOD_DEVICES__
    pthread_mutexattr_setprotocol(&atts, PTHREAD_PRIO_INHERIT);
#endif

    pthread_mutex_init(&g_rtsafe_mutex, &atts);
    pthread_mutex_init(&g_midi_learning_mutex, &atts);

    pthread_mutexattr_destroy(&atts);

    sem_init(&g_postevents_semaphore, 0, 0);

    /* Get the system ports */
    g_capture_ports = jack_get_ports(g_jack_global_client, "system", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
    g_playback_ports = jack_get_ports(g_jack_global_client, "system", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);

    /* Get buffers size */
    g_block_length = jack_get_buffer_size(g_jack_global_client);
    g_sample_rate = jack_get_sample_rate(g_jack_global_client);
    g_midi_buffer_size = jack_port_type_get_buffer_size(g_jack_global_client, JACK_DEFAULT_MIDI_TYPE);

    /* initial transport state */
    g_transport_reset = true;
    g_transport_bpb = 4.0;
    g_transport_bpm = 120.0;
    g_transport_tick = 0.0;

    /* this fails to build if GLOBAL_EFFECT_ID >= MAX_INSTANCES */
    char global_effect_id_static_check[GLOBAL_EFFECT_ID < MAX_INSTANCES?1:-1];

    /* global ports */
    {
        port_t **ports = (port_t **) calloc(3, sizeof(port_t *));

        port_t *port_bpb = ports[0] = calloc(1, sizeof(port_t));
        port_bpb->buffer = &port_bpb->prev_value;
        port_bpb->buffer_count = 1;
        port_bpb->min_value = 0.0f;
        port_bpb->max_value = 1.0f;
        port_bpb->def_value = 0.0f;
        port_bpb->type = TYPE_CONTROL;
        port_bpb->flow = FLOW_INPUT;
        port_bpb->hints = 0x0;
        port_bpb->symbol = g_bpb_port_symbol;

        port_t *port_bpm = ports[1] = calloc(1, sizeof(port_t));
        port_bpm->buffer = &port_bpm->prev_value;
        port_bpm->buffer_count = 1;
        port_bpm->min_value = 0.0f;
        port_bpm->max_value = 1.0f;
        port_bpm->def_value = 0.0f;
        port_bpm->type = TYPE_CONTROL;
        port_bpm->flow = FLOW_INPUT;
        port_bpm->hints = 0x0;
        port_bpm->symbol = g_bpm_port_symbol;

        port_t *port_rolling = ports[2] = calloc(1, sizeof(port_t));
        port_rolling->buffer = &port_rolling->prev_value;
        port_rolling->buffer_count = 1;
        port_rolling->min_value = 0.0f;
        port_rolling->max_value = 1.0f;
        port_rolling->def_value = 0.0f;
        port_rolling->type = TYPE_CONTROL;
        port_rolling->flow = FLOW_INPUT;
        port_rolling->hints = HINT_TOGGLE;
        port_rolling->symbol = g_rolling_port_symbol;

        effect_t *effect = &g_effects[GLOBAL_EFFECT_ID];
        memset(effect, 0, sizeof(effect_t));

        effect->instance = GLOBAL_EFFECT_ID;
        effect->jack_client = g_jack_global_client;

        effect->ports = effect->control_ports = effect->input_control_ports = ports;
        effect->ports_count = effect->control_ports_count = effect->input_control_ports_count = 3;

        effect->control_index = -1;
        effect->enabled_index = -1;
        effect->freewheel_index = -1;
        effect->bpb_index = -1;
        effect->bpm_index = -1;
        effect->speed_index = -1;

        /* Default value of bypass */
        effect->bypass = 0.0f;
        effect->was_bypassed = false;

        effect->bypass_port.buffer_count = 1;
        effect->bypass_port.buffer = &effect->bypass;
        effect->bypass_port.min_value = 0.0f;
        effect->bypass_port.max_value = 1.0f;
        effect->bypass_port.def_value = 0.0f;
        effect->bypass_port.prev_value = 0.0f;
        effect->bypass_port.type = TYPE_CONTROL;
        effect->bypass_port.flow = FLOW_INPUT;
        effect->bypass_port.hints = HINT_TOGGLE;
        effect->bypass_port.symbol = g_bypass_port_symbol;

        /* virtual presets port */
        effect->preset_value = 0.0f;
        effect->presets_port.buffer_count = 1;
        effect->presets_port.buffer = &effect->preset_value;
        effect->presets_port.min_value = 0.0f;
        effect->presets_port.max_value = 1.0f;
        effect->presets_port.def_value = 0.0f;
        effect->presets_port.prev_value = 0.0f;
        effect->presets_port.type = TYPE_CONTROL;
        effect->presets_port.flow = FLOW_INPUT;
        effect->presets_port.hints = HINT_ENUMERATION|HINT_INTEGER;
        effect->presets_port.symbol = g_presets_port_symbol;
    }

    /* Set jack callbacks */
    jack_set_thread_init_callback(g_jack_global_client, JackThreadInit, NULL);
    jack_set_timebase_callback(g_jack_global_client, 1, JackTimebase, NULL);
    jack_set_process_callback(g_jack_global_client, ProcessMidi, NULL);
    jack_set_buffer_size_callback(g_jack_global_client, BufferSize, NULL);

#ifdef HAVE_HYLIA
    /* Init hylia */
    hylia_enabled = false;
    g_hylia_instance = hylia_create();
    memset(&g_hylia_timeinfo, 0, sizeof(g_hylia_timeinfo));

    if (g_hylia_instance)
    {
        hylia_set_beats_per_bar(g_hylia_instance, g_transport_bpb);
        hylia_set_beats_per_minute(g_hylia_instance, g_transport_bpm);
        hylia_set_output_latency(g_hylia_instance, GetHyliaOutputLatency());
    }
#endif

    /* Register jack ports */
    g_midi_in_port = jack_port_register(g_jack_global_client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (! g_midi_in_port)
    {
        fprintf(stderr, "can't register global jack midi-in port\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_JACK_PORT_REGISTER;
    }

    /* Load all LV2 data */
    g_lv2_data = lilv_world_new();
    lilv_world_load_all(g_lv2_data);
    g_plugins = lilv_world_get_all_plugins(g_lv2_data);

    /* Get sample rate */
    g_sample_rate_node = lilv_new_uri(g_lv2_data, LV2_CORE__sampleRate);

    /* URI and URID Feature initialization */
    urid_sem_init();
    g_symap = symap_new();

    g_uri_map.callback_data = g_symap;
    g_uri_map.uri_to_id = &uri_to_id;

    g_urid_map.handle = g_symap;
    g_urid_map.map = urid_to_id;
    g_urid_unmap.handle = g_symap;
    g_urid_unmap.unmap = id_to_urid;

    g_urids.atom_Float           = urid_to_id(g_symap, LV2_ATOM__Float);
    g_urids.atom_Double          = urid_to_id(g_symap, LV2_ATOM__Double);
    g_urids.atom_Int             = urid_to_id(g_symap, LV2_ATOM__Int);
    g_urids.atom_Long            = urid_to_id(g_symap, LV2_ATOM__Long);
    g_urids.atom_eventTransfer   = urid_to_id(g_symap, LV2_ATOM__eventTransfer);

    g_urids.bufsz_maxBlockLength     = urid_to_id(g_symap, LV2_BUF_SIZE__maxBlockLength);
    g_urids.bufsz_minBlockLength     = urid_to_id(g_symap, LV2_BUF_SIZE__minBlockLength);
    g_urids.bufsz_nomimalBlockLength = urid_to_id(g_symap, LV2_BUF_SIZE__nominalBlockLength);
    g_urids.bufsz_sequenceSize   = urid_to_id(g_symap, LV2_BUF_SIZE__sequenceSize);
    g_urids.midi_MidiEvent       = urid_to_id(g_symap, LV2_MIDI__MidiEvent);
    g_urids.param_sampleRate     = urid_to_id(g_symap, LV2_PARAMETERS__sampleRate);
    g_urids.patch_Set            = urid_to_id(g_symap, LV2_PATCH__Set);
    g_urids.patch_property       = urid_to_id(g_symap, LV2_PATCH__property);
    g_urids.patch_value          = urid_to_id(g_symap, LV2_PATCH__value);
    g_urids.time_Position        = urid_to_id(g_symap, LV2_TIME__Position);
    g_urids.time_bar             = urid_to_id(g_symap, LV2_TIME__bar);
    g_urids.time_barBeat         = urid_to_id(g_symap, LV2_TIME__barBeat);
    g_urids.time_beat            = urid_to_id(g_symap, LV2_TIME__beat);
    g_urids.time_beatUnit        = urid_to_id(g_symap, LV2_TIME__beatUnit);
    g_urids.time_beatsPerBar     = urid_to_id(g_symap, LV2_TIME__beatsPerBar);
    g_urids.time_beatsPerMinute  = urid_to_id(g_symap, LV2_TIME__beatsPerMinute);
    g_urids.time_ticksPerBeat    = urid_to_id(g_symap, KX_TIME__TicksPerBeat);
    g_urids.time_frame           = urid_to_id(g_symap, LV2_TIME__frame);
    g_urids.time_speed           = urid_to_id(g_symap, LV2_TIME__speed);

    /* Options Feature initialization */
    g_options[0].context = LV2_OPTIONS_INSTANCE;
    g_options[0].subject = 0;
    g_options[0].key = g_urids.param_sampleRate;
    g_options[0].size = sizeof(float);
    g_options[0].type = g_urids.atom_Int;
    g_options[0].value = &g_block_length;

    g_options[1].context = LV2_OPTIONS_INSTANCE;
    g_options[1].subject = 0;
    g_options[1].key = g_urids.bufsz_minBlockLength;
    g_options[1].size = sizeof(int32_t);
    g_options[1].type = g_urids.atom_Int;
    g_options[1].value = &g_block_length;

    g_options[2].context = LV2_OPTIONS_INSTANCE;
    g_options[2].subject = 0;
    g_options[2].key = g_urids.bufsz_maxBlockLength;
    g_options[2].size = sizeof(int32_t);
    g_options[2].type = g_urids.atom_Int;
    g_options[2].value = &g_block_length;

    g_options[3].context = LV2_OPTIONS_INSTANCE;
    g_options[3].subject = 0;
    g_options[3].key = g_urids.bufsz_nomimalBlockLength;
    g_options[3].size = sizeof(int32_t);
    g_options[3].type = g_urids.atom_Int;
    g_options[3].value = &g_block_length;

    g_options[4].context = LV2_OPTIONS_INSTANCE;
    g_options[4].subject = 0;
    g_options[4].key = g_urids.bufsz_sequenceSize;
    g_options[4].size = sizeof(int32_t);
    g_options[4].type = g_urids.atom_Int;
    g_options[4].value = &g_midi_buffer_size;

    g_options[5].context = LV2_OPTIONS_INSTANCE;
    g_options[5].subject = 0;
    g_options[5].key = 0;
    g_options[5].size = 0;
    g_options[5].type = 0;
    g_options[5].value = NULL;

    g_license.handle = NULL;
    g_license.license = GetLicenseFile;
    g_license.free = FreeLicenseData;

    lv2_atom_forge_init(&g_lv2_atom_forge, &g_urid_map);

    /* Init lilv_instance as NULL for all plugins */
    for (int i = 0; i < MAX_INSTANCES; i++)
        g_effects[i].lilv_instance = NULL;

    /* Init the midi variables */
    for (int i = 0; i < MAX_MIDI_CC_ASSIGN; i++)
    {
        g_midi_cc_list[i].channel = -1;
        g_midi_cc_list[i].controller = 0;
        g_midi_cc_list[i].minimum = 0.0f;
        g_midi_cc_list[i].maximum = 1.0f;
        g_midi_cc_list[i].effect_id = ASSIGNMENT_NULL;
        g_midi_cc_list[i].symbol = NULL;
        g_midi_cc_list[i].port = NULL;
    }
    g_midi_learning = NULL;
    g_midi_program_listen = 0;

#ifdef HAVE_CONTROLCHAIN
    /* Init the control chain variables */
    memset(&g_assignments_list, 0, sizeof(g_assignments_list));

    for (int i = 0; i < CC_MAX_DEVICES; i++)
    {
        for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
        {
            g_assignments_list[i][j].effect_id = ASSIGNMENT_NULL;
        }
    }
#endif

    g_postevents_running = 1;
    g_postevents_ready = true;
    pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);

    /* get transport state */
    UpdateGlobalJackPosition(UPDATE_POSTION_SKIP);

    /* Try activate the jack global client */
    if (jack_activate(g_jack_global_client) != 0)
    {
        fprintf(stderr, "can't activate global jack client\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_JACK_CLIENT_ACTIVATION;
    }

    /* Connect to all good hw ports (system, ttymidi and nooice) */
    const char** const midihwports = jack_get_ports(g_jack_global_client, "", JACK_DEFAULT_MIDI_TYPE,
                                                                              JackPortIsOutput|JackPortIsPhysical);
    if (midihwports != NULL)
    {
        char ourportname[MAX_CHAR_BUF_SIZE+1];
        ourportname[MAX_CHAR_BUF_SIZE] = '\0';

        const char* const ourclientname = jack_get_client_name(g_jack_global_client);

        snprintf(ourportname, MAX_CHAR_BUF_SIZE, "%s:midi_in", ourclientname);

        for (int i=0; midihwports[i] != NULL; ++i)
        {
            const char* const portname = midihwports[i];
            if (strncmp(portname, "ttymidi:", 8) != 0 &&
                strncmp(portname, "system:", 7) != 0 &&
                strncmp(portname, "nooice", 5) != 0)
                continue;

            jack_connect(g_jack_global_client, portname, ourportname);
        }

        jack_free(midihwports);
    }

    g_processing_enabled = true;

    return SUCCESS;

    UNUSED_PARAM(global_effect_id_static_check);
}

int effects_finish(int close_client)
{
    g_postevents_running = -1;
    sem_post(&g_postevents_semaphore);
    pthread_join(g_postevents_thread, NULL);

    effects_remove(REMOVE_ALL);

#ifdef HAVE_CONTROLCHAIN
    if (g_cc_client)
    {
        cc_client_delete(g_cc_client);
        g_cc_client = NULL;
    }
#endif

    if (g_capture_ports) jack_free(g_capture_ports);
    if (g_playback_ports) jack_free(g_playback_ports);
    if (close_client) jack_client_close(g_jack_global_client);
    symap_free(g_symap);
    lilv_node_free(g_sample_rate_node);
    lilv_world_free(g_lv2_data);
    rtsafe_memory_pool_destroy(g_rtsafe_mem_pool);
    sem_destroy(&g_postevents_semaphore);
    pthread_mutex_destroy(&g_rtsafe_mutex);
    pthread_mutex_destroy(&g_midi_learning_mutex);

    effect_t *effect = &g_effects[GLOBAL_EFFECT_ID];
    if (effect->ports)
    {
        for (unsigned int i=0; i < effect->ports_count; i++)
            free(effect->ports[i]);
        free(effect->ports);
    }

#ifdef HAVE_HYLIA
    hylia_cleanup(g_hylia_instance);
    g_hylia_instance = NULL;
#endif

    g_processing_enabled = false;

    return SUCCESS;
}

int effects_add(const char *uid, int instance)
{
    unsigned int i, ports_count;
    char effect_name[32], port_name[MAX_CHAR_BUF_SIZE+1];
    float *audio_buffer, *cv_buffer, *control_buffer;
    jack_port_t *jack_port;
    uint32_t audio_ports_count, input_audio_ports_count, output_audio_ports_count;
    uint32_t control_ports_count, input_control_ports_count, output_control_ports_count;
    uint32_t cv_ports_count, input_cv_ports_count, output_cv_ports_count;
    uint32_t event_ports_count, input_event_ports_count, output_event_ports_count;
    effect_t *effect;
    int32_t error;

    effect_name[31] = '\0';
    port_name[MAX_CHAR_BUF_SIZE] = '\0';

    /* Jack */
    jack_client_t *jack_client;
    jack_status_t jack_status;
    unsigned long jack_flags = 0;

    /* Lilv */
    const LilvPlugin *plugin;
    LilvInstance *lilv_instance;
    LilvNode *plugin_uri;
    LilvNode *lilv_input, *lilv_control_in, *lilv_enabled, *lilv_freeWheeling, *lilv_output;
    LilvNode *lilv_timePosition, *lilv_timeBeatsPerBar, *lilv_timeBeatsPerMinute, *lilv_timeSpeed;
    LilvNode *lilv_enumeration, *lilv_integer, *lilv_toggled, *lilv_trigger, *lilv_logarithmic;
    LilvNode *lilv_control, *lilv_audio, *lilv_cv, *lilv_event, *lilv_midi;
    LilvNode *lilv_default, *lilv_mod_default;
    LilvNode *lilv_minimum, *lilv_mod_minimum;
    LilvNode *lilv_maximum, *lilv_mod_maximum;
    LilvNode *lilv_atom_port, *lilv_worker_interface, *lilv_license_interface;
    const LilvPort *lilv_port;
    const LilvNode *symbol_node;

    if (!uid) return ERR_LV2_INVALID_URI;
    if (!INSTANCE_IS_VALID(instance)) return ERR_INSTANCE_INVALID;
    if (InstanceExist(instance)) return ERR_INSTANCE_ALREADY_EXISTS;

    effect = &g_effects[instance];

    /* Init the struct */
    memset(effect, 0, sizeof(effect_t));
    effect->instance = instance;

    /* Init the pointers */
    lilv_instance = NULL;
    lilv_input = NULL;
    lilv_control_in = NULL;
    lilv_enabled = NULL;
    lilv_enumeration = NULL;
    lilv_freeWheeling = NULL;
    lilv_timePosition = NULL;
    lilv_timeBeatsPerBar = NULL;
    lilv_timeBeatsPerMinute = NULL;
    lilv_timeSpeed = NULL;
    lilv_integer = NULL;
    lilv_toggled = NULL;
    lilv_trigger = NULL;
    lilv_logarithmic = NULL;
    lilv_output = NULL;
    lilv_control = NULL;
    lilv_audio = NULL;
    lilv_cv = NULL;
    lilv_midi = NULL;
    lilv_default = NULL;
    lilv_minimum = NULL;
    lilv_maximum = NULL;
    lilv_mod_default = NULL;
    lilv_mod_minimum = NULL;
    lilv_mod_maximum = NULL;
    lilv_event = NULL;
    lilv_atom_port = NULL;
    lilv_worker_interface = NULL;
    lilv_license_interface = NULL;

    /* Create a client to Jack */
    snprintf(effect_name, 31, "effect_%i", instance);
    jack_client = jack_client_open(effect_name, JackNoStartServer, &jack_status);

    if (!jack_client)
    {
        fprintf(stderr, "can't get jack client\n");
        error = ERR_JACK_CLIENT_CREATION;
        goto error;
    }
    effect->jack_client = jack_client;

    /* Get the plugin */
    plugin_uri = lilv_new_uri(g_lv2_data, uid);
    plugin = lilv_plugins_get_by_uri(g_plugins, plugin_uri);
    lilv_node_free(plugin_uri);

    if (!plugin)
    {
        // NOTE: Reloading the entire world is nasty!
        //       It may result in crashes, and we now have a way to add/remove bundles as needed anyway.
#if 0
        /* If the plugin are not found reload all plugins */
        lilv_world_load_all(g_lv2_data);
        g_plugins = lilv_world_get_all_plugins(g_lv2_data);

        /* Try get the plugin again */
        plugin_uri = lilv_new_uri(g_lv2_data, uid);
        plugin = lilv_plugins_get_by_uri(g_plugins, plugin_uri);
        lilv_node_free(plugin_uri);

        if (!plugin)
#endif
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
    lilv_instance = lilv_plugin_instantiate(plugin, g_sample_rate, effect->features);

    if (!lilv_instance)
    {
        fprintf(stderr, "can't get lilv instance\n");
        error = ERR_LV2_INSTANTIATION;
        goto error;
    }
    effect->lilv_instance = lilv_instance;

    /* Worker */
    effect->worker.instance = lilv_instance;
    sem_init(&effect->worker.sem, 0, 0);
    effect->worker.iface = NULL;
    effect->worker.requests  = NULL;
    effect->worker.responses = NULL;
    effect->worker.response  = NULL;

    lilv_worker_interface = lilv_new_uri(g_lv2_data, LV2_WORKER__interface);
    if (lilv_plugin_has_extension_data(effect->lilv_plugin, lilv_worker_interface))
    {
        const LV2_Worker_Interface *worker_interface;
        worker_interface =
            (const LV2_Worker_Interface*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                           LV2_WORKER__interface);

        worker_init(&effect->worker, worker_interface);
    }

    lilv_license_interface = lilv_new_uri(g_lv2_data, MOD_LICENSE__interface);
    if (lilv_plugin_has_extension_data(effect->lilv_plugin, lilv_license_interface))
    {
        effect->license_iface =
            (const MOD_License_Interface*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                           MOD_LICENSE__interface);
    }

    /* Create the URI for identify the ports */
    ports_count = lilv_plugin_get_num_ports(plugin);
    lilv_audio = lilv_new_uri(g_lv2_data, LILV_URI_AUDIO_PORT);
    lilv_control = lilv_new_uri(g_lv2_data, LILV_URI_CONTROL_PORT);
    lilv_cv = lilv_new_uri(g_lv2_data, LILV_URI_CV_PORT);
    lilv_input = lilv_new_uri(g_lv2_data, LILV_URI_INPUT_PORT);
    lilv_control_in = lilv_new_uri(g_lv2_data, LV2_CORE__control);
    lilv_enabled = lilv_new_uri(g_lv2_data, LV2_CORE__enabled);
    lilv_enumeration = lilv_new_uri(g_lv2_data, LV2_CORE__enumeration);
    lilv_freeWheeling = lilv_new_uri(g_lv2_data, LV2_CORE__freeWheeling);
    lilv_timePosition = lilv_new_uri(g_lv2_data, LV2_TIME__Position);
    lilv_timeBeatsPerBar = lilv_new_uri(g_lv2_data, LV2_TIME__beatsPerBar);
    lilv_timeBeatsPerMinute = lilv_new_uri(g_lv2_data, LV2_TIME__beatsPerMinute);
    lilv_timeSpeed = lilv_new_uri(g_lv2_data, LV2_TIME__speed);
    lilv_integer = lilv_new_uri(g_lv2_data, LV2_CORE__integer);
    lilv_toggled = lilv_new_uri(g_lv2_data, LV2_CORE__toggled);
    lilv_trigger = lilv_new_uri(g_lv2_data, LV2_PORT_PROPS__trigger);
    lilv_logarithmic = lilv_new_uri(g_lv2_data, LV2_PORT_PROPS__logarithmic);
    lilv_output = lilv_new_uri(g_lv2_data, LILV_URI_OUTPUT_PORT);
    lilv_event = lilv_new_uri(g_lv2_data, LILV_URI_EVENT_PORT);
    lilv_atom_port = lilv_new_uri(g_lv2_data, LV2_ATOM__AtomPort);
    lilv_midi = lilv_new_uri(g_lv2_data, LILV_URI_MIDI_EVENT);
    lilv_default = lilv_new_uri(g_lv2_data, LV2_CORE__default);
    lilv_minimum = lilv_new_uri(g_lv2_data, LV2_CORE__minimum);
    lilv_maximum = lilv_new_uri(g_lv2_data, LV2_CORE__maximum);
    lilv_mod_default = lilv_new_uri(g_lv2_data, LILV_NS_MOD "default");
    lilv_mod_minimum = lilv_new_uri(g_lv2_data, LILV_NS_MOD "minimum");
    lilv_mod_maximum = lilv_new_uri(g_lv2_data, LILV_NS_MOD "maximum");

    /* Allocate memory to ports */
    audio_ports_count = 0;
    input_audio_ports_count = 0;
    output_audio_ports_count = 0;
    control_ports_count = 0;
    input_control_ports_count = 0;
    output_control_ports_count = 0;
    cv_ports_count = 0;
    input_cv_ports_count = 0;
    output_cv_ports_count = 0;
    event_ports_count = 0;
    input_event_ports_count = 0;
    output_event_ports_count = 0;
    effect->presets_count = 0;
    effect->presets = NULL;
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
        symbol_node = lilv_port_get_symbol(plugin, lilv_port);
        effect->ports[i]->index = i;
        effect->ports[i]->symbol = lilv_node_as_string(symbol_node);
        effect->ports[i]->scale_points = NULL;

        snprintf(port_name, MAX_CHAR_BUF_SIZE, "%s", lilv_node_as_string(symbol_node));

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

        effect->ports[i]->type = TYPE_UNKNOWN;
        effect->ports[i]->hints = 0x0;

        if (lilv_port_is_a(plugin, lilv_port, lilv_audio))
        {
            effect->ports[i]->type = TYPE_AUDIO;

            /* Allocate memory to audio buffer */
            audio_buffer = (float *) calloc(g_sample_rate, sizeof(float));
            if (!audio_buffer)
            {
                fprintf(stderr, "can't get audio buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }

            effect->ports[i]->buffer = audio_buffer;
            effect->ports[i]->buffer_count = g_sample_rate;
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

            effect->ports[i]->scale_points = lilv_port_get_scale_points(plugin, lilv_port);

            /* Set the minimum value of control */
            float min_value;
            LilvNodes* lilvvalue_minimum = lilv_port_get_value(plugin, lilv_port, lilv_mod_minimum);
            if (lilvvalue_minimum == NULL)
                lilvvalue_minimum = lilv_port_get_value(plugin, lilv_port, lilv_minimum);

            if (lilvvalue_minimum != NULL)
                min_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_minimum));
            else
                min_value = 0.0f;

            /* Set the maximum value of control */
            float max_value;
            LilvNodes* lilvvalue_maximum = lilv_port_get_value(plugin, lilv_port, lilv_mod_maximum);
            if (lilvvalue_maximum == NULL)
                lilvvalue_maximum = lilv_port_get_value(plugin, lilv_port, lilv_maximum);

            if (lilvvalue_maximum != NULL)
                max_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_maximum));
            else
                max_value = 1.0f;

            /* Ensure min < max */
            if (min_value >= max_value)
                max_value = min_value + 0.1f;

            /* multiply ranges by sample rate if requested */
            if (lilv_port_has_property(plugin, lilv_port, g_sample_rate_node))
            {
                min_value *= g_sample_rate;
                max_value *= g_sample_rate;
            }

            /* Set the default value of control */
            float def_value;
            LilvNodes* lilvvalue_default = lilv_port_get_value(plugin, lilv_port, lilv_mod_default);
            if (lilvvalue_default == NULL)
                lilvvalue_default = lilv_port_get_value(plugin, lilv_port, lilv_default);

            if (lilvvalue_default != NULL)
                def_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_default));
            else
                def_value = min_value;

            (*control_buffer) = def_value;

            if (lilv_port_has_property(plugin, lilv_port, lilv_enumeration))
            {
                effect->ports[i]->hints |= HINT_ENUMERATION;

                // make 2 scalepoint enumeration work as toggle
                if (lilv_scale_points_size(effect->ports[i]->scale_points) == 2)
                    effect->ports[i]->hints |= HINT_TOGGLE;
            }
            if (lilv_port_has_property(plugin, lilv_port, lilv_integer))
            {
                effect->ports[i]->hints |= HINT_INTEGER;
            }
            if (lilv_port_has_property(plugin, lilv_port, lilv_toggled))
            {
                effect->ports[i]->hints |= HINT_TOGGLE;
            }
            if (lilv_port_has_property(plugin, lilv_port, lilv_trigger))
            {
                effect->ports[i]->hints |= HINT_TRIGGER;
                effect->hints |= HINT_TRIGGERS;
            }
            if (lilv_port_has_property(plugin, lilv_port, lilv_logarithmic))
            {
                effect->ports[i]->hints |= HINT_LOGARITHMIC;
            }

            effect->ports[i]->jack_port = NULL;
            effect->ports[i]->def_value = def_value;
            effect->ports[i]->min_value = min_value;
            effect->ports[i]->max_value = max_value;
            effect->ports[i]->prev_value = def_value;

            control_ports_count++;
            if (lilv_port_is_a(plugin, lilv_port, lilv_input)) input_control_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output)) output_control_ports_count++;

            lilv_nodes_free(lilvvalue_maximum);
            lilv_nodes_free(lilvvalue_minimum);
            lilv_nodes_free(lilvvalue_default);
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_cv))
        {
            effect->ports[i]->type = TYPE_CV;

            /* Allocate memory to cv buffer */
            cv_buffer = (float *) calloc(g_sample_rate, sizeof(float));
            if (!cv_buffer)
            {
                fprintf(stderr, "can't get cv buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }

            effect->ports[i]->buffer = cv_buffer;
            effect->ports[i]->buffer_count = g_sample_rate;
            lilv_instance_connect_port(lilv_instance, i, cv_buffer);

            /* Jack port creation */
            jack_flags |= JackPortIsControlVoltage;
            jack_port = jack_port_register(jack_client, port_name, JACK_DEFAULT_AUDIO_TYPE, jack_flags, 0);
            if (jack_port == NULL)
            {
                fprintf(stderr, "can't get jack port\n");
                error = ERR_JACK_PORT_REGISTER;
                goto error;
            }
            effect->ports[i]->jack_port = jack_port;

            cv_ports_count++;
            if (lilv_port_is_a(plugin, lilv_port, lilv_input)) input_cv_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output)) output_cv_ports_count++;
        }
        else if (lilv_port_is_a(plugin, lilv_port, lilv_event) ||
                    lilv_port_is_a(plugin, lilv_port, lilv_atom_port))
        {
            effect->ports[i]->type = TYPE_EVENT;
            if (lilv_port_is_a(plugin, lilv_port, lilv_event))
            {
                effect->ports[i]->hints |= HINT_OLD_EVENT_API;
            }
            else if (lilv_port_supports_event(plugin, lilv_port, lilv_timePosition))
            {
                effect->ports[i]->hints |= HINT_TRANSPORT;
                effect->hints |= HINT_TRANSPORT;
            }

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

    const LilvPort* control_in_port = lilv_plugin_get_port_by_designation(plugin, lilv_input, lilv_control_in);
    if (control_in_port)
    {
        effect->control_index = lilv_port_get_index(plugin, control_in_port);
    }
    else
    {
        effect->control_index = -1;
    }

    const LilvPort* enabled_port = lilv_plugin_get_port_by_designation(plugin, lilv_input, lilv_enabled);
    if (enabled_port)
    {
        effect->enabled_index = lilv_port_get_index(plugin, enabled_port);
        *(effect->ports[effect->enabled_index]->buffer) = 1.0f;
    }
    else
    {
        effect->enabled_index = -1;
    }

    const LilvPort* freewheel_port = lilv_plugin_get_port_by_designation(plugin, lilv_input, lilv_freeWheeling);
    if (freewheel_port)
    {
        effect->freewheel_index = lilv_port_get_index(plugin, freewheel_port);
        *(effect->ports[effect->freewheel_index]->buffer) = 0.0f;
    }
    else
    {
        effect->freewheel_index = -1;
    }

    const LilvPort* bpb_port = lilv_plugin_get_port_by_designation(plugin, lilv_input, lilv_timeBeatsPerBar);
    if (bpb_port)
    {
        effect->bpb_index = lilv_port_get_index(plugin, bpb_port);
        *(effect->ports[effect->bpb_index]->buffer) = g_transport_bpb;
    }
    else
    {
        effect->bpb_index = -1;
    }

    const LilvPort* bpm_port = lilv_plugin_get_port_by_designation(plugin, lilv_input, lilv_timeBeatsPerMinute);
    if (bpm_port)
    {
        effect->bpm_index = lilv_port_get_index(plugin, bpm_port);
        *(effect->ports[effect->bpm_index]->buffer) = g_transport_bpm;
    }
    else
    {
        effect->bpm_index = -1;
    }

    const LilvPort* speed_port = lilv_plugin_get_port_by_designation(plugin, lilv_input, lilv_timeSpeed);
    if (speed_port)
    {
        effect->speed_index = lilv_port_get_index(plugin, speed_port);
        *(effect->ports[effect->speed_index]->buffer) = g_jack_rolling ? 1.0f : 0.0f;
    }
    else
    {
        effect->speed_index = -1;
    }

    /* Allocate memory to indexes */
    /* Audio ports */
    if (audio_ports_count > 0)
    {
        effect->audio_ports_count = audio_ports_count;
        effect->audio_ports = (port_t **) calloc(audio_ports_count, sizeof(port_t *));

        if (input_audio_ports_count > 0)
        {
            effect->input_audio_ports_count = input_audio_ports_count;
            effect->input_audio_ports = (port_t **) calloc(input_audio_ports_count, sizeof(port_t *));
        }
        if (output_audio_ports_count > 0)
        {
            effect->output_audio_ports_count = output_audio_ports_count;
            effect->output_audio_ports = (port_t **) calloc(output_audio_ports_count, sizeof(port_t *));
        }
    }

    /* Control ports */
    if (control_ports_count > 0)
    {
        effect->control_ports_count = control_ports_count;
        effect->control_ports = (port_t **) calloc(control_ports_count, sizeof(port_t *));

        if (input_control_ports_count > 0)
        {
            effect->input_control_ports_count = input_control_ports_count;
            effect->input_control_ports = (port_t **) calloc(input_control_ports_count, sizeof(port_t *));
        }
        if (output_control_ports_count > 0)
        {
            effect->output_control_ports_count = output_control_ports_count;
            effect->output_control_ports = (port_t **) calloc(output_control_ports_count, sizeof(port_t *));
        }
    }

    /* CV ports */
    if (cv_ports_count > 0)
    {
        effect->cv_ports_count = cv_ports_count;
        effect->cv_ports = (port_t **) calloc(cv_ports_count, sizeof(port_t *));

        if (input_cv_ports_count > 0)
        {
            effect->input_cv_ports_count = input_cv_ports_count;
            effect->input_cv_ports = (port_t **) calloc(input_cv_ports_count, sizeof(port_t *));
        }
        if (output_cv_ports_count > 0)
        {
            effect->output_cv_ports_count = output_cv_ports_count;
            effect->output_cv_ports = (port_t **) calloc(output_cv_ports_count, sizeof(port_t *));
        }
    }

    /* Event ports */
    if (event_ports_count > 0)
    {
        effect->event_ports_count = event_ports_count;
        effect->event_ports = (port_t **) calloc(event_ports_count, sizeof(port_t *));

        if (input_event_ports_count > 0)
        {
            effect->input_event_ports_count = input_event_ports_count;
            effect->input_event_ports = (port_t **) calloc(input_event_ports_count, sizeof(port_t *));
        }
        if (output_event_ports_count > 0)
        {
            effect->output_event_ports_count = output_event_ports_count;
            effect->output_event_ports = (port_t **) calloc(output_event_ports_count, sizeof(port_t *));
        }
    }

    /* Index the audio, control and event ports */
    audio_ports_count = 0;
    input_audio_ports_count = 0;
    output_audio_ports_count = 0;
    control_ports_count = 0;
    input_control_ports_count = 0;
    output_control_ports_count = 0;
    cv_ports_count = 0;
    input_cv_ports_count = 0;
    output_cv_ports_count = 0;
    event_ports_count = 0;
    input_event_ports_count = 0;
    output_event_ports_count = 0;

    for (i = 0; i < ports_count; i++)
    {
        /* Audio ports */
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
        /* Control ports */
        else if (lilv_port_is_a(plugin, lilv_port, lilv_control))
        {
            effect->control_ports[control_ports_count] = effect->ports[i];
            control_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, lilv_input))
            {
                effect->input_control_ports[input_control_ports_count] = effect->ports[i];
                input_control_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output))
            {
                effect->output_control_ports[output_control_ports_count] = effect->ports[i];
                output_control_ports_count++;
            }
        }
        /* CV ports */
        else if (lilv_port_is_a(plugin, lilv_port, lilv_cv))
        {
            effect->cv_ports[cv_ports_count] = effect->ports[i];
            cv_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, lilv_input))
            {
                effect->input_cv_ports[input_cv_ports_count] = effect->ports[i];
                input_cv_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, lilv_output))
            {
                effect->output_cv_ports[output_cv_ports_count] = effect->ports[i];
                output_cv_ports_count++;
            }
        }
        /* Event ports */
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

    // Index writable properties
    LilvNode *rdfs_label = lilv_new_uri(g_lv2_data, LILV_NS_RDFS "label");
    LilvNode *patch_writable = lilv_new_uri(g_lv2_data, LV2_PATCH__writable);
    LilvNodes* properties = lilv_world_find_nodes(
        g_lv2_data,
        lilv_plugin_get_uri(effect->lilv_plugin),
        patch_writable,
        NULL);
    effect->properties_count = lilv_nodes_size(properties);
    effect->properties = (property_t **) calloc(effect->properties_count, sizeof(property_t *));
    uint32_t j = 0;
    for (j = 0; j < effect->properties_count; j++) effect->properties[j] = NULL;
    j = 0;

    LILV_FOREACH(nodes, p, properties)
    {
        const LilvNode* property = lilv_nodes_get(properties, p);
        LilvNode*       label    = lilv_nodes_get_first(
            lilv_world_find_nodes(
                g_lv2_data, property, rdfs_label, NULL));
        effect->properties[j] = (property_t *) malloc(sizeof(property_t));
        effect->properties[j]->label = lilv_node_duplicate(label);
        effect->properties[j]->property = lilv_node_duplicate(property);
        j++;
    }
    lilv_node_free(patch_writable);
    lilv_node_free(rdfs_label);
    lilv_nodes_free(properties);

    /* Default value of bypass */
    effect->bypass = 0.0f;
    effect->was_bypassed = false;

    effect->bypass_port.buffer_count = 1;
    effect->bypass_port.buffer = &effect->bypass;
    effect->bypass_port.min_value = 0.0f;
    effect->bypass_port.max_value = 1.0f;
    effect->bypass_port.def_value = 0.0f;
    effect->bypass_port.prev_value = 0.0f;
    effect->bypass_port.type = TYPE_CONTROL;
    effect->bypass_port.flow = FLOW_INPUT;
    effect->bypass_port.hints = HINT_TOGGLE;
    effect->bypass_port.symbol = g_bypass_port_symbol;

    // virtual presets port
    effect->preset_value = 0.0f;
    effect->presets_port.buffer_count = 1;
    effect->presets_port.buffer = &effect->preset_value;
    effect->presets_port.min_value = 0.0f;
    effect->presets_port.max_value = 1.0f;
    effect->presets_port.def_value = 0.0f;
    effect->presets_port.prev_value = 0.0f;
    effect->presets_port.type = TYPE_CONTROL;
    effect->presets_port.flow = FLOW_INPUT;
    effect->presets_port.hints = HINT_ENUMERATION|HINT_INTEGER;
    effect->presets_port.symbol = g_presets_port_symbol;

    lilv_node_free(lilv_audio);
    lilv_node_free(lilv_control);
    lilv_node_free(lilv_cv);
    lilv_node_free(lilv_input);
    lilv_node_free(lilv_control_in);
    lilv_node_free(lilv_enabled);
    lilv_node_free(lilv_enumeration);
    lilv_node_free(lilv_freeWheeling);
    lilv_node_free(lilv_timePosition);
    lilv_node_free(lilv_timeBeatsPerBar);
    lilv_node_free(lilv_timeBeatsPerMinute);
    lilv_node_free(lilv_timeSpeed);
    lilv_node_free(lilv_integer);
    lilv_node_free(lilv_toggled);
    lilv_node_free(lilv_trigger);
    lilv_node_free(lilv_logarithmic);
    lilv_node_free(lilv_output);
    lilv_node_free(lilv_event);
    lilv_node_free(lilv_midi);
    lilv_node_free(lilv_default);
    lilv_node_free(lilv_minimum);
    lilv_node_free(lilv_maximum);
    lilv_node_free(lilv_mod_default);
    lilv_node_free(lilv_mod_minimum);
    lilv_node_free(lilv_mod_maximum);
    lilv_node_free(lilv_atom_port);
    lilv_node_free(lilv_worker_interface);
    lilv_node_free(lilv_license_interface);

    /* Jack callbacks */
    jack_set_thread_init_callback(jack_client, JackThreadInit, effect);
    jack_set_process_callback(jack_client, ProcessPlugin, effect);
    jack_set_buffer_size_callback(jack_client, BufferSize, effect);
    jack_set_freewheel_callback(jack_client, FreeWheelMode, effect);

    lilv_instance_activate(lilv_instance);

    /* create ring buffer for events from socket/commandline */
    if (control_in_port)
    {
        effect->events_buffer = jack_ringbuffer_create(g_midi_buffer_size * 16); // 16 taken from jalv source code
        jack_ringbuffer_mlock(effect->events_buffer);
    }

    /* Try activate the Jack client */
    if (jack_activate(jack_client) != 0)
    {
        fprintf(stderr, "can't activate jack_client\n");
        error = ERR_JACK_CLIENT_ACTIVATION;
        goto error;
    }

    LoadPresets(effect);
    return instance;

    error:
        lilv_node_free(lilv_audio);
        lilv_node_free(lilv_control);
        lilv_node_free(lilv_cv);
        lilv_node_free(lilv_input);
        lilv_node_free(lilv_control_in);
        lilv_node_free(lilv_enabled);
        lilv_node_free(lilv_enumeration);
        lilv_node_free(lilv_freeWheeling);
        lilv_node_free(lilv_timePosition);
        lilv_node_free(lilv_timeBeatsPerBar);
        lilv_node_free(lilv_timeBeatsPerMinute);
        lilv_node_free(lilv_timeSpeed);
        lilv_node_free(lilv_integer);
        lilv_node_free(lilv_toggled);
        lilv_node_free(lilv_trigger);
        lilv_node_free(lilv_logarithmic);
        lilv_node_free(lilv_output);
        lilv_node_free(lilv_event);
        lilv_node_free(lilv_midi);
        lilv_node_free(lilv_default);
        lilv_node_free(lilv_minimum);
        lilv_node_free(lilv_maximum);
        lilv_node_free(lilv_mod_default);
        lilv_node_free(lilv_mod_minimum);
        lilv_node_free(lilv_mod_maximum);
        lilv_node_free(lilv_atom_port);
        lilv_node_free(lilv_worker_interface);
        lilv_node_free(lilv_license_interface);

        effects_remove(instance);

    return error;
}

int effects_preset_load(int effect_id, const char *uri)
{
    effect_t *effect;
    if (InstanceExist(effect_id))
    {
        LilvNode* preset_uri = lilv_new_uri(g_lv2_data, uri);

        if (preset_uri && lilv_world_load_resource(g_lv2_data, preset_uri) >= 0)
        {
            LilvState* state = lilv_state_new_from_world(g_lv2_data, &g_urid_map, preset_uri);
            if (!state)
            {
                lilv_node_free(preset_uri);
                return ERR_LV2_CANT_LOAD_STATE;
            }

            effect = &g_effects[effect_id];

            lilv_state_restore(state, effect->lilv_instance, SetParameterFromState, effect, 0, NULL);
            lilv_state_free(state);
            lilv_node_free(preset_uri);

            // force state of special designated ports
            if (effect->enabled_index >= 0)
            {
                *(effect->ports[effect->enabled_index]->buffer) = effect->bypass > 0.5f ? 0.0f : 1.0f;
            }
            if (effect->freewheel_index >= 0)
            {
                *(effect->ports[effect->freewheel_index]->buffer) = 0.0f;
            }
            if (effect->bpb_index >= 0)
            {
                *(effect->ports[effect->bpb_index]->buffer) = g_transport_bpb;
            }
            if (effect->bpm_index >= 0)
            {
                *(effect->ports[effect->bpm_index]->buffer) = g_transport_bpm;
            }
            if (effect->speed_index >= 0)
            {
                *(effect->ports[effect->speed_index]->buffer) = g_jack_rolling ? 1.0f : 0.0f;
            }

            return SUCCESS;
        }

        lilv_node_free(preset_uri);
        return ERR_LV2_INVALID_PRESET_URI;
    }

    return ERR_INSTANCE_NON_EXISTS;
}

int effects_preset_save(int effect_id, const char *dir, const char *file_name, const char *label)
{
    LilvState* const state = lilv_state_new_from_instance(
        g_effects[effect_id].lilv_plugin, g_effects[effect_id].lilv_instance,
        &g_urid_map,
        dir,
        dir,
        dir,
        dir,
        GetPortValueForState, &(g_effects[effect_id]),
        LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE, NULL);

    if (label)
    {
        lilv_state_set_label(state, label);
    }

    int ret = lilv_state_save(
        g_lv2_data, &g_urid_map, &g_urid_unmap, state, NULL, dir, file_name);
    lilv_state_free(state);
    return ret;
}

int effects_preset_show(const char *uri, char **state_str)
{
    LilvNode* preset_uri = lilv_new_uri(g_lv2_data, uri);

    if (lilv_world_load_resource(g_lv2_data, preset_uri) >= 0)
    {
        LilvState* state = lilv_state_new_from_world(g_lv2_data, &g_urid_map, preset_uri);
        if (!state)
        {
            lilv_node_free(preset_uri);
            return ERR_LV2_CANT_LOAD_STATE;
        }

        setenv("LILV_STATE_SKIP_PROPERTIES", "1", 1);

        (*state_str) =
            lilv_state_to_string(g_lv2_data, &g_urid_map, &g_urid_unmap, state, uri, NULL);

        unsetenv("LILV_STATE_SKIP_PROPERTIES");

        lilv_state_free(state);
        lilv_node_free(preset_uri);
        return SUCCESS;
    }

    lilv_node_free(preset_uri);
    return ERR_LV2_INVALID_PRESET_URI;
}

int effects_remove(int effect_id)
{
    uint32_t i;
    int j, start, end;
    effect_t *effect;

    // stop postpone events thread
    if (g_postevents_running == 1)
    {
        g_postevents_running = 0;
        sem_post(&g_postevents_semaphore);
        pthread_join(g_postevents_thread, NULL);
    }

    if (effect_id == REMOVE_ALL)
    {
        /* Disconnect the system connections */
        if (g_capture_ports != NULL)
        {
            for (i = 0; g_capture_ports[i]; i++)
            {
                const char **capture_connections =
                    jack_port_get_connections(jack_port_by_name(g_jack_global_client, g_capture_ports[i]));

                if (capture_connections)
                {
                    for (j = 0; capture_connections[j]; j++)
                    {
                        if (strstr(capture_connections[j], "system"))
                            jack_disconnect(g_jack_global_client, g_capture_ports[i], capture_connections[j]);
                    }

                    jack_free(capture_connections);
                }
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
            effect = &g_effects[j];

            if (jack_deactivate(effect->jack_client) != 0) return ERR_JACK_CLIENT_DEACTIVATION;

            FreeFeatures(effect);

            if (effect->event_ports)
            {
                for (i = 0; i < effect->event_ports_count; i++)
                {
                    lv2_evbuf_free(effect->event_ports[i]->evbuf);
                }
            }

            if (effect->ports)
            {
                for (i = 0; i < effect->ports_count; i++)
                {
                    if (effect->ports[i])
                    {
                        free(effect->ports[i]->buffer);
                        lilv_scale_points_free(effect->ports[i]->scale_points);
                        free(effect->ports[i]);
                    }
                }
                free(effect->ports);
            }

            if (effect->lilv_instance) lilv_instance_deactivate(effect->lilv_instance);
            lilv_instance_free(effect->lilv_instance);
            if (effect->jack_client) jack_client_close(effect->jack_client);

            free(effect->audio_ports);
            free(effect->input_audio_ports);
            free(effect->output_audio_ports);
            free(effect->control_ports);
            if (effect->events_buffer) {
                jack_ringbuffer_free (effect->events_buffer);
            }

            if (effect->presets)
            {
                for (i = 0; i < effect->presets_count; i++)
                {
                    free(effect->presets[i]);
                }
                free(effect->presets);
            }

            InstanceDelete(j);
        }
    }

    if (effect_id == REMOVE_ALL)
    {
        pthread_mutex_lock(&g_midi_learning_mutex);
        g_midi_learning = NULL;
        pthread_mutex_unlock(&g_midi_learning_mutex);

        for (j = 0; j < MAX_MIDI_CC_ASSIGN; j++)
        {
            g_midi_cc_list[j].channel = -1;
            g_midi_cc_list[j].controller = 0;
            g_midi_cc_list[j].minimum = 0.0f;
            g_midi_cc_list[j].maximum = 1.0f;
            g_midi_cc_list[j].effect_id = ASSIGNMENT_NULL;
            g_midi_cc_list[j].symbol = NULL;
            g_midi_cc_list[j].port = NULL;
        }

#ifdef HAVE_CONTROLCHAIN
        if (g_cc_client)
        {
            for (int i = 0; i < CC_MAX_DEVICES; i++)
            {
                for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
                {
                    assignment_t *assignment = &g_assignments_list[i][j];

                    if (assignment->effect_id == ASSIGNMENT_NULL)
                        break;
                    if (assignment->effect_id == ASSIGNMENT_UNUSED)
                        continue;

                    cc_assignment_key_t key;
                    key.device_id = assignment->device_id;
                    key.id = assignment->assignment_id;
                    cc_client_unassignment(g_cc_client, &key);
                }
            }
        }

        memset(&g_assignments_list, 0, sizeof(g_assignments_list));

        for (int i = 0; i < CC_MAX_DEVICES; i++)
        {
            for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
            {
                g_assignments_list[i][j].effect_id = ASSIGNMENT_NULL;
            }
        }
#endif

        // reset all events
        pthread_mutex_lock(&g_rtsafe_mutex);

        // cleanup pending events memory
        struct list_head *it;
        postponed_event_list_data* eventptr;

        list_for_each(it, &g_rtsafe_list)
        {
            eventptr = list_entry(it, postponed_event_list_data, siblings);
            rtsafe_memory_pool_deallocate(g_rtsafe_mem_pool, eventptr);
        }

        INIT_LIST_HEAD(&g_rtsafe_list);
        pthread_mutex_unlock(&g_rtsafe_mutex);
    }
    else
    {
        pthread_mutex_lock(&g_midi_learning_mutex);
        if (g_midi_learning != NULL && g_midi_learning->effect_id == effect_id)
        {
            g_midi_learning->effect_id = ASSIGNMENT_UNUSED;
            g_midi_learning->symbol = NULL;
            g_midi_learning->port = NULL;
            g_midi_learning = NULL;
        }
        pthread_mutex_unlock(&g_midi_learning_mutex);

        for (j = 0; j < MAX_MIDI_CC_ASSIGN; j++)
        {
            if (g_midi_cc_list[j].effect_id == ASSIGNMENT_NULL)
                break;
            if (g_midi_cc_list[j].effect_id == ASSIGNMENT_UNUSED)
                continue;
            if (g_midi_cc_list[j].effect_id != effect_id)
                continue;

            g_midi_cc_list[j].effect_id = ASSIGNMENT_UNUSED;
            g_midi_cc_list[j].channel = -1;
            g_midi_cc_list[j].controller = 0;
            g_midi_cc_list[j].minimum = 0.0f;
            g_midi_cc_list[j].maximum = 1.0f;
            g_midi_cc_list[j].symbol = NULL;
            g_midi_cc_list[j].port = NULL;
        }

#ifdef HAVE_CONTROLCHAIN
        for (int i = 0; i < CC_MAX_DEVICES; i++)
        {
            for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
            {
                assignment_t *assignment = &g_assignments_list[i][j];

                if (assignment->effect_id == ASSIGNMENT_NULL)
                    break;
                if (assignment->effect_id == ASSIGNMENT_UNUSED)
                    continue;
                if (assignment->effect_id != effect_id)
                    continue;

                if (g_cc_client)
                {
                    cc_assignment_key_t key;
                    key.device_id = assignment->device_id;
                    key.id = assignment->assignment_id;
                    cc_client_unassignment(g_cc_client, &key);
                }

                memset(assignment, 0, sizeof(assignment_t));
                assignment->effect_id = ASSIGNMENT_UNUSED;
            }
        }
#endif

        // flush events for all effects except this one
        RunPostPonedEvents(effect_id);
    }

    // start thread again
    if (g_postevents_running == 0)
    {
        g_postevents_running = 1;
        pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);
    }

    return SUCCESS;
}

int effects_connect(const char *portA, const char *portB)
{
    int ret;

    ret = jack_connect(g_jack_global_client, portA, portB);
    if (ret != 0 && ret != EEXIST) ret = jack_connect(g_jack_global_client, portB, portA);
    if (ret == EEXIST) ret = 0;

    if (ret != 0) return ERR_JACK_PORT_CONNECTION;

    return ret;
}

int effects_disconnect(const char *portA, const char *portB)
{
    int ret;

    ret = jack_disconnect(g_jack_global_client, portA, portB);
    if (ret != 0) ret = jack_disconnect(g_jack_global_client, portB, portA);
    if (ret != 0) return ERR_JACK_PORT_DISCONNECTION;

    return ret;
}

int effects_set_parameter(int effect_id, const char *control_symbol, float value)
{
    port_t *port;

    static int last_effect_id = -1;
    static const char *last_symbol = NULL;
    static float *last_buffer = NULL, last_min, last_max;

    if (InstanceExist(effect_id))
    {
        // check whether is setting the same parameter
        if (last_effect_id == effect_id)
        {
            if (last_symbol && strcmp(last_symbol, control_symbol) == 0)
            {
                if (value < last_min)
                    value = last_min;
                else if (value > last_max)
                    value = last_max;

                *last_buffer = value;
                return SUCCESS;
            }
        }

        port = FindEffectInputPortBySymbol(&(g_effects[effect_id]), control_symbol);
        if (port)
        {
            // stores the data of the current control
            last_min = port->min_value;
            last_max = port->max_value;
            last_buffer = port->buffer;
            last_symbol = port->symbol;

            if (value < last_min)
                value = last_min;
            else if (value > last_max)
                value = last_max;

            *last_buffer = value;
            return SUCCESS;
        }

        return ERR_LV2_INVALID_PARAM_SYMBOL;
    }

    return ERR_INSTANCE_NON_EXISTS;
}

int effects_set_property(int effect_id, const char *label, const char *value)
{
    if (InstanceExist(effect_id))
    {
        property_t *prop = FindEffectPropertyByLabel(&(g_effects[effect_id]), label);
        if (prop)
        {
            const char *property = lilv_node_as_uri(prop->property);

            LV2_Atom_Forge forge = g_lv2_atom_forge;
            LV2_Atom_Forge_Frame frame;
            uint8_t buf[1024];
            lv2_atom_forge_set_buffer(&forge, buf, sizeof(buf));

            lv2_atom_forge_object(&forge, &frame, 0, g_urids.patch_Set);
            lv2_atom_forge_key(&forge, g_urids.patch_property);
            lv2_atom_forge_urid(&forge, g_urid_map.map(g_urid_map.handle, property));
            lv2_atom_forge_key(&forge, g_urids.patch_value);
            lv2_atom_forge_path(&forge, value, strlen(value));

            const LV2_Atom* atom = lv2_atom_forge_deref(&forge, frame.ref);
            jack_ringbuffer_write(g_effects[effect_id].events_buffer, (const char *)atom, lv2_atom_total_size(atom));
            return SUCCESS;
        }
        return ERR_LV2_INVALID_PARAM_SYMBOL;
    }
    return ERR_INSTANCE_NON_EXISTS;
}

int effects_get_parameter(int effect_id, const char *control_symbol, float *value)
{
    const port_t *port;

    if (InstanceExist(effect_id))
    {
        port = FindEffectInputPortBySymbol(&(g_effects[effect_id]), control_symbol);
        if (port)
        {
           (*value) = *(port->buffer);
           return SUCCESS;
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
    if (strcmp(op, ">") == 0)
        iop = 0;
    else if (strcmp(op, ">=") == 0)
        iop = 1;
    else if (strcmp(op, "<") == 0)
        iop = 2;
    else if (strcmp(op, "<=") == 0)
        iop = 3;
    else if (strcmp(op, "==") == 0)
        iop = 4;
    else if (strcmp(op, "!=") == 0)
        iop = 5;
    else
        return ERR_ASSIGNMENT_INVALID_OP;


    const LilvNode *symbol = lilv_new_string(g_lv2_data, control_symbol);
    const LilvPort *port = lilv_plugin_get_port_by_symbol(g_effects[effect_id].lilv_plugin, symbol);

    int port_id = lilv_port_get_index(g_effects[effect_id].lilv_plugin, port);

    g_effects[effect_id].monitors_count++;
    g_effects[effect_id].monitors =
        (monitor_t**)realloc(g_effects[effect_id].monitors, sizeof(monitor_t *) * g_effects[effect_id].monitors_count);

    int idx = g_effects[effect_id].monitors_count - 1;
    g_effects[effect_id].monitors[idx] = (monitor_t*)malloc(sizeof(monitor_t));
    g_effects[effect_id].monitors[idx]->port_id = port_id;
    g_effects[effect_id].monitors[idx]->op = iop;
    g_effects[effect_id].monitors[idx]->value = value;
    g_effects[effect_id].monitors[idx]->last_notified_value = 0.0;
    return SUCCESS;
}

int effects_monitor_output_parameter(int effect_id, const char *control_symbol)
{
    port_t *port;

    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;

    port = FindEffectOutputPortBySymbol(&(g_effects[effect_id]), control_symbol);

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    // check if already monitored
    if (port->hints & HINT_MONITORED)
        return SUCCESS;

    // set prev_value
    port->prev_value = (*port->buffer);
    port->hints |= HINT_MONITORED;

    // activate output monitor
    g_effects[effect_id].hints |= HINT_OUTPUT_MONITORS;

    return SUCCESS;
}

int effects_bypass(int effect_id, int value)
{
    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    effect_t *effect = &g_effects[effect_id];
    effect->bypass = value ? 1.0f : 0.0f;

    if (effect->enabled_index >= 0)
    {
        *(effect->ports[effect->enabled_index]->buffer) = value ? 0.0f : 1.0f;
    }

    return SUCCESS;
}

int effects_get_parameter_symbols(int effect_id, int output_ports, const char** symbols)
{
    if (!InstanceExist(effect_id))
    {
        symbols = NULL;
        return ERR_INSTANCE_NON_EXISTS;
    }

    uint32_t i, j;
    effect_t *effect = &g_effects[effect_id];

    for (i = 0, j = 0; i < effect->control_ports_count && j < 127; i++)
    {
        if (output_ports)
        {
            if (effect->control_ports[i]->flow != FLOW_OUTPUT)
                continue;
        }
        else
        {
            if (effect->control_ports[i]->flow != FLOW_INPUT)
                continue;
        }

        symbols[j++] = (const char *) effect->control_ports[i]->symbol;
    }

    symbols[j] = NULL;

    return SUCCESS;
}

int effects_get_presets_uris(int effect_id, const char **uris)
{
    if (!InstanceExist(effect_id))
    {
        uris = NULL;
        return ERR_INSTANCE_NON_EXISTS;
    }

    uint32_t i;
    effect_t *effect = &g_effects[effect_id];

    for (i = 0; i < effect->presets_count; i++)
    {
        uris[i] = (const char *) lilv_node_as_uri(effect->presets[i]->uri);
    }

    uris[i] = NULL;

    return SUCCESS;
}

int effects_get_parameter_info(int effect_id, const char *control_symbol, float **range, const char **scale_points)
{
    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    uint32_t i;
    effect_t *effect = &g_effects[effect_id];

    for (i = 0; i < effect->control_ports_count; i++)
    {
        if (strcmp(control_symbol, effect->control_ports[i]->symbol) == 0)
        {
            (*range[0]) = effect->control_ports[i]->def_value;
            (*range[1]) = effect->control_ports[i]->min_value;
            (*range[2]) = effect->control_ports[i]->max_value;
            (*range[3]) = *(effect->control_ports[i]->buffer);

            /* Get the scale points */
            LilvScalePoints *points = effect->control_ports[i]->scale_points;
            if (points != NULL)
            {
                uint32_t j = 0;
                LilvIter *iter;
                for (iter = lilv_scale_points_begin(points);
                    !lilv_scale_points_is_end(points, iter);
                    iter = lilv_scale_points_next(points, iter))
                {
                    const LilvScalePoint *point = lilv_scale_points_get(points, iter);
                    scale_points[j++] = lilv_node_as_string(lilv_scale_point_get_value(point));
                    scale_points[j++] = lilv_node_as_string(lilv_scale_point_get_label(point));
                }

                scale_points[j++] = NULL;
                scale_points[j] = NULL;
                lilv_scale_points_free(points);
            }
            else
            {
                scale_points[0] = NULL;
            }

            return SUCCESS;
        }
    }

    return ERR_LV2_INVALID_PARAM_SYMBOL;
}

int effects_midi_learn(int effect_id, const char *control_symbol, float minimum, float maximum)
{
    const port_t *port;

    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    const bool is_bypass = !strcmp(control_symbol, g_bypass_port_symbol);

    pthread_mutex_lock(&g_midi_learning_mutex);
    if (g_midi_learning != NULL)
    {
        g_midi_learning->effect_id = ASSIGNMENT_UNUSED;
        g_midi_learning->symbol = NULL;
        g_midi_learning->port = NULL;
        g_midi_learning = NULL;
    }
    pthread_mutex_unlock(&g_midi_learning_mutex);

    // if already mapped set it to re-learn
    for (int i = 0; i < MAX_MIDI_CC_ASSIGN; i++)
    {
        if (g_midi_cc_list[i].effect_id == ASSIGNMENT_NULL)
            break;
        if (g_midi_cc_list[i].effect_id == ASSIGNMENT_UNUSED)
            continue;
        if (g_midi_cc_list[i].effect_id != effect_id)
            continue;
        if (strcmp(g_midi_cc_list[i].symbol, control_symbol))
            continue;

        g_midi_cc_list[i].channel = -1;
        g_midi_cc_list[i].controller = 0;

        if (!is_bypass)
        {
            g_midi_cc_list[i].minimum = minimum;
            g_midi_cc_list[i].maximum = maximum;
        }

        pthread_mutex_lock(&g_midi_learning_mutex);
        g_midi_learning = &g_midi_cc_list[i];
        pthread_mutex_unlock(&g_midi_learning_mutex);
        return SUCCESS;
    }

    // Otherwise locate a free position on list and take it
    for (int i = 0; i < MAX_MIDI_CC_ASSIGN; i++)
    {
        if (INSTANCE_IS_VALID(g_midi_cc_list[i].effect_id))
            continue;

        if (is_bypass)
        {
            g_midi_cc_list[i].symbol = g_bypass_port_symbol;
            g_midi_cc_list[i].port = NULL;
        }
        else
        {
            port = FindEffectInputPortBySymbol(&(g_effects[effect_id]), control_symbol);

            if (port == NULL)
                return ERR_LV2_INVALID_PARAM_SYMBOL;

            g_midi_cc_list[i].minimum = minimum;
            g_midi_cc_list[i].maximum = maximum;
            g_midi_cc_list[i].symbol = port->symbol;
            g_midi_cc_list[i].port = port;
        }

        g_midi_cc_list[i].channel = -1;
        g_midi_cc_list[i].controller = 0;
        g_midi_cc_list[i].effect_id = effect_id;

        pthread_mutex_lock(&g_midi_learning_mutex);
        g_midi_learning = &g_midi_cc_list[i];
        pthread_mutex_unlock(&g_midi_learning_mutex);
        return SUCCESS;
    }

    return ERR_ASSIGNMENT_LIST_FULL;
}

int effects_midi_map(int effect_id, const char *control_symbol, int channel, int controller, float minimum, float maximum)
{
    const port_t *port;

    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    const bool is_bypass = !strcmp(control_symbol, g_bypass_port_symbol);

    // update current mapping first if it exists
    for (int i = 0; i < MAX_MIDI_CC_ASSIGN; i++)
    {
        if (g_midi_cc_list[i].effect_id == ASSIGNMENT_NULL)
            break;
        if (g_midi_cc_list[i].effect_id == ASSIGNMENT_UNUSED)
            continue;
        if (g_midi_cc_list[i].effect_id != effect_id)
            continue;
        if (strcmp(g_midi_cc_list[i].symbol, control_symbol))
            continue;

        g_midi_cc_list[i].channel = channel;
        g_midi_cc_list[i].controller = controller;

        if (!is_bypass)
        {
            g_midi_cc_list[i].minimum = minimum;
            g_midi_cc_list[i].maximum = maximum;
        }

        return SUCCESS;
    }

    // Otherwise locate a free position on list and take it
    for (int i = 0; i < MAX_MIDI_CC_ASSIGN; i++)
    {
        if (INSTANCE_IS_VALID(g_midi_cc_list[i].effect_id))
            continue;

        if (is_bypass)
        {
            g_midi_cc_list[i].symbol = g_bypass_port_symbol;
            g_midi_cc_list[i].port = NULL;
        }
        else
        {
            port = FindEffectInputPortBySymbol(&(g_effects[effect_id]), control_symbol);

            if (port == NULL)
                return ERR_LV2_INVALID_PARAM_SYMBOL;

            g_midi_cc_list[i].minimum = minimum;
            g_midi_cc_list[i].maximum = maximum;
            g_midi_cc_list[i].symbol = port->symbol;
            g_midi_cc_list[i].port = port;
        }

        g_midi_cc_list[i].channel = channel;
        g_midi_cc_list[i].controller = controller;
        g_midi_cc_list[i].effect_id = effect_id;
        return SUCCESS;
    }

    return ERR_ASSIGNMENT_LIST_FULL;
}

int effects_midi_unmap(int effect_id, const char *control_symbol)
{
    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    for (int i = 0; i < MAX_MIDI_CC_ASSIGN; i++)
    {
        if (g_midi_cc_list[i].effect_id == ASSIGNMENT_NULL)
            break;
        if (g_midi_cc_list[i].effect_id == ASSIGNMENT_UNUSED)
            continue;
        if (g_midi_cc_list[i].effect_id != effect_id)
            continue;
        if (strcmp(g_midi_cc_list[i].symbol, control_symbol))
            continue;

        pthread_mutex_lock(&g_midi_learning_mutex);
        if (g_midi_learning == &g_midi_cc_list[i])
            g_midi_learning = NULL;
        pthread_mutex_unlock(&g_midi_learning_mutex);

        g_midi_cc_list[i].channel = -1;
        g_midi_cc_list[i].controller = 0;
        g_midi_cc_list[i].minimum = 0.0f;
        g_midi_cc_list[i].maximum = 1.0f;
        g_midi_cc_list[i].effect_id = ASSIGNMENT_UNUSED;
        g_midi_cc_list[i].symbol = NULL;
        g_midi_cc_list[i].port = NULL;
        return SUCCESS;
    }

    return ERR_LV2_INVALID_PARAM_SYMBOL;
}

int effects_licensee(int effect_id, char **licensee_ptr)
{
    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    effect_t *effect = &g_effects[effect_id];

    if (effect->license_iface)
    {
        char* licensee = NULL;
        LV2_Handle handle = lilv_instance_get_handle(effect->lilv_instance);

        if (effect->license_iface->status(handle) == MOD_LICENSE_SUCCESS)
            licensee = effect->license_iface->licensee(handle);

        if (licensee)
        {
            *licensee_ptr = licensee;
            return SUCCESS;
        }
    }

    return ERR_INSTANCE_UNLICENSED;
}

void effects_midi_program_listen(int enable, int channel)
{
    if (enable == 0 || channel < 0 || channel > 15)
        channel = -1;

    g_midi_program_listen = channel;
}

int effects_cc_map(int effect_id, const char *control_symbol, int device_id, int actuator_id,
                   const char *label, float value, float minimum, float maximum, int steps, const char *unit,
                   int scalepoints_count, const scalepoint_t *scalepoints)
{
#ifdef HAVE_CONTROLCHAIN
    InitializeControlChainIfNeeded();

    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;
    if (!g_cc_client)
        return ERR_CONTROL_CHAIN_UNAVAILABLE;
    if (scalepoints_count == 1)
        return ERR_ASSIGNMENT_INVALID_OP;

    effect_t *effect = &(g_effects[effect_id]);
    port_t *port = FindEffectInputPortBySymbol(effect, control_symbol);

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    cc_assignment_t assignment;
    assignment.device_id = device_id;
    assignment.actuator_id = actuator_id;
    assignment.mode  = 0x0;
    assignment.label = label;
    assignment.value = value;
    assignment.min   = minimum;
    assignment.max   = maximum;
    assignment.def   = port->def_value;
    assignment.steps = steps;
    assignment.unit  = unit;
    assignment.list_count = scalepoints_count;

    if (port->hints & HINT_TOGGLE)
        assignment.mode = CC_MODE_TOGGLE;
    else if (port->hints & HINT_INTEGER)
        assignment.mode = CC_MODE_INTEGER;
    else
        assignment.mode = CC_MODE_REAL;

    if (port->hints & HINT_TRIGGER)
        assignment.mode |= CC_MODE_TRIGGER;

    // note: logarithmic and tap-tempo missing

    cc_item_t *item_data;

    if (scalepoints_count >= 2)
    {
        item_data = malloc(sizeof(cc_item_t)*scalepoints_count);
        assignment.list_items = malloc(sizeof(cc_item_t*)*scalepoints_count);

        if (assignment.list_items != NULL && item_data != NULL)
        {
            assignment.mode |= CC_MODE_OPTIONS;

            for (int i = 0; i < scalepoints_count; i++)
            {
                item_data[i].label = scalepoints[i].label;
                item_data[i].value = scalepoints[i].value;
                assignment.list_items[i] = item_data + i;
            }
        }
        else
        {
            free(item_data);
            free(assignment.list_items);
            return ERR_MEMORY_ALLOCATION;
        }
    }
    else
    {
        item_data = NULL;
        assignment.list_items = NULL;
    }

    if (!strcmp(control_symbol, g_bypass_port_symbol))
    {
        // invert value for bypass
        assignment.value = value > 0.5f ? 0.0f : 1.0f;
        assignment.def = 1.0f;
    }
    else if (!strcmp(control_symbol, g_presets_port_symbol))
    {
        // virtual presets port
        port->min_value = minimum;
        port->max_value = maximum;
        port->def_value = port->prev_value = *port->buffer = value;
    }

    const int assignment_id = cc_client_assignment(g_cc_client, &assignment);

    free(item_data);
    free(assignment.list_items);

    if (assignment_id < 0)
    {
        return ERR_ASSIGNMENT_FAILED;
    }

    assignment_t *item = &g_assignments_list[assignment.device_id][assignment_id];

    item->effect_id = effect_id;
    item->port = port;
    item->device_id = device_id;
    item->assignment_id = assignment_id;

    return SUCCESS;
#else
    return ERR_CONTROL_CHAIN_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
    UNUSED_PARAM(control_symbol);
    UNUSED_PARAM(device_id);
    UNUSED_PARAM(actuator_id);
    UNUSED_PARAM(label);
    UNUSED_PARAM(value);
    UNUSED_PARAM(minimum);
    UNUSED_PARAM(maximum);
    UNUSED_PARAM(steps);
    UNUSED_PARAM(unit);
    UNUSED_PARAM(scalepoints_count);
    UNUSED_PARAM(scalepoints);
#endif
}

int effects_cc_unmap(int effect_id, const char *control_symbol)
{
#ifdef HAVE_CONTROLCHAIN
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;
    if (!g_cc_client)
        return ERR_CONTROL_CHAIN_UNAVAILABLE;

    for (int i = 0; i < CC_MAX_DEVICES; i++)
    {
        for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
        {
            assignment_t *assignment = &g_assignments_list[i][j];

            if (assignment->effect_id == ASSIGNMENT_NULL)
                break;

            if (assignment->effect_id == effect_id && assignment->port != NULL &&
                strcmp(assignment->port->symbol, control_symbol) == 0)
            {
                cc_assignment_key_t key;
                key.device_id = assignment->device_id;
                key.id = assignment->assignment_id;

                memset(assignment, 0, sizeof(assignment_t));
                assignment->effect_id = ASSIGNMENT_UNUSED;

                cc_client_unassignment(g_cc_client, &key);
                return SUCCESS;
            }
        }
    }

    return ERR_ASSIGNMENT_INVALID_OP;
#else
    return ERR_CONTROL_CHAIN_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
    UNUSED_PARAM(control_symbol);
#endif
}

float effects_jack_cpu_load(void)
{
    return jack_cpu_load(g_jack_global_client);
}

#define OS_SEP '/'

void effects_bundle_add(const char* bpath)
{
#ifdef HAVE_NEW_LILV
    // lilv wants the last character as the separator
    char tmppath[PATH_MAX+2];
    char* bundlepath = realpath(bpath, tmppath);

    if (bundlepath == NULL)
        return;

    {
        const size_t size = strlen(bundlepath);
        if (size <= 1)
            return;

        if (bundlepath[size] != OS_SEP)
        {
            bundlepath[size  ] = OS_SEP;
            bundlepath[size+1] = '\0';
        }
    }

    // convert bundle string into a lilv node
    LilvNode* bundlenode = lilv_new_file_uri(g_lv2_data, NULL, bundlepath);

    // load the bundle
    lilv_world_load_bundle(g_lv2_data, bundlenode);

    // free bundlenode, no longer needed
    lilv_node_free(bundlenode);

    // refresh plugins
    g_plugins = lilv_world_get_all_plugins(g_lv2_data);
#endif
}

void effects_bundle_remove(const char* bpath)
{
#ifdef HAVE_NEW_LILV
    // lilv wants the last character as the separator
    char tmppath[PATH_MAX+2];
    char* bundlepath = realpath(bpath, tmppath);

    if (bundlepath == NULL)
        return;

    {
        const size_t size = strlen(bundlepath);
        if (size <= 1)
            return;

        if (bundlepath[size] != OS_SEP)
        {
            bundlepath[size  ] = OS_SEP;
            bundlepath[size+1] = '\0';
        }
    }

    // convert bundle string into a lilv node
    LilvNode* bundlenode = lilv_new_file_uri(g_lv2_data, NULL, bundlepath);

    // unload the bundle
    lilv_world_unload_bundle(g_lv2_data, bundlenode);

    // free bundlenode, no longer needed
    lilv_node_free(bundlenode);

    // refresh plugins
    g_plugins = lilv_world_get_all_plugins(g_lv2_data);
#endif
}

int effects_link_enable(int enable)
{
#ifdef HAVE_HYLIA
    if (g_hylia_instance)
    {
        hylia_enable(g_hylia_instance, enable);
        hylia_enabled = enable;
        g_transport_reset = true;
        return SUCCESS;
    }
#else
    UNUSED_PARAM(enable);
#endif

    return ERR_LINK_UNAVAILABLE;
}

int effects_processing_enable(int enable)
{
    g_processing_enabled = enable;

    if (enable > 1) {
        effects_output_data_ready();
    }

    return SUCCESS;
}

void effects_transport(int rolling, double beats_per_bar, double beats_per_minute)
{
    g_transport_bpb = beats_per_bar;
    g_transport_bpm = beats_per_minute;

#ifdef HAVE_HYLIA
    if (g_hylia_instance)
    {
        hylia_set_beats_per_bar(g_hylia_instance, beats_per_bar);
        hylia_set_beats_per_minute(g_hylia_instance, beats_per_minute);
    }
#endif

    if ((g_jack_pos.valid & JackPositionBBT) == 0)
    {
        // old timebase master no longer active, make ourselves master again
        jack_set_timebase_callback(g_jack_global_client, 1, JackTimebase, NULL);
    }

    if (g_jack_rolling != (rolling != 0) ||
        g_jack_rolling != (jack_transport_query(g_jack_global_client, NULL) == JackTransportRolling))
    {
        if (rolling)
        {
            jack_transport_start(g_jack_global_client);
        }
        else
        {
            jack_transport_stop(g_jack_global_client);
            jack_transport_locate(g_jack_global_client, 0);
        }
        // g_jack_rolling is updated on the next jack callback
        g_transport_reset = true;
    }
}

void effects_output_data_ready(void)
{
    if (! g_postevents_ready)
    {
        g_postevents_ready = true;
        sem_post(&g_postevents_semaphore);
    }
}
