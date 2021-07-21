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

#ifndef UTILS_H
#define UTILS_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <math.h>
#include <float.h>


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

// uncomment the define below to enable the quotation marks evaluation on strarr_split parser
#define ENABLE_QUOTATION_MARKS


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

// message data struct
typedef struct MSG_T {
    int sender_id;
    char *data;
    size_t data_size;
} msg_t;


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

#define MALLOC(n)       malloc(n)
#define FREE(pv)        free(pv)


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// splits the string in each whitespace occurrence and returns an array of strings NULL terminated
char** strarr_split(char *str);
// returns the string array length
uint32_t strarr_length(char **str_array);
// joins a string array in a single string
char* strarr_join(char** const str_array);
// duplicate a string
char *str_duplicate(const char *str);

// safely compare 2 float values, inlined for speed
static inline
bool floats_differ_enough(float a, float b)
{
    return fabsf(a - b) >= FLT_EPSILON;
}

static inline
bool doubles_differ_enough(double a, double b)
{
    return fabs(a - b) >= DBL_EPSILON;
}

// clamp a value to be within a certain range
static inline
int clamp(int value, int min, int max)
{
    return value < min ? min : (value > max ? max : value);
}

// clamp a value to be within a certain range (float version)
static inline
float clampf(float value, float min, float max)
{
    return value < min ? min : (value > max ? max : value);
}


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
