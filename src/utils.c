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

#include "utils.h"
#include <float.h>
#include <math.h>
#include <string.h>


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/


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

static void parse_quote(char *str)
{
    char *pquote, *pstr = str;

    while (*pstr)
    {
        if (*pstr == '"')
        {
            // shift the string to left
            pquote = pstr;
            while (*pquote)
            {
                *pquote = *(pquote+1);
                pquote++;
            }
        }
        pstr++;
    }
}

static void trim_spaces(char *str)
{
    char *pstr = str;
    char c;

    while (*pstr) pstr++;
    while (1)
    {
        c = *(--pstr);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') *pstr = 0;
        else break;
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

bool floats_differ_enough(float a, float b)
{
    return fabsf(a - b) >= FLT_EPSILON;
}

char** strarr_split(char *str)
{
    uint32_t count;
    char *pstr, **list = NULL;
    const char token = ' ';
    uint8_t quote = 0;

    if (!str) return list;

    trim_spaces(str);

    // count the tokens
    pstr = str;
    count = 1;
    while (*pstr)
    {
        if (*pstr == token && quote == 0)
        {
            count++;
        }
#ifdef ENABLE_QUOTATION_MARKS
        if (*pstr == '"')
        {
            if (quote == 0) quote = 1;
            else
            {
                if (*(pstr+1) == '"') pstr++;
                else quote = 0;
            }
        }
#endif
        pstr++;
    }

    // allocates memory to list
    list = MALLOC((count + 1) * sizeof(char *));
    if (list == NULL) return NULL;

    // fill the list pointers
    pstr = str;
    list[0] = pstr;
    count = 0;
    while (*pstr)
    {
        if (*pstr == token && quote == 0)
        {
            *pstr = '\0';
            list[++count] = pstr + 1;
        }
#ifdef ENABLE_QUOTATION_MARKS
        if (*pstr == '"')
        {
            if (quote == 0) quote = 1;
            else
            {
                if (*(pstr+1) == '"') pstr++;
                else quote = 0;
            }
        }
#endif
        pstr++;
    }

    list[++count] = NULL;

#ifdef ENABLE_QUOTATION_MARKS
    count = 0;
    while (list[count]) parse_quote(list[count++]);
#endif

    return list;
}


uint32_t strarr_length(char** const str_array)
{
    uint32_t count = 0;

    if (str_array) while (str_array[count]) count++;
    return count;
}


char* strarr_join(char **str_array)
{
    uint32_t i, len = strarr_length(str_array);

    if (!str_array) return NULL;

    for (i = 1; i < len; i++)
    {
        (*(str_array[i] - 1)) = ' ';
    }

    return (*str_array);
}

char *str_duplicate(const char *str)
{
    char *copy = MALLOC(strlen(str) + 1);
    strcpy(copy, str);
    return copy;
}
