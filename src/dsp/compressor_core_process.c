/* this file is meant to be imported by compressor_core.c, with STEREO macro to define function target */
#ifdef STEREO
void compressor_process(sf_compressor_state_st *state, int size, float *input_L, float *input_R, float *output_L, float *output_R)
#else
void compressor_process_mono(sf_compressor_state_st *state, int size, float *input, float *output)
#endif
{
	// pull out the state into local variables
	float threshold            = state->threshold;
	float knee                 = state->knee;
	float linearthreshold      = state->linearthreshold;
	float slope                = state->slope;
	float attacksamplesinv     = state->attacksamplesinv;
	float satreleasesamplesinv = state->satreleasesamplesinv;
	float k                    = state->k;
	float kneedboffset         = state->kneedboffset;
	float linearthresholdknee  = state->linearthresholdknee;
	float mastergain           = state->mastergain;
	float a                    = state->a;
	float b                    = state->b;
	float c                    = state->c;
	float d                    = state->d;
	float detectoravg          = state->detectoravg;
	float compgain             = state->compgain;
	float maxcompdiffdb        = state->maxcompdiffdb;

	int chunks = size / SF_COMPRESSOR_SPU;
	int samplepos = 0;

	for (int ch = 0; ch < chunks; ch++){
		detectoravg = fixf(detectoravg, 1.0f);
		float desiredgain = detectoravg;
		float scaleddesiredgain = asinf(desiredgain) * state->ang90inv;
		float compdiffdb = lin2db(compgain / scaleddesiredgain);

		// calculate envelope rate based on whether we're attacking or releasing
		float enveloperate;
		if (compdiffdb < 0.0f){ // compgain < scaleddesiredgain, so we're releasing
			compdiffdb = fixf(compdiffdb, -1.0f);
			maxcompdiffdb = -1; // reset for a future attack mode
			// apply the adaptive release curve
			// scale compdiffdb between 0-3
			float releasesamples = adaptivereleasecurve(((clampf(compdiffdb, -12.0f, 0.0f) + 12.0f) * 0.25f), a, b, c, d);
			enveloperate = cmop_db2lin(5.0f / releasesamples);
		}
		else{ // compresorgain > scaleddesiredgain, so we're attacking
			compdiffdb = fixf(compdiffdb, 1.0f);
			if (maxcompdiffdb == -1 || maxcompdiffdb < compdiffdb)
				maxcompdiffdb = compdiffdb;
			float attenuate = maxcompdiffdb;
			if (attenuate < 0.5f)
				attenuate = 0.5f;
			enveloperate = 1.0f - powf(0.25f / attenuate, attacksamplesinv);
		}

		// process the chunk
		for (int chi = 0; chi < SF_COMPRESSOR_SPU; chi++, samplepos++)
		{
#ifdef STEREO
			float inputmax = fabs(input_L[samplepos]) > fabs(input_R[samplepos]) ? fabs(input_L[samplepos]) : fabs(input_R[samplepos]);
#else
			float inputmax = fabs(input[samplepos]);
#endif

			float attenuation;
			if (inputmax < 0.0001f)
				attenuation = 1.0f;
			else{
				float inputcomp = compcurve(inputmax, k, slope, linearthreshold,
					linearthresholdknee, threshold, knee, kneedboffset);
				attenuation = inputcomp / inputmax;
			}

			float rate;
			if (attenuation > detectoravg){ // if releasing
				float attenuationdb = -lin2db(attenuation);
				if (attenuationdb < 2.0f)
					attenuationdb = 2.0f;
				float dbpersample = attenuationdb * satreleasesamplesinv;
				rate = cmop_db2lin(dbpersample) - 1.0f;
			}
			else
				rate = 1.0f;

			detectoravg += (attenuation - detectoravg) * rate;
			if (detectoravg > 1.0f)
				detectoravg = 1.0f;
			detectoravg = fixf(detectoravg, 1.0f);

			if (enveloperate < 1) // attack, reduce gain
				compgain += (scaleddesiredgain - compgain) * enveloperate;
			else{ // release, increase gain
				compgain *= enveloperate;
				if (compgain > 1.0f)
					compgain = 1.0f;
			}

			// apply the gain
#ifdef STEREO
			output_L[samplepos] = input_L[samplepos] * mastergain * sinf(state->ang90 * compgain);
			output_R[samplepos] = input_R[samplepos] * mastergain * sinf(state->ang90 * compgain);
#else
			output[samplepos] = input[samplepos] * mastergain * sinf(state->ang90 * compgain);
#endif
		}
	}

	state->detectoravg   = detectoravg;
	state->compgain      = compgain;
	state->maxcompdiffdb = maxcompdiffdb;
}
