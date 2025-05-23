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

#ifndef MONITOR_CLIENT_H
#define MONITOR_CLIENT_H


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

#if defined(_DARKGLASS_DEVICE_PABLITO)
#define MOD_MONITOR_VOLUME_MUTE -60.f
#define MOD_MONITOR_VOLUME_WAIT 0.02f
#else
#define MOD_MONITOR_VOLUME_MUTE -30.f
#define MOD_MONITOR_VOLUME_WAIT 0.03f
#endif


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

bool monitor_client_init(void);
void monitor_client_stop(void);

bool monitor_client_setup_compressor(int mode, float release);
bool monitor_client_setup_volume(float volume);
bool monitor_client_flush_volume(void);
bool monitor_client_wait_volume(void);


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
