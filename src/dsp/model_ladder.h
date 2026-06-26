/*
 * model_ladder.h — transistor-ladder filter (4-pole resonant low-pass).
 * C port of Stefano D'Angelo's "ImprovedMoog" (the D'Angelo-Valimaki physically-
 * derived nonlinear ladder) from ddiakopoulos/MoogLadders, ISC-licensed — see
 * the copyright header in model_ladder.c. Four tanh-saturated stages with thermal
 * voltage VT, trapezoidal integration; 2x oversampled here for HF coefficient
 * validity and reduced aliasing.
 */
#ifndef MODEL_LADDER_H
#define MODEL_LADDER_H

typedef struct {
    double fs;                 /* base sample rate */
    double V[4], dV[4], tV[4]; /* per-stage integrator/derivative/tanh state */
    double g;                  /* tuning coefficient (at oversampled rate) */
    double drive;              /* input drive into the saturators */
    double resonance;          /* feedback amount (mapped from res 0..1) */
} ladder_t;

void   ladder_init(ladder_t *m, double fs);
void   ladder_set(ladder_t *m, double cutoff_hz, double res01);  /* res 0..1 */
double ladder_process(ladder_t *m, double in);                   /* 2x oversampled */

#endif /* MODEL_LADDER_H */
