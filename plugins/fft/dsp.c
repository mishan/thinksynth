/*
 * Copyright (C) 2004-2014 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef struct {
	float real, imag;
} COMPLEX;


/**************************************************************************
 *
 * FILTER.C - Source code for PC version of filter functions:
 *
 * fir_filter FIR filter floats sample by sample (real time)
 *
 * iir_filter IIR filter floats sample by sample (real time)
 *
 * gaussian Generate Gaussian noise
 *
 * fft Radix 2 FFT
 *
 * *************************************************************************/

/**************************************************************************
 *
 * fir_filter - Perform fir filtering sample by sample on floats
 *
 * Requires array of filter coefficients and pointer to history.
 *
 * Returns one output sample for each input sample.
 *
 * float fir_filter(float input,float *coef,int n,float *history)
 *
 * float input new float input sample
 *
 * float *coef pointer to filter coefficients
 *
 * int n number of coefficients in filter
 *
 * float *history history array pointer
 *
 * Returns float value giving the current output.
 *
 * *************************************************************************/

float fir_filter(float input,float *coef,int n,float *history)
{
	int i;
	float *hist_ptr,*hist1_ptr,*coef_ptr;
	float output;

	hist_ptr = history;
	hist1_ptr = hist_ptr; /* use for history update */
	coef_ptr = coef + n - 1; /* point to last coef */

	/* form output accumulation */
	output = *hist_ptr++ * (*coef_ptr--);

	for(i = 2 ; i < n ; i++) {
		*hist1_ptr++ = *hist_ptr; /* update history array */
		output += (*hist_ptr++) * (*coef_ptr--);
	}

	output += input * (*coef_ptr); /* input tap */

	*hist1_ptr = input; /* last history */

	return(output);
}

/**************************************************************************
 *
 * iir_filter - Perform IIR filtering sample by sample on floats
 *
 * Implements cascaded direct form II second order sections.
 *
 * Requires arrays for history and coefficients.
 *
 * The length (n) of the filter specifies the number of sections.
 *
 * The size of the history array is 2*n.
 *
 * The size of the coefficient array is 4*n + 1 because
 *
 * the first coefficient is the overall scale factor for the filter.
 *
 * Returns one output sample for each input sample.
 *
 * float iir_filter(float input,float *coef,int n,float *history)
 *
 * float input new float input sample
 *
 * float *coef pointer to filter coefficients
 *
 * int n number of sections in filter
 *
 * float *history history array pointer
 *
 * Returns float value giving the current output.
 *
 * *************************************************************************/

float iir_filter(float input,float *coef,int n,float *history)
{
	int i;
	float *hist1_ptr,*hist2_ptr,*coef_ptr;
	float output,new_hist,history1,history2;

	coef_ptr = coef; /* coefficient pointer */
	hist1_ptr = history; /* first history */
	hist2_ptr = hist1_ptr + 1; /* next history */
	output = input * (*coef_ptr++); /* overall input scale factor */

	for(i = 0 ; i < n ; i++) {
		history1 = *hist1_ptr; /* history values */
		history2 = *hist2_ptr;
		output = output - history1 * (*coef_ptr++);
		new_hist = output - history2 * (*coef_ptr++); /* poles */
		output = new_hist + history1 * (*coef_ptr++);
		output = output + history2 * (*coef_ptr++); /* zeros */
		*hist2_ptr++ = *hist1_ptr;
		*hist1_ptr++ = new_hist;
		hist1_ptr++;
		hist2_ptr++;
	}

	return(output);
}

/**************************************************************************
 *
 * gaussian - generates zero mean unit variance Gaussian random numbers
 *
 * Returns one zero mean unit variance Gaussian random numbers as a float.
 *
 * Uses the Box-Muller transformation of two uniform random numbers to
 *
 * Gaussian random numbers.
 *
 * float gaussian()
 *
 * *************************************************************************/

