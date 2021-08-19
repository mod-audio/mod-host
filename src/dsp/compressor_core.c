/*
 * (c) Copyright 2016, Sean Connelly (@velipso), https://sean.cm
 * MIT License
 * Project Home: https://github.com/velipso/sndfilter
 * dynamics compressor based on WebAudio specification:
 *   https://webaudio.github.io/web-audio-api/#the-dynamicscompressornode-interface
 * Adapted on 2021 by Jan Janssen <jan@moddevices.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "compressor_core.h"
#include <math.h>
#include <string.h>

static inline float lin2db(float lin){ // linear to dB
	return 20.0f * log10f(lin);
}

static inline float cmop_db2lin(float db){ // dB to linear
	return powf(10.0f, 0.05f * db);
}

// for more information on the knee curve, check out the compressor-curve.html demo + source code
// included in this repo
static inline float kneecurve(float x, float k, float linearthreshold){
	return linearthreshold + (1.0f - expf(-k * (x - linearthreshold))) / k;
}

static inline float kneeslope(float x, float k, float linearthreshold){
	return k * x / ((k * linearthreshold + 1.0f) * expf(k * (x - linearthreshold)) - 1);
}

static inline float compcurve(float x, float k, float slope, float linearthreshold,
	float linearthresholdknee, float threshold, float knee, float kneedboffset){
	if (x < linearthreshold)
		return x;
	if (knee <= 0.0f) // no knee in curve
		return cmop_db2lin(threshold + slope * (lin2db(x) - threshold));
	if (x < linearthresholdknee)
		return kneecurve(x, k, linearthreshold);
	return cmop_db2lin(kneedboffset + slope * (lin2db(x) - threshold - knee));
}

// for more information on the adaptive release curve, check out adaptive-release-curve.html demo +
// source code included in this repo
static inline float adaptivereleasecurve(float x, float a, float b, float c, float d){
	// a*x^3 + b*x^2 + c*x + d
	float x2 = x * x;
	return a * x2 * x + b * x2 + c * x + d;
}

static inline float clampf(float v, float min, float max){
	return v < min ? min : (v > max ? max : v);
}

static float maxf(float v1, float v2){
	return v1 > v2 ? v1 : v2;
}

static inline float fixf(float v, float def){
	if (isnan(v) || isinf(v))
		return def;
	return v;
}

void compressor_init(sf_compressor_state_st *state, int samplerate)
{
	state->samplerate = samplerate;
	state->detectoravg = 0.0f;
	state->compgain = 1.0f;
	state->maxcompdiffdb = -1.0f;

	state->ang90 = (float)M_PI * 0.5f;
	state->ang90inv = 2.0f / (float)M_PI;
}

// this is the main initialization function
// it does a bunch of pre-calculation so that the inner loop of signal processing is fast
void compressor_set_params(sf_compressor_state_st *state, float threshold,
	float knee, float ratio, float attack, float release, float makeup){

	// useful values
	const float linearthreshold = cmop_db2lin(threshold);
	const float slope = 1.0f / ratio;
	const float attacksamples = state->samplerate * attack;
	const float attacksamplesinv = 1.0f / attacksamples;
	const float releasesamples = state->samplerate * release;
	const float satrelease = 0.0025f; // seconds
	const float satreleasesamplesinv = 1.0f / (state->samplerate * satrelease);

	// calculate knee curve parameters
	float k = 5.0f; // initial guess
	float kneedboffset = 0.0f;
	float linearthresholdknee = 0.0f;
	if (knee > 0.0f){ // if a knee exists, search for a good k value
		const float xknee = cmop_db2lin(threshold + knee);
		float mink = 0.1f;
		float maxk = 10000.0f;
		// search by comparing the knee slope at the current k guess, to the ideal slope
		for (int i = 0; i < 15; i++){
			if (kneeslope(xknee, k, linearthreshold) < slope)
				maxk = k;
			else
				mink = k;
			k = sqrtf(mink * maxk);
		}
		kneedboffset = lin2db(kneecurve(xknee, k, linearthreshold));
		linearthresholdknee = cmop_db2lin(threshold + knee);
	}

	// calculate a master gain based on what sounds good
	const float fulllevel = compcurve(1.0f, k, slope, linearthreshold, linearthresholdknee,
		threshold, knee, kneedboffset);
	const float mastergain = cmop_db2lin(makeup) * powf(1.0f / fulllevel, 0.6f);

	// calculate the adaptive release curve parameters
	// solve a,b,c,d in `y = a*x^3 + b*x^2 + c*x + d`
	// interescting points (0, y1), (1, y2), (2, y3), (3, y4)
	const float y1 = releasesamples * 0.090f;
	const float y2 = releasesamples * 0.160f;
	const float y3 = releasesamples * 0.420f;
	const float y4 = releasesamples * 0.980f;
	const float a = (-y1 + 3.0f * y2 - 3.0f * y3 + y4) / 6.0f;
	const float b = y1 - 2.5f * y2 + 2.0f * y3 - 0.5f * y4;
	const float c = (-11.0f * y1 + 18.0f * y2 - 9.0f * y3 + 2.0f * y4) / 6.0f;
	const float d = y1;

	// save everything
	state->threshold            = threshold;
	state->knee                 = knee;
	state->linearthreshold      = linearthreshold;
	state->slope                = slope;
	state->attacksamplesinv     = attacksamplesinv;
	state->satreleasesamplesinv = satreleasesamplesinv;
	state->k                    = k;
	state->kneedboffset         = kneedboffset;
	state->linearthresholdknee  = linearthresholdknee;
	state->mastergain           = mastergain;
	state->a                    = a;
	state->b                    = b;
	state->c                    = c;
	state->d                    = d;
}

// import stereo process function
#define STEREO
#include "compressor_core_process.c"

// import mono process function
#undef STEREO
#include "compressor_core_process.c"
