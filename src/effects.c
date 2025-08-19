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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <winsock2.h>
#include <windows.h>
typedef unsigned int uint;
#define PRIi64 "lld"
#define PRId64 "llu"
#define RTLD_LOCAL 0
#define RTLD_NOW 0
#define dlopen(path, flags)  LoadLibrary(path)
#define dlsym(lib, funcname) GetProcAddress((HMODULE)lib, funcname)
#define dlclose(lib)         FreeLibrary((HMODULE)lib)
#define setenv(...)
#define unsetenv(...)
#else
#include <dlfcn.h>
#endif

/* Jack */
#include <jack/jack.h>
#include <jack/intclient.h>
#include <jack/metadata.h>
#include <jack/midiport.h>
#include <jack/thread.h>
#include <jack/transport.h>
#include <jack/uuid.h>

/* LV2 and Lilv */
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/event/event.h>
#include <lv2/log/log.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/patch/patch.h>
#include <lv2/parameters/parameters.h>
#include <lv2/port-props/port-props.h>
#include <lv2/presets/presets.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/urid/urid.h>
#include <lv2/uri-map/uri-map.h>
#include <lv2/worker/worker.h>
#include "lv2/control-input-port-change-request.h"
#include "lv2/kxstudio-properties.h"
#include "lv2/lv2-hmi.h"
#include "lv2/mod-license.h"

// do not enable external-ui support in embed targets
#if !(defined(_MOD_DEVICE_DUO) || defined(_MOD_DEVICE_DUOX) || defined(_MOD_DEVICE_DWARF))
#define WITH_EXTERNAL_UI_SUPPORT
#endif

#ifdef WITH_EXTERNAL_UI_SUPPORT
#include <lv2/data-access/data-access.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/ui/ui.h>
#endif

#ifdef HAVE_CONTROLCHAIN
/* Control Chain */
#include <cc_client.h>
#endif

#ifdef HAVE_HYLIA
/* Hylia / Link */
#include <hylia.h>
#endif

#include "mod-host.h"
#ifdef __MOD_DEVICES__
#include "sys_host.h"
#include "dsp/gate_core.h"
#endif

#ifndef HAVE_NEW_LILV
#define lilv_free(x) free(x)
#warning Your current lilv version does not support loading or unloading bundles
#endif

#ifndef LV2_BUF_SIZE__nominalBlockLength
#define LV2_BUF_SIZE__nominalBlockLength LV2_BUF_SIZE_PREFIX "nominalBlockLength"
#endif

#ifndef LV2_CORE__enabled
#define LV2_CORE__enabled LV2_CORE_PREFIX "enabled"
#endif

#ifndef LV2_STATE__threadSafeRestore
#define LV2_STATE__threadSafeRestore LV2_STATE_PREFIX "threadSafeRestore"
#endif

#ifndef LILV_URI_CV_PORT
#define LILV_URI_CV_PORT "http://lv2plug.in/ns/lv2core#CVPort"
#endif

#ifndef HAVE_LV2_STATE_FREE_PATH
// forwards compatibility with old lv2 headers
#define LV2_STATE__freePath LV2_STATE_PREFIX "freePath"
typedef void* LV2_State_Free_Path_Handle;
typedef struct {
    LV2_State_Free_Path_Handle handle;
    void (*free_path)(LV2_State_Free_Path_Handle handle, char* path);
} LV2_State_Free_Path;
#endif

#define LILV_NS_MOD "http://moddevices.com/ns/mod#"

// custom jack flag used for cv
// needed because we prefer jack2 which doesn't have metadata yet
#define JackPortIsControlVoltage 0x100

#if defined(_MOD_DEVICE_DUOX) || defined(_MOD_DEVICE_DWARF)
#define MOD_IO_PROCESSING_ENABLED
#endif

#ifdef _DARKGLASS_DEVICE_PABLITO
#define _DARKGLASS_PABLITO
#endif

/* Local */
#include "effects.h"
#include "monitor.h"
#include "socket.h"
#include "uridmap.h"
#include "lv2_evbuf.h"
#include "worker.h"
#include "state-paths.h"
#include "symap.h"
#include "monitor/monitor-client.h"
#include "sha1/sha1.h"
#include "rtmempool/list.h"
#include "rtmempool/rtmempool.h"
#include "filter.h"
#include "mod-memset.h"

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
    HINT_SHOULD_UPDATE = 1 << 6, // inputs only, for external UIs
    // cv
    HINT_CV_MOD        = 1 << 0, // uses mod cvport
    HINT_CV_RANGES     = 1 << 1, // port info includes ranges
    // events
    HINT_TRANSPORT     = 1 << 0,
    HINT_MIDI_EVENT    = 1 << 1,
    HINT_OLD_EVENT_API = 1 << 2,
};

enum PluginHints {
    //HINT_TRANSPORT     = 1 << 0, // must match HINT_TRANSPORT set above
    HINT_TRIGGERS        = 1 << 1,
    HINT_OUTPUT_MONITORS = 1 << 2,
    HINT_HAS_MIDI_INPUT  = 1 << 3,
    HINT_HAS_STATE       = 1 << 4,
    HINT_STATE_UNSAFE    = 1 << 5, // state restore needs mutex protection
    HINT_IS_LIVE         = 1 << 6, // needs to be always running, cannot have processing disabled
};

enum TransportSyncMode {
    TRANSPORT_SYNC_NONE,
    TRANSPORT_SYNC_ABLETON_LINK,
    TRANSPORT_SYNC_MIDI,
};

enum {
    URI_MAP_FEATURE,
    URID_MAP_FEATURE,
    URID_UNMAP_FEATURE,
    OPTIONS_FEATURE,
#ifdef __MOD_DEVICES__
    HMI_WC_FEATURE,
#endif
    LICENSE_FEATURE,
    BUF_SIZE_POWER2_FEATURE,
    BUF_SIZE_FIXED_FEATURE,
    BUF_SIZE_BOUNDED_FEATURE,
    LOG_FEATURE,
    STATE_FREE_PATH_FEATURE,
    STATE_MAKE_PATH_FEATURE,
    CTRLPORT_REQUEST_FEATURE,
    WORKER_FEATURE,
#ifdef WITH_EXTERNAL_UI_SUPPORT
    UI_DATA_ACCESS,
    UI_INSTANCE_ACCESS,
#endif
    FEATURE_TERMINATOR
};

enum PostPonedEventType {
    POSTPONED_PARAM_SET,
    POSTPONED_AUDIO_MONITOR,
    POSTPONED_OUTPUT_MONITOR,
    POSTPONED_MIDI_CONTROL_CHANGE,
    POSTPONED_MIDI_PROGRAM_CHANGE,
    POSTPONED_MIDI_MAP,
    POSTPONED_TRANSPORT,
    POSTPONED_JACK_MIDI_CONNECT,
    POSTPONED_LOG_TRACE, // stack allocated, rt-safe
    POSTPONED_LOG_MESSAGE, // heap allocated
    POSTPONED_PROCESS_OUTPUT_BUFFER
};

enum UpdatePositionFlag {
    UPDATE_POSITION_SKIP,
    UPDATE_POSITION_IF_CHANGED,
    UPDATE_POSITION_FORCED,
};


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

#ifdef __MOD_DEVICES__
typedef struct HMI_ADDRESSING_T hmi_addressing_t;
#endif
typedef struct PORT_T port_t;

typedef struct AUDIO_MONITOR_T {
    jack_port_t *port;
    char *source_port_name;
    float value;
} audio_monitor_t;

typedef struct CV_SOURCE_T {
    port_t *port;
    jack_port_t *jack_port;
    float source_min_value;
    float source_max_value;
    float source_diff_value;
    float min_value;
    float max_value;
    float diff_value;
    float prev_value;
} cv_source_t;

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
    cv_source_t* cv_source;
    pthread_mutex_t cv_source_mutex;
#ifdef __MOD_DEVICES__
    hmi_addressing_t* hmi_addressing;
#endif
} port_t;

typedef struct PROPERTY_T {
    LilvNode* uri;
    LilvNode* type;
    bool monitored;
} property_t;

typedef struct PROPERTY_EVENT_T {
    uint32_t size;
    uint8_t  body[];
} property_event_t;

typedef struct PRESET_T {
    LilvNode *uri;
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
    const LV2_Feature **features;

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
    int32_t reset_index;
    int32_t bpb_index;
    int32_t bpm_index;
    int32_t speed_index;

    preset_t **presets;
    uint32_t presets_count;

    monitor_t **monitors;
    uint32_t monitors_count;

    worker_t worker;
    const MOD_License_Interface *license_iface;
    const LV2_Options_Interface *options_interface;
    const LV2_State_Interface *state_iface;
#ifdef __MOD_DEVICES__
    const LV2_HMI_PluginNotification *hmi_notif;
#endif

    jack_ringbuffer_t *events_in_buffer;
    jack_ringbuffer_t *events_out_buffer;
    char *events_in_buffer_helper;

    bool activated;

    // previous transport state
    bool transport_rolling;
    uint32_t transport_frame;
    double transport_bpb;
    double transport_bpm;

    // current and previous bypass state
    port_t bypass_port;
    float bypass;
    bool was_bypassed;

    // cached plugin information, avoids iterating controls each cycle
    enum PluginHints hints;

    // virtual presets port
    port_t presets_port;
    float preset_value;

    // thread-safe state restore
    pthread_mutex_t state_restore_mutex;

    // state save/restore custom directory
    const char* state_dir;

#ifdef WITH_EXTERNAL_UI_SUPPORT
    // UI related objects
    void *ui_libhandle;
    LV2UI_Handle *ui_handle;
    const LV2UI_Descriptor *ui_desc;
    const LV2UI_Idle_Interface *ui_idle_iface;
#endif
} effect_t;

typedef struct LILV_NODES_T {
    LilvNode *atom_port;
    LilvNode *audio;
    LilvNode *control;
    LilvNode *control_in;
    LilvNode *cv;
    LilvNode *default_;
    LilvNode *enabled;
    LilvNode *enumeration;
    LilvNode *event;
    LilvNode *freeWheeling;
    LilvNode *hmi_interface;
    LilvNode *input;
    LilvNode *integer;
    LilvNode *is_live;
    LilvNode *license_interface;
    LilvNode *logarithmic;
    LilvNode *maximum;
    LilvNode *midiEvent;
    LilvNode *minimum;
    LilvNode *minimumSize;
    LilvNode *mod_cvport;
    LilvNode *mod_default;
    LilvNode *mod_default_custom;
    LilvNode *mod_maximum;
    LilvNode *mod_minimum;
    LilvNode *options_interface;
    LilvNode *output;
    LilvNode *patch_readable;
    LilvNode *patch_writable;
    LilvNode *preferMomentaryOff;
    LilvNode *preferMomentaryOn;
    LilvNode *preset;
    LilvNode *rawMIDIClockAccess;
    LilvNode *rdfs_range;
    LilvNode *reset;
    LilvNode *sample_rate;
    LilvNode *state_interface;
    LilvNode *state_load_default_state;
    LilvNode *state_thread_safe_restore;
    LilvNode *timeBeatsPerBar;
    LilvNode *timeBeatsPerMinute;
    LilvNode *timePosition;
    LilvNode *timeSpeed;
    LilvNode *toggled;
    LilvNode *trigger;
    LilvNode *worker_interface;
} lilv_nodes_t;

typedef struct URIDS_T {
    LV2_URID atom_Bool;
    LV2_URID atom_Double;
    LV2_URID atom_Float;
    LV2_URID atom_Int;
    LV2_URID atom_Long;
    LV2_URID atom_Object;
    LV2_URID atom_Path;
    LV2_URID atom_String;
    LV2_URID atom_Tuple;
    LV2_URID atom_URI;
    LV2_URID atom_Vector;
    LV2_URID atom_eventTransfer;
    LV2_URID bufsz_maxBlockLength;
    LV2_URID bufsz_minBlockLength;
    LV2_URID bufsz_nomimalBlockLength;
    LV2_URID bufsz_sequenceSize;
    LV2_URID jack_client;
    LV2_URID log_Error;
    LV2_URID log_Note;
    LV2_URID log_Trace;
    LV2_URID log_Warning;
    LV2_URID midi_MidiEvent;
    LV2_URID param_sampleRate;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_property;
    LV2_URID patch_sequence;
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
    LV2_URID threads_schedPolicy;
    LV2_URID threads_schedPriority;
} urids_t;

typedef struct MIDI_CC_T {
    int8_t channel;
    uint8_t controller;
    float minimum;
    float maximum;
    int effect_id;
    const char* symbol;
    port_t* port;
} midi_cc_t;

typedef struct ASSIGNMENT_T {
    int effect_id;
    port_t *port;
    int device_id;
    int assignment_id;
    int actuator_id;
    int actuator_pair_id;
    int assignment_pair_id;
    bool supports_set_value;
} assignment_t;

typedef struct HMI_ADDRESSING_T {
    int actuator_id;
    uint8_t page;
    uint8_t subpage;
} hmi_addressing_t;

typedef struct POSTPONED_PARAMETER_EVENT_T {
    int effect_id;
    const char* symbol;
    float value;
} postponed_parameter_event_t;

typedef struct POSTPONED_AUDIO_MONITOR_EVENT_T {
    int index;
    float value;
} postponed_audio_monitor_event_t;

typedef struct POSTPONED_MIDI_CONTROL_CHANGE_EVENT_T {
    int8_t channel;
    int8_t control;
    int16_t value;
} postponed_midi_control_change_event_t;

typedef struct POSTPONED_MIDI_PROGRAM_CHANGE_EVENT_T {
    int8_t program;
    int8_t channel;
} postponed_midi_program_change_event_t;

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

typedef struct POSTPONED_JACK_MIDI_CONNECT_EVENT_T {
    jack_port_id_t port;
} postponed_jack_midi_connect_event_t;

typedef struct POSTPONED_LOG_TRACE_EVENT_T {
    char msg[32];
} postponed_log_trace_event_t;

typedef struct POSTPONED_LOG_MESSAGE_EVENT_T {
    LogType type;
    char *msg; // NOTE: heap allocated, needs to be freed on reader side
} postponed_log_message_event_t;

typedef struct POSTPONED_PROCESS_OUTPUT_BUFFER_EVENT_T {
    int effect_id;
} postponed_process_output_buffer_event_t;

typedef struct POSTPONED_EVENT_T {
    enum PostPonedEventType type;
    union {
        postponed_parameter_event_t parameter;
        postponed_audio_monitor_event_t audio_monitor;
        postponed_midi_control_change_event_t control_change;
        postponed_midi_program_change_event_t program_change;
        postponed_midi_map_event_t midi_map;
        postponed_transport_event_t transport;
        postponed_jack_midi_connect_event_t jack_midi_connect;
        postponed_log_trace_event_t log_trace;
        postponed_log_message_event_t log_message;
        postponed_process_output_buffer_event_t process_out_buf;
    };
} postponed_event_t;

typedef struct POSTPONED_EVENT_LIST_DATA {
    postponed_event_t event;
    struct list_head siblings;
} postponed_event_list_data;

typedef struct POSTPONED_CACHED_EFFECT_LIST_DATA {
    int effect_id;
    struct list_head siblings;
} postponed_cached_effect_list_data;

typedef struct POSTPONED_CACHED_EFFECT_EVENTS {
    int last_effect_id;
    postponed_cached_effect_list_data effects;
} postponed_cached_effect_events;

typedef struct POSTPONED_CACHED_SYMBOL_LIST_DATA {
    int effect_id;
    char symbol[MAX_CHAR_BUF_SIZE+1];
    struct list_head siblings;
} postponed_cached_symbol_list_data;

typedef struct POSTPONED_CACHED_SYMBOL_EVENTS {
    int last_effect_id;
    char last_symbol[MAX_CHAR_BUF_SIZE+1];
    postponed_cached_symbol_list_data symbols;
} postponed_cached_symbol_events;

typedef struct RAW_MIDI_PORT_ITEM {
    int instance;
    jack_port_t* jack_port;
    struct list_head siblings;
} raw_midi_port_item;


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

#define UNUSED_PARAM(var)           do { (void)(var); } while (0)
#define INSTANCE_IS_VALID(id)       ((id >= 0) && (id < MAX_INSTANCES))

/* used to indicate if a parameter change was initiated from us */
#define MAGIC_PARAMETER_SEQ_NUMBER -1337

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

/* raw-midi-clock-access ports */
static struct list_head g_raw_midi_port_list;
static pthread_mutex_t  g_raw_midi_port_mutex;

/* Jack */
static jack_client_t *g_jack_global_client;
static jack_nframes_t g_sample_rate, g_max_allowed_midi_delta;
static float g_sample_rate_f;
static const char **g_capture_ports, **g_playback_ports;
static int32_t g_midi_buffer_size, g_block_length;
static int32_t g_thread_policy, g_thread_priority;
#ifdef MOD_IO_PROCESSING_ENABLED
static jack_port_t *g_audio_in1_port;
static jack_port_t *g_audio_in2_port;
static jack_port_t *g_audio_out1_port;
static jack_port_t *g_audio_out2_port;
#endif
static audio_monitor_t *g_audio_monitors;
static pthread_mutex_t g_audio_monitor_mutex;
static int g_audio_monitor_count;
static jack_port_t *g_midi_in_port;
static jack_position_t g_jack_pos;
static bool g_jack_rolling;
static uint32_t g_jack_xruns;
static volatile double g_transport_bpb;
static volatile double g_transport_bpm;
static volatile bool g_transport_reset;
static volatile enum TransportSyncMode g_transport_sync_mode;
static bool g_aggregated_midi_enabled;
static bool g_processing_enabled;
static bool g_verbose_debug;
static bool g_cpu_load_enabled;
static volatile bool g_cpu_load_trigger;

// Wall clock time since program startup
static uint64_t g_monotonic_frame_count = 0;

// Used for the MIDI Beat Clock Slave
static volatile uint64_t g_previous_midi_event_time = 0;

/* LV2 and Lilv */
static LilvWorld *g_lv2_data;
static const LilvPlugins *g_plugins;
static char *g_lv2_scratch_dir;

/* Global features */
static Symap* g_symap;
static lilv_nodes_t g_lilv_nodes;
static urids_t g_urids;
#ifdef __MOD_DEVICES__
static LV2_HMI_WidgetControl g_hmi_wc;
#endif
static MOD_License_Feature g_license;
static LV2_Atom_Forge g_lv2_atom_forge;
static LV2_Log_Log g_lv2_log;
static LV2_Options_Option g_options[9];
static LV2_State_Free_Path g_state_freePath;
static LV2_URID_Map g_urid_map;
static LV2_URID_Unmap g_urid_unmap;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static LV2_URI_Map_Feature g_uri_map;
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#pragma GCC diagnostic pop
#endif

static LV2_Feature g_buf_size_features[3] = {
    { LV2_BUF_SIZE__powerOf2BlockLength, NULL },
    { LV2_BUF_SIZE__fixedBlockLength, NULL },
    { LV2_BUF_SIZE__boundedBlockLength, NULL }
};
#ifdef __MOD_DEVICES__
static LV2_Feature g_hmi_wc_feature = { LV2_HMI__WidgetControl, &g_hmi_wc };
#endif
static LV2_Feature g_license_feature = { MOD_LICENSE__feature, &g_license };
static LV2_Feature g_lv2_log_feature = { LV2_LOG__log, &g_lv2_log };
static LV2_Feature g_options_feature = { LV2_OPTIONS__options, &g_options };
static LV2_Feature g_state_freePath_feature = { LV2_STATE__freePath, &g_state_freePath };
static LV2_Feature g_uri_map_feature = { LV2_URI_MAP_URI, &g_uri_map };
static LV2_Feature g_urid_map_feature = { LV2_URID__map, &g_urid_map };
static LV2_Feature g_urid_unmap_feature = { LV2_URID__unmap, &g_urid_unmap };

/* MIDI Learn */
static pthread_mutex_t g_midi_learning_mutex;

/* MIDI control and program monitoring */
static bool g_monitored_midi_controls[16];
static bool g_monitored_midi_programs[16];

#ifdef HAVE_HYLIA
static hylia_t* g_hylia_instance;
static hylia_time_info_t g_hylia_timeinfo;
#endif

#ifdef __MOD_DEVICES__
/* HMI integration */
static int g_hmi_shmfd;
static sys_serial_shm_data* g_hmi_data;
static pthread_t g_hmi_client_thread;
static pthread_mutex_t g_hmi_mutex;
static hmi_addressing_t g_hmi_addressings[MAX_HMI_ADDRESSINGS];

/* internal processing */
static int g_compressor_mode = 0;
static int g_compressor_release = 100;
#ifdef MOD_IO_PROCESSING_ENABLED
static int g_noisegate_channel = 0;
static int g_noisegate_decay = 10;
static int g_noisegate_threshold = -60;
static gate_t g_noisegate;
#endif
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
static void AllocatePortBuffers(effect_t* effect, int in_size, int out_size);
static int BufferSize(jack_nframes_t nframes, void* data);
static void FreeWheelMode(int starting, void* data);
static void PortRegistration(jack_port_id_t port_id, int reg, void* data);
static int XRun(void* data);
static void RunPostPonedEvents(int ignored_effect_id);
static void* PostPonedEventsThread(void* arg);
#ifdef __MOD_DEVICES__
static void* HMIClientThread(void* arg);
#endif
static int ProcessPlugin(jack_nframes_t nframes, void *arg);
static bool SetPortValue(port_t *port, float value, int effect_id, bool is_bypass, bool from_ui);
static float UpdateValueFromMidi(midi_cc_t* mcc, uint16_t mvalue, bool highres);
static bool UpdateGlobalJackPosition(enum UpdatePositionFlag flag, bool do_post);
static int ProcessGlobalClient(jack_nframes_t nframes, void *arg);
static void JackTimebase(jack_transport_state_t state, jack_nframes_t nframes,
                         jack_position_t* pos, int new_pos, void* arg);
static void JackThreadInit(void *arg);
static void GetFeatures(effect_t *effect);
static void TriggerJackTimebase(bool reset_to_zero);

static property_t *FindEffectPropertyByURI(effect_t *effect, const char *uri);
static port_t *FindEffectInputPortBySymbol(effect_t *effect, const char *control_symbol);
static port_t *FindEffectOutputPortBySymbol(effect_t *effect, const char *control_symbol);
static const void *GetPortValueForState(const char* symbol, void* user_data, uint32_t* size, uint32_t* type);
static int LoadPresets(effect_t *effect);
static void FreeFeatures(effect_t *effect);
static void FreePluginString(void* handle, char *str);
static void ConnectToAllHardwareMIDIPorts(void);
static void ConnectToMIDIThroughPorts(void);
#ifdef __MOD_DEVICES__
static void HMIWidgetsSetLedWithBlink(LV2_HMI_WidgetControl_Handle handle,
                                      LV2_HMI_Addressing addressing,
                                      LV2_HMI_LED_Colour led_color,
                                      int on_blink_time,
                                      int off_blink_time);
static void HMIWidgetsSetLedWithBrightness(LV2_HMI_WidgetControl_Handle handle,
                                           LV2_HMI_Addressing addressing,
                                           LV2_HMI_LED_Colour led_color,
                                           int brightness);
static void HMIWidgetsSetLabel(LV2_HMI_WidgetControl_Handle handle,
                               LV2_HMI_Addressing addressing,
                               const char* label);
static void HMIWidgetsSetValue(LV2_HMI_WidgetControl_Handle handle,
                               LV2_HMI_Addressing addressing,
                               const char* value);
static void HMIWidgetsSetUnit(LV2_HMI_WidgetControl_Handle handle,
                              LV2_HMI_Addressing addressing,
                              const char* unit);
static void HMIWidgetsSetIndicator(LV2_HMI_WidgetControl_Handle handle,
                                   LV2_HMI_Addressing addressing,
                                   float indicator_poss);
static void HMIWidgetsPopupMessage(LV2_HMI_WidgetControl_Handle handle,
                                   LV2_HMI_Addressing addressing,
                                   LV2_HMI_Popup_Style style,
                                   const char* title,
                                   const char* text);
#endif
static char *GetLicenseFile(MOD_License_Handle handle, const char *license_uri);
static int LogPrintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...);
static int LogVPrintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list ap);
static LV2_ControlInputPort_Change_Status RequestControlPortChange(LV2_ControlInputPort_Change_Request_Handle handle,
                                                                   uint32_t index, float value);
static char* MakePluginStatePathFromSratchDir(LV2_State_Make_Path_Handle handle, const char *path);
static char* MakePluginStatePathDuringLoadSave(LV2_State_Make_Path_Handle handle, const char *path);
#ifdef HAVE_CONTROLCHAIN
static void CCDataUpdate(void* arg);
static void InitializeControlChainIfNeeded(void);
static bool CheckCCDeviceProtocolVersion(int device_id, int major, int minor);
#endif
#ifdef HAVE_HYLIA
static uint32_t GetHyliaOutputLatency(void);
#endif
#ifdef WITH_EXTERNAL_UI_SUPPORT
static void ExternalControllerWriteFunction(LV2UI_Controller controller,
                                            uint32_t port_index,
                                            uint32_t buffer_size,
                                            uint32_t port_protocol,
                                            const void *buffer);
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


#if defined(__APPLE__) || defined(_WIN32)
// FIXME missing on macOS and Windows
static char* _strchrnul(const char *s, int c)
{
    char *r = strchr(s, c);

    if (r != NULL)
        *r = '\0';

    return r;
}
#define strchrnul _strchrnul
#endif

#ifdef _WIN32
// missing on Windows
static char* realpath(const char *name, char *resolved)
{
    if (name == NULL)
        return NULL;

    if (_access(name, 4) != 0)
        return NULL;

    char *retname = NULL;

    if ((retname = resolved) == NULL)
        retname = malloc(PATH_MAX + 2);

    if (retname == NULL)
        return NULL;

    return _fullpath(retname, name, PATH_MAX);
}
#define realpath _realpath
#endif

static void InstanceDelete(int effect_id)
{
    if (INSTANCE_IS_VALID(effect_id))
        memset(&g_effects[effect_id], 0, sizeof(effect_t));
}

static int InstanceExist(int effect_id)
{
    if (INSTANCE_IS_VALID(effect_id))
    {
        return (int)(g_effects[effect_id].jack_client != NULL);
    }

    return 0;
}

