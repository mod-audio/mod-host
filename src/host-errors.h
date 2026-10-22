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

#ifndef HOST_ERRORS_H
#define HOST_ERRORS_H


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

    ERR_HOST_INVALID_URI = -101,
    ERR_HOST_INSTANTIATION = -102,
    ERR_HOST_INVALID_PARAM_SYMBOL = -103,
    ERR_HOST_INVALID_PRESET_URI = -104,
    ERR_HOST_CANT_LOAD_STATE = -105,

    /* the names above under the LV2 prefix they were introduced with */
    ERR_LV2_INVALID_URI = ERR_HOST_INVALID_URI,
    ERR_LV2_INSTANTIATION = ERR_HOST_INSTANTIATION,
    ERR_LV2_INVALID_PARAM_SYMBOL = ERR_HOST_INVALID_PARAM_SYMBOL,
    ERR_LV2_INVALID_PRESET_URI = ERR_HOST_INVALID_PRESET_URI,
    ERR_LV2_CANT_LOAD_STATE = ERR_HOST_CANT_LOAD_STATE,

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


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
