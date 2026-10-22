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

#ifndef HOST_BACKEND_H
#define HOST_BACKEND_H


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

/* The plugin backend behind the socket protocol, or called directly by a host in the same process.
   A NULL entry answers ERR_INVALID_OPERATION. Fields are only ever appended. */
typedef struct HOST_BACKEND_T {
    int (*add)(const char *uri, int instance, const char *client_name);
    int (*remove)(int instance);
    int (*bypass)(int instance, int value);
    int (*param_set)(int instance, const char *symbol, float value);
    int (*param_get)(int instance, const char *symbol, float *value);
    int (*preset_load)(int instance, const char *uri);
    int (*state_save)(const char *dir);
    int (*state_load)(const char *dir);
    int (*connect)(const char *port_a, const char *port_b);
    int (*disconnect)(const char *port_a, const char *port_b);
} host_backend_t;


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