static void AllocatePortBuffers(effect_t* effect, int in_size, int out_size)
{
    uint32_t i;

    for (i = 0; i < effect->event_ports_count; i++)
    {
        const int size = effect->event_ports[i]->flow == FLOW_INPUT ? in_size : out_size;
        if (size == 0)
            continue;
        lv2_evbuf_free(effect->event_ports[i]->evbuf);
        effect->event_ports[i]->evbuf = lv2_evbuf_new(
            size,
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

        // if ringbuffers exist, keep their existing size
        const int in_size = effect->events_in_buffer ? 0 : (g_midi_buffer_size * 16);
        const int out_size = effect->events_out_buffer ? 0 : (g_midi_buffer_size * 16);
        AllocatePortBuffers(effect, in_size, out_size);

        // notify plugin of the change
        if (effect->options_interface != NULL) {
            LV2_Options_Option options[5];

            options[0].context = LV2_OPTIONS_INSTANCE;
            options[0].subject = 0;
            options[0].key = g_urids.bufsz_nomimalBlockLength;
            options[0].size = sizeof(int32_t);
            options[0].type = g_urids.atom_Int;
            options[0].value = &g_block_length;

            options[1].context = LV2_OPTIONS_INSTANCE;
            options[1].subject = 0;
            options[1].key = g_urids.bufsz_minBlockLength;
            options[1].size = sizeof(int32_t);
            options[1].type = g_urids.atom_Int;
            options[1].value = &g_block_length;

            options[2].context = LV2_OPTIONS_INSTANCE;
            options[2].subject = 0;
            options[2].key = g_urids.bufsz_maxBlockLength;
            options[2].size = sizeof(int32_t);
            options[2].type = g_urids.atom_Int;
            options[2].value = &g_block_length;

            options[3].context = LV2_OPTIONS_INSTANCE;
            options[3].subject = 0;
            options[3].key = g_urids.bufsz_sequenceSize;
            options[3].size = sizeof(int32_t);
            options[3].type = g_urids.atom_Int;
            options[3].value = &g_midi_buffer_size;

            options[4].context = LV2_OPTIONS_INSTANCE;
            options[4].subject = 0;
            options[4].key = 0;
            options[4].size = 0;
            options[4].type = 0;
            options[4].value = NULL;

            if (effect->activated)
                lilv_instance_deactivate(effect->lilv_instance);

            effect->options_interface->set(effect->lilv_instance->lv2_handle, options);

            if (effect->activated)
                lilv_instance_activate(effect->lilv_instance);
        }
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

static void PortRegistration(jack_port_id_t port_id, int reg, void* data)
{
    if (g_aggregated_midi_enabled || reg == 0)
        return;

    /* port flags to connect to */
    static const int target_port_flags = JackPortIsTerminal|JackPortIsPhysical|JackPortIsOutput;

    const jack_port_t* const port = jack_port_by_id(g_jack_global_client, port_id);

    if ((jack_port_flags(port) & target_port_flags) != target_port_flags)
        return;
    if (strcmp(jack_port_type(port), JACK_DEFAULT_MIDI_TYPE) != 0)
        return;

    postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

    if (posteventptr == NULL)
        return;

    posteventptr->event.type = POSTPONED_JACK_MIDI_CONNECT;
    posteventptr->event.jack_midi_connect.port = port_id;

    pthread_mutex_lock(&g_rtsafe_mutex);
    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    sem_post(&g_postevents_semaphore);
    return;

    UNUSED_PARAM(data);
}

static int XRun(void *data)
{
    ++g_jack_xruns;

    return 0;

    UNUSED_PARAM(data);
}

static bool ShouldIgnorePostPonedEffectEvent(int effect_id, postponed_cached_effect_events* cached_events)
{
    if (effect_id == cached_events->last_effect_id)
    {
        // already received this event, like just now
        return true;
    }

    // we received some events, but last one was different
    // we might be getting interleaved events, so let's check if it's there
    struct list_head *it;
    list_for_each(it, &cached_events->effects.siblings)
    {
        postponed_cached_effect_list_data* const peffect = list_entry(it, postponed_cached_effect_list_data, siblings);

        if (effect_id == peffect->effect_id)
        {
            // haha! found you little bastard!
            return true;
        }
    }

    // we'll process this event because it's the "last" of its type
    // also add it to the list of received events
    postponed_cached_effect_list_data* const peffect = malloc(sizeof(postponed_cached_effect_list_data));

    if (peffect)
    {
        peffect->effect_id = effect_id;
        list_add_tail(&peffect->siblings, &cached_events->effects.siblings);
    }

    return false;
}

static bool ShouldIgnorePostPonedSymbolEvent(postponed_parameter_event_t* ev,
                                             postponed_cached_symbol_events* cached_events)
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

static int socket_send_feedback_debug(const char *buffer)
{
    if (g_verbose_debug) {
        printf("DEBUG: RunPostPonedEvents() Sending '%s'\n", buffer);
        fflush(stdout);
    }
    return socket_send_feedback(buffer);
}

static void RunPostPonedEvents(int ignored_effect_id)
{
    if (g_verbose_debug) {
        puts("DEBUG: RunPostPonedEvents() START");
        fflush(stdout);
    }

    // local queue to where we'll save rtsafe list
    struct list_head queue;
    INIT_LIST_HEAD(&queue);

    // move rtsafe list to our local queue, and clear it
    pthread_mutex_lock(&g_rtsafe_mutex);
    list_splice_init(&g_rtsafe_list, &queue);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    // fetch this value only once per run
    const bool cpu_load_trigger = g_jack_global_client != NULL && g_cpu_load_trigger;

    if (cpu_load_trigger)
    {
        g_cpu_load_trigger = false;
    }
    else if (list_empty(&queue))
    {
        // nothing to do
        if (g_verbose_debug) {
            puts("DEBUG: RunPostPonedEvents() Queue is empty");
            fflush(stdout);
        }
        return;
    }

    // local buffer
    #define FEEDBACK_BUF_SIZE (MAX_CHAR_BUF_SIZE * 2)
    char buf[FEEDBACK_BUF_SIZE+1];
    buf[FEEDBACK_BUF_SIZE] = '\0';

    // cached data, to make sure we only handle similar events once
    bool got_midi_program = false;
    bool got_transport = false;
    postponed_cached_effect_events cached_audio_monitor, cached_process_out_buf;
    postponed_cached_symbol_events cached_param_set, cached_output_mon;

    cached_audio_monitor.last_effect_id = -1;
    cached_process_out_buf.last_effect_id = -1;
    cached_param_set.last_effect_id = -1;
    cached_param_set.last_symbol[0] = '\0';
    cached_param_set.last_symbol[MAX_CHAR_BUF_SIZE] = '\0';
    cached_output_mon.last_effect_id = -1;
    cached_output_mon.last_symbol[0] = '\0';
    cached_output_mon.last_symbol[MAX_CHAR_BUF_SIZE] = '\0';
    INIT_LIST_HEAD(&cached_audio_monitor.effects.siblings);
    INIT_LIST_HEAD(&cached_process_out_buf.effects.siblings);
    INIT_LIST_HEAD(&cached_param_set.symbols.siblings);
    INIT_LIST_HEAD(&cached_output_mon.symbols.siblings);

    // if all we have are jack_midi_connect requests, do not send feedback to server
    bool got_only_jack_midi_requests = !cpu_load_trigger;

    if (g_verbose_debug) {
        puts("DEBUG: RunPostPonedEvents() Before the queue iteration");
        fflush(stdout);
    }

    // itenerate backwards
    struct list_head *it, *it2;
    postponed_event_list_data* eventptr;

    list_for_each_prev(it, &queue)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);

#ifdef DEBUG
        if (g_verbose_debug) {
            printf("DEBUG: RunPostPonedEvents() ptr %p type %i\n", eventptr, eventptr->event.type);
            fflush(stdout);
        }
#endif

        if (got_only_jack_midi_requests && eventptr->event.type != POSTPONED_JACK_MIDI_CONNECT)
            got_only_jack_midi_requests = false;

        switch (eventptr->event.type)
        {
        case POSTPONED_PARAM_SET:
            if (eventptr->event.parameter.effect_id == ignored_effect_id)
                continue;
            if (ShouldIgnorePostPonedSymbolEvent(&eventptr->event.parameter, &cached_param_set))
                continue;

            snprintf(buf, FEEDBACK_BUF_SIZE, "param_set %i %s %f", eventptr->event.parameter.effect_id,
                                                                   eventptr->event.parameter.symbol,
                                                                   eventptr->event.parameter.value);
            socket_send_feedback_debug(buf);

            // save for fast checkup next time
            cached_param_set.last_effect_id = eventptr->event.parameter.effect_id;
            strncpy(cached_param_set.last_symbol, eventptr->event.parameter.symbol, MAX_CHAR_BUF_SIZE);
            break;

        case POSTPONED_AUDIO_MONITOR:
            if (ShouldIgnorePostPonedEffectEvent(eventptr->event.audio_monitor.index, &cached_audio_monitor))
                continue;

            pthread_mutex_lock(&g_audio_monitor_mutex);
            if (eventptr->event.audio_monitor.index < g_audio_monitor_count)
                g_audio_monitors[eventptr->event.audio_monitor.index].value = 0.f;
            pthread_mutex_unlock(&g_audio_monitor_mutex);

            snprintf(buf, FEEDBACK_BUF_SIZE, "audio_monitor %i %f", eventptr->event.audio_monitor.index,
                                                                    eventptr->event.audio_monitor.value);
            socket_send_feedback_debug(buf);

            // save for fast checkup next time
            cached_audio_monitor.last_effect_id = eventptr->event.audio_monitor.index;
            break;

        case POSTPONED_OUTPUT_MONITOR:
            if (eventptr->event.parameter.effect_id == ignored_effect_id)
                continue;
            if (ShouldIgnorePostPonedSymbolEvent(&eventptr->event.parameter, &cached_output_mon))
                continue;

            snprintf(buf, FEEDBACK_BUF_SIZE, "output_set %i %s %f", eventptr->event.parameter.effect_id,
                                                                    eventptr->event.parameter.symbol,
                                                                    eventptr->event.parameter.value);
            socket_send_feedback_debug(buf);

            // save for fast checkup next time
            cached_output_mon.last_effect_id = eventptr->event.parameter.effect_id;
            strncpy(cached_output_mon.last_symbol, eventptr->event.parameter.symbol, MAX_CHAR_BUF_SIZE);
            break;

        case POSTPONED_MIDI_MAP:
            if (eventptr->event.midi_map.effect_id == ignored_effect_id)
                continue;
            snprintf(buf, FEEDBACK_BUF_SIZE, "midi_mapped %i %s %i %i %f %f %f", eventptr->event.midi_map.effect_id,
                                                                                 eventptr->event.midi_map.symbol,
                                                                                 eventptr->event.midi_map.channel,
                                                                                 eventptr->event.midi_map.controller,
                                                                                 eventptr->event.midi_map.value,
                                                                                 eventptr->event.midi_map.minimum,
                                                                                 eventptr->event.midi_map.maximum);
            socket_send_feedback_debug(buf);
            break;

        case POSTPONED_MIDI_CONTROL_CHANGE:
            snprintf(buf, FEEDBACK_BUF_SIZE, "midi_control_change %i %i %i",
                     eventptr->event.control_change.channel,
                     eventptr->event.control_change.control,
                     eventptr->event.control_change.value);
            socket_send_feedback_debug(buf);
            break;

        case POSTPONED_MIDI_PROGRAM_CHANGE:
            if (got_midi_program)
            {
                if (g_verbose_debug) {
                    puts("DEBUG: RunPostPonedEvents() Ignoring old midi program change event");
                    fflush(stdout);
                }
                continue;
            }

            snprintf(buf, FEEDBACK_BUF_SIZE, "midi_program_change %i %i",
                     eventptr->event.program_change.program,
                     eventptr->event.program_change.channel);
            socket_send_feedback_debug(buf);

            // ignore older midi program changes
            got_midi_program = true;
            break;

        case POSTPONED_TRANSPORT:
            if (got_transport)
                continue;

            snprintf(buf, FEEDBACK_BUF_SIZE, "transport %i %f %f", eventptr->event.transport.rolling ? 1 : 0,
                                                                   eventptr->event.transport.bpb,
                                                                   eventptr->event.transport.bpm);
            socket_send_feedback_debug(buf);

            // ignore older transport changes
            got_transport = true;
            break;

        case POSTPONED_JACK_MIDI_CONNECT:
            if (g_jack_global_client != NULL) {
                const jack_port_id_t port_id = eventptr->event.jack_midi_connect.port;
                const jack_port_t *const port = jack_port_by_id(g_jack_global_client, port_id);

                if (port != NULL) {
                    if (g_midi_in_port != NULL)
                        jack_connect(g_jack_global_client, jack_port_name(port), jack_port_name(g_midi_in_port));

                    struct list_head *it3;
                    pthread_mutex_lock(&g_raw_midi_port_mutex);
                    list_for_each(it3, &g_raw_midi_port_list)
                    {
                        raw_midi_port_item* const portitemptr = list_entry(it3, raw_midi_port_item, siblings);

                        jack_connect(g_jack_global_client,
                                      jack_port_name(port),
                                      jack_port_name(portitemptr->jack_port));
                    }
                    pthread_mutex_unlock(&g_raw_midi_port_mutex);
                }
            }
            break;

        case POSTPONED_PROCESS_OUTPUT_BUFFER:
            if (eventptr->event.process_out_buf.effect_id == ignored_effect_id)
                continue;
            if (ShouldIgnorePostPonedEffectEvent(eventptr->event.process_out_buf.effect_id, &cached_process_out_buf))
                continue;

            if (INSTANCE_IS_VALID(eventptr->event.process_out_buf.effect_id))
            {
                effect_t *effect = &g_effects[eventptr->event.process_out_buf.effect_id];
                const size_t space = jack_ringbuffer_read_space(effect->events_out_buffer);

                uint32_t key;
                LV2_Atom atom;
                bool supported;

                for (size_t j = 0; j < space; j += sizeof(uint32_t) + sizeof(LV2_Atom) + atom.size)
                {
                    jack_ringbuffer_read(effect->events_out_buffer, (char*)&key, sizeof(uint32_t));
                    jack_ringbuffer_read(effect->events_out_buffer, (char*)&atom, sizeof(LV2_Atom));

                    char *body = mod_calloc(1, atom.size);
                    jack_ringbuffer_read(effect->events_out_buffer, body, atom.size);

                    supported = true;
                    int wrtn = snprintf(buf, FEEDBACK_BUF_SIZE, "patch_set %i %s ", effect->instance,
                                                                                    id_to_urid(g_symap, key));
                    if (atom.type == g_urids.atom_Bool)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "b %i", *(int32_t*)body != 0 ? 1 : 0);
                    }
                    else if (atom.type == g_urids.atom_Int)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "i %i", *(int32_t*)body);
                    }
                    else if (atom.type == g_urids.atom_Long)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "l %" PRId64, *(int64_t*)body);
                    }
                    else if (atom.type == g_urids.atom_Float)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "f %f", *(float*)body);
                    }
                    else if (atom.type == g_urids.atom_Double)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "g %f", *(double*)body);
                    }
                    else if (atom.type == g_urids.atom_String)
                    {
                        if (atom.size + 2 > FEEDBACK_BUF_SIZE - (uint)wrtn)
                        {
                            char* rbuf = malloc(wrtn + atom.size + 3);
                            strcpy(rbuf, buf);
                            strcpy(rbuf + wrtn, "s ");
                            strncpy(rbuf + (wrtn + 2), body, atom.size);
                            rbuf[wrtn + atom.size + 2] = '\0';

                            supported = false;
                            socket_send_feedback_debug(rbuf);
                            free(rbuf);
                        }
                        else
                        {
                            snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "s %s", body);
                        }
                    }
                    else if (atom.type == g_urids.atom_Path)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "p %s", body);
                    }
                    else if (atom.type == g_urids.atom_URI)
                    {
                        snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "u %s", body);
                    }
                    else if (atom.type == g_urids.atom_Vector)
                    {
                        LV2_Atom_Vector_Body *vbody = (LV2_Atom_Vector_Body*)body;
                        uint32_t n_elems = (atom.size - sizeof(LV2_Atom_Vector_Body)) / vbody->child_size;

                        wrtn += snprintf(buf + wrtn, FEEDBACK_BUF_SIZE - wrtn, "v %u-", n_elems);

                        char* rbuf;
                        if (n_elems <= 8)
                        {
                            rbuf = buf;
                        }
                        else
                        {
                            rbuf = malloc(wrtn + (n_elems * 16 + 1) + 3);
                            strcpy(rbuf, buf);
                        }

                        /**/ if (vbody->child_type == g_urids.atom_Bool)
                        {
                            rbuf[wrtn++] = 'b';
                            rbuf[wrtn++] = '-';
                            int32_t *vcontent = (int32_t*)(vbody + 1);
                            for (uint32_t v = 0; v < n_elems; ++v)
                                wrtn += snprintf(rbuf + wrtn, 16, "%i:", *(vcontent + v) != 0 ? 1 : 0);
                        }
                        else if (vbody->child_type == g_urids.atom_Int)
                        {
                            rbuf[wrtn++] = 'i';
                            rbuf[wrtn++] = '-';
                            int32_t *vcontent = (int32_t*)(vbody + 1);
                            for (uint32_t v = 0; v < n_elems; ++v)
                                wrtn += snprintf(rbuf + wrtn, 16, "%i:", *(vcontent + v));
                        }
                        else if (vbody->child_type == g_urids.atom_Long)
                        {
                            rbuf[wrtn++] = 'l';
                            rbuf[wrtn++] = '-';
                            int64_t *vcontent = (int64_t*)(vbody + 1);
                            for (uint32_t v = 0; v < n_elems; ++v)
                                wrtn += snprintf(rbuf + wrtn, 16, "%" PRIi64 ":", *(vcontent + v));
                        }
                        else if (vbody->child_type == g_urids.atom_Float)
                        {
                            rbuf[wrtn++] = 'f';
                            rbuf[wrtn++] = '-';
                            float *vcontent = (float*)(vbody + 1);
                            for (uint32_t v = 0; v < n_elems; ++v)
                                wrtn += snprintf(rbuf + wrtn, 16, "%f:", *(vcontent + v));
                        }
                        else if (vbody->child_type == g_urids.atom_Double)
                        {
                            rbuf[wrtn++] = 'g';
                            rbuf[wrtn++] = '-';
                            double *vcontent = (double*)(vbody + 1);
                            for (uint32_t v = 0; v < n_elems; ++v)
                                wrtn += snprintf(rbuf + wrtn, 16, "%f:", *(vcontent + v));
                        }
                        else
                        {
                            supported = false;
                        }

                        rbuf[wrtn-1] = '\0';

                        if (rbuf != buf)
                        {
                            supported = false;
                            socket_send_feedback_debug(rbuf);
                            free(rbuf);
                        }
                    }
                    /*
                    else if (atom.type == g_urids.atom_Tuple)
                    {
                        // LV2_Atom_Tuple *tuple = (LV2_Atom_Tuple*)body;
                        // TODO
                    }
                    */
                    else
                    {
                        supported = false;
                    }

                    if (supported)
                        socket_send_feedback_debug(buf);

                    free(body);
                }
            }

            // save for fast checkup next time
            cached_process_out_buf.last_effect_id = eventptr->event.process_out_buf.effect_id;
            break;

        // these are handled later, during "cleanup"
        case POSTPONED_LOG_TRACE:
        case POSTPONED_LOG_MESSAGE:
            break;
        }
    }

    if (cpu_load_trigger)
    {
        snprintf(buf, FEEDBACK_BUF_SIZE, "cpu_load %f %f %d", jack_cpu_load(g_jack_global_client),
                                                             #ifdef HAVE_JACK2_1_9_23
                                                              jack_max_cpu_load(g_jack_global_client),
                                                             #else
                                                              0.f,
                                                             #endif
                                                              g_jack_xruns);
        socket_send_feedback_debug(buf);
    }

    if (g_verbose_debug) {
        puts("DEBUG: RunPostPonedEvents() After the queue iteration");
        fflush(stdout);
    }

    // cleanup memory
    postponed_cached_effect_list_data *peffect;
    postponed_cached_symbol_list_data *psymbol;

    list_for_each_safe(it, it2, &cached_audio_monitor.effects.siblings)
    {
        peffect = list_entry(it, postponed_cached_effect_list_data, siblings);
        free(peffect);
    }
    list_for_each_safe(it, it2, &cached_process_out_buf.effects.siblings)
    {
        peffect = list_entry(it, postponed_cached_effect_list_data, siblings);
        free(peffect);
    }
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
    // also take the chance to handle non-critical events that need to be in order
    list_for_each_safe(it, it2, &queue)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);

        switch (eventptr->event.type)
        {
        case POSTPONED_LOG_TRACE:
            snprintf(buf, FEEDBACK_BUF_SIZE, "log %d %s", LOG_TRACE, eventptr->event.log_trace.msg);
            socket_send_feedback_debug(buf);
            break;

        case POSTPONED_LOG_MESSAGE: {
            char *msg = eventptr->event.log_message.msg;
            const int prefix_len = snprintf(buf, FEEDBACK_BUF_SIZE, "log %d ", eventptr->event.log_message.type);
            const size_t msg_len = strlen(msg);

            if (prefix_len > 0 && prefix_len < 8) {
                char* msg2 = malloc(prefix_len + msg_len + 1);
                if (msg2 != NULL) {
                    memcpy(msg2, buf, prefix_len);
                    memcpy(msg2+prefix_len, msg, msg_len + 1);
                    socket_send_feedback_debug(msg2);
                    free(msg2);
                }
            }
            free(msg);
            break;
        }

        default:
            break;
        }

        rtsafe_memory_pool_deallocate(g_rtsafe_mem_pool, eventptr);
    }

    if (g_postevents_ready && !got_only_jack_midi_requests)
    {
        // report data finished to server
        g_postevents_ready = false;
        socket_send_feedback_debug("data_finish");
    }

    if (g_verbose_debug) {
        puts("DEBUG: RunPostPonedEvents() END");
        fflush(stdout);
    }
}

static void* PostPonedEventsThread(void* arg)
{
    while (g_postevents_running == 1)
    {
        if (sem_timedwait_secs(&g_postevents_semaphore, 1) != 0)
            continue;

        if (g_postevents_running == 1 && g_postevents_ready)
        {
            RunPostPonedEvents(-3); // as all effects are valid we set ignored_effect_id to -3
            if (g_verbose_debug) {
                printf("DEBUG: PostPonedEventsThread() looping (code %d)\n", g_postevents_running);
                fflush(stdout);
            }
        }
    }

    if (g_verbose_debug) {
        printf("DEBUG: PostPonedEventsThread() stopping (code %d)\n", g_postevents_running);
        fflush(stdout);
    }

    return NULL;

    UNUSED_PARAM(arg);
}

#ifdef __MOD_DEVICES__
static void* HMIClientThread(void* arg)
{
    sys_serial_shm_data_channel* const data = (sys_serial_shm_data_channel*)arg;

    sys_serial_event_type etype;
    char msg[SYS_SERIAL_SHM_DATA_SIZE];

    while (g_hmi_data != NULL && &g_hmi_data->client == data)
    {
        if (sem_timedwait_secs(&data->sem, 1) != 0)
            continue;

        if (g_hmi_data == NULL || &g_hmi_data->client != data)
            break;

        while (data->head != data->tail)
        {
            if (! sys_serial_read(data, &etype, msg))
                continue;

            if (g_verbose_debug) {
                printf("DEBUG: HMIClientThread received event %x (%c)\n", etype, etype-0x80);
                fflush(stdout);
            }

            switch (etype)
            {
            case sys_serial_event_type_compressor_mode:
                g_compressor_mode = clamp(msg[0] - '0', 0, 4);
                monitor_client_setup_compressor(g_compressor_mode, g_compressor_release);
                break;
            case sys_serial_event_type_compressor_release:
                g_compressor_release = clampf(atof(msg), 50.0f, 500.0f);
                monitor_client_setup_compressor(g_compressor_mode, g_compressor_release);
                break;
#ifdef MOD_IO_PROCESSING_ENABLED
            case sys_serial_event_type_noisegate_channel:
                g_noisegate_channel = clamp(msg[0] - '0', 0, 3);
                break;
            case sys_serial_event_type_noisegate_decay:
                g_noisegate_decay = clampf(atof(msg), 1.0f, 500.0f);
                gate_update(&g_noisegate, g_sample_rate, 10, 1,
                            g_noisegate_decay, 1,
                            g_noisegate_threshold, g_noisegate_threshold - 20);
                break;
            case sys_serial_event_type_noisegate_threshold:
                g_noisegate_threshold = clampf(atof(msg), -70.0f, -10.0f);
                gate_update(&g_noisegate, g_sample_rate, 10, 1,
                            g_noisegate_decay, 1,
                            g_noisegate_threshold, g_noisegate_threshold - 20);
                break;
#endif
            case sys_serial_event_type_pedalboard_gain:
                monitor_client_setup_volume(clampf(atof(msg), MOD_MONITOR_VOLUME_MUTE, 20.0f));
                break;
            default:
                break;
            }
        }
    }

    return NULL;
}
#endif

