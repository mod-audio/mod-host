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

    ERR_ASSIGNMENT_ALREADY_EXISTS = -301,
    ERR_ASSIGNMENT_INVALID_OP = -302,
    ERR_ASSIGNMENT_LIST_FULL = -303,
    ERR_ASSIGNMENT_FAILED = -304,

    ERR_CONTROL_CHAIN_UNAVAILABLE = -401,
    ERR_LINK_UNAVAILABLE = -402,

    ERR_MEMORY_ALLOCATION = -901,
    ERR_INVALID_OPERATION = -902,
};


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

#define MAX_INSTANCES           10000
#define MAX_MIDI_CC_ASSIGN      1024
#define MAX_POSTPONED_EVENTS    8192

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
int effects_add(const char *uid, int instance);
int effects_remove(int effect_id);
int effects_preset_load(int effect_id, const char *uri);
int effects_preset_save(int effect_id, const char *dir, const char *file_name, const char *label);
int effects_preset_show(const char *uri, char **state_str);
int effects_connect(const char *portA, const char *portB);
int effects_disconnect(const char *portA, const char *portB);
int effects_set_parameter(int effect_id, const char *control_symbol, float value);
int effects_set_property(int effect_id, const char *label, const char *value);
int effects_get_parameter(int effect_id, const char *control_symbol, float *value);
int effects_monitor_parameter(int effect_id, const char *control_symbol, const char *op, float value);
int effects_monitor_output_parameter(int effect_id, const char *control_symbol);
int effects_bypass(int effect_id, int value);
int effects_get_parameter_symbols(int effect_id, int output_ports, const char** symbols);
int effects_get_presets_uris(int effect_id, const char **uris);
int effects_get_parameter_info(int effect_id, const char *control_symbol, float **range, const char **scale_points);
int effects_midi_learn(int effect_id, const char *control_symbol, float minimum, float maximum);
int effects_midi_map(int effect_id, const char *control_symbol, int channel, int controller, float minimum, float maximum);
int effects_midi_unmap(int effect_id, const char *control_symbol);
int effects_licensee(int effect_id, char **licensee);
void effects_midi_program_listen(int enable, int channel);
int effects_cc_map(int effect_id, const char *control_symbol, int device_id, int actuator_id,
                   const char* label, float value, float minimum, float maximum, int steps, const char *unit,
                   int scalepoints_count, const scalepoint_t *scalepoints);
int effects_cc_unmap(int effect_id, const char *control_symbol);
float effects_jack_cpu_load(void);
void effects_bundle_add(const char* bundlepath);
void effects_bundle_remove(const char* bundlepath);
int effects_link_enable(int enable);
int effects_processing_enable(int enable);
void effects_transport(int rolling, double beats_per_bar, double beats_per_minute);
void effects_output_data_ready(void);

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
