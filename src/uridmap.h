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

#ifndef URID_H
#define URID_H

#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/uri-map/uri-map.h>

void urid_sem_init(void);

LV2_URID map_urid(LV2_URID_Map_Handle handle, const char* uri);
const char* unmap_urid(LV2_URID_Unmap_Handle handle, LV2_URID urid);

uint32_t uri_to_id(LV2_URI_Map_Callback_Data callback_data, const char* map, const char* uri);
LV2_URID urid_to_id(LV2_URID_Map_Handle handle, const char* uri);
const char* id_to_urid(LV2_URID_Unmap_Handle handle, LV2_URID urid);

#endif