static int ProcessPlugin(jack_nframes_t nframes, void *arg)
{
    effect_t *effect;
    port_t *port;
    unsigned int i;

    if (arg == NULL) return 0;
    effect = arg;

    if ((effect->hints & HINT_IS_LIVE) == 0 &&
        (!g_processing_enabled || (
         (effect->hints & HINT_STATE_UNSAFE) && pthread_mutex_trylock(&effect->state_restore_mutex) != 0)))
    {
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

    /* common variables */
    bool needs_post = false;
    const float *buffer_in;
    float *buffer_out;
    float value;

    /* transport */
    uint8_t stack_buf[MAX_CHAR_BUF_SIZE+1];
    memset(stack_buf, 0, sizeof(stack_buf));
    LV2_Atom* lv2_pos = (LV2_Atom*)stack_buf;

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
            doubles_differ_enough(effect->transport_bpb, pos.beats_per_bar) ||
            doubles_differ_enough(effect->transport_bpm, pos.beats_per_minute) ||
            (effect->bypass < 0.5f && effect->was_bypassed))
        {
            effect->transport_rolling = rolling;
            effect->transport_frame = pos.frame;
            effect->transport_bpb = pos.beats_per_bar;
            effect->transport_bpm = pos.beats_per_minute;

            LV2_Atom_Forge forge = g_lv2_atom_forge;
            lv2_atom_forge_set_buffer(&forge, stack_buf, sizeof(stack_buf));

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

                if (pos.valid & JackTickDouble)
                    lv2_atom_forge_float(&forge, pos.beat - 1 + (pos.tick_double / pos.ticks_per_beat));
                else
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
        port = effect->input_event_ports[i];
        lv2_evbuf_reset(port->evbuf, true);

        if (effect->bypass > 0.5f && effect->enabled_index < 0)
        {
            // effect is now bypassed, but wasn't before
            if (!effect->was_bypassed && (port->hints & HINT_MIDI_EVENT) != 0)
            {
                LV2_Evbuf_Iterator iter = lv2_evbuf_begin(port->evbuf);

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
            LV2_Evbuf_Iterator iter = lv2_evbuf_begin(port->evbuf);

            /* Write time position */
            if (lv2_pos->size > 0 && (port->hints & HINT_TRANSPORT) != 0)
            {
                lv2_evbuf_write(&iter, 0, 0, lv2_pos->type, lv2_pos->size, LV2_ATOM_BODY_CONST(lv2_pos));
            }

            /* Write Jack MIDI input */
            void* buf = jack_port_get_buffer(port->jack_port, nframes);
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
        }
    }

    for (i = 0; i < effect->output_event_ports_count; i++)
        lv2_evbuf_reset(effect->output_event_ports[i]->evbuf, false);

    /* control in events */
    if (effect->events_in_buffer && effect->events_in_buffer_helper && effect->control_index >= 0)
    {
        const size_t space = jack_ringbuffer_read_space(effect->events_in_buffer);
        LV2_Atom *atom = (LV2_Atom*)effect->events_in_buffer_helper;
        port = effect->ports[effect->control_index];

        for (size_t j = 0; j < space; j += sizeof(LV2_Atom) + atom->size)
        {
            jack_ringbuffer_read(effect->events_in_buffer, (char*)atom, sizeof(LV2_Atom));
            jack_ringbuffer_read(effect->events_in_buffer, (char*)(atom + 1), atom->size);

            LV2_Evbuf_Iterator e = lv2_evbuf_end(port->evbuf);
            lv2_evbuf_write(&e, nframes - 1, 0, atom->type, atom->size, (uint8_t*)(atom + 1));
        }
    }

    for (i = 0; i < effect->input_control_ports_count; i++)
    {
        port = effect->input_control_ports[i];

        if (port->cv_source)
        {
            if (pthread_mutex_trylock(&port->cv_source_mutex) != 0)
                continue;
            if (!port->cv_source) {
                pthread_mutex_unlock(&port->cv_source_mutex);
                continue;
            }

            cv_source_t *cv_source = port->cv_source;

            value = ((float*)jack_port_get_buffer(cv_source->jack_port, 1))[0];

            // convert value from source port into something relevant for this parameter
            if (value <= cv_source->source_min_value) {
                value = cv_source->min_value;

            } else if (value >= cv_source->source_max_value) {
                value = cv_source->max_value;

            } else {
                // normalize value to 0-1
                value = (value - cv_source->source_min_value) / cv_source->source_diff_value;

                if (port->hints & HINT_TOGGLE) {
                    // use min|max values if toggle
                    value = value > 0.5f ? cv_source->max_value : cv_source->min_value;

                } else {
                    // otherwise unnormalize value to full scale
                    value = cv_source->min_value + (value * cv_source->diff_value);

                    // and round to integer if needed
                    if (port->hints & HINT_INTEGER) {
                        value = roundf(value);
                    }
                }
            }

            // ignore requests for same value
            if (! floats_differ_enough(cv_source->prev_value, value)) {
                pthread_mutex_unlock(&port->cv_source_mutex);
                continue;
            }

            if (SetPortValue(port, value, effect->instance, false, false)) {
                needs_post = true;
                cv_source->prev_value = value;
            }

            pthread_mutex_unlock(&port->cv_source_mutex);
        }
    }

    // handle bypass as CV addressing
    {
        port = &effect->bypass_port;

        if (port->cv_source && pthread_mutex_trylock(&port->cv_source_mutex) == 0)
        {
            cv_source_t *cv_source = port->cv_source;

            value = ((float*)jack_port_get_buffer(cv_source->jack_port, 1))[0];

            // NOTE: values are reversed as this is bypass special behaviour
            if (value <= cv_source->source_min_value) {
                value = cv_source->max_value;

            } else if (value >= cv_source->source_max_value) {
                value = cv_source->min_value;

            } else {
                value = ((value - cv_source->source_min_value) / cv_source->source_diff_value) > 0.5f
                        ? cv_source->min_value
                        : cv_source->max_value;
            }

            // ignore requests for same value
            if (floats_differ_enough(cv_source->prev_value, value)) {
                if (SetPortValue(port, value, effect->instance, true, false)) {
                    needs_post = true;
                    cv_source->prev_value = value;
                }
            }

            pthread_mutex_unlock(&port->cv_source_mutex);
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
            value = *(effect->ports[port_id]->buffer);
            if (monitor_check_condition(effect->monitors[i]->op, effect->monitors[i]->value, value) &&
                floats_differ_enough(value, effect->monitors[i]->last_notified_value)) {
                if (monitor_send(effect->instance, effect->ports[port_id]->symbol, value) >= 0)
                    effect->monitors[i]->last_notified_value = value;
            }
        }
    }

    /* MIDI out events */
    for (i = 0; i < effect->output_event_ports_count; i++)
    {
        port = effect->output_event_ports[i];

        if (port->flow == FLOW_OUTPUT && port->type == TYPE_EVENT)
        {
            void *buf = port->jack_port ? jack_port_get_buffer(port->jack_port, nframes) : NULL;

            if (buf != NULL)
                jack_midi_clear_buffer(buf);

            if (effect->bypass > 0.5f && effect->enabled_index < 0)
            {
                // effect is now bypassed, but wasn't before
                if (buf != NULL)
                {
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
                        void* bufIn = jack_port_get_buffer(effect->input_event_ports[i]->jack_port, nframes);
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
            }
            else
            {
                LV2_Evbuf_Iterator it;
                bool can_write_midi = true;

                for (it = lv2_evbuf_begin(port->evbuf); lv2_evbuf_is_valid(it); it = lv2_evbuf_next(it))
                {
                    uint32_t frames, subframes, type, size;
                    uint8_t* body;

                    if (! lv2_evbuf_get(it, &frames, &subframes, &type, &size, &body))
                        continue;

                    if (buf != NULL && type == g_urids.midi_MidiEvent)
                    {
                        if (can_write_midi && jack_midi_event_write(buf, frames, body, size) == ENOBUFS)
                            can_write_midi = false;
                    }
                    else if (effect->events_out_buffer && type == g_urids.atom_Object)
                    {
                        const LV2_Atom_Object_Body *objbody = (const LV2_Atom_Object_Body*)body;

                        if (objbody->otype == g_urids.patch_Set)
                        {
                            const LV2_Atom_Int  *sequence = NULL;
                            const LV2_Atom_URID *property = NULL;
                            const LV2_Atom      *lv2value = NULL;

                            lv2_atom_object_body_get(size, objbody,
                                                     g_urids.patch_sequence, (const LV2_Atom**)&sequence,
                                                     g_urids.patch_property, (const LV2_Atom**)&property,
                                                     g_urids.patch_value,    &lv2value,
                                                     0);

                            if (sequence != NULL && sequence->body == MAGIC_PARAMETER_SEQ_NUMBER)
                                continue;

                            if (jack_ringbuffer_write_space(effect->events_out_buffer) < sizeof(uint32_t) + sizeof(LV2_Atom) + lv2value->size)
                                continue;

                            jack_ringbuffer_write(effect->events_out_buffer, (const char*)&property->body, sizeof(uint32_t));
                            jack_ringbuffer_write(effect->events_out_buffer, (const char*)lv2value, lv2_atom_total_size(lv2value));

                            postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                            if (posteventptr == NULL)
                                continue;

                            posteventptr->event.type = POSTPONED_PROCESS_OUTPUT_BUFFER;
                            posteventptr->event.process_out_buf.effect_id = effect->instance;

                            pthread_mutex_lock(&g_rtsafe_mutex);
                            list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                            pthread_mutex_unlock(&g_rtsafe_mutex);

                            needs_post = true;
                        }
                    }
                }
            }
        }
    }

    if (effect->hints & HINT_TRIGGERS)
    {
        for (i = 0; i < effect->input_control_ports_count; i++)
        {
            port = effect->input_control_ports[i];

            if ((port->hints & HINT_TRIGGER) && floats_differ_enough(port->prev_value, port->def_value))
                port->prev_value = *(port->buffer) = port->def_value;
        }
    }

    if (effect->hints & HINT_OUTPUT_MONITORS)
    {
        for (i = 0; i < effect->output_control_ports_count; i++)
        {
            port = effect->output_control_ports[i];

            if ((port->hints & HINT_MONITORED) == 0)
                continue;

            value = *(port->buffer);

            if (! floats_differ_enough(port->prev_value, value))
                continue;

            postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

            if (posteventptr == NULL)
                continue;

            port->prev_value = value;

            posteventptr->event.type = POSTPONED_OUTPUT_MONITOR;
            posteventptr->event.parameter.effect_id = effect->instance;
            posteventptr->event.parameter.symbol    = port->symbol;
            posteventptr->event.parameter.value     = value;

            pthread_mutex_lock(&g_rtsafe_mutex);
            list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
            pthread_mutex_unlock(&g_rtsafe_mutex);

            needs_post = true;
        }
    }

    effect->was_bypassed = effect->bypass > 0.5f;

    if (effect->hints & HINT_STATE_UNSAFE)
        pthread_mutex_unlock(&effect->state_restore_mutex);

    if (needs_post)
        sem_post(&g_postevents_semaphore);

    return 0;
}

static bool SetPortValue(port_t *port, float value, int effect_id, bool is_bypass, bool from_ui)
{
    bool update_transport = false;

    if (is_bypass)
    {
        effect_t *effect = &g_effects[effect_id];
        if (effect->enabled_index >= 0)
            *(effect->ports[effect->enabled_index]->buffer) = (value > 0.5f) ? 0.0f : 1.0f;
    }
    else if (effect_id == GLOBAL_EFFECT_ID)
    {
        if (!strcmp(port->symbol, g_bpb_port_symbol))
        {
            g_transport_bpb = value;
            update_transport = true;
        }
        else if (!strcmp(port->symbol, g_bpm_port_symbol))
        {
            g_transport_bpm = value;
            update_transport = true;
        }
        else if (!strcmp(port->symbol, g_rolling_port_symbol))
        {
            if (value > 0.5f)
            {
                jack_transport_start(g_jack_global_client);
            }
            else
            {
                jack_transport_stop(g_jack_global_client);
                jack_transport_locate(g_jack_global_client, 0);
            }
            g_transport_reset = true;
            update_transport = true;
        }
    }
    else if (!from_ui)
    {
#ifdef WITH_EXTERNAL_UI_SUPPORT
        port->hints |= HINT_SHOULD_UPDATE;
#endif
    }

    port->prev_value = *(port->buffer) = value;

    postponed_event_list_data* const posteventptr =
        rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

    if (posteventptr == NULL)
        return false;

    posteventptr->event.type = POSTPONED_PARAM_SET;
    posteventptr->event.parameter.effect_id = effect_id;
    posteventptr->event.parameter.symbol    = port->symbol;
    posteventptr->event.parameter.value     = value;

    pthread_mutex_lock(&g_rtsafe_mutex);
    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    if (update_transport)
        return UpdateGlobalJackPosition(UPDATE_POSITION_FORCED, false);

    return true;
}

// FIXME merge most of this with SetPortValue
static float UpdateValueFromMidi(midi_cc_t* mcc, uint16_t mvalue, bool highres)
{
    const uint16_t mvaluediv = highres ? 8192 : 64;

    if (!strcmp(mcc->symbol, g_bypass_port_symbol))
    {
        effect_t *effect = &g_effects[mcc->effect_id];

        const bool bypassed = mvalue < mvaluediv;
        effect->bypass = bypassed ? 1.0f : 0.0f;

        if (effect->enabled_index >= 0)
            *(effect->ports[effect->enabled_index]->buffer) = bypassed ? 0.0f : 1.0f;

        return bypassed ? 1.0f : 0.0f;
    }

    port_t* port = mcc->port;
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
    port->prev_value = *(port->buffer) = value;
    return value;
}


static bool UpdateGlobalJackPosition(enum UpdatePositionFlag flag, bool do_post)
{
    bool old_rolling;
    double old_bpb, old_bpm;

    if (flag != UPDATE_POSITION_SKIP)
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

    if (flag == UPDATE_POSITION_SKIP)
        return false;
    if (flag == UPDATE_POSITION_IF_CHANGED &&
        old_rolling == g_jack_rolling &&
        !doubles_differ_enough(old_bpb, g_transport_bpb) &&
        !doubles_differ_enough(old_bpm, g_transport_bpm))
        return false;

    postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

    if (!posteventptr)
        return false;

    posteventptr->event.type = POSTPONED_TRANSPORT;
    posteventptr->event.transport.rolling = g_jack_rolling;
    posteventptr->event.transport.bpb     = g_transport_bpb;
    posteventptr->event.transport.bpm     = g_transport_bpm;

    pthread_mutex_lock(&g_rtsafe_mutex);
    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    if (do_post)
        sem_post(&g_postevents_semaphore);

    return true;
}

static int ProcessGlobalClient(jack_nframes_t nframes, void *arg)
{
    jack_midi_event_t event;
    uint8_t channel, controller;
    uint8_t status_nibble;
    uint16_t mvalue;
    float value;
    double dvalue;
    bool handled, highres, needs_post = false;
    enum UpdatePositionFlag pos_flag = UPDATE_POSITION_IF_CHANGED;

#ifdef HAVE_HYLIA
    if (g_transport_sync_mode == TRANSPORT_SYNC_ABLETON_LINK)
    {
        hylia_process(g_hylia_instance, nframes, &g_hylia_timeinfo);

        // check for updated beats per bar
        dvalue = g_hylia_timeinfo.beatsPerBar;

        if (dvalue > 0.0 && doubles_differ_enough(g_transport_bpb, dvalue))
        {
            g_transport_bpb = dvalue;
            pos_flag = UPDATE_POSITION_FORCED;
        }

        // check for updated beats per minute
        dvalue = g_hylia_timeinfo.beatsPerMinute;

        if (dvalue > 0.0 && doubles_differ_enough(g_transport_bpm, dvalue))
        {
            g_transport_bpm = dvalue;
            pos_flag = UPDATE_POSITION_FORCED;
        }
    }
#endif

    // Handle input MIDI events
    void *const port_buf = jack_port_get_buffer(g_midi_in_port, nframes);
    const jack_nframes_t event_count = jack_midi_get_event_count(port_buf);

    for (jack_nframes_t i = 0 ; i < event_count; i++)
    {
        if (jack_midi_event_get(&event, port_buf, i) != 0)
            break;

        // Handle MIDI Beat Clock
        if (g_transport_sync_mode == TRANSPORT_SYNC_MIDI)
        {
            switch (event.buffer[0])
            {
            case 0xF8: { // Clock tick
                // Calculate the timestamp difference to the previous MBC event
                const uint64_t current = g_monotonic_frame_count + event.time;

                if (g_previous_midi_event_time != 0)
                {
                    const uint64_t delta = current - g_previous_midi_event_time;

                    if (delta < g_max_allowed_midi_delta)
                    {
                        const double filtered_delta = beat_clock_tick_filter(delta);

                        // rounded to 2 decimal points
                        dvalue = rint(beats_per_minute(filtered_delta, g_sample_rate) * 100.0) / 100.0;

                        if (dvalue > 0.0 && doubles_differ_enough(g_transport_bpm, dvalue))
                        {
                            // set a sane low limit
                            if (dvalue <= 20.0)
                                g_transport_bpm = 20;
                            // >300 BPM over MIDI is unreasonable
                            else if (dvalue >= 300.0)
                                g_transport_bpm = 300.0;
                            // we are good!
                            else
                                g_transport_bpm = dvalue;

                            pos_flag = UPDATE_POSITION_FORCED;
                        }
                    } else {
                        reset_filter();
                    }
                } else {
                    reset_filter();
                }

                g_previous_midi_event_time = current;
                break;
            }

            case 0xFA: // Start
            case 0xFB: // Continue
                g_previous_midi_event_time = 0;
                jack_transport_start(g_jack_global_client);
                break;

            case 0xFC: // Stop
                jack_transport_stop(g_jack_global_client);
                jack_transport_locate(g_jack_global_client, 0);
                break;

            default:
                // TODO: Handle MIDI Song Position Pointer
                break;
            }
        }

        status_nibble = event.buffer[0] & 0xF0;

        // Handle MIDI program change
        if (status_nibble == 0xC0)
        {
            channel = (event.buffer[0] & 0x0F);

            if (g_monitored_midi_programs[channel] && event.size == 2)
            {
#ifdef _DARKGLASS_PABLITO
                if (event.buffer[1] == 0 || event.buffer[1] == 127)
                    continue;
#endif
                // Append to the queue
                postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                  if (posteventptr)
                  {
                      posteventptr->event.type = POSTPONED_MIDI_PROGRAM_CHANGE;
                      posteventptr->event.program_change.program = event.buffer[1];
                      posteventptr->event.program_change.channel = channel;

                      pthread_mutex_lock(&g_rtsafe_mutex);
                      list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                      pthread_mutex_unlock(&g_rtsafe_mutex);

                      needs_post = true;
                }
            }
            else
            {
                // Wrong channel or size. Discard.
                continue;
            }
        } // endif MIDI program change

        if (event.size != 3)
            continue;

        // check if it's a CC or Pitchbend event
        if (status_nibble == 0xB0)
        {
            controller = event.buffer[1];
            mvalue     = event.buffer[2];
            highres    = false;
#ifdef _DARKGLASS_PABLITO
            channel    = (event.buffer[0] & 0x0F);
            switch (controller)
            {
            // these can be assigned to parameters, handle them first
            case 17 ... 32:
            case 89:
                if (! g_monitored_midi_programs[channel])
                    continue;

                for (int j = 0; j < MAX_MIDI_CC_ASSIGN; j++)
                {
                    if (g_midi_cc_list[j].effect_id == ASSIGNMENT_NULL)
                        break;
                    if (g_midi_cc_list[j].effect_id == ASSIGNMENT_UNUSED)
                        continue;

                    if (g_midi_cc_list[j].controller == controller)
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

                if (needs_post)
                    continue;

            // fall-through
            // if the special parameter CCs are not handled, report up as regular MIDI CC
            case 0:
            case 7:
            case 14 ... 16:
            case 85 ... 87:
            case 102 ... 119:
                if (g_monitored_midi_programs[channel])
                {
                    postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                    if (posteventptr)
                    {
                        posteventptr->event.type = POSTPONED_MIDI_CONTROL_CHANGE;
                        posteventptr->event.control_change.channel = channel;
                        posteventptr->event.control_change.control = controller;
                        posteventptr->event.control_change.value = mvalue;

                        pthread_mutex_lock(&g_rtsafe_mutex);
                        list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                        pthread_mutex_unlock(&g_rtsafe_mutex);

                        needs_post = true;
                    }
                }
                continue;
            }
#else
        }
        else if (status_nibble == 0xE0)
        {
            controller = MIDI_PITCHBEND_AS_CC;
            mvalue     = (event.buffer[2] << 7) | event.buffer[1];
            highres    = true;
#endif
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

#ifdef _DARKGLASS_PABLITO
            if (! g_monitored_midi_programs[channel])
                continue;
#else
            if (g_midi_cc_list[j].channel != channel)
                continue;
#endif

            // TODO: avoid race condition against effects_midi_unmap
            if (g_midi_cc_list[j].controller == controller)
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
            else if (g_monitored_midi_programs[channel])
            {
                postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                if (posteventptr)
                {
                    posteventptr->event.type = POSTPONED_MIDI_CONTROL_CHANGE;
                    posteventptr->event.control_change.channel = channel;
                    posteventptr->event.control_change.control = controller;
                    posteventptr->event.control_change.value = mvalue;

                    pthread_mutex_lock(&g_rtsafe_mutex);
                    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                    pthread_mutex_unlock(&g_rtsafe_mutex);

                    needs_post = true;
                }
            }
        }
    }

#ifdef MOD_IO_PROCESSING_ENABLED
    // Handle audio
    const float *const audio_in1_buf = (float*)jack_port_get_buffer(g_audio_in1_port, nframes);
    const float *const audio_in2_buf = (float*)jack_port_get_buffer(g_audio_in2_port, nframes);
    /* */ float *const audio_out1_buf = (float*)jack_port_get_buffer(g_audio_out1_port, nframes);
    /* */ float *const audio_out2_buf = (float*)jack_port_get_buffer(g_audio_out2_port, nframes);

    switch (g_noisegate_channel)
    {
    case 0: // no gate
        if (audio_out1_buf != audio_in1_buf)
            memcpy(audio_out1_buf, audio_in1_buf, sizeof(float)*nframes);
        if (audio_out2_buf != audio_in2_buf)
            memcpy(audio_out2_buf, audio_in2_buf, sizeof(float)*nframes);
        break;
    case 1: // left channel only
        if (audio_out2_buf != audio_in2_buf)
            memcpy(audio_out2_buf, audio_in2_buf, sizeof(float)*nframes);
        for (uint32_t i=0; i<nframes; ++i)
            audio_out1_buf[i] = gate_push_sample_and_apply(&g_noisegate, audio_in1_buf[i]);
        break;
    case 2: // right channel only
        if (audio_out1_buf != audio_in1_buf)
            memcpy(audio_out1_buf, audio_in1_buf, sizeof(float)*nframes);
        for (uint32_t i=0; i<nframes; ++i)
            audio_out2_buf[i] = gate_push_sample_and_apply(&g_noisegate, audio_in2_buf[i]);
        break;
    case 3: // left & right channels
        for (uint32_t i=0; i<nframes; ++i)
        {
            gate_push_samples_and_run(&g_noisegate, audio_in1_buf[i], audio_in2_buf[i]);
            audio_out1_buf[i] = gate_apply(&g_noisegate, audio_in1_buf[i]);
            audio_out2_buf[i] = gate_apply(&g_noisegate, audio_in2_buf[i]);
        }
        break;
    }
#endif

    // Handle audio monitors
    if (pthread_mutex_trylock(&g_audio_monitor_mutex) == 0)
    {
        float *monitorbuf;
        float absvalue, oldvalue;

        for (int i = 0; i < g_audio_monitor_count; ++i)
        {
            monitorbuf = (float*)jack_port_get_buffer(g_audio_monitors[i].port, nframes);
            oldvalue = value = g_audio_monitors[i].value;

            for (jack_nframes_t i = 0 ; i < nframes; i++)
            {
                absvalue = fabsf(monitorbuf[i]);

                if (absvalue > value)
                    value = absvalue;
            }

            if (oldvalue < value)
            {
                g_audio_monitors[i].value = value;

                postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

                if (posteventptr)
                {
                    posteventptr->event.type = POSTPONED_AUDIO_MONITOR;
                    posteventptr->event.audio_monitor.index = i;
                    posteventptr->event.audio_monitor.value = value;

                    pthread_mutex_lock(&g_rtsafe_mutex);
                    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
                    pthread_mutex_unlock(&g_rtsafe_mutex);

                    needs_post = true;
                }
            }
        }

        pthread_mutex_unlock(&g_audio_monitor_mutex);
    }

    if (UpdateGlobalJackPosition(pos_flag, false))
        needs_post = true;

    if (g_cpu_load_enabled)
    {
        const uint32_t cpu_update_rate = g_sample_rate / 2;
        const uint32_t frame_check = g_monotonic_frame_count % cpu_update_rate;

        if (frame_check + nframes >= cpu_update_rate)
        {
            g_cpu_load_trigger = true;
            needs_post = true;
        }
    }

    if (needs_post)
        sem_post(&g_postevents_semaphore);

    // Increase by one period
    g_monotonic_frame_count += nframes;

    return 0;

    UNUSED_PARAM(arg);
}

/**
 * If the transport is rolling and if we are the Jack Timebase Master
 * then this callback acts once per cycle and effects the following
 * cycle.
 *
 * This realtime function must not wait.
 *
 * Read: http://jackaudio.org/files/docs/html/transport-design.html
 */
static void JackTimebase(jack_transport_state_t state, jack_nframes_t nframes,
                         jack_position_t *pos, int new_pos, void *arg)
{
    double tick;

    // Who sets these global variables?

    // g_transport_bpb, g_transport_bpm, g_transport_reset are static
    // volatile and meant to be set from anywhere.

    // g_transport_bpm is only set on lines 1352
    // (UpdateValueFromMIDI), 1427 (ProcessGlobalClient), 2016 (CCDataUpdate)
    // and 4451 (effects_transport).

    // Update the extended position information.
    pos->beats_per_bar = g_transport_bpb;
    pos->beats_per_minute = g_transport_bpm;

    // Is this a hacky way to express rounding? Better use nearbyint()?
    const int32_t beats_per_bar_int = (int32_t)(pos->beats_per_bar + 0.5f);

    if (new_pos || g_transport_reset) // Is caching involved? No.
    {
        // Do we have to set every "constant" data field every time?
        pos->valid = JackPositionBBT | JackTickDouble;
        pos->beat_type = 4.0f;
        pos->ticks_per_beat = TRANSPORT_TICKS_PER_BEAT;

        double abs_beat, abs_tick;

#ifdef HAVE_HYLIA
        if (g_transport_sync_mode == TRANSPORT_SYNC_ABLETON_LINK)
        {
            if (g_hylia_timeinfo.beat >= 0.0)
            {
                abs_beat = g_hylia_timeinfo.beat;
                abs_tick = abs_beat * TRANSPORT_TICKS_PER_BEAT;
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
            /// ????
            // 1. analyse frame position. 2. calculate ticks. 3. derive values for everything else???

            // What is min? why 60?
            const double min = (double)pos->frame / (double)(g_sample_rate * 60);
            abs_beat = min * pos->beats_per_minute;
            abs_tick = abs_beat * TRANSPORT_TICKS_PER_BEAT;
            g_transport_reset = false;
        }

        const double bar  = floor(abs_beat / pos->beats_per_bar);
        const double beat = floor(fmod(abs_beat, pos->beats_per_bar));

        pos->bar  = (int32_t)(bar) + 1;
        pos->beat = (int32_t)(beat) + 1;
        pos->bar_start_tick = ((bar * pos->beats_per_bar) + beat) * TRANSPORT_TICKS_PER_BEAT;

        tick = abs_tick - (abs_beat * pos->ticks_per_beat);
    }
    else // not new_pos nor g_transport_reset
    {
        // update the current tick with the beat.
        tick = pos->tick_double +
              (nframes * TRANSPORT_TICKS_PER_BEAT * pos->beats_per_minute / (double)(g_sample_rate * 60));

        // why adjust? why can overflow happen?
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
    pos->tick_double = tick;
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
    const LV2_Feature **features = (const LV2_Feature**) mod_calloc(FEATURE_TERMINATOR+1, sizeof(LV2_Feature*));

    /* URI and URID Features are the same for all instances (global declaration) */

    /* Options Feature is the same for all instances (global declaration) */

    /* State path features, includes custom pointer */
    LV2_State_Make_Path *makePath = (LV2_State_Make_Path*) malloc(sizeof(LV2_State_Make_Path));
    makePath->handle = effect;
    makePath->path = MakePluginStatePathFromSratchDir;

    LV2_Feature *state_make_path_feature = (LV2_Feature*) malloc(sizeof(LV2_Feature));
    state_make_path_feature->URI = LV2_STATE__makePath;
    state_make_path_feature->data = makePath;

    /* ControlInputPort change request feature, includes custom pointer */
    LV2_ControlInputPort_Change_Request *ctrlportReqChange
        = (LV2_ControlInputPort_Change_Request*) malloc(sizeof(LV2_ControlInputPort_Change_Request));
    ctrlportReqChange->handle = effect;
    ctrlportReqChange->request_change = RequestControlPortChange;

    LV2_Feature *ctrlportReqChange_feature = (LV2_Feature*) malloc(sizeof(LV2_Feature));
    ctrlportReqChange_feature->URI = LV2_CONTROL_INPUT_PORT_CHANGE_REQUEST_URI;
    ctrlportReqChange_feature->data = ctrlportReqChange;

    /* Worker Feature, must be last as it can be null */
    LV2_Feature *work_schedule_feature = NULL;

    if (lilv_plugin_has_extension_data(effect->lilv_plugin, g_lilv_nodes.worker_interface))
    {
        LV2_Worker_Schedule *schedule = (LV2_Worker_Schedule*) malloc(sizeof(LV2_Worker_Schedule));
        schedule->handle = &effect->worker;
        schedule->schedule_work = worker_schedule;

        work_schedule_feature = (LV2_Feature*) malloc(sizeof(LV2_Feature));
        work_schedule_feature->URI = LV2_WORKER__schedule;
        work_schedule_feature->data = schedule;
    }

    /* Buf-size Feature is the same for all instances (global declaration) */

    /* Features array */
    features[URI_MAP_FEATURE]           = &g_uri_map_feature;
    features[URID_MAP_FEATURE]          = &g_urid_map_feature;
    features[URID_UNMAP_FEATURE]        = &g_urid_unmap_feature;
    features[OPTIONS_FEATURE]           = &g_options_feature;
#ifdef __MOD_DEVICES__
    features[HMI_WC_FEATURE]            = &g_hmi_wc_feature;
#endif
    features[LICENSE_FEATURE]           = &g_license_feature;
    features[BUF_SIZE_POWER2_FEATURE]   = &g_buf_size_features[0];
    features[BUF_SIZE_FIXED_FEATURE]    = &g_buf_size_features[1];
    features[BUF_SIZE_BOUNDED_FEATURE]  = &g_buf_size_features[2];
    features[LOG_FEATURE]               = &g_lv2_log_feature;
    features[STATE_FREE_PATH_FEATURE]   = &g_state_freePath_feature;
    features[STATE_MAKE_PATH_FEATURE]   = state_make_path_feature;
    features[CTRLPORT_REQUEST_FEATURE]  = ctrlportReqChange_feature;
    features[WORKER_FEATURE]            = work_schedule_feature;
#ifdef WITH_EXTERNAL_UI_SUPPORT
    features[UI_DATA_ACCESS]            = NULL;
    features[UI_INSTANCE_ACCESS]        = NULL;
#endif
    features[FEATURE_TERMINATOR]        = NULL;

    effect->features = features;

    /* also update jack client option value */
    g_options[7].value = effect->jack_client;
}

/**
* If transport is stopped, ensure jack invokes the timebase master by
* invoking a jack reposition on the current position.
 */
static void TriggerJackTimebase(bool reset_to_zero)
{
    jack_position_t pos;
    if (jack_transport_query(g_jack_global_client, &pos) == JackTransportStopped) {
        if (reset_to_zero) {
            pos.frame = 0;
        }
        int res = jack_transport_reposition(g_jack_global_client, &pos);
        if (res < 0) {
            fprintf(stderr, "Failed to trigger timebase master.  Call "
                            "will occur when transport starts or a client updates "
                            "position.\n");
        }
    }
}

static property_t *FindEffectPropertyByURI(effect_t *effect, const char *uri)
{
    for (uint32_t i = 0; i < effect->properties_count; i++)
    {
        if (strcmp(uri, lilv_node_as_uri(effect->properties[i]->uri)) == 0)
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
    else if (type == g_urids.atom_Int || type == g_urids.atom_Bool)
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
    LilvNodes* presets = lilv_plugin_get_related(effect->lilv_plugin, g_lilv_nodes.preset);
    uint32_t presets_count = lilv_nodes_size(presets);
    effect->presets_count = presets_count;
    // allocate for presets
    effect->presets = (preset_t **) mod_calloc(presets_count, sizeof(preset_t *));
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
        if (effect->features[STATE_MAKE_PATH_FEATURE])
        {
            free(effect->features[STATE_MAKE_PATH_FEATURE]->data);
            free((void*)effect->features[STATE_MAKE_PATH_FEATURE]);
        }
        /*
        if (effect->features[STATE_MAP_PATH_FEATURE])
        {
            free(effect->features[STATE_MAP_PATH_FEATURE]->data);
            free((void*)effect->features[STATE_MAP_PATH_FEATURE]);
        }
        */
        if (effect->features[CTRLPORT_REQUEST_FEATURE])
        {
            free(effect->features[CTRLPORT_REQUEST_FEATURE]->data);
            free((void*)effect->features[CTRLPORT_REQUEST_FEATURE]);
        }
        if (effect->features[WORKER_FEATURE])
        {
            free(effect->features[WORKER_FEATURE]->data);
            free((void*)effect->features[WORKER_FEATURE]);
        }
        free(effect->features);
    }
}

// back to normal
#pragma GCC diagnostic pop

static void FreePluginString(void* handle, char *str)
{
    return free(str);

    UNUSED_PARAM(handle);
}

static void ConnectToAllHardwareMIDIPorts(void)
{
    if (g_jack_global_client == NULL)
        return;

    const char** const midihwports = jack_get_ports(g_jack_global_client, "",
                                                    JACK_DEFAULT_MIDI_TYPE,
                                                    JackPortIsTerminal|JackPortIsPhysical|JackPortIsOutput);
    if (midihwports != NULL)
    {
        const char *ourportname = jack_port_name(g_midi_in_port);

        for (int i=0; midihwports[i] != NULL; ++i)
            jack_connect(g_jack_global_client, midihwports[i], ourportname);

        struct list_head *it;
        pthread_mutex_lock(&g_raw_midi_port_mutex);
        list_for_each(it, &g_raw_midi_port_list)
        {
            raw_midi_port_item* const portitemptr = list_entry(it, raw_midi_port_item, siblings);

            for (int i=0; midihwports[i] != NULL; ++i)
                jack_connect(g_jack_global_client,
                             midihwports[i],
                             jack_port_name(portitemptr->jack_port));
        }
        pthread_mutex_unlock(&g_raw_midi_port_mutex);

        jack_free(midihwports);
    }
}

static void ConnectToMIDIThroughPorts(void)
{
    if (g_jack_global_client == NULL)
        return;

    const char** const midihwports = jack_get_ports(g_jack_global_client, "system:midi_capture_",
                                                    JACK_DEFAULT_MIDI_TYPE,
                                                    JackPortIsTerminal|JackPortIsPhysical|JackPortIsOutput);
    if (midihwports != NULL)
    {
        char  aliases[2][320];
        char* aliasesptr[2] = {
            aliases[0],
            aliases[1]
        };

        const char *ourportname = jack_port_name(g_midi_in_port);

        for (int i=0; midihwports[i] != NULL; ++i)
        {
            jack_port_t* const port = jack_port_by_name(g_jack_global_client, midihwports[i]);

            if (port == NULL)
                continue;
            if (jack_port_get_aliases(port, aliasesptr) <= 0)
                continue;
            if (strncmp(aliases[0], "alsa_pcm:Midi-Through/", 22))
                continue;
            jack_connect(g_jack_global_client, midihwports[i], ourportname);
        }

        jack_free(midihwports);
    }
}

#ifdef __MOD_DEVICES__
static void HMIWidgetsSetLedWithBlink(LV2_HMI_WidgetControl_Handle handle,
                                      LV2_HMI_Addressing addressing_ptr,
                                      LV2_HMI_LED_Colour led_color,
                                      int on_blink_time,
                                      int off_blink_time)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsSetLedWithBlink %i: %i %i %i\n",
               assignment_id, led_color, on_blink_time, off_blink_time);
        fflush(stdout);
    }

    if (on_blink_time < 0)
    {
        if (on_blink_time < LV2_HMI_LED_Blink_Fast)
            on_blink_time = LV2_HMI_LED_Blink_Fast;
        off_blink_time = 0;
    }
    else
    {
        if (on_blink_time > 5000)
            on_blink_time = 5000;

        if (off_blink_time < 0)
            off_blink_time = 0;
        else if (off_blink_time > 5000)
            off_blink_time = 5000;
    }

    char msg[32];
    snprintf(msg, sizeof(msg), "%i %i %i %i", assignment_id, led_color, on_blink_time, off_blink_time);
    msg[sizeof(msg)-1] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_led_blink, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

static void HMIWidgetsSetLedWithBrightness(LV2_HMI_WidgetControl_Handle handle,
                                           LV2_HMI_Addressing addressing_ptr,
                                           LV2_HMI_LED_Colour led_color,
                                           int brightness)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsSetLedWithBrightness %i: %i %i\n",
               assignment_id, led_color, brightness);
        fflush(stdout);
    }

    if (brightness < LV2_HMI_LED_Brightness_Normal)
        brightness = LV2_HMI_LED_Brightness_Normal;
    else if (brightness > 100)
        brightness = 100;

    char msg[32];
    snprintf(msg, sizeof(msg), "%i %i %i", assignment_id, led_color, brightness);
    msg[sizeof(msg)-1] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_led_brightness, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

static void HMIWidgetsSetLabel(LV2_HMI_WidgetControl_Handle handle,
                               LV2_HMI_Addressing addressing_ptr,
                               const char* label)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsSetLabel %i: '%s'\n", assignment_id, label);
        fflush(stdout);
    }

    char msg[24];
    snprintf(msg, sizeof(msg), "%i %s", assignment_id, label);
    msg[sizeof(msg)-1] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_name, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