float gaussian()
{
	static int ready = 0; /* flag to indicated stored value */
	static float gstore; /* place to store other value */
	static const float rconst1 = (float)(2.0/RAND_MAX);
	static const float rconst2 = (float)(RAND_MAX/2.0);
	float v1,v2,r,fac;

	/* make two numbers if none stored */
	if(ready == 0) {
		do {
			v1 = (float)rand() - rconst2;
			v2 = (float)rand() - rconst2;
			v1 *= rconst1;
			v2 *= rconst1;
			r = v1*v1 + v2*v2;
		} while(r > 1.0f); /* make radius less than 1 */

		/* remap v1 and v2 to two Gaussian numbers */
		fac = sqrt(-2.0f*log(r)/r);
		gstore = v1*fac; /* store one */
		ready = 1; /* set ready flag */
		return(v2*fac); /* return one */
	}
	else {
		ready = 0; /* reset ready flag for next pair */
		return(gstore); /* return the stored one */
	}
}

/**************************************************************************
 *
 * fft - In-place radix 2 decimation in time FFT
 *
 * Requires pointer to complex array, x and power of 2 size of FFT, m
 *
 * (size of FFT = 2**m). Places FFT output on top of input COMPLEX array.
 *
 * void fft(COMPLEX *x, int m)
 *
 * *************************************************************************/

void fft(COMPLEX *x,int m)
{
	static COMPLEX *w; /* used to store the w complex array */
	static int mstore = 0; /* stores m for future reference */
	static int n = 1; /* length of fft stored for future */
	COMPLEX u,temp,tm;
	COMPLEX *xi,*xip,*xj,*wptr;
	int i,j,k,l,le,windex;

	double arg,w_real,w_imag,wrecur_real,wrecur_imag,wtemp_real;

	if(m != mstore) {
		/* free previously allocated storage and set new m */
		if(mstore != 0) free(w);
		mstore = m;

		if(m == 0) return; /* if m=0 then done */

		/* n = 2**m = fft length */
		n = 1 << m;
		le = n >> 1;

		/* allocate the storage for w */
		w = (COMPLEX *) calloc(le-1,sizeof(COMPLEX));

		if(!w) {
			exit(1);
		}

		/* calculate the w values recursively */
		arg = M_PI/le; /* PI/le calculation */
		wrecur_real = w_real = cos(arg);
		wrecur_imag = w_imag = -sin(arg);
		xj = w;

		for (j = 1 ; j < le ; j++) {
			xj->real = (float)wrecur_real;
			xj->imag = (float)wrecur_imag;
			xj++;
			wtemp_real = wrecur_real*w_real - wrecur_imag*w_imag;
			wrecur_imag = wrecur_real*w_imag + wrecur_imag*w_real;
			wrecur_real = wtemp_real;
		}
	}

	/* start fft */
	le = n;
	windex = 1;
	for (l = 0 ; l < m ; l++) {
		int increment = le & ~1;
		le >>= 1;

		/* first iteration with no multiplies */
		for(i = 0 ; i < n ; i += increment) {
			xi = x + i;
			xip = xi + le;
			temp.real = xi->real + xip->real;
			temp.imag = xi->imag + xip->imag;
			xip->real = xi->real - xip->real;
			xip->imag = xi->imag - xip->imag;
			*xi = temp;
		}

		/* remaining iterations use stored w */
		wptr = w + windex - 1;
		for (j = 1 ; j < le ; j++) {
			u = *wptr;
			for (i = j ; i < n ; i += increment) {
				xi = x + i;
				xip = xi + le;
				temp.real = xi->real + xip->real;
				temp.imag = xi->imag + xip->imag;
				tm.real = xi->real - xip->real;
				tm.imag = xi->imag - xip->imag;
				xip->real = tm.real*u.real - tm.imag*u.imag;
				xip->imag = tm.real*u.imag + tm.imag*u.real;
				*xi = temp;
			}
			wptr = wptr + windex;
		}
		windex <<= 1;
	}

	/* rearrange data by bit reversing */
	j = 0;
	for (i = 1 ; i < (n-1) ; i++) {
		k = n >> 1;
		while(k <= j) {
			j -= k;
			k >>= 1;
		}
		j = j + k;
		if (i < j) {
			xi = x + i;
			xj = x + j;
			temp = *xj;
			*xj = *xi;
			*xi = temp;
		}
	}
}
