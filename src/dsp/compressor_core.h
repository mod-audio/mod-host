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

#ifndef COMPRESSOR_CORE__H
#define COMPRESSOR_CORE__H

// samples per update; the compressor works by dividing the input chunks into even smaller sizes,
// and performs heavier calculations after each mini-chunk to adjust the final envelope
#define SF_COMPRESSOR_SPU        32

typedef struct {
	float threshold;
	float knee;
	float linearpregain;
	float linearthreshold;
	float slope;
	float attacksamplesinv;
	float satreleasesamplesinv;
	float k;
	float kneedboffset;
	float linearthresholdknee;
	float mastergain;
	float a; // adaptive release polynomial coefficients
	float b;
	float c;
	float d;
	float detectoravg;
	float compgain;
	float maxcompdiffdb;
	float samplerate;
	float ang90;
	float ang90inv;
} sf_compressor_state_st;

void compressor_init(sf_compressor_state_st *state, int samplerate);

// this function will process the input sound based on the state passed
// the input and output buffers should be the same size
void compressor_process(sf_compressor_state_st *state, int size, float *bufferL, float *bufferR);
void compressor_process_mono(sf_compressor_state_st *state, int size, float *buffer);

void compressor_set_params(sf_compressor_state_st *state, float threshold,
	float knee, float ratio, float attack, float release, float makeup);

#endif //COMPRESSOR_CORE__H