static void HMIWidgetsSetValue(LV2_HMI_WidgetControl_Handle handle,
                               LV2_HMI_Addressing addressing_ptr,
                               const char* value)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsSetValue %i: '%s'\n", assignment_id, value);
        fflush(stdout);
    }

    char msg[24];
    snprintf(msg, sizeof(msg), "%i %s", assignment_id, value);
    msg[sizeof(msg)-1] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_value, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

static void HMIWidgetsSetUnit(LV2_HMI_WidgetControl_Handle handle,
                              LV2_HMI_Addressing addressing_ptr,
                              const char* unit)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsSetUnit %i: '%s'\n", assignment_id, unit);
        fflush(stdout);
    }

    char msg[24];
    snprintf(msg, sizeof(msg), "%i %s", assignment_id, unit);
    msg[sizeof(msg)-1] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_unit, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

static void HMIWidgetsSetIndicator(LV2_HMI_WidgetControl_Handle handle,
                                   LV2_HMI_Addressing addressing_ptr,
                                   float indicator_poss)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsSetIndicator %i: %f\n", assignment_id, indicator_poss);
        fflush(stdout);
    }

    if (indicator_poss < 0.0f)
        indicator_poss = 0.0f;
    else if (indicator_poss > 1.0f)
        indicator_poss = 1.0f;

    char msg[32];
    snprintf(msg, sizeof(msg), "%i %f", assignment_id, indicator_poss);
    msg[31] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_widget_indicator, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

static void HMIWidgetsPopupMessage(LV2_HMI_WidgetControl_Handle handle,
                                   LV2_HMI_Addressing addressing_ptr,
                                   LV2_HMI_Popup_Style style,
                                   const char* title,
                                   const char* text)
{
    if (handle == NULL || addressing_ptr == NULL || g_hmi_data == NULL) {
        return;
    }

    const hmi_addressing_t *addressing = (const hmi_addressing_t*)addressing_ptr;

    const int assignment_id = addressing->actuator_id;
    const uint8_t page = addressing->page;
    const uint8_t subpage = addressing->subpage;

    if (g_verbose_debug) {
        printf("DEBUG: HMIWidgetsPopupMessage %i: %i '%s' '%s'\n", assignment_id, style, title, text);
        fflush(stdout);
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "%i %i \"%s\" \"%s\"", assignment_id, style, title, text);
    msg[sizeof(msg)-1] = '\0';

    pthread_mutex_lock(&g_hmi_mutex);
    sys_serial_write(&g_hmi_data->server, sys_serial_event_type_popup, page, subpage, msg);
    pthread_mutex_unlock(&g_hmi_mutex);
}

#endif

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
    filebuffer = (char*)mod_calloc(1, filesize+1);
    if (! filebuffer)
        goto end;

    // read file
    fseek(file, 0, SEEK_SET);
    if (fread(filebuffer, 1, filesize, file) != (size_t)filesize)
    {
        free(filebuffer);
        filebuffer = NULL;
    }

end:
    fclose(file);
    return filebuffer;

    UNUSED_PARAM(handle);
}

static int LogPrintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, ...)
{
    int ret;
    va_list args;
    va_start(args, fmt);
    ret = LogVPrintf(handle, type, fmt, args);
    va_end(args);

    return ret;

    UNUSED_PARAM(handle);
}

static int LogVPrintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, va_list ap)
{
    int ret = -1;
    char *strp = NULL;

    // try to allocate message early, we can only format once so do it now
    if (type == g_urids.log_Error || type == g_urids.log_Warning || type == g_urids.log_Note)
    {
        ret = vasprintf(&strp, fmt, ap);

        if (ret < 0)
            return ret;

        if (strp == NULL)
        {
            errno = ENOMEM;
            return -1;
        }

        if (type == g_urids.log_Error)
        {
            fprintf(stderr, "\x1b[31m");
            fputs(strp, stderr);
            fprintf(stderr, "\x1b[0m\n");
            fflush(stderr);
        }
        else if (type == g_urids.log_Warning)
        {
            fputs(strp, stderr);
            fflush(stderr);
        }
        else
        {
            fputs(strp, stdout);
            fflush(stdout);
        }
    }
    else if (type == g_urids.log_Trace)
    {
        // handled later during event reserve
        ret = 0;
    }
    else
    {
        errno = EINVAL;
        return -1;
    }

    postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

    if (posteventptr == NULL)
    {
        free(strp);
        return ret;
    }

    if (type == g_urids.log_Trace)
    {
        posteventptr->event.type = POSTPONED_LOG_TRACE;
        ret = vsnprintf(posteventptr->event.log_trace.msg, sizeof(posteventptr->event.log_trace.msg)-1, fmt, ap);
        posteventptr->event.log_trace.msg[sizeof(posteventptr->event.log_trace.msg)-1] = '\0';
        if (g_verbose_debug) {
            fprintf(stdout, "\x1b[30;1m");
            fputs(posteventptr->event.log_trace.msg, stdout);
            fprintf(stdout, "\x1b[0m\n");
            fflush(stdout);
        }
    }
    else
    {
        posteventptr->event.type = POSTPONED_LOG_MESSAGE;

        if (type == g_urids.log_Error)
            posteventptr->event.log_message.type = LOG_ERROR;
        else if (type == g_urids.log_Warning)
            posteventptr->event.log_message.type = LOG_WARNING;
        else
            posteventptr->event.log_message.type = LOG_NOTE;

        posteventptr->event.log_message.msg = strp;
    }

    pthread_mutex_lock(&g_rtsafe_mutex);
    list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    sem_post(&g_postevents_semaphore);
    return ret;

    UNUSED_PARAM(handle);
}

static LV2_ControlInputPort_Change_Status RequestControlPortChange(LV2_ControlInputPort_Change_Request_Handle handle,
                                                                   uint32_t index, float value)
{
    effect_t *effect = (effect_t*)handle;

    if (index >= effect->ports_count)
        return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_INVALID_INDEX;

    port_t *port = effect->ports[index];

    if (port->type != TYPE_CONTROL || port->flow != FLOW_INPUT)
        return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_INVALID_INDEX;

    // ignore requests for same value
    if (!floats_differ_enough(port->prev_value, value))
        return LV2_CONTROL_INPUT_PORT_CHANGE_SUCCESS;

    if (SetPortValue(port, value, effect->instance, false, false))
        sem_post(&g_postevents_semaphore);

    return LV2_CONTROL_INPUT_PORT_CHANGE_SUCCESS;
}

static char* MakePluginStatePathFromSratchDir(LV2_State_Make_Path_Handle handle, const char *path)
{
    effect_t *effect = (effect_t*)handle;
    return MakePluginStatePath(effect->instance, g_lv2_scratch_dir, path);
}

