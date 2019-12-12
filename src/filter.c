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


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "filter.h"

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define AVERAGE_FILTER_STEPS  47
#define BINOMIAL_FILTER_ORDER 37

/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/

/* These coefficients were calculated using the following Python
  * script:
  *
  * # Pascal's triangle as binomial coefficients, but normalized such that the sum is 1.
  * # Print the k-th row of the triangle:
  * def normalized_binomial_coefficients(k):
  *     coeffs = []
  *     for i in range(k-1, k):
  *         for n in range(0, i+1):
  *             coeffs.append(scipy.special.comb(i, n, exact=True))
  *     s = sum(coeffs)
  *     print([e/s for e in coeffs])
  *
  * normalized_binomial_coefficients(37)
  */
static const double coeffs[BINOMIAL_FILTER_ORDER] = {
  1.4551915228366852e-11,
  5.238689482212067e-10,
  9.167706593871117e-09,
  1.0390067473053932e-07,
  8.571805665269494e-07,
  5.485955625772476e-06,
  2.8344104066491127e-05,
  0.0001214747317135334,
  0.0004403459024615586,
  0.0013699650298804045,
  0.003698905580677092,
  0.008742867736145854,
  0.018214307783637196,
  0.033626414369791746,
  0.05524339503608644,
  0.08102364605292678,
  0.1063435354444664,
  0.12511004169937223,
  0.13206059957155958,
  0.12511004169937223,
  0.1063435354444664,
  0.08102364605292678,
  0.05524339503608644,
  0.033626414369791746,
  0.018214307783637196,
  0.008742867736145854,
  0.003698905580677092,
  0.0013699650298804045,
  0.0004403459024615586,
  0.0001214747317135334,
  2.8344104066491127e-05,
  5.485955625772476e-06,
  8.571805665269494e-07,
  1.0390067473053932e-07,
  9.167706593871117e-09,
  5.238689482212067e-10,
  1.4551915228366852e-11
};

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

// all elements initially 120
static unsigned int g_delta[AVERAGE_FILTER_STEPS];
static double g_average[BINOMIAL_FILTER_ORDER];
static bool g_reset_average = true;

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

/**
 * Calculate the BPM from the time difference of two adjacent MIDI
 * Beat Clock signals.
 *
 * `delta` is time in samples. Due to filtering, this is not integer.
 */
double beats_per_minute(const double delta, const jack_nframes_t sample_rate) {
  /*
   * \text{bpm} = \frac{120}{2\cdot{}24}\cdot{}\cfrac{\text{SR}}{\delta t}
   */
  return (2.5 * sample_rate) / delta;
}


/**
 * `raw_delta` is the time difference in samples between two
 * adjacent MIDI Beat Clock ticks. Over time this has jitter.
 * This function filters the jitter and returns a more steady time delta.
 *
 * Currently this filter is implemented as a moving average filter
 * followed by a binomial weighted FIR filter. The design goals are:
 *
 * + no overshoot
 * + quick adjustment on tempo change steps
 */

double beat_clock_tick_filter(unsigned int raw_delta) {
  const bool reset_average = g_reset_average;
  double result;
  unsigned int sum;

  // Shift
  if (reset_average) {
    g_reset_average = false;
    for (unsigned int i = 0; i < AVERAGE_FILTER_STEPS; ++i) {
      g_delta[i] = raw_delta;
    }
    sum = raw_delta * AVERAGE_FILTER_STEPS;
  } else {
    // Shift
    memmove(g_delta+1, g_delta, sizeof(g_delta[0])*(AVERAGE_FILTER_STEPS-1));
    g_delta[0] = raw_delta;

    // Sum
    sum = 0;
    for (unsigned int i = 0; i < AVERAGE_FILTER_STEPS; ++i) {
      sum += g_delta[i];
    }
  }

  // Binomial filter following

  if (reset_average) {
    result = sum/AVERAGE_FILTER_STEPS;
    for (unsigned int i = 0; i < BINOMIAL_FILTER_ORDER; ++i) {
      g_average[i] = result;
    }
  } else {
    // Shift
    memmove(g_average+1, g_average, sizeof(g_average[0])*(BINOMIAL_FILTER_ORDER-1));
    g_average[0] = sum/AVERAGE_FILTER_STEPS;
  }

  // Sum
  result = 0;
  for (unsigned int i = 0; i < BINOMIAL_FILTER_ORDER; ++i) {
    result += coeffs[i] * g_average[i];
  }

  return result;
}

/**
 * reset filter average values for the next call to `beat_clock_tick_filter`.
 */
void reset_filter(void)
{
  g_reset_average = true;
}
