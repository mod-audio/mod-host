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
#include "filter.h"

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

const unsigned int filter_order = 48;
static double delta_t[48] = { 0.0 }; // all elements 0.0

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
 * `delta_t` is time in samples. Due to filtering, this is not integer.
 */
float beats_per_minute(const float delta_t, const jack_nframes_t sample_rate) {
  /*
   * \text{bpm} = \frac{120}{2\cdot{}24}\cdot{}\cfrac{\text{SR}}{\delta t}
   */  
  return (2.5 * (sample_rate)) / delta_t;
}

/**
 * `raw_delta_t` is the time difference in samples between two
 * adjacent MIDI Beat Clock ticks. Over time this has jitter. This
 * function filters the jitter and returns a more steady time delta.
 *
 * Currently this filter is implemented as a centered binomial FIR
 * filter.
 */
float beat_clock_tick_filter(unsigned long long raw_delta_t) {
  float result = 0.0;

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
   * normalized_binomial_coefficients(48)
   */  
  const double coeffs[] = {
			   7.105427357601002e-15, 3.339550858072471e-13,
			   7.680966973566683e-12, 1.1521450460350025e-10,
			   1.2673595506385027e-09, 1.0899292135491123e-08,
			   7.629504494843786e-08, 4.4687097755513605e-07,
			   2.2343548877756803e-06, 9.682204513694614e-06,
			   3.6792377152039535e-05, 0.0001237561776932239,
			   0.00037126853307967167, 0.000999569127522193,
			   0.0024275250239824686, 0.005340555052761431,
			   0.010681110105522862, 0.01947731842771816,
			   0.032462197379530267, 0.0495475644213883,
			   0.06936659018994362, 0.08918561595849894,
			   0.10540118249640784, 0.11456650271348678,
			   0.11456650271348678, 0.10540118249640784,
			   0.08918561595849894, 0.06936659018994362,
			   0.0495475644213883, 0.032462197379530267,
			   0.01947731842771816, 0.010681110105522862,
			   0.005340555052761431, 0.0024275250239824686,
			   0.000999569127522193, 0.00037126853307967167,
			   0.0001237561776932239, 3.6792377152039535e-05,
			   9.682204513694614e-06, 2.2343548877756803e-06,
			   4.4687097755513605e-07, 7.629504494843786e-08,
			   1.0899292135491123e-08, 1.2673595506385027e-09,
			   1.1521450460350025e-10, 7.680966973566683e-12,
			   3.339550858072471e-13, 7.105427357601002e-15
  };
  
  // Shift
  for (unsigned int i = filter_order-1; i >= 1; --i) {
    delta_t[i] = delta_t[i-1];
  }  
  delta_t[0] = raw_delta_t;
  
  // Sum up
  for (unsigned int i = 0; i < filter_order; ++i) {
    result += coeffs[i] * delta_t[i];
  }
  return result;
}
