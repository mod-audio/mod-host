/*
  ==============================================================================

 * VEJA NoiseGate
 * Copyright (C) 2021 Jan Janssen <jan@moddevices.com>
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

  ==============================================================================
*/

#ifndef GATE_CORE_H_INCLUDED
#define GATE_CORE_H_INCLUDED

#include "circular_buffer.h"

#include <math.h>

typedef enum {
    IDLE,
    HOLD,
    DECAY
} gate_state_t;

typedef struct GATE_T {
    float _alpha;
    float _rmsValue, _keyValue, _upperThreshold, _lowerThreshold, _gainFactor;
    uint32_t _attackTime, _decayTime, _holdTime;
    uint32_t _attackCounter, _decayCounter, _holdCounter;
    uint32_t _currentState;
    uint32_t _tau;
    ringbuffer_t window;
    gate_state_t state;
} gate_t;

inline void gate_init(gate_t* const gate)
{
    gate->_alpha = 1.0f;
    gate->_rmsValue = 0.0f;
    gate->_keyValue = 0.0f;
    gate->_upperThreshold = 0.0f;
    gate->_lowerThreshold = 0.0f;
    gate->_attackTime = 0;
    gate->_decayTime = 0;
    gate->_holdTime = 0;
    gate->_attackCounter = 0;
    gate->_decayCounter = 0;
    gate->_holdCounter = 0;
    gate->_currentState = IDLE;
    gate->_tau = 0;
    gate->_gainFactor = 0.0f;
    ringbuffer_clear(&gate->window, GATE_RINGBUFFER_SIZE);
}

inline float gate_run(gate_t* const gate, const float input)
{
    gate->_keyValue = ringbuffer_push_and_calculate_power(&gate->window, input);

    switch (gate->_currentState)
    {
        case IDLE:
            gate->_rmsValue = sqrt(gate->_keyValue * gate->_keyValue) * 0.707106781187;
            if (gate->_rmsValue > gate->_upperThreshold)
            {
                if (gate->_attackCounter > gate->_attackTime)
                {
                    gate->_currentState = HOLD;
                    gate->_holdCounter = 0;
                    gate->_attackCounter = 0;
                    gate->_gainFactor = 1.0f;
                }
                else
                {
                    gate->_attackCounter++;
                    if (gate->_attackCounter != 0) 
                        gate->_gainFactor = (powf((float)gate->_attackCounter, 2.0f) / powf((float)gate->_attackTime, 2.0f));
                    else 
                        gate->_gainFactor = 0.0f;
                }
            }
            else
                gate->_gainFactor = 0.0f;
        break;

        case HOLD:
            gate->_rmsValue = sqrt(gate->_keyValue * gate->_keyValue) * 0.707106781187;
            if (gate->_rmsValue > gate->_lowerThreshold)
                gate->_holdCounter = 0;
            else if (gate->_holdCounter < gate->_holdTime)
                gate->_holdCounter++;
            else if (gate->_holdCounter >= gate->_holdTime)
            {
                gate->_currentState = DECAY;
                gate->_decayCounter = 0;
            }

            gate->_gainFactor = 1.0f;
        break;

        case DECAY:
            if (gate->_decayCounter > gate->_decayTime)
            {
                gate->_currentState = IDLE;
                gate->_gainFactor = 0.0f;
            }
            else
            {
                float dif = (float)gate->_decayCounter - (float)gate->_decayTime;
                if (gate->_decayCounter != 0) 
                {
                    gate->_decayCounter++;
                    gate->_gainFactor = powf(dif, 2.0f) / powf((float)gate->_decayTime, 2.0f);
                }
                else
                {
                    gate->_decayCounter++;
                    gate->_gainFactor = 1.0f;
                }
            }
        break;
    }

    return input * gate->_gainFactor;
}

inline float gate_apply(gate_t* const gate, const float input)
{
    return input * gate->_gainFactor;
}

inline void gate_update(gate_t* const gate,
                        const uint32_t sampleRate,
                        const uint32_t attack,
                        const uint32_t hold,
                        const uint32_t decay,
                        const uint32_t alpha,
                        const float upperThreshold,
                        const float lowerThreshold)
{
    gate->_tau = sampleRate * 0.001f; //sample time in ms
    gate->_upperThreshold = powf(10.0f, (upperThreshold / 20.0f)); // dB to level
    gate->_lowerThreshold = powf(10.0f, (lowerThreshold / 20.0f));
    gate->_attackTime = attack * gate->_tau;
    gate->_decayTime = decay * gate->_tau;
    gate->_holdTime = hold * gate->_tau;
    gate->_alpha = alpha;
}

#endif // GATE_CORE_H_INCLUDED
