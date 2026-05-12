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

#ifndef LILV_H
#define LILV_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "uridmap.h"
#include "symap.h"

#include <lilv/lilv.h>


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           DATA TYPES
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
    HINT_ENUMERATION    = 1 << 0,
    HINT_INTEGER        = 1 << 1,
    HINT_TOGGLE         = 1 << 2,
    HINT_TRIGGER        = 1 << 3,
    HINT_LOGARITHMIC    = 1 << 4,
    HINT_MONITORED      = 1 << 5, // outputs only
    HINT_SHOULD_UPDATE  = 1 << 6, // inputs only, for external UIs | FIXME remove this
    HINT_STATE_INACTIVE = 1 << 7,
    HINT_STATE_BLOCKED  = 1 << 8,
    // cv
    HINT_CV_MOD         = 1 << 0, // uses mod cvport
    HINT_CV_RANGES      = 1 << 1, // port info includes ranges
    // events
    HINT_TRANSPORT      = 1 << 0,
    HINT_MIDI_EVENT     = 1 << 1,
    HINT_OLD_EVENT_API  = 1 << 2,
};

enum PluginHints {
    //HINT_TRANSPORT     = 1 << 0, // must match HINT_TRANSPORT set above
    HINT_TRIGGERS        = 1 << 1,
    HINT_OUTPUT_MONITORS = 1 << 2,
    HINT_HAS_MIDI_INPUT  = 1 << 3,
    HINT_HAS_STATE       = 1 << 4,
    HINT_STATE_UNSAFE    = 1 << 5, // state restore needs mutex protection
    HINT_IS_LIVE         = 1 << 6, // needs to be always running, cannot have processing disabled
    HINT_NO_PRE_RUN      = 1 << 7, // do not keep plugin active for pre-run
};

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
    LilvNode *noPreRun;
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

typedef struct LV2_FEATURES_T {
    bool hmi;
    bool license;
    bool options;
    bool state;
    bool stateLoadDefault;
    bool stateThreadSafeRestore;
    bool worker;
} lv2_features_t;

typedef struct LV2_PORT_T {
    enum PortType type;
    enum PortFlow flow;
    enum PortHints hints;
    const char *symbol;
    float min_value;
    float max_value;
    float def_value;
    LilvScalePoints* scale_points;
} lv2_port_t;



/*
************************************************************************************************************************
*           GLOBAL VARIABLES
************************************************************************************************************************
*/

extern lilv_nodes_t g_lilv_nodes;
extern LilvWorld *g_lv2_data;
extern Symap* g_symap;
extern urids_t g_urids;

extern char *g_lv2_scratch_dir;


/*
************************************************************************************************************************
*           MACRO'S
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

void lilv_init();
void lilv_cleanup();

void lilv_add_bundle(const char *path);
void lilv_remove_bundle(const char *path, const char *resource);

const LilvPlugin* lilv_get_plugin(const char *uri);

uint32_t lilv_get_port_index(const LilvPlugin *plugin, const char *symbol);


/*
************************************************************************************************************************
*           CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