static char* MakePluginStatePathDuringLoadSave(LV2_State_Make_Path_Handle handle, const char *path)
{
    effect_t *effect = (effect_t*)handle;
    return MakePluginStatePath(effect->instance, effect->state_dir, path);
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

        if (g_verbose_debug) {
            printf("DEBUG: CCDataUpdate received %f as value of portsymbol %s (with prev value as %f)\n",
                   data->value, assignment->port->symbol, assignment->port->prev_value);
            fflush(stdout);
        }

        // invert value if bypass
        if ((is_bypass = !strcmp(assignment->port->symbol, g_bypass_port_symbol)))
            data->value = 1.0f - data->value;

        // ignore requests for same value
        if (!floats_differ_enough(assignment->port->prev_value, data->value))
        {
            if (g_verbose_debug) {
                puts("DEBUG: CCDataUpdate ignoring value change (matches previous value)");
                fflush(stdout);
            }
            continue;
        }

        if (SetPortValue(assignment->port, data->value, assignment->effect_id, is_bypass, false))
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

static bool CheckCCDeviceProtocolVersion(int device_id, int major, int minor)
{
    char *descriptor = cc_client_device_descriptor(g_cc_client, device_id);

    if (!descriptor)
    {
        printf("CheckCCDeviceProtocolVersion error: failed to get device descriptor with id %i\n", device_id);
        return false;
    }

    // we quickly parse the json ourselves in order to prevent adding more dependencies into mod-host
    const char *vsplit, *vstart, *vend;

    vsplit = strstr(descriptor, "\"protocol\":");
    if (!vsplit)
    {
        puts("CheckCCDeviceProtocolVersion error: failed to find protocol field");
        goto free;
    }

    vstart = strstr(vsplit+10, "\"");
    if (!vstart)
    {
        puts("CheckCCDeviceProtocolVersion error: failed to find protocol field start");
        goto free;
    }
    ++vstart;

    vend = strstr(vstart, "\"");
    if (!vend)
    {
        puts("CheckCCDeviceProtocolVersion error: failed to find protocol field end");
        goto free;
    }

    char buf_major[8] = {0};
    char buf_minor[8] = {0};
    char *buf = buf_major;

    for (int i=0; i<7; ++vstart)
    {
        if (*vstart >= '0' && *vstart <= '9')
        {
            buf[i++] = *vstart;
            continue;
        }

        if (*vstart == '.' && buf == buf_major)
        {
            i = 0;
            buf = buf_minor;
            continue;
        }

        break;
    }

    const int device_major = atoi(buf_major);
    const int device_minor = atoi(buf_minor);

    free(descriptor);
    return device_major >= major && device_minor >= minor;

free:
    free(descriptor);
    return false;
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

#ifdef WITH_EXTERNAL_UI_SUPPORT
static void ExternalControllerWriteFunction(LV2UI_Controller controller,
                                            uint32_t port_index,
                                            uint32_t buffer_size,
                                            uint32_t port_protocol,
                                            const void *buffer)
{
    if (port_protocol != 0)
    {
        printf("ExternalControllerWriteFunction %p %u %u %u %p\n", controller, port_index, buffer_size, port_protocol, buffer);
        return;
    }

    effect_t *effect = (effect_t*)controller;

    if (port_index >= effect->ports_count)
        return;

    port_t *port = effect->ports[port_index];
    const float value = *((const float*)buffer);

    if (port->type != TYPE_CONTROL || port->flow != FLOW_INPUT)
        return;

    // ignore requests for same value
    if (!floats_differ_enough(port->prev_value, value))
        return;

    if (SetPortValue(port, value, effect->instance, false, true))
        sem_post(&g_postevents_semaphore);
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

    /* Register jack ports */
    g_midi_in_port = jack_port_register(g_jack_global_client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (! g_midi_in_port)
    {
        fprintf(stderr, "can't register global jack midi-in port\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_JACK_PORT_REGISTER;
    }

#ifdef MOD_IO_PROCESSING_ENABLED
    g_audio_in1_port = jack_port_register(g_jack_global_client, "in1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    g_audio_in2_port = jack_port_register(g_jack_global_client, "in2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    g_audio_out1_port = jack_port_register(g_jack_global_client, "out1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    g_audio_out2_port = jack_port_register(g_jack_global_client, "out2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (! (g_audio_in1_port && g_audio_in2_port && g_audio_out1_port && g_audio_out2_port))
    {
        fprintf(stderr, "can't register global jack audio ports\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_JACK_PORT_REGISTER;
    }

    jack_port_tie(g_audio_in1_port, g_audio_out1_port);
    jack_port_tie(g_audio_in2_port, g_audio_out2_port);
#endif

    if (g_jack_global_client != NULL && strcmp(jack_get_client_name(g_jack_global_client), "mod-host") == 0 && ! monitor_client_init())
    {
        return ERR_JACK_CLIENT_CREATION;
    }

    memset(g_effects, 0, sizeof(g_effects));

    INIT_LIST_HEAD(&g_rtsafe_list);
    INIT_LIST_HEAD(&g_raw_midi_port_list);

    if (!rtsafe_memory_pool_create(&g_rtsafe_mem_pool, "mod-host", sizeof(postponed_event_list_data),
                                   MAX_POSTPONED_EVENTS))
    {
        fprintf(stderr, "can't allocate realtime-safe memory pool\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_MEMORY_ALLOCATION;
    }

    pthread_mutexattr_t mutex_atts;
    pthread_mutexattr_init(&mutex_atts);
#ifdef __MOD_DEVICES__
    pthread_mutexattr_setprotocol(&mutex_atts, PTHREAD_PRIO_INHERIT);
#endif

    pthread_mutex_init(&g_rtsafe_mutex, &mutex_atts);
    pthread_mutex_init(&g_raw_midi_port_mutex, &mutex_atts);
    pthread_mutex_init(&g_audio_monitor_mutex, &mutex_atts);
    pthread_mutex_init(&g_midi_learning_mutex, &mutex_atts);
#ifdef __MOD_DEVICES__
    pthread_mutex_init(&g_hmi_mutex, &mutex_atts);
#endif

    sem_init(&g_postevents_semaphore, 0, 0);

    /* Get the system ports */
    g_capture_ports = jack_get_ports(g_jack_global_client, "system", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
    g_playback_ports = jack_get_ports(g_jack_global_client, "system", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);

    /* Get buffers size */
    g_block_length = jack_get_buffer_size(g_jack_global_client);
    g_sample_rate = jack_get_sample_rate(g_jack_global_client);
    g_sample_rate_f = g_sample_rate;
    g_midi_buffer_size = jack_port_type_get_buffer_size(g_jack_global_client, JACK_DEFAULT_MIDI_TYPE);
    g_max_allowed_midi_delta = (jack_nframes_t)(g_sample_rate * 0.2); // max 200ms of allowed delta

    /* Get RT thread information */
    const char* const mod_plugthreadprio = getenv("MOD_PLUGIN_THREAD_PRIORITY");
    if (mod_plugthreadprio != NULL)
    {
        g_thread_priority = atoi(mod_plugthreadprio);
        g_thread_policy = SCHED_FIFO;
    }
    else
    {
        const int prio = jack_is_realtime(g_jack_global_client)
                       ? jack_client_real_time_priority(g_jack_global_client)
                       : -1;
        if (prio > 1)
        {
            g_thread_priority = prio - 1;
            g_thread_policy = SCHED_FIFO;
        }
        else
        {
            g_thread_priority = 0;
            g_thread_policy = SCHED_OTHER;
        }
    }

    /* initial transport state */
    g_transport_reset = true;
    g_transport_bpb = 4.0;
    g_transport_bpm = 120.0;
    g_transport_sync_mode = TRANSPORT_SYNC_NONE;

    /* check verbose mode */
    const char* const mod_log = getenv("MOD_LOG");
    g_verbose_debug = mod_log != NULL && atoi(mod_log) != 0;

    /* this fails to build if GLOBAL_EFFECT_ID >= MAX_INSTANCES */
    char global_effect_id_static_check1[GLOBAL_EFFECT_ID >= MAX_PLUGIN_INSTANCES?1:-1];
    char global_effect_id_static_check2[GLOBAL_EFFECT_ID < MAX_INSTANCES?1:-1];

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
        pthread_mutex_init(&port_bpb->cv_source_mutex, &mutex_atts);

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
        pthread_mutex_init(&port_bpm->cv_source_mutex, &mutex_atts);

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
        pthread_mutex_init(&port_rolling->cv_source_mutex, &mutex_atts);

        effect_t *effect = &g_effects[GLOBAL_EFFECT_ID];

        effect->instance = GLOBAL_EFFECT_ID;
        effect->jack_client = g_jack_global_client;

        effect->ports = effect->control_ports = effect->input_control_ports = ports;
        effect->ports_count = effect->control_ports_count = effect->input_control_ports_count = 3;

        effect->control_index = -1;
        effect->enabled_index = -1;
        effect->freewheel_index = -1;
        effect->reset_index = -1;
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
        pthread_mutex_init(&effect->bypass_port.cv_source_mutex, &mutex_atts);

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
        pthread_mutex_init(&effect->presets_port.cv_source_mutex, &mutex_atts);
    }

    pthread_mutexattr_destroy(&mutex_atts);

    /* Set jack callbacks */
    jack_set_thread_init_callback(g_jack_global_client, JackThreadInit, NULL);
    jack_set_timebase_callback(g_jack_global_client, 1, JackTimebase, NULL);
    jack_set_process_callback(g_jack_global_client, ProcessGlobalClient, NULL);
    jack_set_buffer_size_callback(g_jack_global_client, BufferSize, NULL);
    jack_set_port_registration_callback(g_jack_global_client, PortRegistration, NULL);
    jack_set_xrun_callback(g_jack_global_client, XRun, NULL);

#ifdef HAVE_HYLIA
    /* Init hylia */
    g_hylia_instance = hylia_create();
    memset(&g_hylia_timeinfo, 0, sizeof(g_hylia_timeinfo));

    if (g_hylia_instance)
    {
        hylia_set_beats_per_bar(g_hylia_instance, g_transport_bpb);
        hylia_set_beats_per_minute(g_hylia_instance, g_transport_bpm);
        hylia_set_output_latency(g_hylia_instance, GetHyliaOutputLatency());
    }
#endif

    /* Load all LV2 data */
    g_lv2_data = lilv_world_new();
    lilv_world_load_all(g_lv2_data);
    g_plugins = lilv_world_get_all_plugins(g_lv2_data);

    /* Lilv Nodes initialization */
    g_lilv_nodes.atom_port = lilv_new_uri(g_lv2_data, LV2_ATOM__AtomPort);
    g_lilv_nodes.audio = lilv_new_uri(g_lv2_data, LILV_URI_AUDIO_PORT);
    g_lilv_nodes.control = lilv_new_uri(g_lv2_data, LILV_URI_CONTROL_PORT);
    g_lilv_nodes.control_in = lilv_new_uri(g_lv2_data, LV2_CORE__control);
    g_lilv_nodes.cv = lilv_new_uri(g_lv2_data, LILV_URI_CV_PORT);
    g_lilv_nodes.default_ = lilv_new_uri(g_lv2_data, LV2_CORE__default);
    g_lilv_nodes.enabled = lilv_new_uri(g_lv2_data, LV2_CORE__enabled);
    g_lilv_nodes.enumeration = lilv_new_uri(g_lv2_data, LV2_CORE__enumeration);
    g_lilv_nodes.event = lilv_new_uri(g_lv2_data, LILV_URI_EVENT_PORT);
    g_lilv_nodes.freeWheeling = lilv_new_uri(g_lv2_data, LV2_CORE__freeWheeling);
    g_lilv_nodes.hmi_interface = lilv_new_uri(g_lv2_data, LV2_HMI__PluginNotification);
    g_lilv_nodes.input = lilv_new_uri(g_lv2_data, LILV_URI_INPUT_PORT);
    g_lilv_nodes.integer = lilv_new_uri(g_lv2_data, LV2_CORE__integer);
    g_lilv_nodes.license_interface = lilv_new_uri(g_lv2_data, MOD_LICENSE__interface);
    g_lilv_nodes.is_live = lilv_new_uri(g_lv2_data, LV2_CORE__isLive);
    g_lilv_nodes.logarithmic = lilv_new_uri(g_lv2_data, LV2_PORT_PROPS__logarithmic);
    g_lilv_nodes.maximum = lilv_new_uri(g_lv2_data, LV2_CORE__maximum);
    g_lilv_nodes.midiEvent = lilv_new_uri(g_lv2_data, LV2_MIDI__MidiEvent);
    g_lilv_nodes.minimum = lilv_new_uri(g_lv2_data, LV2_CORE__minimum);
    g_lilv_nodes.minimumSize = lilv_new_uri(g_lv2_data, LV2_RESIZE_PORT__minimumSize);
    g_lilv_nodes.mod_cvport = lilv_new_uri(g_lv2_data, LILV_NS_MOD "CVPort");
    g_lilv_nodes.mod_default = lilv_new_uri(g_lv2_data, LILV_NS_MOD "default");
#if defined(_MOD_DEVICE_DUO)
    g_lilv_nodes.mod_default_custom = lilv_new_uri(g_lv2_data, LILV_NS_MOD "default_duo");
#elif defined(_MOD_DEVICE_DUOX)
    g_lilv_nodes.mod_default_custom = lilv_new_uri(g_lv2_data, LILV_NS_MOD "default_duox");
#elif defined(_MOD_DEVICE_DWARF)
    g_lilv_nodes.mod_default_custom = lilv_new_uri(g_lv2_data, LILV_NS_MOD "default_dwarf");
#elif defined(_MOD_DEVICE_X86_64)
    g_lilv_nodes.mod_default_custom = lilv_new_uri(g_lv2_data, LILV_NS_MOD "default_x64");
#else
    g_lilv_nodes.mod_default_custom = NULL;
#endif
    g_lilv_nodes.mod_maximum = lilv_new_uri(g_lv2_data, LILV_NS_MOD "maximum");
    g_lilv_nodes.mod_minimum = lilv_new_uri(g_lv2_data, LILV_NS_MOD "minimum");
    g_lilv_nodes.options_interface = lilv_new_uri(g_lv2_data, LV2_OPTIONS__interface);
    g_lilv_nodes.output = lilv_new_uri(g_lv2_data, LILV_URI_OUTPUT_PORT);
    g_lilv_nodes.patch_writable = lilv_new_uri(g_lv2_data, LV2_PATCH__writable);
    g_lilv_nodes.patch_readable = lilv_new_uri(g_lv2_data, LV2_PATCH__readable);
    g_lilv_nodes.preferMomentaryOff = lilv_new_uri(g_lv2_data, LILV_NS_MOD "preferMomentaryOffByDefault");
    g_lilv_nodes.preferMomentaryOn = lilv_new_uri(g_lv2_data, LILV_NS_MOD "preferMomentaryOnByDefault");
    g_lilv_nodes.preset = lilv_new_uri(g_lv2_data, LV2_PRESETS__Preset);
    g_lilv_nodes.rawMIDIClockAccess = lilv_new_uri(g_lv2_data, LILV_NS_MOD "rawMIDIClockAccess");
    g_lilv_nodes.rdfs_range = lilv_new_uri(g_lv2_data, LILV_NS_RDFS "range");
    g_lilv_nodes.reset = lilv_new_uri(g_lv2_data, LV2_KXSTUDIO_PROPERTIES__Reset);
    g_lilv_nodes.sample_rate = lilv_new_uri(g_lv2_data, LV2_CORE__sampleRate);
    g_lilv_nodes.state_interface = lilv_new_uri(g_lv2_data, LV2_STATE__interface);
    g_lilv_nodes.state_load_default_state = lilv_new_uri(g_lv2_data, LV2_STATE__loadDefaultState);
    g_lilv_nodes.state_thread_safe_restore = lilv_new_uri(g_lv2_data, LV2_STATE__threadSafeRestore);
    g_lilv_nodes.timeBeatsPerBar = lilv_new_uri(g_lv2_data, LV2_TIME__beatsPerBar);
    g_lilv_nodes.timeBeatsPerMinute = lilv_new_uri(g_lv2_data, LV2_TIME__beatsPerMinute);
    g_lilv_nodes.timePosition = lilv_new_uri(g_lv2_data, LV2_TIME__Position);
    g_lilv_nodes.timeSpeed = lilv_new_uri(g_lv2_data, LV2_TIME__speed);
    g_lilv_nodes.toggled = lilv_new_uri(g_lv2_data, LV2_CORE__toggled);
    g_lilv_nodes.trigger = lilv_new_uri(g_lv2_data, LV2_PORT_PROPS__trigger);
    g_lilv_nodes.worker_interface = lilv_new_uri(g_lv2_data, LV2_WORKER__interface);

    /* URI and URID Feature initialization */
    urid_sem_init();
    g_symap = symap_new();

    g_uri_map.callback_data = g_symap;
    g_uri_map.uri_to_id = &uri_to_id;

    g_urid_map.handle = g_symap;
    g_urid_map.map = urid_to_id;
    g_urid_unmap.handle = g_symap;
    g_urid_unmap.unmap = id_to_urid;

    g_urids.atom_Double          = urid_to_id(g_symap, LV2_ATOM__Double);
    g_urids.atom_Bool            = urid_to_id(g_symap, LV2_ATOM__Bool);
    g_urids.atom_Float           = urid_to_id(g_symap, LV2_ATOM__Float);
    g_urids.atom_Int             = urid_to_id(g_symap, LV2_ATOM__Int);
    g_urids.atom_Long            = urid_to_id(g_symap, LV2_ATOM__Long);
    g_urids.atom_Object          = urid_to_id(g_symap, LV2_ATOM__Object);
    g_urids.atom_Path            = urid_to_id(g_symap, LV2_ATOM__Path);
    g_urids.atom_String          = urid_to_id(g_symap, LV2_ATOM__String);
    g_urids.atom_Tuple           = urid_to_id(g_symap, LV2_ATOM__Tuple);
    g_urids.atom_URI             = urid_to_id(g_symap, LV2_ATOM__URI);
    g_urids.atom_Vector          = urid_to_id(g_symap, LV2_ATOM__Vector);
    g_urids.atom_eventTransfer   = urid_to_id(g_symap, LV2_ATOM__eventTransfer);

    g_urids.bufsz_maxBlockLength     = urid_to_id(g_symap, LV2_BUF_SIZE__maxBlockLength);
    g_urids.bufsz_minBlockLength     = urid_to_id(g_symap, LV2_BUF_SIZE__minBlockLength);
    g_urids.bufsz_nomimalBlockLength = urid_to_id(g_symap, LV2_BUF_SIZE__nominalBlockLength);
    g_urids.bufsz_sequenceSize   = urid_to_id(g_symap, LV2_BUF_SIZE__sequenceSize);

    g_urids.jack_client          = urid_to_id(g_symap, "http://jackaudio.org/metadata/client");

    g_urids.log_Error            = urid_to_id(g_symap, LV2_LOG__Error);
    g_urids.log_Note             = urid_to_id(g_symap, LV2_LOG__Note);
    g_urids.log_Trace            = urid_to_id(g_symap, LV2_LOG__Trace);
    g_urids.log_Warning          = urid_to_id(g_symap, LV2_LOG__Warning);

    g_urids.midi_MidiEvent       = urid_to_id(g_symap, LV2_MIDI__MidiEvent);
    g_urids.param_sampleRate     = urid_to_id(g_symap, LV2_PARAMETERS__sampleRate);

    g_urids.patch_Get            = urid_to_id(g_symap, LV2_PATCH__Get);
    g_urids.patch_Set            = urid_to_id(g_symap, LV2_PATCH__Set);
    g_urids.patch_property       = urid_to_id(g_symap, LV2_PATCH__property);
    g_urids.patch_sequence       = urid_to_id(g_symap, LV2_PATCH__sequenceNumber);
    g_urids.patch_value          = urid_to_id(g_symap, LV2_PATCH__value);

    g_urids.time_Position        = urid_to_id(g_symap, LV2_TIME__Position);
    g_urids.time_bar             = urid_to_id(g_symap, LV2_TIME__bar);
    g_urids.time_barBeat         = urid_to_id(g_symap, LV2_TIME__barBeat);
    g_urids.time_beat            = urid_to_id(g_symap, LV2_TIME__beat);
    g_urids.time_beatUnit        = urid_to_id(g_symap, LV2_TIME__beatUnit);
    g_urids.time_beatsPerBar     = urid_to_id(g_symap, LV2_TIME__beatsPerBar);
    g_urids.time_beatsPerMinute  = urid_to_id(g_symap, LV2_TIME__beatsPerMinute);
    g_urids.time_ticksPerBeat    = urid_to_id(g_symap, LV2_KXSTUDIO_PROPERTIES__TimePositionTicksPerBeat);
    g_urids.time_frame           = urid_to_id(g_symap, LV2_TIME__frame);
    g_urids.time_speed           = urid_to_id(g_symap, LV2_TIME__speed);

    g_urids.threads_schedPolicy   = urid_to_id(g_symap, "http://ardour.org/lv2/threads/#schedPolicy");
    g_urids.threads_schedPriority = urid_to_id(g_symap, "http://ardour.org/lv2/threads/#schedPriority");

    /* Options Feature initialization */
    g_options[0].context = LV2_OPTIONS_INSTANCE;
    g_options[0].subject = 0;
    g_options[0].key = g_urids.param_sampleRate;
    g_options[0].size = sizeof(float);
    g_options[0].type = g_urids.atom_Float;
    g_options[0].value = &g_sample_rate_f;

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
    g_options[5].key = g_urids.threads_schedPolicy;
    g_options[5].size = sizeof(int32_t);
    g_options[5].type = g_urids.atom_Int;
    g_options[5].value = &g_thread_policy;

    g_options[6].context = LV2_OPTIONS_INSTANCE;
    g_options[6].subject = 0;
    g_options[6].key = g_urids.threads_schedPriority;
    g_options[6].size = sizeof(int32_t);
    g_options[6].type = g_urids.atom_Int;
    g_options[6].value = &g_thread_priority;

    g_options[7].context = LV2_OPTIONS_INSTANCE;
    g_options[7].subject = 0;
    g_options[7].key = g_urids.jack_client;
    g_options[7].size = sizeof(jack_client_t*);
    g_options[7].type = g_urids.jack_client;
    g_options[7].value = NULL;

    g_options[8].context = LV2_OPTIONS_INSTANCE;
    g_options[8].subject = 0;
    g_options[8].key = 0;
    g_options[8].size = 0;
    g_options[8].type = 0;
    g_options[8].value = NULL;

#ifdef __MOD_DEVICES__
    g_hmi_wc.size                    = sizeof(g_hmi_wc);
    g_hmi_wc.set_led_with_blink      = HMIWidgetsSetLedWithBlink;
    g_hmi_wc.set_led_with_brightness = HMIWidgetsSetLedWithBrightness;
    g_hmi_wc.set_label               = HMIWidgetsSetLabel;
    g_hmi_wc.set_value               = HMIWidgetsSetValue;
    g_hmi_wc.set_unit                = HMIWidgetsSetUnit;
    g_hmi_wc.set_indicator           = HMIWidgetsSetIndicator;
    g_hmi_wc.popup_message           = HMIWidgetsPopupMessage;

    if (client != NULL)
    {
        // HMI integration setup
        if (sys_serial_open(&g_hmi_shmfd, &g_hmi_data))
        {
            g_hmi_wc.handle = g_hmi_data;
            sys_serial_write(&g_hmi_data->server, sys_serial_event_type_special_req, 0, 0, "restart");
            pthread_create(&g_hmi_client_thread, NULL, HMIClientThread, &g_hmi_data->client);
        }
        else
        {
            g_hmi_wc.handle = NULL;
            fprintf(stderr, "sys_host HMI setup failed\n");
        }
    }

    for (int i = 0; i < MAX_HMI_ADDRESSINGS; i++)
        g_hmi_addressings[i].actuator_id = -1;

#ifdef MOD_IO_PROCESSING_ENABLED
    gate_init(&g_noisegate);
#endif
#endif

    g_license.handle = NULL;
    g_license.license = GetLicenseFile;
    g_license.free = FreePluginString;

    g_lv2_log.handle = NULL;
    g_lv2_log.printf = LogPrintf;
    g_lv2_log.vprintf = LogVPrintf;

    g_state_freePath.handle = NULL;
    g_state_freePath.free_path = FreePluginString;

    g_lv2_scratch_dir = str_duplicate("/tmp/mod-host-scratch-dir");

    lv2_atom_forge_init(&g_lv2_atom_forge, &g_urid_map);

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

    memset(g_monitored_midi_controls, 0, sizeof(g_monitored_midi_controls));
    memset(g_monitored_midi_programs, 0, sizeof(g_monitored_midi_programs));

#ifdef HAVE_CONTROLCHAIN
    /* Init the control chain variables */
    memset(&g_assignments_list, 0, sizeof(g_assignments_list));

    for (int i = 0; i < CC_MAX_DEVICES; i++)
    {
        for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
        {
            g_assignments_list[i][j].effect_id = ASSIGNMENT_NULL;
            g_assignments_list[i][j].assignment_pair_id = -1;
        }
    }
#endif

    /* Start the thread that consumes from the event queue */
    g_postevents_running = 1;
    g_postevents_ready = true;
    pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);

    /* get transport state */
    UpdateGlobalJackPosition(UPDATE_POSITION_SKIP, false);

    /* Try activate the jack global client */
    if (jack_activate(g_jack_global_client) != 0)
    {
        fprintf(stderr, "can't activate global jack client\n");
        if (client == NULL)
            jack_client_close(g_jack_global_client);
        return ERR_JACK_CLIENT_ACTIVATION;
    }

    /* Connect to midi-merger if avaiable */
    g_aggregated_midi_enabled = jack_port_by_name(g_jack_global_client, "mod-midi-merger:out") != NULL;

    if (g_aggregated_midi_enabled)
    {
        const char *ourportname = jack_port_name(g_midi_in_port);
        jack_connect(g_jack_global_client, "mod-midi-merger:out", ourportname);
        ConnectToMIDIThroughPorts();
    }
    /* Else connect to all good hw ports (system, ttymidi and nooice) */
    else
    {
        ConnectToAllHardwareMIDIPorts();
    }

#ifdef MOD_IO_PROCESSING_ENABLED
    /* Connect to capture ports if avaiable */
    if (g_capture_ports != NULL && g_capture_ports[0] != NULL)
    {
        const char *ourportname;
#ifndef _MOD_DEVICE_DWARF
        ourportname = jack_port_name(g_audio_in1_port);
#else
        ourportname = jack_port_name(g_audio_in2_port);
#endif
        jack_connect(g_jack_global_client, g_capture_ports[0], ourportname);

        if (g_capture_ports[1] != NULL)
        {
#ifndef _MOD_DEVICE_DWARF
            ourportname = jack_port_name(g_audio_in2_port);
#else
            ourportname = jack_port_name(g_audio_in1_port);
#endif
            jack_connect(g_jack_global_client, g_capture_ports[1], ourportname);
        }
    }
#endif

    g_processing_enabled = true;

    return SUCCESS;

    UNUSED_PARAM(global_effect_id_static_check1);
    UNUSED_PARAM(global_effect_id_static_check2);
}

int effects_finish(int close_client)
{
    g_postevents_running = -1;
    sem_post(&g_postevents_semaphore);
    pthread_join(g_postevents_thread, NULL);

    if (close_client)
        monitor_client_stop();

    effects_remove(REMOVE_ALL);

#ifdef __MOD_DEVICES__
    if (g_hmi_data != NULL)
    {
        sys_serial_shm_data* hmi_data = g_hmi_data;
        g_hmi_data = NULL;

        sem_post(&hmi_data->client.sem);
        pthread_join(g_hmi_client_thread, NULL);

        sys_serial_close(g_hmi_shmfd, hmi_data);
    }
#endif

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
    lilv_node_free(g_lilv_nodes.atom_port);
    lilv_node_free(g_lilv_nodes.audio);
    lilv_node_free(g_lilv_nodes.control);
    lilv_node_free(g_lilv_nodes.control_in);
    lilv_node_free(g_lilv_nodes.cv);
    lilv_node_free(g_lilv_nodes.default_);
    lilv_node_free(g_lilv_nodes.enabled);
    lilv_node_free(g_lilv_nodes.enumeration);
    lilv_node_free(g_lilv_nodes.event);
    lilv_node_free(g_lilv_nodes.freeWheeling);
    lilv_node_free(g_lilv_nodes.hmi_interface);
    lilv_node_free(g_lilv_nodes.input);
    lilv_node_free(g_lilv_nodes.integer);
    lilv_node_free(g_lilv_nodes.license_interface);
    lilv_node_free(g_lilv_nodes.logarithmic);
    lilv_node_free(g_lilv_nodes.is_live);
    lilv_node_free(g_lilv_nodes.maximum);
    lilv_node_free(g_lilv_nodes.midiEvent);
    lilv_node_free(g_lilv_nodes.minimum);
    lilv_node_free(g_lilv_nodes.minimumSize);
    lilv_node_free(g_lilv_nodes.mod_cvport);
    lilv_node_free(g_lilv_nodes.mod_default);
    lilv_node_free(g_lilv_nodes.mod_default_custom);
    lilv_node_free(g_lilv_nodes.mod_maximum);
    lilv_node_free(g_lilv_nodes.mod_minimum);
    lilv_node_free(g_lilv_nodes.output);
    lilv_node_free(g_lilv_nodes.patch_readable);
    lilv_node_free(g_lilv_nodes.patch_writable);
    lilv_node_free(g_lilv_nodes.preferMomentaryOff);
    lilv_node_free(g_lilv_nodes.preferMomentaryOn);
    lilv_node_free(g_lilv_nodes.preset);
    lilv_node_free(g_lilv_nodes.rawMIDIClockAccess);
    lilv_node_free(g_lilv_nodes.rdfs_range);
    lilv_node_free(g_lilv_nodes.reset);
    lilv_node_free(g_lilv_nodes.sample_rate);
    lilv_node_free(g_lilv_nodes.state_interface);
    lilv_node_free(g_lilv_nodes.state_load_default_state);
    lilv_node_free(g_lilv_nodes.state_thread_safe_restore);
    lilv_node_free(g_lilv_nodes.timeBeatsPerBar);
    lilv_node_free(g_lilv_nodes.timeBeatsPerMinute);
    lilv_node_free(g_lilv_nodes.timePosition);
    lilv_node_free(g_lilv_nodes.timeSpeed);
    lilv_node_free(g_lilv_nodes.toggled);
    lilv_node_free(g_lilv_nodes.trigger);
    lilv_node_free(g_lilv_nodes.worker_interface);
    lilv_world_free(g_lv2_data);
    rtsafe_memory_pool_destroy(g_rtsafe_mem_pool);
    sem_destroy(&g_postevents_semaphore);
    pthread_mutex_destroy(&g_rtsafe_mutex);
    pthread_mutex_destroy(&g_raw_midi_port_mutex);
    pthread_mutex_destroy(&g_audio_monitor_mutex);
    pthread_mutex_destroy(&g_midi_learning_mutex);
#ifdef __MOD_DEVICES__
    pthread_mutex_destroy(&g_hmi_mutex);
#endif

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

    free(g_lv2_scratch_dir);
    g_lv2_scratch_dir = NULL;

    g_processing_enabled = false;

    return SUCCESS;
}

int effects_add(const char *uri, int instance, int activate)
{
    unsigned int ports_count;
    char effect_name[32], port_name[MAX_CHAR_BUF_SIZE+1];
    float *audio_buffer, *cv_buffer, *control_buffer;
    jack_port_t *jack_port;
    uint32_t audio_ports_count, input_audio_ports_count, output_audio_ports_count;
    uint32_t control_ports_count, input_control_ports_count, output_control_ports_count;
    uint32_t cv_ports_count, input_cv_ports_count, output_cv_ports_count;
    uint32_t event_ports_count, input_event_ports_count, output_event_ports_count;
    effect_t *effect;
    port_t *port;
    int32_t error;

    effect_name[31] = '\0';
    port_name[MAX_CHAR_BUF_SIZE] = '\0';

    /* Jack */
    jack_client_t *jack_client;
    jack_status_t jack_status;
    unsigned long jack_flags = 0;
    jack_port_t *raw_midi_port = NULL;

    /* Lilv */
    const LilvPlugin *plugin;
    LilvInstance *lilv_instance;
    LilvNode *plugin_uri;
    const LilvPort *control_in_port;
    const LilvPort *lilv_port;
    const LilvNode *symbol_node;
    uint32_t control_in_size, control_out_size, worker_buf_size;

    if (!uri) return ERR_LV2_INVALID_URI;
    if (!INSTANCE_IS_VALID(instance)) return ERR_INSTANCE_INVALID;
    if (InstanceExist(instance)) return ERR_INSTANCE_ALREADY_EXISTS;

    effect = &g_effects[instance];

    /* Init the struct */
    mod_memset(effect, 0, sizeof(effect_t));
    effect->instance = instance;
    effect->activated = activate;

    /* Init the pointers */
    plugin_uri = NULL;
    lilv_instance = NULL;

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
    plugin_uri = lilv_new_uri(g_lv2_data, uri);
    plugin = lilv_plugins_get_by_uri(g_plugins, plugin_uri);

    if (!plugin)
    {
        // NOTE: Reloading the entire world is nasty!
        //       It may result in crashes, and we now have a way to add/remove bundles as needed anyway.
#if 0
        /* If the plugin are not found reload all plugins */
        lilv_world_load_all(g_lv2_data);
        g_plugins = lilv_world_get_all_plugins(g_lv2_data);

        /* Try get the plugin again */
        plugin = lilv_plugins_get_by_uri(g_plugins, plugin_uri);

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

    pthread_mutexattr_t mutex_atts;
    pthread_mutexattr_init(&mutex_atts);
#ifdef __MOD_DEVICES__
    pthread_mutexattr_setprotocol(&mutex_atts, PTHREAD_PRIO_INHERIT);
#endif

    /* Create and activate the plugin instance */
    lilv_instance = lilv_plugin_instantiate(plugin, g_sample_rate, effect->features);

    if (!lilv_instance)
    {
        fprintf(stderr, "can't get lilv instance\n");
        error = ERR_LV2_INSTANTIATION;
        goto error;
    }
    effect->lilv_instance = lilv_instance;

    /* query control_in port and its minimum size */
    control_out_size = 0;
    worker_buf_size = 4096;
    control_in_port = lilv_plugin_get_port_by_designation(plugin, g_lilv_nodes.input, g_lilv_nodes.control_in);
    if (control_in_port)
    {
        control_in_size = g_midi_buffer_size * 16; // 16 taken from jalv source code
        effect->control_index = lilv_port_get_index(plugin, control_in_port);

        LilvNodes *lilvminsize = lilv_port_get_value(plugin, control_in_port, g_lilv_nodes.minimumSize);
        if (lilvminsize != NULL)
        {
            const int minsize = lilv_node_as_int(lilv_nodes_get_first(lilvminsize));
            if (minsize > 0 && (uint)minsize > worker_buf_size)
                worker_buf_size = minsize;
        }
    }
    else
    {
        control_in_size = 0;
        effect->control_index = -1;
    }

    /* Query plugin features */
    if (lilv_plugin_has_feature(effect->lilv_plugin, g_lilv_nodes.is_live))
        effect->hints |= HINT_IS_LIVE;

    /* Query plugin extensions/interfaces */
    if (lilv_plugin_has_extension_data(effect->lilv_plugin, g_lilv_nodes.worker_interface))
    {
        const LV2_Worker_Interface *worker_interface =
            (const LV2_Worker_Interface*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                           LV2_WORKER__interface);

        worker_init(&effect->worker, lilv_instance, worker_interface, worker_buf_size);
    }

    if (lilv_plugin_has_extension_data(effect->lilv_plugin, g_lilv_nodes.options_interface))
    {
        effect->options_interface =
            (const LV2_Options_Interface*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                            LV2_OPTIONS__interface);
    }

    if (lilv_plugin_has_extension_data(effect->lilv_plugin, g_lilv_nodes.license_interface))
    {
        effect->license_iface =
            (const MOD_License_Interface*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                            MOD_LICENSE__interface);
    }

    if (lilv_plugin_has_extension_data(effect->lilv_plugin, g_lilv_nodes.state_interface))
    {
        effect->state_iface =
            (const LV2_State_Interface*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                          LV2_STATE__interface);
        effect->hints |= HINT_HAS_STATE;

        if (! lilv_plugin_has_feature(effect->lilv_plugin, g_lilv_nodes.state_thread_safe_restore))
        {
            effect->hints |= HINT_STATE_UNSAFE;
            pthread_mutex_init(&effect->state_restore_mutex, &mutex_atts);
        }

        if (lilv_plugin_has_feature(effect->lilv_plugin, g_lilv_nodes.state_load_default_state))
        {
            LilvState *state = lilv_state_new_from_world(g_lv2_data, &g_urid_map, plugin_uri);

            if (state != NULL) {
                lilv_state_restore(state, effect->lilv_instance, NULL, NULL,
                                   LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE, effect->features);
                lilv_state_free(state);
            }
        }
    }

#ifdef __MOD_DEVICES__
    if (lilv_plugin_has_extension_data(effect->lilv_plugin, g_lilv_nodes.hmi_interface))
    {
        effect->hmi_notif =
            (const LV2_HMI_PluginNotification*) lilv_instance_get_extension_data(effect->lilv_instance,
                                                                                 LV2_HMI__PluginNotification);
    }
#endif

    /* Create the URI for identify the ports */
    ports_count = lilv_plugin_get_num_ports(plugin);

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
    worker_buf_size = 0;
    effect->presets_count = 0;
    effect->presets = NULL;
    effect->monitors_count = 0;
    effect->monitors = NULL;
    effect->ports_count = ports_count;
    effect->ports = (port_t **) mod_calloc(ports_count, sizeof(port_t *));

    for (unsigned int i = 0; i < ports_count; i++)
    {
        /* Allocate memory to current port */
        effect->ports[i] = port = (port_t *) mod_calloc(1, sizeof(port_t));

        pthread_mutex_init(&port->cv_source_mutex, &mutex_atts);

        /* Lilv port */
        lilv_port = lilv_plugin_get_port_by_index(plugin, i);
        symbol_node = lilv_port_get_symbol(plugin, lilv_port);
        port->index = i;
        port->symbol = lilv_node_as_string(symbol_node);

        snprintf(port_name, MAX_CHAR_BUF_SIZE, "%s", lilv_node_as_string(symbol_node));

        /* Port flow */
        port->flow = FLOW_UNKNOWN;
        if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input))
        {
            jack_flags = JackPortIsInput;
            port->flow = FLOW_INPUT;
        }
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output))
        {
            jack_flags = JackPortIsOutput;
            port->flow = FLOW_OUTPUT;
        }

        port->type = TYPE_UNKNOWN;
        port->hints = 0x0;

        if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.audio))
        {
            port->type = TYPE_AUDIO;

            /* Allocate memory to audio buffer */
            audio_buffer = (float *) mod_calloc(g_sample_rate, sizeof(float));
            if (!audio_buffer)
            {
                fprintf(stderr, "can't get audio buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }

            port->buffer = audio_buffer;
            port->buffer_count = g_sample_rate;
            lilv_instance_connect_port(lilv_instance, i, audio_buffer);

            /* Jack port creation */
            jack_port = jack_port_register(jack_client, port_name, JACK_DEFAULT_AUDIO_TYPE, jack_flags, 0);
            if (jack_port == NULL)
            {
                fprintf(stderr, "can't get jack port\n");
                error = ERR_JACK_PORT_REGISTER;
                goto error;
            }
            port->jack_port = jack_port;

            audio_ports_count++;
            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input)) input_audio_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output)) output_audio_ports_count++;
        }
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.control))
        {
            port->type = TYPE_CONTROL;

            /* Allocate memory to control port */
            control_buffer = (float *) malloc(sizeof(float));
            if (!control_buffer)
            {
                fprintf(stderr, "can't get control buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }
            port->buffer = control_buffer;
            port->buffer_count = 1;
            lilv_instance_connect_port(lilv_instance, i, control_buffer);

            port->scale_points = lilv_port_get_scale_points(plugin, lilv_port);

            /* Set the minimum value of control */
            float min_value;
            LilvNodes* lilvvalue_minimum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.mod_minimum);
            if (lilvvalue_minimum == NULL)
                lilvvalue_minimum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.minimum);

            if (lilvvalue_minimum != NULL)
                min_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_minimum));
            else
                min_value = 0.0f;

            /* Set the maximum value of control */
            float max_value;
            LilvNodes* lilvvalue_maximum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.mod_maximum);
            if (lilvvalue_maximum == NULL)
                lilvvalue_maximum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.maximum);

            if (lilvvalue_maximum != NULL)
                max_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_maximum));
            else
                max_value = 1.0f;

            /* Ensure min < max */
            if (min_value >= max_value)
                max_value = min_value + 0.1f;

            /* multiply ranges by sample rate if requested */
            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.sample_rate))
            {
                min_value *= g_sample_rate;
                max_value *= g_sample_rate;
            }

            /* Set the default value of control */
            float def_value;


            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.preferMomentaryOff))
            {
                def_value = max_value;
            }
            else if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.preferMomentaryOn))
            {
                def_value = min_value;
            }
            else
            {
                LilvNodes* lilvvalue_default = g_lilv_nodes.mod_default_custom != NULL
                                             ? lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.mod_default_custom)
                                             : NULL;
                if (lilvvalue_default == NULL)
                    lilvvalue_default = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.mod_default);
                if (lilvvalue_default == NULL)
                    lilvvalue_default = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.default_);

                if (lilvvalue_default != NULL)
                    def_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_default));
                else
                    def_value = min_value;

                lilv_nodes_free(lilvvalue_default);
            }

            (*control_buffer) = def_value;

            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.enumeration))
            {
                port->hints |= HINT_ENUMERATION;

                // make 2 scalepoint enumeration work as toggle
                if (lilv_scale_points_size(port->scale_points) == 2)
                    port->hints |= HINT_TOGGLE;
            }
            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.integer))
            {
                port->hints |= HINT_INTEGER;
            }
            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.toggled))
            {
                port->hints |= HINT_TOGGLE;
            }
            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.trigger))
            {
                port->hints |= HINT_TRIGGER;
                effect->hints |= HINT_TRIGGERS;
            }
            if (lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.logarithmic))
            {
                port->hints |= HINT_LOGARITHMIC;
            }

            port->jack_port = NULL;
            port->def_value = def_value;
            port->min_value = min_value;
            port->max_value = max_value;
            port->prev_value = def_value;

            control_ports_count++;
            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input)) input_control_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output)) output_control_ports_count++;

            lilv_nodes_free(lilvvalue_maximum);
            lilv_nodes_free(lilvvalue_minimum);
        }
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.cv) || lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.mod_cvport))
        {
            port->type = TYPE_CV;

            /* Allocate memory to cv buffer */
            cv_buffer = (float *) mod_calloc(g_sample_rate, sizeof(float));
            if (!cv_buffer)
            {
                fprintf(stderr, "can't get cv buffer\n");
                error = ERR_MEMORY_ALLOCATION;
                goto error;
            }

            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.mod_cvport))
                port->hints |= HINT_CV_MOD;

            port->buffer = cv_buffer;
            port->buffer_count = g_sample_rate;
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

            /* Set the minimum value of control */
            float min_value;
            LilvNodes* lilvvalue_minimum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.mod_minimum);
            if (lilvvalue_minimum == NULL)
                lilvvalue_minimum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.minimum);

            if (lilvvalue_minimum != NULL)
                min_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_minimum));
            else
                min_value = -5.0f;

            /* Set the maximum value of control */
            float max_value;
            LilvNodes* lilvvalue_maximum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.mod_maximum);
            if (lilvvalue_maximum == NULL)
                lilvvalue_maximum = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.maximum);

            if (lilvvalue_maximum != NULL)
                max_value = lilv_node_as_float(lilv_nodes_get_first(lilvvalue_maximum));
            else
                max_value = 5.0f;

            /* Ensure min < max */
            if (min_value >= max_value)
            {
                max_value = min_value + 0.1f;
            }
            else if (lilvvalue_minimum != NULL && lilvvalue_maximum != NULL)
            {
                // if range is valid, set metadata
                port->hints |= HINT_CV_RANGES;

                jack_uuid_t uuid = jack_port_uuid(jack_port);
                if (!jack_uuid_empty(uuid)) {
                    char str_value[32];
                    mod_memset(str_value, 0, sizeof(str_value));

                    snprintf(str_value, 31, "%f", min_value);
                    jack_set_property(jack_client, uuid, LV2_CORE__minimum, str_value, NULL);

                    snprintf(str_value, 31, "%f", max_value);
                    jack_set_property(jack_client, uuid, LV2_CORE__maximum, str_value, NULL);
                }
            }

            port->min_value = min_value;
            port->max_value = max_value;

            port->jack_port = jack_port;

            cv_ports_count++;
            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input)) input_cv_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output)) output_cv_ports_count++;

            lilv_nodes_free(lilvvalue_maximum);
            lilv_nodes_free(lilvvalue_minimum);
        }
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.event) ||
                    lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.atom_port))
        {
            port->type = TYPE_EVENT;
            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.event))
            {
                port->hints |= HINT_OLD_EVENT_API;
                port->hints |= HINT_MIDI_EVENT;
                effect->hints |= HINT_HAS_MIDI_INPUT;
            }
            else
            {
                if (lilv_port_supports_event(plugin, lilv_port, g_lilv_nodes.midiEvent))
                {
                    port->hints |= HINT_MIDI_EVENT;
                    effect->hints |= HINT_HAS_MIDI_INPUT;
                }
                if (lilv_port_supports_event(plugin, lilv_port, g_lilv_nodes.timePosition))
                {
                    port->hints |= HINT_TRANSPORT;
                    effect->hints |= HINT_TRANSPORT;
                }
            }

            if (port->flow == FLOW_OUTPUT && control_out_size == 0)
                control_out_size = g_midi_buffer_size * 16; // 16 taken from jalv source code

            LilvNodes *lilvminsize = lilv_port_get_value(plugin, lilv_port, g_lilv_nodes.minimumSize);
            if (lilvminsize != NULL)
            {
                const int iminsize = lilv_node_as_int(lilvminsize);
                if (iminsize > 0)
                {
                    const uint minsize = (uint)iminsize;
                    if (port->flow == FLOW_INPUT)
                    {
                        if (minsize > control_in_size)
                            control_in_size = minsize;
                    }
                    else if (port->flow == FLOW_OUTPUT)
                    {
                        if (minsize > control_out_size)
                            control_out_size = minsize;
                    }
                }
                lilv_nodes_free(lilvminsize);
            }

            jack_port = jack_port_register(jack_client, port_name, JACK_DEFAULT_MIDI_TYPE, jack_flags, 0);
            if (jack_port == NULL)
            {
                fprintf(stderr, "can't get jack port\n");
                error = ERR_JACK_PORT_REGISTER;
                goto error;
            }
            port->jack_port = jack_port;
            event_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input)) input_event_ports_count++;
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output)) output_event_ports_count++;

            if (raw_midi_port == NULL && lilv_port_has_property(plugin, lilv_port, g_lilv_nodes.rawMIDIClockAccess))
            {
                raw_midi_port_item* const portitemptr = malloc(sizeof(raw_midi_port_item));

                if (portitemptr != NULL)
                {
                    raw_midi_port = jack_port;
                    portitemptr->instance = instance;
                    portitemptr->jack_port = jack_port;

                    pthread_mutex_lock(&g_raw_midi_port_mutex);
                    list_add_tail(&portitemptr->siblings, &g_raw_midi_port_list);
                    pthread_mutex_unlock(&g_raw_midi_port_mutex);
                }
            }
        }
    }

    // special ports
    {
        const LilvPort* enabled_port = lilv_plugin_get_port_by_designation(plugin,
                                                                           g_lilv_nodes.input,
                                                                           g_lilv_nodes.enabled);
        if (enabled_port)
        {
            effect->enabled_index = lilv_port_get_index(plugin, enabled_port);
            *(effect->ports[effect->enabled_index]->buffer) = 1.0f;
        }
        else
        {
            effect->enabled_index = -1;
        }

        const LilvPort* freewheel_port = lilv_plugin_get_port_by_designation(plugin,
                                                                             g_lilv_nodes.input,
                                                                             g_lilv_nodes.freeWheeling);
        if (freewheel_port)
        {
            effect->freewheel_index = lilv_port_get_index(plugin, freewheel_port);
            *(effect->ports[effect->freewheel_index]->buffer) = 0.0f;
        }
        else
        {
            effect->freewheel_index = -1;
        }

        const LilvPort* reset_port = lilv_plugin_get_port_by_designation(plugin,
                                                                         g_lilv_nodes.input,
                                                                         g_lilv_nodes.reset);
        if (reset_port)
        {
            effect->reset_index = lilv_port_get_index(plugin, reset_port);
            *(effect->ports[effect->reset_index]->buffer) = 0.0f;
        }
        else
        {
            effect->reset_index = -1;
        }

        const LilvPort* bpb_port = lilv_plugin_get_port_by_designation(plugin,
                                                                       g_lilv_nodes.input,
                                                                       g_lilv_nodes.timeBeatsPerBar);
        if (bpb_port)
        {
            effect->bpb_index = lilv_port_get_index(plugin, bpb_port);
            *(effect->ports[effect->bpb_index]->buffer) = g_transport_bpb;
        }
        else
        {
            effect->bpb_index = -1;
        }

        const LilvPort* bpm_port = lilv_plugin_get_port_by_designation(plugin,
                                                                       g_lilv_nodes.input,
                                                                       g_lilv_nodes.timeBeatsPerMinute);
        if (bpm_port)
        {
            effect->bpm_index = lilv_port_get_index(plugin, bpm_port);
            *(effect->ports[effect->bpm_index]->buffer) = g_transport_bpm;
        }
        else
        {
            effect->bpm_index = -1;
        }

        const LilvPort* speed_port = lilv_plugin_get_port_by_designation(plugin,
                                                                         g_lilv_nodes.input,
                                                                         g_lilv_nodes.timeSpeed);
        if (speed_port)
        {
            effect->speed_index = lilv_port_get_index(plugin, speed_port);
            *(effect->ports[effect->speed_index]->buffer) = g_jack_rolling ? 1.0f : 0.0f;
        }
        else
        {
            effect->speed_index = -1;
        }
    }

    /* Allocate memory to indexes */
    /* Audio ports */
    if (audio_ports_count > 0)
    {
        effect->audio_ports_count = audio_ports_count;
        effect->audio_ports = (port_t **) mod_calloc(audio_ports_count, sizeof(port_t *));

        if (input_audio_ports_count > 0)
        {
            effect->input_audio_ports_count = input_audio_ports_count;
            effect->input_audio_ports = (port_t **) mod_calloc(input_audio_ports_count, sizeof(port_t *));
        }
        if (output_audio_ports_count > 0)
        {
            effect->output_audio_ports_count = output_audio_ports_count;
            effect->output_audio_ports = (port_t **) mod_calloc(output_audio_ports_count, sizeof(port_t *));
        }
    }

    /* Control ports */
    if (control_ports_count > 0)
    {
        effect->control_ports_count = control_ports_count;
        effect->control_ports = (port_t **) mod_calloc(control_ports_count, sizeof(port_t *));

        if (input_control_ports_count > 0)
        {
            effect->input_control_ports_count = input_control_ports_count;
            effect->input_control_ports = (port_t **) mod_calloc(input_control_ports_count, sizeof(port_t *));
        }
        if (output_control_ports_count > 0)
        {
            effect->output_control_ports_count = output_control_ports_count;
            effect->output_control_ports = (port_t **) mod_calloc(output_control_ports_count, sizeof(port_t *));
        }
    }

    /* CV ports */
    if (cv_ports_count > 0)
    {
        effect->cv_ports_count = cv_ports_count;
        effect->cv_ports = (port_t **) mod_calloc(cv_ports_count, sizeof(port_t *));

        if (input_cv_ports_count > 0)
        {
            effect->input_cv_ports_count = input_cv_ports_count;
            effect->input_cv_ports = (port_t **) mod_calloc(input_cv_ports_count, sizeof(port_t *));
        }
        if (output_cv_ports_count > 0)
        {
            effect->output_cv_ports_count = output_cv_ports_count;
            effect->output_cv_ports = (port_t **) mod_calloc(output_cv_ports_count, sizeof(port_t *));
        }
    }

    /* Event ports */
    if (event_ports_count > 0)
    {
        effect->event_ports_count = event_ports_count;
        effect->event_ports = (port_t **) mod_calloc(event_ports_count, sizeof(port_t *));

        if (input_event_ports_count > 0)
        {
            effect->input_event_ports_count = input_event_ports_count;
            effect->input_event_ports = (port_t **) mod_calloc(input_event_ports_count, sizeof(port_t *));
        }
        if (output_event_ports_count > 0)
        {
            effect->output_event_ports_count = output_event_ports_count;
            effect->output_event_ports = (port_t **) mod_calloc(output_event_ports_count, sizeof(port_t *));
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

    for (unsigned int i = 0; i < ports_count; i++)
    {
        /* Audio ports */
        lilv_port = lilv_plugin_get_port_by_index(plugin, i);
        if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.audio))
        {
            effect->audio_ports[audio_ports_count] = effect->ports[i];
            audio_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input))
            {
                effect->input_audio_ports[input_audio_ports_count] = effect->ports[i];
                input_audio_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output))
            {
                effect->output_audio_ports[output_audio_ports_count] = effect->ports[i];
                output_audio_ports_count++;
            }
        }
        /* Control ports */
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.control))
        {
            effect->control_ports[control_ports_count] = effect->ports[i];
            control_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input))
            {
                effect->input_control_ports[input_control_ports_count] = effect->ports[i];
                input_control_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output))
            {
                effect->output_control_ports[output_control_ports_count] = effect->ports[i];
                output_control_ports_count++;
            }
        }
        /* CV ports */
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.cv) || lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.mod_cvport))
        {
            effect->cv_ports[cv_ports_count] = effect->ports[i];
            cv_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input))
            {
                effect->input_cv_ports[input_cv_ports_count] = effect->ports[i];
                input_cv_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output))
            {
                effect->output_cv_ports[output_cv_ports_count] = effect->ports[i];
                output_cv_ports_count++;
            }
        }
        /* Event ports */
        else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.event) ||
                 lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.atom_port))
        {
            effect->event_ports[event_ports_count] = effect->ports[i];
            event_ports_count++;

            if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.input))
            {
                effect->input_event_ports[input_event_ports_count] = effect->ports[i];
                input_event_ports_count++;
            }
            else if (lilv_port_is_a(plugin, lilv_port, g_lilv_nodes.output))
            {
                effect->output_event_ports[output_event_ports_count] = effect->ports[i];
                output_event_ports_count++;
            }
        }
    }

    AllocatePortBuffers(effect, control_in_size, control_out_size);

    {
        // Index readable and writable properties
        LilvNodes *writable_properties = lilv_world_find_nodes(
            g_lv2_data,
            lilv_plugin_get_uri(effect->lilv_plugin),
            g_lilv_nodes.patch_writable,
            NULL);
        LilvNodes *readable_properties = lilv_world_find_nodes(
            g_lv2_data,
            lilv_plugin_get_uri(effect->lilv_plugin),
            g_lilv_nodes.patch_readable,
            NULL);
        effect->properties_count = lilv_nodes_size(writable_properties) + lilv_nodes_size(readable_properties);
        effect->properties = (property_t **) mod_calloc(effect->properties_count, sizeof(property_t *));

        // TODO mix readable and writable
        uint32_t j = 0;
        LILV_FOREACH(nodes, p, writable_properties)
        {
            const LilvNode* property = lilv_nodes_get(writable_properties, p);
            effect->properties[j] = (property_t *) malloc(sizeof(property_t));
            effect->properties[j]->uri = lilv_node_duplicate(property);
            effect->properties[j]->type = lilv_world_get(g_lv2_data, property, g_lilv_nodes.rdfs_range, NULL);
            effect->properties[j]->monitored = true; // always true for writable properties
            j++;
        }
        LILV_FOREACH(nodes, p, readable_properties)
        {
            const LilvNode* property = lilv_nodes_get(readable_properties, p);
            effect->properties[j] = (property_t *) malloc(sizeof(property_t));
            effect->properties[j]->uri = lilv_node_duplicate(property);
            effect->properties[j]->type = lilv_world_get(g_lv2_data, property, g_lilv_nodes.rdfs_range, NULL);
            effect->properties[j]->monitored = false; // optional
            j++;
        }

        lilv_nodes_free(writable_properties);
        lilv_nodes_free(readable_properties);
    }

    /* create ring buffer for events from socket/commandline */
    if (control_in_size != 0)
    {
        effect->events_in_buffer = jack_ringbuffer_create(control_in_size);
        jack_ringbuffer_mlock(effect->events_in_buffer);
        effect->events_in_buffer_helper = malloc(control_in_size);
    }
    if (control_out_size != 0)
    {
        effect->events_out_buffer = jack_ringbuffer_create(control_out_size);
        jack_ringbuffer_mlock(effect->events_out_buffer);
    }

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
    pthread_mutex_init(&effect->bypass_port.cv_source_mutex, &mutex_atts);

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
    pthread_mutex_init(&effect->presets_port.cv_source_mutex, &mutex_atts);

    pthread_mutexattr_destroy(&mutex_atts);

    lilv_node_free(plugin_uri);

    /* Jack callbacks */
    jack_set_thread_init_callback(jack_client, JackThreadInit, effect);
    jack_set_process_callback(jack_client, ProcessPlugin, effect);
    jack_set_buffer_size_callback(jack_client, BufferSize, effect);
    jack_set_freewheel_callback(jack_client, FreeWheelMode, effect);


    if (activate)
    {
        lilv_instance_activate(lilv_instance);

        /* Try activate the Jack client */
        if (jack_activate(jack_client) != 0)
        {
            fprintf(stderr, "can't activate jack_client\n");
            error = ERR_JACK_CLIENT_ACTIVATION;
            goto error;
        }
    }

    if (raw_midi_port != NULL)
    {
        const char *rawportname = jack_port_name(raw_midi_port);

        if (g_aggregated_midi_enabled)
        {
            jack_connect(g_jack_global_client, "mod-midi-merger:out", rawportname);
        }
        else
        {
            const char** const midihwports = jack_get_ports(g_jack_global_client, "",
                                                            JACK_DEFAULT_MIDI_TYPE,
                                                            JackPortIsTerminal|JackPortIsPhysical|JackPortIsOutput);
            if (midihwports != NULL)
            {
                for (int i=0; midihwports[i] != NULL; ++i)
                    jack_connect(g_jack_global_client, midihwports[i], rawportname);

                jack_free(midihwports);
            }
        }
    }

    LoadPresets(effect);
    return instance;

