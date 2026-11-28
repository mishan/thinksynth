/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef THINK_FOLD_H
#define THINK_FOLD_H

/* dist::fold's antialiasing, on its own so statecheck can hold it to the
   integral it stands for. See fold.cpp. */

#include <math.h>

/* The mean of sin over the triangle u0, u1, u2 span, in series about u1:
   for when u0 and u2 are too close for the closed form's divisor. */
static inline double foldSeries (double u0, double u1, double u2)
{
    const double a = u0 - u1, b = u2 - u1;
    double h = 1, bk = 1, fact = 1, ec = 0, es = 0;

    for (int k = 0; k < 120; k++)
    {
        if (k > 0)
        {
            bk *= b;
            h = a * h + bk;     /* a^k + a^(k-1) b + ... + b^k */
            fact *= k;
        }

        const double term = 2 * h / ((k + 1) * (double)(k + 2) * fact);

        if (k % 2 == 0)
            ec += k % 4 == 0 ? term : -term;
        else
            es += k % 4 == 1 ? term : -term;

        if (k > 4 && fabs(term) < 1e-18)
            break;
    }

    return sin(u1) * ec + cos(u1) * es;
}

/* -(sin a - sin b) / (a - b): F = -sin's first divided difference. */
static inline double foldD1 (double a, double b)
{
    const double half = 0.5 * (a - b);
    const double sinc = fabs(half) < 1e-8 ? 1 : sin(half) / half;

    return -cos(0.5 * (a + b)) * sinc;
}

static inline double foldAdaa (double u0, double u1, double u2)
{
    /* Divided by at least 0.01, the closed form's last-bit error grows a
       hundredfold at most: under float's. */
    if (fabs(u0 - u2) < 0.01)
        return foldSeries(u0, u1, u2);

    return 2 * (foldD1(u0, u1) - foldD1(u1, u2)) / (u0 - u2);
}

#endif /* THINK_FOLD_H */
