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

#ifndef  MOD_HOST_H
#define  MOD_HOST_H


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


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

/* Socket definitions */
#define SOCKET_DEFAULT_PORT     5555
#define SOCKET_MSG_BUFFER_SIZE  1024

/* Protocol commands definition */
#define EFFECT_ADD          "add %s %i"
#define EFFECT_REMOVE       "remove %i"
#define EFFECT_PRESET_LOAD  "preset_load %i %s"
#define EFFECT_PRESET_SAVE  "preset_save %i %s %s %s"
#define EFFECT_PRESET_SHOW  "preset_show %s"
#define EFFECT_CONNECT      "connect %s %s"
#define EFFECT_DISCONNECT   "disconnect %s %s"
#define EFFECT_BYPASS       "bypass %i %i"
#define EFFECT_PARAM_SET    "param_set %i %s %s"
#define EFFECT_PARAM_GET    "param_get %i %s"
#define EFFECT_PARAM_MON    "param_monitor %i %s %s %f"
#define EFFECT_LICENSEE     "licensee %i"
#define MONITOR_ADDR_SET    "monitor %s %i %i"
#define MONITOR_OUTPUT      "monitor_output %i %s"
#define MIDI_LEARN          "midi_learn %i %s %f %f"
#define MIDI_MAP            "midi_map %i %s %i %i %f %f"
#define MIDI_UNMAP          "midi_unmap %i %s"
#define MIDI_PROGRAM_LISTEN "midi_program_listen %i %i"
#define CC_MAP              "cc_map %i %s %i %i %s %f %f %f %i %s %i ..."
#define CC_UNMAP            "cc_unmap %i %s"
#define CPU_LOAD            "cpu_load"
#define LOAD_COMMANDS       "load %s"
#define SAVE_COMMANDS       "save %s"
#define BUNDLE_ADD          "bundle_add %s"
#define BUNDLE_REMOVE       "bundle_remove %s"
#define FEATURE_ENABLE      "feature_enable %s %i"
#define TRANSPORT           "transport %i %f %f"
#define OUTPUT_DATA_READY   "output_data_ready"
#define HELP                "help"
#define QUIT                "quit"


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
