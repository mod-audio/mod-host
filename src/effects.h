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

#ifndef EFFECTS_H
#define EFFECTS_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           DO NOT CHANGE THESE DEFINES
************************************************************************************************************************
*/

/* Errors definitions */
enum {
    SUCCESS = 0,
    ERR_INSTANCE_INVALID = -1,
    ERR_INSTANCE_ALREADY_EXISTS = -2,
    ERR_INSTANCE_NON_EXISTS = -3,
    ERR_INSTANCE_UNLICENSED = -4,

    ERR_LV2_INVALID_URI = -101,
    ERR_LV2_INSTANTIATION = -102,
    ERR_LV2_INVALID_PARAM_SYMBOL = -103,
    ERR_LV2_INVALID_PRESET_URI = -104,
    ERR_LV2_CANT_LOAD_STATE = -105,

    ERR_JACK_CLIENT_CREATION = -201,
    ERR_JACK_CLIENT_ACTIVATION = -202,
    ERR_JACK_CLIENT_DEACTIVATION = -203,
    ERR_JACK_PORT_REGISTER = -204,
    ERR_JACK_PORT_CONNECTION = -205,
    ERR_JACK_PORT_DISCONNECTION = -206,
    ERR_JACK_VALUE_OUT_OF_RANGE = -207,

    ERR_ASSIGNMENT_ALREADY_EXISTS = -301,
    ERR_ASSIGNMENT_INVALID_OP = -302,
    ERR_ASSIGNMENT_LIST_FULL = -303,
    ERR_ASSIGNMENT_FAILED = -304,
    ERR_ASSIGNMENT_UNUSED = -305,

    ERR_CONTROL_CHAIN_UNAVAILABLE = -401,
    ERR_ABLETON_LINK_UNAVAILABLE = -402,
    ERR_HMI_UNAVAILABLE = -403,
    ERR_EXTERNAL_UI_UNAVAILABLE = -404,

    ERR_MEMORY_ALLOCATION = -901,
    ERR_INVALID_OPERATION = -902
};

/* Log definitions */
typedef enum {
    LOG_TRACE = 0,
    LOG_NOTE = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3
} LogType;

/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

#define MAX_PLUGIN_INSTANCES    9990
#define MAX_TOOL_INSTANCES      10
#define MAX_INSTANCES           (MAX_PLUGIN_INSTANCES + MAX_TOOL_INSTANCES)
#define MAX_MIDI_CC_ASSIGN      1024
#define MAX_POSTPONED_EVENTS    8192
#define MAX_HMI_ADDRESSINGS     128

// used for local stack variables
#define MAX_CHAR_BUF_SIZE       255


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

typedef struct {
    const char *label;
    float value;
} scalepoint_t;

typedef struct {
    const char *symbol;
    float value;
} flushed_param_t;


/*
************************************************************************************************************************
*           GLOBAL VARIABLES
************************************************************************************************************************
*/


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

int effects_init(void* client);
int effects_finish(int close_client);
int effects_add(const char *uri, int instance, int activate);
int effects_remove(int effect_id);
int effects_activate(int effect_id, int value);
int effects_preset_load(int effect_id, const char *uri);
int effects_preset_save(int effect_id, const char *dir, const char *file_name, const char *label);
int effects_preset_show(const char *uri, char **state_str);
int effects_connect(const char *portA, const char *portB);
int effects_disconnect(const char *portA, const char *portB);
int effects_disconnect_all(const char *port);
int effects_set_parameter(int effect_id, const char *control_symbol, float value);
int effects_get_parameter(int effect_id, const char *control_symbol, float *value);
int effects_flush_parameters(int effect_id, int reset, int param_count, const flushed_param_t *params);
int effects_set_property(int effect_id, const char *uri, const char *value);
int effects_get_property(int effect_id, const char *uri);
int effects_monitor_parameter(int effect_id, const char *control_symbol, const char *op, float value);
int effects_monitor_output_parameter(int effect_id, const char *control_symbol, int enable);
int effects_bypass(int effect_id, int value);
int effects_get_parameter_symbols(int effect_id, int output_ports, const char** symbols);
int effects_get_presets_uris(int effect_id, const char **uris);
int effects_get_parameter_info(int effect_id, const char *control_symbol, float **range, const char **scale_points);
int effects_midi_learn(int effect_id, const char *control_symbol, float minimum, float maximum);
int effects_midi_map(int effect_id, const char *control_symbol, int channel, int controller, float minimum, float maximum);
int effects_midi_unmap(int effect_id, const char *control_symbol);
int effects_licensee(int effect_id, char **licensee);
int effects_set_beats_per_minute(double bpm);
int effects_set_beats_per_bar(float bpb);

int effects_cc_map(int effect_id, const char *control_symbol, int device_id, int actuator_id,
                   const char* label, float value, float minimum, float maximum, int steps, int extraflags,
                   const char *unit, int scalepoints_count, const scalepoint_t *scalepoints);
int effects_cc_value_set(int effect_id, const char *control_symbol, float value);
int effects_cc_unmap(int effect_id, const char *control_symbol);

int effects_cv_map(int effect_id, const char *control_symbol, const char *source_port_name, float minimum, float maximum, const char* mode);
int effects_cv_unmap(int effect_id, const char *control_symbol);

int effects_hmi_map(int effect_id, const char *control_symbol, int hw_id, int page, int subpage,
                    int caps, int flags, const char *label, float minimum, float maximum, int steps);
int effects_hmi_unmap(int effect_id, const char *control_symbol);

float effects_jack_cpu_load(void);
float effects_jack_max_cpu_load(void);
void effects_bundle_add(const char *bundlepath);
void effects_bundle_remove(const char *bundlepath, const char *resource);
int effects_state_load(const char *dir);
int effects_state_save(const char *dir);
int effects_state_set_tmpdir(const char *dir);
int effects_aggregated_midi_enable(int enable);
int effects_cpu_load_enable(int enable);
int effects_freewheeling_enable(int enable);
int effects_processing_enable(int enable);
int effects_monitor_audio_levels(const char *source_port_name, int enable);
int effects_monitor_midi_control(int channel, int enable);
int effects_monitor_midi_program(int channel, int enable);
void effects_transport(int rolling, double beats_per_bar, double beats_per_minute);
int effects_transport_sync_mode(const char *mode);
void effects_output_data_ready(void);
int effects_show_external_ui(int effect_id);
void effects_idle_external_uis(void);

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
