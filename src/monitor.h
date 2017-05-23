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

#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define MAX_ATOM_JSON 16384

/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct {
    LV2_Atom_Forge*   forge;
    LV2_URID_Unmap*   unmap;
    size_t            len;
    bool              overflow;
    uint8_t           buf[MAX_ATOM_JSON];
} ModAtomWriter;


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/


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


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

int monitor_start(char *addr, int port);
int monitor_status(void);
int monitor_stop(void);

int monitor_send(int instance, const char *symbol, float value);
int monitor_check_condition(int op, float cond_value, float value);

void init_atom_writer (ModAtomWriter* w, LV2_Atom_Forge* forge, LV2_URID_Unmap* unmap);
int monitor_format_atom (ModAtomWriter* w, int instance, const char *symbol, uint32_t type_urid, uint32_t size, uint8_t* body);
int monitor_send_atom (ModAtomWriter* w, int instance, const char *symbol, uint32_t type_urid, uint32_t size, uint8_t* body);
