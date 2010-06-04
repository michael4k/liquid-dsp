/*
 * Copyright (c) 2007, 2008, 2009, 2010 Joseph Gaeddert
 * Copyright (c) 2007, 2008, 2009, 2010 Virginia Polytechnic
 *                                      Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// Frequency modulator/demodulator
//

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "liquid.internal.h"

#define FREQMODEM_DEMOD_USE_PLL     1

struct freqmodem_s {
    nco oscillator;
    float fc;       // carrier frequency
    float m;        // modulation index
    float m_inv;    // 1/m

    // phase difference
    float complex q;
};

freqmodem freqmodem_create(float _m,
                           float _fc)
{
    freqmodem fm = (freqmodem) malloc(sizeof(struct freqmodem_s));
    fm->m = _m;
    fm->fc = _fc;
    if (fm->m <= 0.0f || fm->m > 2.0f*M_PI) {
        printf("error: freqmodem_create(), modulation index %12.4e out of range (0,2*pi)\n", fm->m);
        exit(0);
    } else if (fm->fc <= -M_PI || fm->fc >= M_PI) {
        printf("error: freqmodem_create(), carrier frequency %12.4e out of range (-pi,pi)\n", fm->fc);
        exit(0);
    }

    fm->m_inv = 1.0f / fm->m;

    // create oscillator
    fm->oscillator = nco_create(LIQUID_VCO);

#if FREQMODEM_DEMOD_USE_PLL
    // TODO : set initial NCO frequency ?
    // create phase-locked loop
    nco_pll_set_bandwidth(fm->oscillator, 0.05f);
#endif

    freqmodem_reset(fm);

    return fm;
}

void freqmodem_destroy(freqmodem _fm)
{
    nco_destroy(_fm->oscillator);
    free(_fm);
}

void freqmodem_print(freqmodem _fm)
{
    printf("freqmodem:\n");
    printf("    mod. index  :   %8.4f\n", _fm->m);
    printf("    fc          :   %8.4f\n", _fm->fc);
}

void freqmodem_reset(freqmodem _fm)
{
    // reset oscillator, phase-locked loop
    nco_reset(_fm->oscillator);

    // clear complex phase term
    _fm->q = 0.0f;
}

void freqmodem_modulate(freqmodem _fm,
                        float _x,
                        float complex *_y)
{
    nco_set_frequency(_fm->oscillator,
                      (_fm->m)*_x + _fm->fc);

    nco_cexpf(_fm->oscillator, _y);
    nco_step(_fm->oscillator);
}

void freqmodem_demodulate(freqmodem _fm,
                          float complex _y,
                          float *_x)
{
#if FREQMODEM_DEMOD_USE_PLL
    // 
    // push through phase-locked loop
    //

    // compute phase error from internal NCO complex exponential
    float complex p;
    nco_cexpf(_fm->oscillator, &p);
    float phase_error = cargf( conjf(p)*_y );

    // step the PLL and the internal NCO object
    nco_pll_step(_fm->oscillator, phase_error);
    nco_step(_fm->oscillator);

    // demodulated signal is (weighted) nco frequency
    *_x = (nco_get_frequency(_fm->oscillator) -_fm->fc) * _fm->m_inv;
#else
    // compute phase difference and normalize by modulation index
    *_x = (cargf(conjf(_fm->q)*(_y)) - _fm->fc) * _fm->m_inv;

    _fm->q = _y;
#endif
}