error:
    lilv_node_free(plugin_uri);
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

            lilv_state_restore(state, effect->lilv_instance, SetParameterFromState, effect,
                               LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE, effect->features);
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
            if (effect->reset_index >= 0)
            {
                *(effect->ports[effect->reset_index]->buffer) = 0.0f;
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
    effect_t *effect;
    char *scratch_dir;

    if (! InstanceExist(effect_id)) {
        return ERR_INSTANCE_INVALID;
    }

    effect = &g_effects[effect_id];
    scratch_dir = GetPluginStateDir(effect->instance, g_lv2_scratch_dir);

    LilvState* const state = lilv_state_new_from_instance(
        effect->lilv_plugin,
        effect->lilv_instance,
        &g_urid_map,
        scratch_dir,
        dir,
        dir,
        dir,
        GetPortValueForState, effect,
        LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE,
        effect->features);

    if (label) {
        lilv_state_set_label(state, label);
    }

    int ret = lilv_state_save(g_lv2_data, &g_urid_map, &g_urid_unmap, state, NULL, dir, file_name);

    lilv_state_free(state);
    free(scratch_dir);
    return ret;
}

int effects_preset_show(const char *uri, char **state_str)
{
    LilvNode* preset_uri = lilv_new_uri(g_lv2_data, uri);

    if (! preset_uri) {
        return ERR_LV2_INVALID_PRESET_URI;
    }

    if (lilv_world_load_resource(g_lv2_data, preset_uri) >= 0)
    {
        LilvState* state = lilv_state_new_from_world(g_lv2_data, &g_urid_map, preset_uri);
        if (!state)
        {
            lilv_node_free(preset_uri);
            return ERR_LV2_CANT_LOAD_STATE;
        }

        setenv("LILV_STATE_SKIP_PROPERTIES", "2", 1);

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
    int start, end;
    effect_t *effect;
    char state_filename[g_lv2_scratch_dir != NULL ? PATH_MAX : 1];

    // stop postpone events thread
    if (g_postevents_running == 1)
    {
        g_postevents_running = 0;
        sem_post(&g_postevents_semaphore);
        pthread_join(g_postevents_thread, NULL);
    }

    // disconnect system ports
    if (effect_id == REMOVE_ALL)
    {
        /* Disconnect the system connections */
        if (g_capture_ports != NULL)
        {
            for (int i = 0; g_capture_ports[i]; i++)
            {
                jack_port_t *port = jack_port_by_name(g_jack_global_client, g_capture_ports[i]);
                if (! port)
                    continue;

                const char **capture_connections = jack_port_get_connections(port);
                if (capture_connections)
                {
                    for (int j = 0; capture_connections[j]; j++)
                    {
                        if (strstr(capture_connections[j], "system"))
                            jack_disconnect(g_jack_global_client, g_capture_ports[i], capture_connections[j]);
                    }

                    jack_free(capture_connections);
                }
            }
        }

        start = 0;
        end = MAX_PLUGIN_INSTANCES;
    }
    else
    {
        start = effect_id;
        end = start + 1;
    }

    // stop plugins processing
    for (int j = start; j < end; j++)
    {
        if (InstanceExist(j))
        {
            effect = &g_effects[j];

            if (jack_deactivate(effect->jack_client) != 0)
                return ERR_JACK_CLIENT_DEACTIVATION;
        }
    }

    // remove addressings, midi learn and other stuff related to plugins
    if (effect_id == REMOVE_ALL)
    {
        pthread_mutex_lock(&g_midi_learning_mutex);
        g_midi_learning = NULL;
        pthread_mutex_unlock(&g_midi_learning_mutex);

        for (int j = MAX_MIDI_CC_ASSIGN, unused = ASSIGNMENT_NULL; --j >= 0;)
        {
            if (g_midi_cc_list[j].effect_id >= MAX_PLUGIN_INSTANCES && g_midi_cc_list[j].effect_id < MAX_INSTANCES)
            {
                unused = ASSIGNMENT_UNUSED;
                continue;
            }
            g_midi_cc_list[j].channel = -1;
            g_midi_cc_list[j].controller = 0;
            g_midi_cc_list[j].minimum = 0.0f;
            g_midi_cc_list[j].maximum = 1.0f;
            g_midi_cc_list[j].effect_id = unused;
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

                    cc_assignment_key_t key = {0};
                    key.id = assignment->assignment_id;
                    key.device_id = assignment->device_id;
                    key.pair_id = assignment->assignment_pair_id;
                    cc_client_unassignment(g_cc_client, &key);
                }
            }
        }

        mod_memset(&g_assignments_list, 0, sizeof(g_assignments_list));

        for (int i = 0; i < CC_MAX_DEVICES; i++)
        {
            for (int j = 0; j < CC_MAX_ASSIGNMENTS; j++)
            {
                g_assignments_list[i][j].effect_id = ASSIGNMENT_NULL;
                g_assignments_list[i][j].assignment_pair_id = -1;
            }
        }
#endif

#ifdef __MOD_DEVICES__
        for (int i = 0; i < MAX_HMI_ADDRESSINGS; i++)
            g_hmi_addressings[i].actuator_id = -1;

        if (g_hmi_data != NULL)
        {
            pthread_mutex_lock(&g_hmi_mutex);
            sys_serial_write(&g_hmi_data->server, sys_serial_event_type_special_req, 0, 0, "pages");
            pthread_mutex_unlock(&g_hmi_mutex);
        }

        // this resets volume back 0dB if needed
        // monitor_client_setup_volume(0.0f);
#endif

        // reset all events
        struct list_head queue, *it, *it2;

        // RT/mempool stuff
        INIT_LIST_HEAD(&queue);
        pthread_mutex_lock(&g_rtsafe_mutex);
        list_splice_init(&g_rtsafe_list, &queue);
        pthread_mutex_unlock(&g_rtsafe_mutex);

        list_for_each_safe(it, it2, &queue)
        {
            postponed_event_list_data *const eventptr = list_entry(it, postponed_event_list_data, siblings);
            rtsafe_memory_pool_deallocate(g_rtsafe_mem_pool, eventptr);
        }

        // raw midi port list
        INIT_LIST_HEAD(&queue);
        pthread_mutex_lock(&g_raw_midi_port_mutex);
        list_splice_init(&g_raw_midi_port_list, &queue);
        pthread_mutex_unlock(&g_raw_midi_port_mutex);

        list_for_each_safe(it, it2, &queue)
        {
            raw_midi_port_item *const portitemptr = list_entry(it, raw_midi_port_item, siblings);
            free(portitemptr);
        }
    }
    else
    {
        struct list_head *it;
        pthread_mutex_lock(&g_raw_midi_port_mutex);
        list_for_each(it, &g_raw_midi_port_list)
        {
            raw_midi_port_item *const portitemptr = list_entry(it, raw_midi_port_item, siblings);

            if (portitemptr->instance == effect_id)
            {
                list_del(it);
                free(portitemptr);
                break;
            }
        }
        pthread_mutex_unlock(&g_raw_midi_port_mutex);

        pthread_mutex_lock(&g_midi_learning_mutex);
        if (g_midi_learning != NULL && g_midi_learning->effect_id == effect_id)
        {
            g_midi_learning->effect_id = ASSIGNMENT_UNUSED;
            g_midi_learning->symbol = NULL;
            g_midi_learning->port = NULL;
            g_midi_learning = NULL;
        }
        pthread_mutex_unlock(&g_midi_learning_mutex);

        for (int j = 0; j < MAX_MIDI_CC_ASSIGN; j++)
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
                    cc_assignment_key_t key = {0};
                    key.id = assignment->assignment_id;
                    key.device_id = assignment->device_id;
                    key.pair_id = assignment->assignment_pair_id;
                    cc_client_unassignment(g_cc_client, &key);
                }

                mod_memset(assignment, 0, sizeof(assignment_t));
                assignment->effect_id = ASSIGNMENT_UNUSED;
                assignment->assignment_pair_id = -1;
            }
        }
#endif

        // flush events for all effects except this one
        RunPostPonedEvents(effect_id);
    }

    // now finally cleanup the plugin(s)
    for (int j = start; j < end; j++)
    {
        if (InstanceExist(j))
        {
            effect = &g_effects[j];

#ifdef WITH_EXTERNAL_UI_SUPPORT
            if (effect->ui_libhandle != NULL)
            {
                if (effect->ui_desc != NULL && effect->ui_handle != NULL && effect->ui_desc->cleanup != NULL)
                    effect->ui_desc->cleanup(effect->ui_handle);

                dlclose(effect->ui_libhandle);
            }
#endif

            FreeFeatures(effect);

            if (effect->event_ports)
            {
                for (uint32_t i = 0; i < effect->event_ports_count; i++)
                {
                    lv2_evbuf_free(effect->event_ports[i]->evbuf);
                }
            }

            if (effect->ports)
            {
                for (uint32_t i = 0; i < effect->ports_count; i++)
                {
                    if (effect->ports[i])
                    {
#ifdef __MOD_DEVICES__
                        if (effect->ports[i]->hmi_addressing != NULL)
                        {
                            if (g_hmi_data != NULL)
                            {
                                char msg[24];
                                snprintf(msg, sizeof(msg), "%i", effect->ports[i]->hmi_addressing->actuator_id);
                                msg[sizeof(msg)-1] = '\0';

                                pthread_mutex_lock(&g_hmi_mutex);
                                sys_serial_write(&g_hmi_data->server,
                                                 sys_serial_event_type_unassign,
                                                 effect->ports[i]->hmi_addressing->page,
                                                 effect->ports[i]->hmi_addressing->subpage, msg);
                                pthread_mutex_unlock(&g_hmi_mutex);
                            }

                            effect->ports[i]->hmi_addressing->actuator_id = -1;
                        }
#endif

                        // TODO destroy port mutexes
                        free(effect->ports[i]->buffer);
                        lilv_scale_points_free(effect->ports[i]->scale_points);
                        free(effect->ports[i]);
                    }
                }
                free(effect->ports);
            }

            if (effect->properties)
            {
                for (uint32_t i = 0; i < effect->properties_count; i++)
                {
                    if (effect->properties[i])
                    {
                        lilv_node_free(effect->properties[i]->uri);
                        lilv_node_free(effect->properties[i]->type);
                        free(effect->properties[i]);
                    }
                }
                free(effect->properties);
            }

            if (effect->lilv_instance)
            {
                if (effect->activated)
                    lilv_instance_deactivate(effect->lilv_instance);

                lilv_instance_free(effect->lilv_instance);
            }

            if (effect->jack_client)
                jack_client_close(effect->jack_client);

            free(effect->audio_ports);
            free(effect->input_audio_ports);
            free(effect->output_audio_ports);

            free(effect->control_ports);
            free(effect->input_control_ports);
            free(effect->output_control_ports);

            free(effect->cv_ports);
            free(effect->input_cv_ports);
            free(effect->output_cv_ports);

            free(effect->event_ports);
            free(effect->input_event_ports);
            free(effect->output_event_ports);

            if (effect->events_in_buffer)
                jack_ringbuffer_free(effect->events_in_buffer);
            if (effect->events_in_buffer_helper)
                free(effect->events_in_buffer_helper);
            if (effect->events_out_buffer)
                jack_ringbuffer_free(effect->events_out_buffer);

            if (effect->presets)
            {
                for (uint32_t i = 0; i < effect->presets_count; i++)
                {
                    lilv_free(effect->presets[i]->uri);
                    free(effect->presets[i]);
                }
                free(effect->presets);
            }

            if (effect->hints & HINT_HAS_STATE)
            {
                if (g_lv2_scratch_dir != NULL)
                {
                    // recursively delete state folder
                    memset(state_filename, 0, sizeof(state_filename));
                    snprintf(state_filename, PATH_MAX-1, "%s/effect-%d",
                             g_lv2_scratch_dir, effect->instance);
                    RecursivelyRemovePluginPath(state_filename);
                }

                if (effect->hints & HINT_STATE_UNSAFE)
                    pthread_mutex_destroy(&effect->state_restore_mutex);
            }

            InstanceDelete(j);
        }
    }

    // clear param_set cache
    effects_set_parameter(-1, NULL, 0.f);

    // start thread again
    if (g_postevents_running == 0)
    {
        if (g_verbose_debug)
        {
            puts("DEBUG: effects_remove restarted RunPostPonedEvents thread");
            fflush(stdout);
        }

        g_postevents_running = 1;
        pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);
    }

    return SUCCESS;
}

#if 0
static void* effects_activate_thread(void* arg)
{
    jack_client_t *jack_client = arg;

    if (jack_activate(jack_client) != 0)
        fprintf(stderr, "can't activate jack_client\n");

    return NULL;
}

static void* effects_deactivate_thread(void* arg)
{
    jack_client_t *jack_client = arg;

    if (jack_deactivate(jack_client) != 0)
        fprintf(stderr, "can't deactivate jack_client\n");

    return NULL;
}
#endif

int effects_activate(int effect_id, int value)
{
    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }
#if 0
    if (effect_id > effect_id_end)
    {
        return ERR_INVALID_OPERATION;
    }

    if (effect_id == effect_id_end)
    {
#endif
        effect_t *effect = &g_effects[effect_id];

        if (value)
        {
            if (! effect->activated)
            {
                effect->activated = true;
                lilv_instance_activate(effect->lilv_instance);

                if (jack_activate(effect->jack_client) != 0)
                {
                    fprintf(stderr, "can't activate jack_client\n");
                    return ERR_JACK_CLIENT_ACTIVATION;
                }
            }
        }
        else
        {
            if (effect->activated)
            {
                effect->activated = false;

                if (jack_deactivate(effect->jack_client) != 0)
                {
                    fprintf(stderr, "can't deactivate jack_client\n");
                    return ERR_JACK_CLIENT_DEACTIVATION;
                }

                lilv_instance_deactivate(effect->lilv_instance);
            }
        }
#if 0
    }
    else
    {
        int num_threads = 0;
        pthread_t *threads = malloc(sizeof(pthread_t) * (effect_id_end - effect_id));

        // create threads to activate all clients
        for (int i = effect_id; i <= effect_id_end; ++i)
        {
            effect_t *effect = &g_effects[i];

            if (effect->jack_client == NULL)
                continue;

            if (value)
            {
                if (! effect->activated)
                {
                    effect->activated = true;
                    lilv_instance_activate(effect->lilv_instance);

                    pthread_create(&threads[num_threads++], NULL, effects_activate_thread, effect->jack_client);
                }
            }
            else
            {
                if (effect->activated)
                {
                    pthread_create(&threads[num_threads++], NULL, effects_deactivate_thread, effect->jack_client);
                }
            }
        }

        // wait for all threads to be done
        for (int i = 0; i < num_threads; ++i)
            pthread_join(threads[i], NULL);

        free(threads);

        // if deactivating, do the last lv2 deactivate step now
        if (! value)
        {
            for (int i = effect_id; i <= effect_id_end; ++i)
            {
                effect_t *effect = &g_effects[i];

                if (! effect->activated || effect->jack_client == NULL)
                    continue;

                effect->activated = false;
                lilv_instance_deactivate(effect->lilv_instance);
            }
        }
    }
#endif

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

int effects_disconnect_all(const char *port)
{
    int ret;

    ret = jack_port_disconnect(g_jack_global_client, jack_port_by_name(g_jack_global_client, port));
    if (ret != 0) return ERR_JACK_PORT_DISCONNECTION;

    return ret;
}

int effects_set_parameter(int effect_id, const char *control_symbol, float value)
{
    port_t *port;

    static int last_effect_id = -1;
    static const char *last_symbol = NULL;
    static float *last_buffer, *last_prev, last_min, last_max;
#ifdef WITH_EXTERNAL_UI_SUPPORT
    static enum PortHints *last_hints;
#endif

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

                *last_buffer = *last_prev = value;
#ifdef WITH_EXTERNAL_UI_SUPPORT
                *last_hints |= HINT_SHOULD_UPDATE;
#endif
                return SUCCESS;
            }
        }

        port = FindEffectInputPortBySymbol(&(g_effects[effect_id]), control_symbol);
        if (port)
        {
            // stores the data of the current control
            last_effect_id = effect_id;
            last_min = port->min_value;
            last_max = port->max_value;
            last_buffer = port->buffer;
            last_prev   = &port->prev_value;
            last_symbol = port->symbol;

            if (value < last_min)
                value = last_min;
            else if (value > last_max)
                value = last_max;

            *last_prev = *last_buffer = value;

#ifdef WITH_EXTERNAL_UI_SUPPORT
            last_hints = &port->hints;
            port->hints |= HINT_SHOULD_UPDATE;
#endif
            return SUCCESS;
        }

        last_effect_id = -1;
        return ERR_LV2_INVALID_PARAM_SYMBOL;
    }

    last_effect_id = -1;
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
           *value = *(port->buffer);
           return SUCCESS;
        }

        return ERR_LV2_INVALID_PARAM_SYMBOL;
    }

    return ERR_INSTANCE_NON_EXISTS;
}

int effects_flush_parameters(int effect_id, int reset, int param_count, const flushed_param_t *params)
{
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;

    effect_t *effect = &(g_effects[effect_id]);
    port_t *port;
    float value;

    if (effect->reset_index >= 0 && reset != 0)
    {
        port = effect->ports[effect->reset_index];
        port->prev_value = *(port->buffer) = reset;
    }

    for (int i = 0; i < param_count; i++)
    {
        port = FindEffectInputPortBySymbol(effect, params[i].symbol);
        if (port)
        {
            value = params[i].value;

            if (value < port->min_value)
                value = port->max_value;
            else if (value > port->max_value)
                value = port->max_value;

            port->prev_value = *(port->buffer) = params[i].value;

#ifdef WITH_EXTERNAL_UI_SUPPORT
            port->hints |= HINT_SHOULD_UPDATE;
#endif
        }
    }

    // reset a 2nd time in case plugin was processing while we changed parameters
    if (effect->reset_index >= 0 && reset != 0)
    {
        port = effect->ports[effect->reset_index];
        port->prev_value = *(port->buffer) = reset;
    }

    return SUCCESS;
}

