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

#include <string.h>
#include <stdlib.h>
#include "utils.h"

str_array_t string_split(const char *str, const char token)
{
    unsigned int i = 0, j = 0, k = 0;
    char *str_copy, *pstr;
    str_array_t str_array;

    str_array.count = 0;
    str_array.data = NULL;
    if (!str) return str_array;

    str_copy = strdup(str);
    pstr = str_copy;

    /* count the tokens */
    str_array.count = 1;
    while (*pstr)
    {
        if (*pstr == token) str_array.count++;
        pstr++;
    }

    /* allocates memory to list */
    str_array.data = calloc(str_array.count, sizeof(char *));

    /* fill the list pointers */
    pstr = str_copy;
    while (*pstr)
    {
        if (*pstr == token)
        {
            *pstr = '\0';
            str_array.data[j] = &str_copy[k];
            j++;
            k = i + 1;
        }

        i++;
        pstr++;
    }

    str_array.data[j] = &str_copy[k];

    return str_array;
}

void free_str_array(str_array_t str_array)
{
    if (str_array.data[0]) free(str_array.data[0]);
    if (str_array.data) free(str_array.data);
}
