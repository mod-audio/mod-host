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
 * ***********************************************************************************************************************
 *           INCLUDE FILES
 ************************************************************************************************************************
 */

#include "lilv.h"

#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/log/log.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/patch/patch.h>
#include <lv2/port-props/port-props.h>
#include <lv2/presets/presets.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/worker/worker.h>
#include "lv2/kxstudio-properties.h"
#include "lv2/lv2-hmi.h"
#include "lv2/mod-license.h"


/*
 * ***********************************************************************************************************************
 *           LOCAL DEFINES
 ************************************************************************************************************************
 */

#define LILV_NS_MOD "http://moddevices.com/ns/mod#"


/*
 * ***********************************************************************************************************************
 *           LOCAL CONSTANTS
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           LOCAL DATA TYPES
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           LOCAL MACROS
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           LOCAL GLOBAL VARIABLES
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           LOCAL FUNCTION PROTOTYPES
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           LOCAL CONFIGURATION ERRORS
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           LOCAL FUNCTIONS
 ************************************************************************************************************************
 */


/*
 * ***********************************************************************************************************************
 *           GLOBAL FUNCTIONS
 ************************************************************************************************************************
 */

lilv_nodes_t g_lilv_nodes = { 0 };
LilvWorld *g_lv2_data = NULL;
Symap* g_symap = NULL;
urids_t g_urids = { 0 };

const LilvPlugins *g_plugins = NULL;
char *g_lv2_scratch_dir = NULL;

void lilv_init()
{
    /* Load all LV2 data */
    g_lv2_data = lilv_world_new();
#ifdef LILV_OPTION_OBJECT_INDEX
    lilv_world_set_option(g_lv2_data, LILV_OPTION_OBJECT_INDEX, NULL);
#endif
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
    g_lilv_nodes.noPreRun = lilv_new_uri(g_lv2_data, "http://www.darkglass.com/lv2/ns#noPreRun");
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
}

void lilv_cleanup()
{
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
    lilv_node_free(g_lilv_nodes.noPreRun);
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
}
