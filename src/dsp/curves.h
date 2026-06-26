/*
 * curves.h — PushNPull bipolar curve bank (12 fixed sidechain/rhythm shapes).
 * Pure functions: phase p in [0,1) -> modulation m in [-1,1].
 * m=0 resting/unity, m<0 pull (duck/close), m>0 push (boost/open).
 * `slope` is the active/transition portion length (0.02..1).
 */
#ifndef CURVES_H
#define CURVES_H
#include <math.h>

#define PNP_PI 3.14159265358979323846

enum {
    CURVE_SIDECHAIN1 = 0, CURVE_SIDECHAIN2, CURVE_PUNCH, CURVE_SUBBASS,
    CURVE_GATE, CURVE_REVERSE, CURVE_PULSE, CURVE_PUSH,
    CURVE_TRIM, CURVE_SWELL, CURVE_STUTTER, CURVE_PUMP
};
#define PNP_CURVE_COUNT 12

static const char *PNP_CURVE_NAMES[PNP_CURVE_COUNT] = {
    "Sidechain 1", "Sidechain 2", "Punch", "Sub Bass",
    "Gate", "Reverse", "Pulse", "Push",
    "Trim", "Swell", "Stutter", "Pump"
};
static const double PNP_CURVE_DEFAULT_SLOPE[PNP_CURVE_COUNT] = {
    0.55, 0.65, 0.40, 0.85, 0.12, 0.40, 0.50, 0.55,
    0.18, 0.70, 0.90, 0.80
};

static inline double pnp_smoothstep(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return t * t * (3.0 - 2.0 * t);
}

/* Evaluate curve `cv` at phase p (0..1) with active length `slope`. */
static inline double pnp_curve_eval(int cv, double p, double slope) {
    double A = slope;
    if (A < 0.02) A = 0.02;
    if (A > 1.0)  A = 1.0;
    if (p < 0.0) p = 0.0;
    if (p >= 1.0) p -= 1.0;

    switch (cv) {
        case CURVE_SIDECHAIN1:                         /* classic: -1 at beat, smoothstep up */
            if (p >= A) return 0.0;
            return -(1.0 - pnp_smoothstep(p / A));
        case CURVE_SIDECHAIN2:                         /* rounded dip, no instant drop */
            if (p >= A) return 0.0;
            return -sin(PNP_PI * (p / A));
        case CURVE_PUNCH: {                            /* fast (concave) recovery */
            if (p >= A) return 0.0;
            double x = 1.0 - p / A;                    /* 1..0 */
            return -(x * x);
        }
        case CURVE_SUBBASS:                            /* slow near-linear recovery */
            if (p >= A) return 0.0;
            return -(1.0 - p / A);
        case CURVE_GATE: {                             /* short gate notch, click-safe edges */
            double edge = 0.05 * A;
            if (p >= A) return 0.0;
            if (p < edge)       return -(p / edge);
            if (p > A - edge)   return -((A - p) / edge);
            return -1.0;
        }
        case CURVE_REVERSE: {                          /* anticipatory: ducks INTO p=1 */
            double start = 1.0 - A;
            if (p < start) return 0.0;
            double x = (p - start) / A;                /* 0..1 */
            return -pnp_smoothstep(x);
        }
        case CURVE_PULSE: {                            /* two dips per cycle (p=0, p=0.5) */
            double h = (p < 0.5) ? 0.0 : 0.5;
            double lp = (p - h) * 2.0;                 /* 0..1 within the half */
            if (lp >= A) return 0.0;
            return -(1.0 - pnp_smoothstep(lp / A));
        }
        case CURVE_PUSH:                               /* upward accent: +1 at beat, decays */
            if (p >= A) return 0.0;
            return +(1.0 - pnp_smoothstep(p / A));
        case CURVE_TRIM: {                             /* kick-trim: sharp duck, fast cubic recovery */
            if (p >= A) return 0.0;
            double x = 1.0 - p / A;                    /* 1..0 */
            return -(x * x * x);
        }
        case CURVE_SWELL: {                            /* rhythm FX: positive boost bump */
            if (p >= A) return 0.0;
            double x = p / A;                          /* 0..1 */
            return +0.5 * (1.0 - cos(2.0 * PNP_PI * x));   /* 0 -> +1 -> 0 */
        }
        case CURVE_STUTTER: {                          /* trance gate: 4 quick dips across the window */
            if (p >= A) return 0.0;
            double cell = (p / A) * 4.0;
            double local = cell - floor(cell);         /* 0..1 within each dip */
            return -(1.0 - pnp_smoothstep(local));
        }
        case CURVE_PUMP: {                             /* bipolar: duck on beat, boost mid, settles */
            if (p >= A) return 0.0;
            double x = p / A;                          /* 0..1 */
            return -cos(2.0 * PNP_PI * x) * (1.0 - x);
        }
        default:
            return 0.0;
    }
}
#endif /* CURVES_H */
