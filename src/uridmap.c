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

#include "uridmap.h"
#include "symap.h"
#include "mod-semaphore.h"
#include "zix/thread.h"

#define UNUSED_PARAM(var) do { (void)(var); } while (0)

static sem_t symap_lock;

void urid_sem_init(void)
{
    sem_init(&symap_lock, 0, 1);
}

LV2_URID map_urid(LV2_URID_Map_Handle handle, const char* uri)
{
    sem_wait(&symap_lock);
    const LV2_URID id = symap_map(handle, uri);
    sem_post(&symap_lock);
    return id;
}

const char* unmap_urid(LV2_URID_Unmap_Handle handle, LV2_URID urid)
{
    sem_wait(&symap_lock);
    const char *uri = symap_unmap(handle, urid);
    sem_post(&symap_lock);
    return uri;
}

LV2_URID urid_to_id(LV2_URID_Map_Handle handle, const char* uri)
{
    return map_urid(handle, uri);
}

const char* id_to_urid(LV2_URID_Unmap_Handle handle, LV2_URID urid)
{
    return unmap_urid(handle, urid);
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

uint32_t uri_to_id(LV2_URI_Map_Callback_Data callback_data, const char* map, const char* uri)
{
    UNUSED_PARAM(map);
    return map_urid(callback_data, uri);
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#pragma GCC diagnostic pop
#endif