static inline
bool lv2_atom_forge_property_set(LV2_Atom_Forge *forge, LV2_URID urid, const char *value, LV2_URID type)
{
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_object(forge, &frame, 0, g_urids.patch_Set);

    lv2_atom_forge_key(forge, g_urids.patch_sequence);
    lv2_atom_forge_int(forge, MAGIC_PARAMETER_SEQ_NUMBER);

    lv2_atom_forge_key(forge, g_urids.patch_property);
    lv2_atom_forge_urid(forge, urid);

    lv2_atom_forge_key(forge, g_urids.patch_value);

    // handle simple types first
    /**/ if (type == forge->Bool)
        lv2_atom_forge_bool(forge, atoi(value) != 0);
    else if (type == forge->Int)
        lv2_atom_forge_int(forge, atoi(value));
    else if (type == forge->Long)
        lv2_atom_forge_long(forge, atol(value));
    else if (type == forge->Float)
        lv2_atom_forge_float(forge, atof(value));
    else if (type == forge->Double)
        lv2_atom_forge_double(forge, atof(value));
    else if (type == forge->String)
        lv2_atom_forge_string(forge, value, strlen(value));
    else if (type == forge->URI)
        lv2_atom_forge_uri(forge, value, strlen(value));
    else if (type == forge->Path)
        lv2_atom_forge_path(forge, value, strlen(value));

    // vector (array of a specific type)
    // with string format as: ${num_elems}-${value_type_char}-${data:separated:by:colon}
    else if (type == forge->Vector)
    {
        const char *const first_sep = strchr(value, '-');

        if (*first_sep == '\0')
            return false;

        const char *const second_sep = strchr(first_sep+1, '-');

        if (*second_sep == '\0')
            return false;

        const char value_type = *(first_sep + 1);

        LV2_URID child_type;
        uint32_t elem_size;
        switch (value_type)
        {
        case 'b':
            child_type = forge->Bool;
            elem_size = sizeof(int32_t);
            break;
        case 'i':
            child_type = forge->Int;
            elem_size = sizeof(int32_t);
            break;
        case 'l':
            child_type = forge->Long;
            elem_size = sizeof(int64_t);
            break;
        case 'f':
            child_type = forge->Float;
            elem_size = sizeof(float);
            break;
        case 'g':
            child_type = forge->Double;
            elem_size = sizeof(double);
            break;
        default:
            return false;
        }

        uint32_t n_elems = 0;

        for (const char *s = second_sep; s != NULL && *s != '\0'; s = strchrnul(s, ':'))
            ++n_elems;

        if (n_elems == 0)
            return false;

        char localbuf[24];
        snprintf(localbuf, sizeof(localbuf)-1, "%u-", n_elems);
        localbuf[15] = '\0';

        if (strncmp(value, localbuf, strlen(localbuf)))
            return false;

        uint32_t i = 0;
        size_t size;
        char *end;
        void *elems = malloc(elem_size * n_elems);

        switch (value_type)
        {
        case 'b':
            for (const char *s = second_sep; s != NULL && *s != '\0'; s = strchrnul(s, ':'))
            {
                end  = strchrnul(s, ':');
                size = end - s;
                memcpy(localbuf, s, size);
                localbuf[size] = '\0';
                ((int32_t*)elems)[i++] = atoi(localbuf) != 0;
            }
            break;
        case 'i':
            for (const char *s = second_sep; s != NULL && *s != '\0'; s = strchrnul(s, ':'))
            {
                end  = strchrnul(s, ':');
                size = end - s;
                memcpy(localbuf, s, size);
                localbuf[size] = '\0';
                ((int32_t*)elems)[i++] = atoi(localbuf);
            }
            break;
        case 'l':
            for (const char *s = second_sep; s != NULL && *s != '\0'; s = strchrnul(s, ':'))
            {
                end  = strchrnul(s, ':');
                size = end - s;
                memcpy(localbuf, s, size);
                localbuf[size] = '\0';
                ((int64_t*)elems)[i++] = atol(localbuf);
            }
            break;
        case 'f':
            for (const char *s = second_sep; s != NULL && *s != '\0'; s = strchrnul(s, ':'))
            {
                end  = strchrnul(s, ':');
                size = end - s;
                memcpy(localbuf, s, size);
                localbuf[size] = '\0';
                ((float*)elems)[i++] = atof(localbuf);
            }
            break;
        case 'g':
            for (const char *s = second_sep; s != NULL && *s != '\0'; s = strchrnul(s, ':'))
            {
                end  = strchrnul(s, ':');
                size = end - s;
                memcpy(localbuf, s, size);
                localbuf[size] = '\0';
                ((double*)elems)[i++] = atof(localbuf);
            }
            break;
        }

        lv2_atom_forge_vector(forge, elem_size, child_type, n_elems, elems);
        free(elems);
    }

#if 0 /* TODO */
    // tuple (dictonary-like object)
    // pair of type and then value; all separated by zero with a final zero
    else if (type == forge->Tuple)
    {
        uint32_t n_elems = 0;

        for (const char *s = value + strlen(value) + 1; *s != '\0'; s += strlen(s) + 1)
        {
            ++n_elems;

            if (n_elems % 2)
            {
                // verify that child type is valid
                const LV2_URID child_type = g_urid_map.map(g_urid_map.handle, value);

                if (child_type == forge->Bool)
                    continue;
                if (child_type == forge->Int)
                    continue;
                if (child_type == forge->Long)
                    continue;
                if (child_type == forge->Float)
                    continue;
                if (child_type == forge->Double)
                    continue;
                if (child_type == forge->String)
                    continue;
                if (child_type == forge->URI)
                    continue;
                if (child_type == forge->Path)
                    continue;

                return false;
            }
        }

        if (n_elems % 2)
            return false;

        LV2_Atom_Forge_Frame tuple_frame;
        lv2_atom_forge_tuple(forge, &tuple_frame);

        for (const char *s = value + strlen(value) + 1; *s != '\0'; s += strlen(s) + 1)
        {
            const LV2_URID child_type = g_urid_map.map(g_urid_map.handle, value);
            value += strlen(value) + 1;

            /**/ if (child_type == forge->Bool)
                lv2_atom_forge_bool(forge, atoi(value) != 0);
            else if (child_type == forge->Int)
                lv2_atom_forge_int(forge, atoi(value));
            else if (child_type == forge->Long)
                lv2_atom_forge_long(forge, atol(value));
            else if (child_type == forge->Float)
                lv2_atom_forge_float(forge, atof(value));
            else if (child_type == forge->Double)
                lv2_atom_forge_double(forge, atof(value));
            else if (child_type == forge->String)
                lv2_atom_forge_string(forge, value, strlen(value));
            else if (child_type == forge->URI)
                lv2_atom_forge_uri(forge, value, strlen(value));
            else if (child_type == forge->Path)
                lv2_atom_forge_path(forge, value, strlen(value));
        }

        lv2_atom_forge_pop(forge, &tuple_frame);
    }
#endif

    // unsupported type
    else
    {
        return false;
    }

    lv2_atom_forge_pop(forge, &frame);

    return true;
}

int effects_set_property(int effect_id, const char *uri, const char *value)
{
    if (InstanceExist(effect_id))
    {
        effect_t *effect = &g_effects[effect_id];

        if (effect->events_in_buffer == NULL || effect->events_in_buffer_helper == NULL) {
            return ERR_INVALID_OPERATION;
        }

        property_t *prop = FindEffectPropertyByURI(effect, uri);
        if (prop && prop->type)
        {
            LV2_Atom_Forge forge = g_lv2_atom_forge;
            const LV2_URID urid = g_urid_map.map(g_urid_map.handle, uri);
            const LV2_URID type = g_urid_map.map(g_urid_map.handle, lilv_node_as_uri(prop->type));
            size_t size;

            if (type == forge.Bool || type == forge.Int || type == forge.Float)
                size = 4;
            else if (type == forge.Long || type == forge.Double)
                size = 8;
#if 0 /* TODO */
            else if (type == forge.Tuple)
                size = strlen(value)*8 + sizeof(LV2_Atom) * num_elems;
#endif
            else if (type == forge.Vector)
                size = strlen(value)*8 + sizeof(LV2_Atom_Vector_Body);
            else
                size = strlen(value)+1;

            // size as used by the forge (can overshoot for string->number conversion)
            const size_t bufsize = lv2_atom_pad_size(sizeof(LV2_Atom_Object))
                                 + 3U * lv2_atom_pad_size(2U * sizeof(uint32_t)) /* keys */
                                 + lv2_atom_pad_size(sizeof(LV2_Atom_URID))
                                 + lv2_atom_pad_size(sizeof(LV2_Atom_Int))
                                 + lv2_atom_pad_size(sizeof(LV2_Atom) + size) + 8U;
            uint8_t *buf = malloc(bufsize);

            if (!buf) {
                return ERR_MEMORY_ALLOCATION;
            }

            lv2_atom_forge_set_buffer(&forge, buf, bufsize);

            if (lv2_atom_forge_property_set(&forge, urid, value, type))
            {
                const LV2_Atom *atom = (LV2_Atom*)buf;
                const size_t atomsize = lv2_atom_total_size(atom);

                if (atomsize > jack_ringbuffer_write_space(effect->events_in_buffer))
                {
                    free(buf);
                    return ERR_MEMORY_ALLOCATION;
                }

                jack_ringbuffer_write(effect->events_in_buffer, (const char*)atom, atomsize);
                free(buf);
                return SUCCESS;
            }

            free(buf);
            return ERR_INVALID_OPERATION;
        }
        return ERR_LV2_INVALID_URI;
    }

    return ERR_INSTANCE_NON_EXISTS;
}

int effects_get_property(int effect_id, const char *uri)
{
    if (InstanceExist(effect_id))
    {
        effect_t *effect = &g_effects[effect_id];

        if (!effect->events_in_buffer) {
            return ERR_INVALID_OPERATION;
        }

        uint8_t buf[1024];

        LV2_Atom_Forge forge = g_lv2_atom_forge;
        lv2_atom_forge_set_buffer(&forge, buf, sizeof(buf));

        LV2_Atom_Forge_Frame frame;
        lv2_atom_forge_object(&forge, &frame, 0, g_urids.patch_Get);

        if (uri && *uri != '\0')
        {
            property_t *prop = FindEffectPropertyByURI(effect, uri);

            if (! prop)
                return ERR_LV2_INVALID_URI;

            lv2_atom_forge_key(&forge, g_urids.patch_property);
            lv2_atom_forge_urid(&forge, g_urid_map.map(g_urid_map.handle, uri));
        }

        lv2_atom_forge_pop(&forge, &frame);

        const LV2_Atom* atom = (LV2_Atom*)buf;
        jack_ringbuffer_write(effect->events_in_buffer,
                              (const char*)atom,
                              lv2_atom_total_size(atom));
        return SUCCESS;
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


    effect_t *effect = &g_effects[effect_id];
    const LilvNode *symbol = lilv_new_string(g_lv2_data, control_symbol);
    const LilvPort *port = lilv_plugin_get_port_by_symbol(effect->lilv_plugin, symbol);

    int port_id = lilv_port_get_index(effect->lilv_plugin, port);

    effect->monitors_count++;
    effect->monitors =
        (monitor_t**)realloc(effect->monitors, sizeof(monitor_t *) * effect->monitors_count);

    int idx = effect->monitors_count - 1;
    effect->monitors[idx] = (monitor_t*)malloc(sizeof(monitor_t));
    effect->monitors[idx]->port_id = port_id;
    effect->monitors[idx]->op = iop;
    effect->monitors[idx]->value = value;
    effect->monitors[idx]->last_notified_value = 0.0;
    return SUCCESS;
}

int effects_monitor_output_parameter(int effect_id, const char *control_symbol_or_uri, int enable)
{
    port_t *port;

    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;

    effect_t *effect = &g_effects[effect_id];
    port = FindEffectOutputPortBySymbol(effect, control_symbol_or_uri);

    if (port != NULL)
    {
        if (enable == 0)
        {
            // check if not monitored
            if ((port->hints & HINT_MONITORED) == 0)
                return SUCCESS;

            // remove monitored flag
            port->hints &= ~HINT_MONITORED;

            // stop postpone events thread
            if (g_postevents_running == 1)
            {
                g_postevents_running = 0;
                sem_post(&g_postevents_semaphore);
                pthread_join(g_postevents_thread, NULL);
            }

            // flush events for all effects except this one
            RunPostPonedEvents(effect_id);

            // start thread again
            if (g_postevents_running == 0)
            {
                if (g_verbose_debug)
                {
                    puts("DEBUG: effects_monitor_output_parameter restarted RunPostPonedEvents thread");
                    fflush(stdout);
                }

                g_postevents_running = 1;
                pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);
            }

            return SUCCESS;
        }

        // check if already monitored
        if (port->hints & HINT_MONITORED)
            return SUCCESS;

        // set prev_value
        port->prev_value = *(port->buffer);
        port->hints |= HINT_MONITORED;

        // simulate an output monitor event here, to report current value
        postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

        if (posteventptr != NULL)
        {
            posteventptr->event.type = POSTPONED_OUTPUT_MONITOR;
            posteventptr->event.parameter.effect_id = effect->instance;
            posteventptr->event.parameter.symbol    = port->symbol;
            posteventptr->event.parameter.value     = port->prev_value;

            pthread_mutex_lock(&g_rtsafe_mutex);
            list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
            pthread_mutex_unlock(&g_rtsafe_mutex);

            sem_post(&g_postevents_semaphore);
        }
    }
    else
    {
        property_t *property = FindEffectPropertyByURI(effect, control_symbol_or_uri);

        if (property == NULL)
            return ERR_LV2_INVALID_PARAM_SYMBOL;

        property->monitored = enable != 0;
    }

    // activate output monitor
    effect->hints |= HINT_OUTPUT_MONITORS;

    return SUCCESS;
}

int effects_bypass(int effect_id, int value)
{
    if (!InstanceExist(effect_id))
    {
        return ERR_INSTANCE_NON_EXISTS;
    }

    effect_t *effect = &g_effects[effect_id];
    effect->bypass_port.prev_value = effect->bypass = value ? 1.0f : 0.0f;

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
    port_t *port;

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
    port_t *port;

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

/**
 * Set Beats Per Minute in the Jack timebase.
 * Returns NULL on success or a negative value on error.
 */
int effects_set_beats_per_minute(double bpm)
{
  int result = SUCCESS;
  if ((20.0 <= bpm) && (bpm <= 300.0)) {
    // Change the current global value and set a flag that it was changed.
    g_transport_bpm = bpm;
    g_transport_reset = true;
    TriggerJackTimebase(false);
    UpdateGlobalJackPosition(UPDATE_POSITION_FORCED, false);
  } else {
    result = ERR_JACK_VALUE_OUT_OF_RANGE;
  }
  return result;
}

/**
 * Set Beats Per Bar in the Jack timebase.
 * Returns NULL on success or a negative value on error.
 */
int effects_set_beats_per_bar(float bpb)
{
  int result = SUCCESS;
  if ((1.0 <= bpb) && (bpb <= 16.0)) {
    // Change the current global value and set a flag that it was changed.
    g_transport_bpb = bpb;
    g_transport_reset = true;
    TriggerJackTimebase(false);
    UpdateGlobalJackPosition(UPDATE_POSITION_FORCED, true);
  } else {
    result = ERR_JACK_VALUE_OUT_OF_RANGE;
  }
  return result;
}

int effects_cc_map(int effect_id, const char *control_symbol, int device_id, int actuator_id,
                   const char *label, float value, float minimum, float maximum, int steps, int extraflags,
                   const char *unit, int scalepoints_count, const scalepoint_t *scalepoints)
{
#ifdef HAVE_CONTROLCHAIN
    InitializeControlChainIfNeeded();

    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;
    if (!g_cc_client)
        return ERR_CONTROL_CHAIN_UNAVAILABLE;
    if (scalepoints_count == 1 || extraflags < 0)
        return ERR_ASSIGNMENT_INVALID_OP;

    effect_t *effect = &(g_effects[effect_id]);
    port_t *port = FindEffectInputPortBySymbol(effect, control_symbol);

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    cc_assignment_t assignment = {0};
    assignment.device_id = device_id;
    assignment.actuator_id = actuator_id;
    assignment.label = label;
    assignment.value = value;
    assignment.min   = minimum;
    assignment.max   = maximum;
    assignment.def   = port->def_value;
    assignment.steps = steps;
    assignment.unit  = unit;
    assignment.list_count = scalepoints_count;
    assignment.actuator_pair_id = -1;
    assignment.assignment_pair_id = -1;

    if (extraflags & CC_MODE_TAP_TEMPO)
        assignment.mode = CC_MODE_TAP_TEMPO;
    else if (port->hints & HINT_TOGGLE)
        assignment.mode = CC_MODE_TOGGLE;
    else if (port->hints & HINT_INTEGER)
        assignment.mode = CC_MODE_INTEGER;
    else
        assignment.mode = CC_MODE_REAL;

    if (port->hints & HINT_LOGARITHMIC)
        assignment.mode |= CC_MODE_LOGARITHMIC;
    if (port->hints & HINT_TRIGGER)
        assignment.mode |= CC_MODE_TRIGGER;

    if (extraflags & CC_MODE_COLOURED)
        assignment.mode = CC_MODE_COLOURED;
    if (extraflags & CC_MODE_MOMENTARY)
        assignment.mode = CC_MODE_MOMENTARY;

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
        port->def_value = port->prev_value = *(port->buffer) = value;
    }

    if (g_verbose_debug) {
        puts("DEBUG: cc_map sending:");
        printf("\tdevice_id:   %i\n", assignment.device_id);
        printf("\tactuator_id: %i\n", assignment.actuator_id);
        printf("\tmode:        %x\n", assignment.mode);
        printf("\tlabel:       \"%s\"\n", assignment.label);
        printf("\tmin:         %f\n", assignment.min);
        printf("\tmax:         %f\n", assignment.max);
        printf("\tdef:         %f\n", assignment.def);
        printf("\tvalue:       %f\n", assignment.value);
        printf("\tsteps:       %i\n", assignment.steps);
        printf("\tunit:        \"%s\"\n", assignment.unit);
        printf("\tlist_count:  %i\n", assignment.list_count);
        for (int i = 0; i < scalepoints_count; i++)
            printf("\t #%02i: %f \"%s\"\n", i+1, scalepoints[i].value, scalepoints[i].label);
        fflush(stdout);
    }

    const int assignment_id = cc_client_assignment(g_cc_client, &assignment);

    free(item_data);
    free(assignment.list_items);

    if (assignment_id < 0)
    {
        if (g_verbose_debug) {
            puts("DEBUG: cc_map failed");
            fflush(stdout);
        }
        return ERR_ASSIGNMENT_FAILED;
    }

    assignment_t *item = &g_assignments_list[assignment.device_id][assignment_id];

    item->effect_id = effect_id;
    item->port = port;
    item->device_id = device_id;
    item->actuator_id = actuator_id;
    item->assignment_id = assignment_id;
    item->actuator_pair_id = assignment.actuator_pair_id;
    item->assignment_pair_id = assignment.assignment_pair_id;
    item->supports_set_value = CheckCCDeviceProtocolVersion(device_id, 0, 6);

    if (assignment.assignment_pair_id != -1)
    {
        assignment_t *item2 = &g_assignments_list[assignment.device_id][assignment.assignment_pair_id];

        item2->effect_id = effect_id;
        item2->port = port;
        item2->device_id = device_id;
        item2->actuator_id = assignment.actuator_pair_id;
        item2->assignment_id = assignment.assignment_pair_id;
        item2->actuator_pair_id = actuator_id;
        item2->assignment_pair_id = assignment_id;
        item2->supports_set_value = item->supports_set_value;
    }

    if (g_verbose_debug) {
        printf("DEBUG: cc_map assignment supports set_value: %s, has pair %i\n",
               item->supports_set_value ? "true" : "false", assignment.assignment_pair_id);
        fflush(stdout);
    }

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
    UNUSED_PARAM(extraflags);
    UNUSED_PARAM(unit);
    UNUSED_PARAM(scalepoints_count);
    UNUSED_PARAM(scalepoints);
#endif
}

int effects_cc_value_set(int effect_id, const char *control_symbol, float value)
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
                if (! assignment->supports_set_value)
                    return ERR_INVALID_OPERATION;

                // invert value for bypass
                if (!strcmp(control_symbol, BYPASS_PORT_SYMBOL))
                    value = value > 0.5f ? 0.0f : 1.0f;

                if (g_verbose_debug) {
                    puts("DEBUG: cc_value_set sending:");
                    printf("\tdevice_id:         %i\n", assignment->device_id);
                    printf("\tassignment_id:     %i\n", assignment->assignment_id);
                    printf("\assignment_pair_id: %i\n", assignment->assignment_pair_id);
                    printf("\tactuator_id:       %i\n", assignment->actuator_id);
                    printf("\tvalue:             %f\n", value);
                    fflush(stdout);
                }

                cc_set_value_t update = {0};
                update.device_id = assignment->device_id;
                update.assignment_id = assignment->assignment_id;
                update.actuator_id = assignment->actuator_id;
                update.value = value;

                cc_client_value_set(g_cc_client, &update);
                return SUCCESS;
            }
        }
    }

    if (g_verbose_debug) {
        puts("DEBUG: cc_value_set failed");
        fflush(stdout);
    }

    return ERR_ASSIGNMENT_INVALID_OP;
#else
    return ERR_CONTROL_CHAIN_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
    UNUSED_PARAM(control_symbol);
    UNUSED_PARAM(value);
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
                cc_assignment_key_t key = {0};
                key.id = assignment->assignment_id;
                key.device_id = assignment->device_id;
                key.pair_id = assignment->assignment_pair_id;

                mod_memset(assignment, 0, sizeof(assignment_t));
                assignment->effect_id = ASSIGNMENT_UNUSED;
                assignment->assignment_pair_id = -1;

                if (key.pair_id != -1)
                {
                    assignment = &g_assignments_list[i][key.pair_id];
                    mod_memset(assignment, 0, sizeof(assignment_t));
                    assignment->effect_id = ASSIGNMENT_UNUSED;
                    assignment->assignment_pair_id = -1;
                }

                if (g_verbose_debug) {
                    puts("DEBUG: cc_unmap sending:");
                    printf("\tdevice_id: %i\n", key.device_id);
                    printf("\tid:        %i\n", key.id);
                    printf("\tpair_id:   %i\n", key.pair_id);
                    fflush(stdout);
                }

                cc_client_unassignment(g_cc_client, &key);
                return SUCCESS;
            }
        }
    }

    if (g_verbose_debug) {
        puts("DEBUG: cc_unmap failed");
        fflush(stdout);
    }

    return ERR_ASSIGNMENT_INVALID_OP;
#else
    return ERR_CONTROL_CHAIN_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
    UNUSED_PARAM(control_symbol);
#endif
}

int effects_cv_map(int effect_id, const char *control_symbol, const char *source_port_name, float minimum, float maximum, const char* mode)
{
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;

    if (!mode || strlen(mode) != 1) {
        return ERR_INVALID_OPERATION;
    }

    const char op_mode = mode[0];

    switch (op_mode) {
    case '=':
    case '-':
    case '+':
    case 'b':
        break;
    default:
        return ERR_INVALID_OPERATION;
    }

    jack_port_t *source_jack_port = jack_port_by_name(g_jack_global_client, source_port_name);

    // FIXME proper error code
    if (!source_jack_port)
        return ERR_JACK_VALUE_OUT_OF_RANGE;

    effect_t *effect = &(g_effects[effect_id]);
    port_t *port = FindEffectInputPortBySymbol(effect, control_symbol);

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    cv_source_t *cv_source;
    jack_port_t *jack_port;

    // when readdressing to the same port, destroy old data
    cv_source_t *cv_source_to_delete = NULL;

    // check if this effect port already has as a cv addressing
    if (port->cv_source) {
        // if yes, it needs to match existing port (reconfiguring addressing, like changing range)
        if (port->cv_source->port != port) {
            return ERR_INVALID_OPERATION;
        }

        cv_source = malloc(sizeof(cv_source_t));

        if (!cv_source)
            return ERR_MEMORY_ALLOCATION;

        jack_port = port->cv_source->jack_port;
        cv_source_to_delete = port->cv_source;

    } else {
        // no addressing on this plugin port yet, create new one
        cv_source = malloc(sizeof(cv_source_t));

        if (!cv_source)
            return ERR_MEMORY_ALLOCATION;

        jack_port = jack_port_register(effect->jack_client,
                                       port->symbol,
                                       JACK_DEFAULT_AUDIO_TYPE,
                                       JackPortIsInput|JackPortIsControlVoltage, 0);

        if (!jack_port) {
            free(cv_source);
            return ERR_JACK_PORT_REGISTER;
        }
    }

    bool source_is_mod_cv = false;
    bool source_has_ranges = false;
    float source_min_value = -5.0f;
    float source_max_value = 5.0f;

    // FIXME internal clients cannot set metadata for now

    if (!strncmp(source_port_name, "mod-spi2jack:", 13)) {
        source_is_mod_cv = source_has_ranges = true;
        source_min_value = 0.0f;
        source_max_value = !strcmp(source_port_name+13, "exp_pedal") ? 5.0f : 10.0f;
    } else if (!strncmp(source_port_name, "mod-jack2spi:", 13)) {
        source_is_mod_cv = source_has_ranges = true;
        source_min_value = 0.0f;
        source_max_value = 10.0f;
    } else {
        const jack_uuid_t uuid = jack_port_uuid(source_jack_port);
        if (!jack_uuid_empty(uuid)) {
            char *value_min = NULL;
            char *value_max = NULL;

            // get values from jack metadata
            if (jack_get_property(uuid, LV2_CORE__minimum, &value_min, NULL) == 0 &&
                jack_get_property(uuid, LV2_CORE__maximum, &value_max, NULL) == 0)
            {
                source_has_ranges = true;
                source_min_value = atof(value_min);
                source_max_value = atof(value_max);

                // TODO set and fetch mod-type port here

            // find values when client is from mod-host, as fallback
            }
            else if (!strncmp(source_port_name, "effect_", 7))
            {
                char effect_str[6];
                const char *source_symbol = NULL;
                int source_effect_id = -1;

                mod_memset(effect_str, 0, sizeof(effect_str));
                strncpy(effect_str, source_port_name+7, 5);

                for (int i=1; i<6; ++i) {
                    if (effect_str[i] == '\0')
                        break;
                    if (effect_str[i] == ':') {
                        effect_str[i] = '\0';
                        source_symbol = source_port_name + (8 + i);
                        source_effect_id = atoi(effect_str);
                        break;
                    }
                }

                if (source_symbol != NULL && InstanceExist(source_effect_id)) {
                    const effect_t *source_effect = &g_effects[source_effect_id];

                    for (uint32_t i = 0; i < source_effect->output_cv_ports_count; ++i)
                    {
                        if (!strcmp(source_effect->output_cv_ports[i]->symbol, source_symbol))
                        {
                            const port_t *source_port = source_effect->output_cv_ports[i];

                            source_min_value = source_port->min_value;
                            source_max_value = source_port->max_value;

                            if (source_port->hints & HINT_CV_MOD)
                                source_is_mod_cv = true;
                            if (source_port->hints & HINT_CV_RANGES)
                                source_has_ranges = true;

                            break;
                        }
                    }
                }
            }

            if (value_min != NULL)
                jack_free(value_min);
            if (value_max != NULL)
                jack_free(value_max);
        }
    }

    // just in case
    if (source_min_value >= source_max_value)
        source_max_value = source_min_value+0.1f;

    // convert range into valid operational mode (unipolar-, unipolar+ or bipolar)
    const float source_diff_value = source_max_value - source_min_value;

    switch (op_mode) {
    case '-':
        if (source_is_mod_cv || source_has_ranges)
        {
            source_min_value = -source_diff_value;
            source_max_value = 0.0f;
        }
        else
        {
            source_min_value = -1.0f;
            source_max_value = 0.0f;
        }
        break;

    case '+':
        if (source_is_mod_cv || source_has_ranges)
        {
            source_min_value = 0.0f;
            source_max_value = source_diff_value;
        }
        else
        {
            source_min_value = 0.0f;
            source_max_value = 1.0f;
        }
        break;

    case 'b':
        if (source_is_mod_cv || source_has_ranges)
        {
            if (source_min_value < 0.0f && source_max_value > 0.0f) {
                // already bipolar, do not modify ranges
            } else {
                // make 0 the middlepoint
                source_min_value = -source_diff_value * 0.5f;
                source_max_value = source_diff_value * 0.5f;
            }
        }
        else
        {
            source_min_value = -1.0f;
            source_max_value = 1.0f;
        }
        break;
    }

    cv_source->port = port;
    cv_source->jack_port = jack_port;
    cv_source->source_min_value = source_min_value;
    cv_source->source_max_value = source_max_value;
    cv_source->source_diff_value = source_diff_value;
    cv_source->min_value = minimum;
    cv_source->max_value = maximum;
    cv_source->diff_value = maximum - minimum;
    cv_source->prev_value = port->prev_value;

    pthread_mutex_lock(&port->cv_source_mutex);
    port->cv_source = cv_source;
    pthread_mutex_unlock(&port->cv_source_mutex);

    jack_connect(effect->jack_client, source_port_name, jack_port_name(jack_port));

    if (cv_source_to_delete)
        free(cv_source_to_delete);

    return SUCCESS;
}

int effects_cv_unmap(int effect_id, const char *control_symbol)
{
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;

    effect_t *effect = &(g_effects[effect_id]);
    port_t *port = FindEffectInputPortBySymbol(effect, control_symbol);

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    cv_source_t *cv_source;

    pthread_mutex_lock(&port->cv_source_mutex);

    // not really success, but not an error either if the mapping does not exist
    if (!port->cv_source) {
        pthread_mutex_unlock(&port->cv_source_mutex);
        return SUCCESS;
    }

    cv_source = port->cv_source;
    port->cv_source = NULL;

    pthread_mutex_unlock(&port->cv_source_mutex);

    if (cv_source->jack_port)
        jack_port_unregister(effect->jack_client, cv_source->jack_port);

    free(cv_source);

    return SUCCESS;
}

