/*
 * shaper.c — turns a beat phasor's phase into a bipolar modulation value.
 * Applies Shift (phase offset, groove) then dispatches to the curve bank.
 * Pure: no host deps, unit-tested in the native harness.
 */
#include "shaper.h"
#include "curves.h"
#include <math.h>

void shaper_init(shaper_t *s) {
    s->curve = CURVE_SIDECHAIN1;
    s->slope = PNP_CURVE_DEFAULT_SLOPE[CURVE_SIDECHAIN1];
    s->shift = 0.5;   /* center = no offset */
    s->attack = 0.0;  /* 0 = instant onset (no softening) */
}

double shaper_eval(const shaper_t *s, double phase) {
    /* shift in [0,1], 0.5 = no offset; maps to a -0.5..+0.5 cycle offset.
     * A LATER duck (shift>0.5) means the trough appears at higher phase, so we
     * subtract the offset from the lookup phase. */
    double off = s->shift - 0.5;
    double p = phase - off;
    p -= floor(p);                 /* wrap into [0,1) */

    /* Attack: soften the ONSET without changing depth. Ease from rest into the
     * curve's onset value over the first `attack` of the cycle (so the dip/bump
     * still reaches full depth, just ramped-in), then resume the curve delayed
     * by the attack time. attack=0 => the curve passes straight through. */
    double a = s->attack;
    if (a > 1e-4) {
        if (a > 1.0) a = 1.0;
        double onset = pnp_curve_eval(s->curve, 0.0, s->slope);   /* full onset value */
        if (p < a) return onset * pnp_smoothstep(p / a);          /* ramp rest -> onset */
        return pnp_curve_eval(s->curve, p - a, s->slope);         /* curve, delayed */
    }
    return pnp_curve_eval(s->curve, p, s->slope);
}
