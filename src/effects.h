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

#ifndef EFFECT_H
#define EFFECT_H


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

    ERR_LV2_INVALID_URI = -101,
    ERR_LILV_INSTANTIATION = -102,

    ERR_JACK_CLIENT_CREATION = -201,
    ERR_JACK_CLIENT_ACTIVATION = -202,
    ERR_JACK_CLIENT_DEACTIVATION = -203,
    ERR_JACK_PORT_REGISTER = -204,
    ERR_JACK_PORT_CONNECTION = -205,
    ERR_JACK_PORT_DISCONNECTION = -206,

    ERR_MEMORY_ALLOCATION = -301,
};


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

#define MAX_INSTANCES           10000
#define AUDIO_INPUT_PORTS       2
#define AUDIO_OUTPUT_PORTS      2


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/


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

int effects_init(void);
int effects_finish(void);
int effects_add(const char *uid, int instance);
int effects_remove(int effect_id);
int effects_connect(const char *portA, const char *portB);
int effects_disconnect(const char *portA, const char *portB);
int effects_set_parameter(int effect_id, const char *control_symbol, float value);
int effects_get_parameter(int effect_id, const char *control_symbol, float *value);
int effects_bypass(int effect_id, int value);


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