int effects_hmi_map(int effect_id, const char *control_symbol, int hw_id, int page, int subpage,
                    int caps, int flags, const char *label, float minimum, float maximum, int steps)
{
#ifdef __MOD_DEVICES__
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;
    if (effect_id >= MAX_PLUGIN_INSTANCES)
        return ERR_ASSIGNMENT_INVALID_OP;
    if (page < 0 || page > 0xff)
        return ERR_ASSIGNMENT_INVALID_OP;
    if (subpage < 0 || subpage > 0xff)
        return ERR_ASSIGNMENT_INVALID_OP;

    effect_t *effect = &(g_effects[effect_id]);

    if (effect->hmi_notif == NULL)
        return ERR_ASSIGNMENT_UNUSED;

    port_t *port = NULL;

    // special handling for bypass, mapped to lv2:enabled designation
    if (!strcmp(control_symbol, g_bypass_port_symbol))
    {
        if (effect->enabled_index >= 0)
            port = effect->ports[effect->enabled_index];
    }
    // do not allow other special ports
    else if (control_symbol[0] != ':')
    {
        port = FindEffectInputPortBySymbol(effect, control_symbol);
    }

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    // check if already addressed
    if (port->hmi_addressing != NULL)
        return port->hmi_addressing->actuator_id == hw_id ? ERR_ASSIGNMENT_ALREADY_EXISTS
                                                          : ERR_ASSIGNMENT_FAILED;

    // find unused hmi addressing
    hmi_addressing_t *addressing;

    for (int i = 0; i < MAX_HMI_ADDRESSINGS;)
    {
        addressing = &g_hmi_addressings[i];

        if (addressing->actuator_id != -1)
        {
            if (++i == MAX_HMI_ADDRESSINGS)
                addressing = NULL;
            continue;
        }

        addressing->actuator_id = hw_id;
        addressing->page = page;
        addressing->subpage = subpage;
        break;
    }

    if (addressing == NULL)
        return ERR_ASSIGNMENT_LIST_FULL;

    const LV2_HMI_AddressingInfo info = {
        .caps = (LV2_HMI_AddressingCapabilities)caps,
        .flags = (LV2_HMI_AddressingFlags)flags,
        .label = label,
        .min = minimum,
        .max = maximum,
        .steps = steps,
    };
    port->hmi_addressing = addressing;
    effect->hmi_notif->addressed(lilv_instance_get_handle(effect->lilv_instance),
                                 port->index, (LV2_HMI_Addressing)addressing, &info);

    return SUCCESS;
#else
    return ERR_HMI_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
    UNUSED_PARAM(control_symbol);
    UNUSED_PARAM(hw_id);
    UNUSED_PARAM(page);
    UNUSED_PARAM(subpage);
    UNUSED_PARAM(caps);
    UNUSED_PARAM(flags);
    UNUSED_PARAM(label);
    UNUSED_PARAM(minimum);
    UNUSED_PARAM(maximum);
    UNUSED_PARAM(steps);
#endif
}

int effects_hmi_unmap(int effect_id, const char *control_symbol)
{
#ifdef __MOD_DEVICES__
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;

    effect_t *effect = &(g_effects[effect_id]);

    if (effect->hmi_notif == NULL)
        return ERR_INVALID_OPERATION;

    port_t *port = NULL;

    // special handling for bypass, mapped to lv2:enabled designation
    if (!strcmp(control_symbol, g_bypass_port_symbol))
    {
        if (effect->enabled_index >= 0)
            port = effect->ports[effect->enabled_index];
    }
    // do not allow other special ports
    else if (control_symbol[0] != ':')
    {
        port = FindEffectInputPortBySymbol(effect, control_symbol);
    }

    if (port == NULL)
        return ERR_LV2_INVALID_PARAM_SYMBOL;

    effect->hmi_notif->unaddressed(lilv_instance_get_handle(effect->lilv_instance), port->index);

    if (port->hmi_addressing != NULL)
    {
        if (g_hmi_data != NULL)
        {
            char msg[24];
            snprintf(msg, sizeof(msg), "%i", port->hmi_addressing->actuator_id);
            msg[sizeof(msg)-1] = '\0';

            pthread_mutex_lock(&g_hmi_mutex);
            sys_serial_write(&g_hmi_data->server,
                             sys_serial_event_type_unassign,
                             port->hmi_addressing->page,
                             port->hmi_addressing->subpage, msg);
            pthread_mutex_unlock(&g_hmi_mutex);
        }

        port->hmi_addressing->actuator_id = -1;
        port->hmi_addressing = NULL;
    }

    return SUCCESS;
#else
    return ERR_HMI_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
    UNUSED_PARAM(control_symbol);
#endif
}

float effects_jack_cpu_load(void)
{
    return jack_cpu_load(g_jack_global_client);
}

float effects_jack_max_cpu_load(void)
{
#ifdef HAVE_JACK2_1_9_23
    return jack_max_cpu_load(g_jack_global_client);
#else
    return jack_cpu_load(g_jack_global_client);
#endif
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
#else
    UNUSED_PARAM(bpath);
#endif
}

void effects_bundle_remove(const char *bpath, const char *resource)
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

    // unload resource if requested
    if (resource != NULL && resource[0] != '\0')
    {
        LilvNode *resourcenode = lilv_new_uri(g_lv2_data, resource);
        if (resourcenode)
        {
            lilv_world_unload_resource(g_lv2_data, resourcenode);
            lilv_node_free(resourcenode);
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
#else
    UNUSED_PARAM(bpath);
#endif
}

#undef OS_SEP

int effects_state_load(const char *dir)
{
    effect_t *effect;
    LilvState *state;

    char state_filename[PATH_MAX];
    memset(state_filename, 0, sizeof(state_filename));

    LV2_State_Make_Path makePath = {
        NULL, MakePluginStatePathDuringLoadSave
    };
    const LV2_Feature feature_makePath = { LV2_STATE__makePath, &makePath };

    const LV2_Feature* features[] = {
        &g_uri_map_feature,
        &g_urid_map_feature,
        &g_urid_unmap_feature,
        &g_options_feature,
#ifdef __MOD_DEVICES__
        &g_hmi_wc_feature,
#endif
        &g_license_feature,
        &g_buf_size_features[0],
        &g_buf_size_features[1],
        &g_buf_size_features[2],
        &g_lv2_log_feature,
        &g_state_freePath_feature,
        &feature_makePath,
        NULL, // ctrlPortReq
        NULL, // worker
        NULL
    };

    for (int i = 0; i < MAX_PLUGIN_INSTANCES; ++i)
    {
        if (g_effects[i].lilv_instance == NULL)
            continue;
        if (g_effects[i].lilv_plugin == NULL)
            continue;
        if ((g_effects[i].hints & HINT_HAS_STATE) == 0x0)
            continue;

        effect = &g_effects[i];
        snprintf(state_filename, PATH_MAX-1, "%s/effect-%d/effect.ttl", dir, effect->instance);

        if (access(state_filename, F_OK) != 0)
            continue;

        state = lilv_state_new_from_file(g_lv2_data, &g_urid_map, NULL, state_filename);

        if (state == NULL)
        {
            fprintf(stderr, "failed to load effect #%d state from %s\n", effect->instance, state_filename);
            continue;
        }

        makePath.handle = effect;
        features[CTRLPORT_REQUEST_FEATURE] = effect->features[CTRLPORT_REQUEST_FEATURE];
        features[WORKER_FEATURE] = effect->features[WORKER_FEATURE];

        if (effect->hints & HINT_STATE_UNSAFE)
            pthread_mutex_lock(&effect->state_restore_mutex);

        effect->state_dir = dir;

        lilv_state_restore(state,
                           effect->lilv_instance,
                           NULL, NULL,
                           LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE,
                           features);

        effect->state_dir = NULL;

        if (effect->hints & HINT_STATE_UNSAFE)
            pthread_mutex_unlock(&effect->state_restore_mutex);

        lilv_state_free(state);
    }

    return SUCCESS;
}

int effects_state_save(const char *dir)
{
#ifdef _WIN32
    if (_access(dir, 4) != 0 && _mkdir(dir) != 0)
#else
    if (access(dir, F_OK) != 0 && mkdir(dir, 0755) != 0)
#endif
    {
        fprintf(stderr, "failed to get access to project folder %s\n", dir);
        return ERR_INVALID_OPERATION;
    }

    effect_t *effect;
    LilvState *state;
    char *scratch_dir, *plugin_dir;

    char state_dir[PATH_MAX];
    memset(state_dir, 0, sizeof(state_dir));

    LV2_State_Make_Path makePath = {
        NULL, MakePluginStatePathDuringLoadSave
    };
    const LV2_Feature feature_makePath = { LV2_STATE__makePath, &makePath };

    const LV2_Feature* features[] = {
        &g_uri_map_feature,
        &g_urid_map_feature,
        &g_urid_unmap_feature,
        &g_options_feature,
#ifdef __MOD_DEVICES__
        &g_hmi_wc_feature,
#endif
        &g_license_feature,
        &g_buf_size_features[0],
        &g_buf_size_features[1],
        &g_buf_size_features[2],
        &g_lv2_log_feature,
        &g_state_freePath_feature,
        &feature_makePath,
        NULL, // ctrlPortReq
        NULL, // worker
        NULL
    };

    for (int i = 0; i < MAX_PLUGIN_INSTANCES; ++i)
    {
        if (g_effects[i].lilv_instance == NULL)
            continue;
        if (g_effects[i].lilv_plugin == NULL)
            continue;
        if ((g_effects[i].hints & HINT_HAS_STATE) == 0x0)
            continue;

        effect = &g_effects[i];

        plugin_dir = MakePluginStatePath(effect->instance, dir, ".");

        if (plugin_dir != NULL)
        {
            scratch_dir = GetPluginStateDir(effect->instance, g_lv2_scratch_dir);

            makePath.handle = effect;
            features[CTRLPORT_REQUEST_FEATURE] = effect->features[CTRLPORT_REQUEST_FEATURE];
            features[WORKER_FEATURE] = effect->features[WORKER_FEATURE];

            effect->state_dir = dir;

            state = lilv_state_new_from_instance(effect->lilv_plugin,
                                                 effect->lilv_instance,
                                                 &g_urid_map,
                                                 scratch_dir,
                                                 plugin_dir,
                                                 plugin_dir,
                                                 plugin_dir,
                                                 NULL, NULL, // control port values
                                                 LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE,
                                                 features);

            effect->state_dir = NULL;
        }
        else
        {
            scratch_dir = NULL;
            state = NULL;
        }

        if (state != NULL) {
            snprintf(state_dir, PATH_MAX-1, "%s/effect-%d", dir, effect->instance);
            lilv_state_save(g_lv2_data, &g_urid_map, &g_urid_unmap, state, NULL, state_dir, "effect.ttl");
            lilv_state_free(state);
        } else {
            // no state available, delete file if present
            snprintf(state_dir, PATH_MAX-1, "%s/effect-%d/manifest.ttl", dir, effect->instance);
            unlink(state_dir);
            snprintf(state_dir, PATH_MAX-1, "%s/effect-%d/effect.ttl", dir, effect->instance);
            unlink(state_dir);
        }

        free(plugin_dir);
        free(scratch_dir);
    }

    // TODO search and remove old unused state ttls
    /*
    for file in dir/effect*.ttl; do
        if (g_effect[instance].lilv_instance == NULL)
            unlink(state_filename);
    done
    */

    return SUCCESS;
}

int effects_state_set_tmpdir(const char *dir)
{
    char *olddir = g_lv2_scratch_dir;
    char *newdir = str_duplicate(dir);

    if (newdir == NULL)
        return ERR_MEMORY_ALLOCATION;

    g_lv2_scratch_dir = newdir;
    free(olddir);

    return SUCCESS;
}

int effects_aggregated_midi_enable(int enable)
{
    if (g_jack_global_client == NULL)
        return ERR_INVALID_OPERATION;

#ifdef HAVE_JACK2
    if (enable) {
        if (g_midi_in_port == NULL)
            return ERR_INVALID_OPERATION;

        const char **connectedports, **midihwports;
        const char *ourportname = jack_port_name(g_midi_in_port);

        // step 1. disconnect everything from our mod-host port
        connectedports = jack_port_get_connections(g_midi_in_port);
        if (connectedports != NULL) {
            for (int i=0; connectedports[i] != NULL; ++i)
                jack_disconnect(g_jack_global_client, connectedports[i], ourportname);

            jack_free(connectedports);
        }

        // step 2. disconnect everything that is connected to physical MIDI ports
        midihwports = jack_get_ports(g_jack_global_client, "", JACK_DEFAULT_MIDI_TYPE,
                                     JackPortIsTerminal|JackPortIsPhysical|JackPortIsOutput);
        if (midihwports != NULL)
        {
            for (int i=0; midihwports[i] != NULL; ++i)
            {
                connectedports = jack_port_get_connections(jack_port_by_name(g_jack_global_client, midihwports[i]));
                if (connectedports != NULL) {
                    for (int j=0; connectedports[j] != NULL; ++j)
                        jack_disconnect(g_jack_global_client, midihwports[i], connectedports[j]);

                    jack_free(connectedports);
                }
            }

            jack_free(midihwports);
        }

        midihwports = jack_get_ports(g_jack_global_client, "", JACK_DEFAULT_MIDI_TYPE,
                                     JackPortIsTerminal|JackPortIsPhysical|JackPortIsInput);
        if (midihwports != NULL)
        {
            for (int i=0; midihwports[i] != NULL; ++i)
            {
                connectedports = jack_port_get_connections(jack_port_by_name(g_jack_global_client, midihwports[i]));
                if (connectedports != NULL) {
                    for (int j=0; connectedports[j] != NULL; ++j)
                        jack_disconnect(g_jack_global_client, connectedports[j], midihwports[i]);

                    jack_free(connectedports);
                }
            }

            jack_free(midihwports);
        }

        // step 3. load the aggregated midi clients
        if (jack_port_by_name(g_jack_global_client, "mod-midi-merger:out") == NULL)
            if (jack_internal_client_load(g_jack_global_client, "mod-midi-merger",
                                          JackUseExactName|JackLoadName, NULL, "mod-midi-merger") == 0)
                return ERR_JACK_CLIENT_ACTIVATION;

        if (jack_port_by_name(g_jack_global_client, "mod-midi-broadcaster:out") == NULL)
            if (jack_internal_client_load(g_jack_global_client, "mod-midi-broadcaster",
                                          JackUseExactName|JackLoadName, NULL, "mod-midi-broadcaster") == 0)
                return ERR_JACK_CLIENT_ACTIVATION;

        // step 4. Connect to midi-merger
        jack_connect(g_jack_global_client, "mod-midi-merger:out", ourportname);

        // step 5. Connect to midi through ports
        ConnectToMIDIThroughPorts();

        // step 6. Connect all raw-midi ports
        {
            struct list_head *it;
            pthread_mutex_lock(&g_raw_midi_port_mutex);
            list_for_each(it, &g_raw_midi_port_list)
            {
                raw_midi_port_item* const portitemptr = list_entry(it, raw_midi_port_item, siblings);
                jack_connect(g_jack_global_client, "mod-midi-merger:out", jack_port_name(portitemptr->jack_port));
            }
            pthread_mutex_unlock(&g_raw_midi_port_mutex);
        }

    } else {
        // step 1. remove aggregated midi clients
        jack_intclient_t merger = jack_internal_client_handle(g_jack_global_client, "mod-midi-merger", NULL);
        if (merger != 0)
            jack_internal_client_unload(g_jack_global_client, merger);

        jack_intclient_t broadcaster = jack_internal_client_handle(g_jack_global_client, "mod-midi-broadcaster", NULL);
        if (broadcaster != 0)
            jack_internal_client_unload(g_jack_global_client, broadcaster);

        // step 2. connect to all midi hw ports
        ConnectToAllHardwareMIDIPorts();
    }
#endif // HAVE_JACK2

    g_aggregated_midi_enabled = enable != 0;
    return SUCCESS;
}

int effects_cpu_load_enable(int enable)
{
    if (g_jack_global_client == NULL)
        return ERR_INVALID_OPERATION;

    g_cpu_load_enabled = enable != 0;
    effects_output_data_ready();
    return SUCCESS;
}

int effects_freewheeling_enable(int enable)
{
    if (g_jack_global_client == NULL)
        return ERR_INVALID_OPERATION;

    if (enable == 2)
        effects_transport(1, g_transport_bpb, g_transport_bpm);

    return jack_set_freewheel(g_jack_global_client, enable);
}

int effects_processing_enable(int enable)
{
    switch (enable)
    {
    // regular on/off
    case 0:
    case 1:
        g_processing_enabled = enable != 0;
        break;

    // turn on while reporting feedback data ready
    case 2:
        g_processing_enabled = true;
        effects_output_data_ready();
        break;

    // use fade-out while turning off
    case -1:
        monitor_client_setup_volume(MOD_MONITOR_VOLUME_MUTE);
        if (g_processing_enabled)
        {
            monitor_client_wait_volume();
            g_processing_enabled = false;
        }
        break;

    // don't use fade-out while turning off, mute right away
    case -2:
        monitor_client_setup_volume(MOD_MONITOR_VOLUME_MUTE);
        monitor_client_flush_volume();
        g_processing_enabled = false;
        break;

    // use fade-in while turning on
    case 3:
        g_processing_enabled = true;
        monitor_client_setup_volume(0.f);
        break;

    default:
        return ERR_INVALID_OPERATION;
    }

    return SUCCESS;
}

int effects_monitor_audio_levels(const char *source_port_name, int enable)
{
    if (g_jack_global_client == NULL)
        return ERR_INVALID_OPERATION;

    if (enable)
    {
        for (int i = 0; i < g_audio_monitor_count; ++i)
        {
            if (!strcmp(g_audio_monitors[i].source_port_name, source_port_name))
                return SUCCESS;
        }

        pthread_mutex_lock(&g_audio_monitor_mutex);
        g_audio_monitors = realloc(g_audio_monitors, sizeof(audio_monitor_t) * (g_audio_monitor_count + 1));
        pthread_mutex_unlock(&g_audio_monitor_mutex);

        if (g_audio_monitors == NULL)
            return ERR_MEMORY_ALLOCATION;

        audio_monitor_t *monitor = &g_audio_monitors[g_audio_monitor_count];

        char port_name[0xff];
        snprintf(port_name, sizeof(port_name) - 1, "monitor_%d", g_audio_monitor_count + 1);

        jack_port_t *port = jack_port_register(g_jack_global_client,
                                               port_name,
                                               JACK_DEFAULT_AUDIO_TYPE,
                                               JackPortIsInput,
                                               0);
        if (port == NULL)
            return ERR_JACK_PORT_REGISTER;

        snprintf(port_name, sizeof(port_name) - 1, "%s:monitor_%d",
                 jack_get_client_name(g_jack_global_client), g_audio_monitor_count + 1);
        jack_connect(g_jack_global_client, source_port_name, port_name);

        monitor->port = port;
        monitor->source_port_name = strdup(source_port_name);
        monitor->value = 0.f;

        ++g_audio_monitor_count;
    }
    else
    {
        if (g_audio_monitor_count == 0)
            return ERR_INVALID_OPERATION;

        audio_monitor_t *monitor = &g_audio_monitors[g_audio_monitor_count - 1];

        if (strcmp(monitor->source_port_name, source_port_name))
            return ERR_INVALID_OPERATION;

        pthread_mutex_lock(&g_audio_monitor_mutex);
        --g_audio_monitor_count;
        pthread_mutex_unlock(&g_audio_monitor_mutex);

        jack_port_unregister(g_jack_global_client, monitor->port);
        free(monitor->source_port_name);

        if (g_audio_monitor_count == 0)
        {
            free(g_audio_monitors);
            g_audio_monitors = NULL;
        }
    }

    return SUCCESS;
}

int effects_monitor_midi_control(int channel, int enable)
{
    if (channel < 0 || channel > 15)
        return ERR_INVALID_OPERATION;

    g_monitored_midi_controls[channel] = enable != 0;
    return SUCCESS;
}

int effects_monitor_midi_program(int channel, int enable)
{
    if (channel < 0 || channel > 15)
        return ERR_INVALID_OPERATION;

    g_monitored_midi_programs[channel] = enable != 0;
    return SUCCESS;
}

void effects_transport(int rolling, double beats_per_bar, double beats_per_minute)
{
    // give warning if changing BPM while clock slave is enabled
    if (g_transport_sync_mode == TRANSPORT_SYNC_MIDI && fabs(g_transport_bpm - beats_per_minute) > 0.1) {
        fprintf(stderr, "trying to change transport BPM while MIDI sync enabled, expect issues!\n");
    }

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

        if (g_verbose_debug) {
            puts("DEBUG: effects_transport old timebase master not valid, mod-host is master again");
        }
    }

    const bool rolling_changed = g_jack_rolling != (rolling != 0) ||
        g_jack_rolling != (jack_transport_query(g_jack_global_client, NULL) == JackTransportRolling);

    if (rolling_changed)
    {
        if (rolling)
        {
            jack_transport_start(g_jack_global_client);
            if (g_verbose_debug) {
                puts("DEBUG: effects_transport started rolling");
            }
        }
        else
        {
            jack_transport_stop(g_jack_global_client);
            jack_transport_locate(g_jack_global_client, 0);
            if (g_verbose_debug) {
                puts("DEBUG: effects_transport stopped rolling and relocated to frame 0");
            }
        }
        // g_jack_rolling is updated on the next jack callback
        g_transport_reset = true;
    }
    else
    {
        if (g_verbose_debug) {
            puts("DEBUG: effects_transport rolling status ignored");
        }
    }

    if (g_verbose_debug) {
        printf("DEBUG: Transport changed to rolling:%d bpm:%f bpb:%f\n", rolling, beats_per_minute, beats_per_bar);
    }

    TriggerJackTimebase(rolling_changed);
}

int effects_transport_sync_mode(const char* mode)
{
    if (mode == NULL)
        return ERR_INVALID_OPERATION;

    if (!strcmp(mode, "link"))
    {
#ifdef HAVE_HYLIA
        if (g_hylia_instance)
        {
            if (g_transport_sync_mode != TRANSPORT_SYNC_ABLETON_LINK)
                g_transport_sync_mode = TRANSPORT_SYNC_NONE; // disabling sync mode first
            hylia_enable(g_hylia_instance, true);
            g_transport_sync_mode = TRANSPORT_SYNC_ABLETON_LINK;
            g_transport_reset = true;
            return SUCCESS;
        }
#endif
        g_transport_sync_mode = TRANSPORT_SYNC_NONE;
        return ERR_ABLETON_LINK_UNAVAILABLE;
    }

#ifdef HAVE_HYLIA
    // disable link if previously enabled
    if (g_transport_sync_mode == TRANSPORT_SYNC_ABLETON_LINK)
    {
        g_transport_sync_mode = TRANSPORT_SYNC_NONE; // disabling sync mode first
        hylia_enable(g_hylia_instance, false);
        g_transport_reset = true;
    }
#endif

    if (!strcmp(mode, "midi"))
    {
        g_previous_midi_event_time = 0;
        g_transport_sync_mode = TRANSPORT_SYNC_MIDI;
        effects_output_data_ready();
        return SUCCESS;
    }

    g_transport_sync_mode = TRANSPORT_SYNC_NONE;
    return SUCCESS;
}

void effects_output_data_ready(void)
{
    if (g_verbose_debug) {
        printf("DEBUG: effects_output_data_ready() UI is ready to receive more stuff (code %i)\n",
               g_postevents_ready);
        fflush(stdout);
    }

    if (! g_postevents_ready)
    {
        g_postevents_ready = true;
        sem_post(&g_postevents_semaphore);
    }
}

int effects_show_external_ui(int effect_id)
{
#ifdef WITH_EXTERNAL_UI_SUPPORT
    if (!InstanceExist(effect_id))
        return ERR_INSTANCE_NON_EXISTS;
    if (effect_id >= MAX_PLUGIN_INSTANCES)
        return ERR_ASSIGNMENT_INVALID_OP;

    effect_t *effect = &g_effects[effect_id];
    LilvUIs *uis = lilv_plugin_get_uis(effect->lilv_plugin);

    if (uis == NULL)
        return ERR_ASSIGNMENT_INVALID_OP;

    LILV_FOREACH(nodes, i, uis)
    {
        const LilvUI *ui = lilv_uis_get(uis, i);
        lilv_world_load_resource(g_lv2_data, lilv_ui_get_uri(ui));

        const LilvNode *binary_node = lilv_ui_get_binary_uri(ui);
        const LilvNode *bundle_node = lilv_ui_get_bundle_uri(ui);

        void *libhandle = dlopen(lilv_file_uri_parse(lilv_node_as_string(binary_node), NULL), RTLD_NOW|RTLD_LOCAL);
        if (libhandle == NULL)
            continue;

        // stuff that could need cleanup
        LV2UI_Handle *handle = NULL;
        const LV2UI_Descriptor *desc = NULL;
        const LV2UI_Idle_Interface *idle_iface;
        const LV2UI_Show_Interface *show_iface;
        uint32_t index = 0;

        LV2UI_DescriptorFunction descfn;
        if ((descfn = (LV2UI_DescriptorFunction)dlsym(libhandle, "lv2ui_descriptor")) == NULL)
            goto cleanup;

        while ((desc = descfn(index++)) != NULL)
        {
            if (desc->extension_data == NULL)
                continue;
            if ((idle_iface = desc->extension_data(LV2_UI__idleInterface)) == NULL)
                continue;
            if ((show_iface = desc->extension_data(LV2_UI__showInterface)) == NULL)
                continue;

            // if we got this far, we have everything needed
            LV2_Extension_Data_Feature extension_data = {
                effect->lilv_instance->lv2_descriptor->extension_data
            };
            const LV2_Feature feature_dataAccess = { LV2_DATA_ACCESS_URI, &extension_data };
            const LV2_Feature feature_instAccess = { LV2_INSTANCE_ACCESS_URI, effect->lilv_instance->lv2_handle };
            const LV2_Feature* features[] = {
                &g_uri_map_feature,
                &g_urid_map_feature,
                &g_urid_unmap_feature,
                &g_options_feature,
                &g_buf_size_features[0],
                &g_buf_size_features[1],
                &g_buf_size_features[2],
                &g_lv2_log_feature,
                &feature_dataAccess,
                &feature_instAccess,
                NULL
            };

            LV2UI_Widget widget;
            handle = desc->instantiate(desc,
                                       lilv_node_as_uri(lilv_plugin_get_uri(effect->lilv_plugin)),
                                       lilv_file_uri_parse(lilv_node_as_string(bundle_node), NULL),
                                       ExternalControllerWriteFunction,
                                       effect, &widget, features);

            if (handle == NULL)
                continue;

            // notify UI of current state
            if (desc->port_event != NULL)
            {
                for (uint32_t j = 0; j < effect->control_ports_count; j++)
                    desc->port_event(handle,
                                     effect->control_ports[j]->index,
                                     sizeof(float), 0,
                                     effect->control_ports[j]->buffer);
            }

            show_iface->show(handle);

            effect->ui_desc = desc;
            effect->ui_handle = handle;
            effect->ui_idle_iface = idle_iface;
            effect->ui_libhandle = libhandle;

            lilv_uis_free(uis);
            return SUCCESS;
        }

cleanup:
        if (desc != NULL && handle != NULL && desc->cleanup != NULL)
            desc->cleanup(handle);

        dlclose(libhandle);
    }

    lilv_uis_free(uis);
    return ERR_INVALID_OPERATION;
#else
    return ERR_EXTERNAL_UI_UNAVAILABLE;

    UNUSED_PARAM(effect_id);
#endif
}

void effects_idle_external_uis(void)
{
#ifdef WITH_EXTERNAL_UI_SUPPORT
    for (int i = 0; i < MAX_PLUGIN_INSTANCES; ++i)
    {
        effect_t *effect = &g_effects[i];

        if (effect->lilv_instance == NULL)
            continue;
        if (effect->lilv_plugin == NULL)
            continue;

        if (effect->ui_handle && effect->ui_idle_iface)
        {
            if (effect->ui_desc->port_event != NULL)
            {
                port_t *port;
                for (uint32_t j = 0; j < effect->input_control_ports_count; j++)
                {
                    port = effect->input_control_ports[j];
                    if (port->hints & HINT_SHOULD_UPDATE)
                    {
                        port->hints &= ~HINT_SHOULD_UPDATE;
                        effect->ui_desc->port_event(effect->ui_handle,
                                                    port->index,
                                                    sizeof(float), 0,
                                                    port->buffer);
                    }
                }
                for (uint32_t j = 0; j < effect->output_control_ports_count; j++)
                {
                    effect->ui_desc->port_event(effect->ui_handle,
                                                effect->output_control_ports[j]->index,
                                                sizeof(float), 0,
                                                effect->output_control_ports[j]->buffer);
                }
            }

            effect->ui_idle_iface->idle(effect->ui_handle);
        }
    }
#endif
}
