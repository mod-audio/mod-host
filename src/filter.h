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

#ifndef  FILTER_H
#define  FILTER_H

/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <jack/jack.h>

/*
************************************************************************************************************************
*           DO NOT CHANGE THESE DEFINES
************************************************************************************************************************
*/


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

/**
 * Calculate the BPM from the time difference of two adjacent MIDI
 * Beat Clock signals.
 *
 * `delta` is time in samples. Due to filtering this is not integer.
 */
double beats_per_minute(const double delta, const jack_nframes_t sample_rate);

/**
 * `raw_delta` is the time difference in samples between two
 * adjacent MIDI Beat Clock ticks. Over time this has jitter.
 * This function filters the jitter and returns a more steady time delta.
 */
double beat_clock_tick_filter(unsigned int raw_delta);

/**
 * reset filter average values for the next call to `beat_clock_tick_filter`.
 */
void reset_filter(void);

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

#endif  /* FILTER_H */
