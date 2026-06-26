/*
 * vol.h — PushNPull volume-path helpers: bipolar gain from the modulation
 * signal, and a one-pole low/high crossover for low-band-only ducking.
 * One-pole (6 dB/oct) split: low + high == input exactly (unity recombine),
 * so applying gain only to `low` leaves the high band untouched and phase-clean.
 */
#ifndef PNP_VOL_H
#define PNP_VOL_H
#include <math.h>

/* gain = 1 - volume*m, clamped to >= 0 (never inverts phase). volume bipolar:
 * 0 = off (unity); negative = duck (with a duck curve m<0, gain dips below 1);
 * positive = pump/boost (gain rises above 1). Default volume is -1 (full duck). */
static inline float pnp_gain(float volume, float m) {
    float g = 1.0f - volume * m;
    return g < 0.0f ? 0.0f : g;
}

typedef struct { double a; double zL; double zR; int stereo_idx; } pnp_split_t;

/* one-pole lowpass coefficient for cutoff fc (Hz) at sample rate fs. */
static inline void pnp_split_init(pnp_split_t *s, double fs, double fc) {
    double x = exp(-2.0 * 3.14159265358979323846 * fc / fs);
    s->a = x;          /* pole */
    s->zL = 0.0; s->zR = 0.0;
}
static inline void pnp_split_set_fc(pnp_split_t *s, double fs, double fc) {
    s->a = exp(-2.0 * 3.14159265358979323846 * fc / fs);
}
/* mono split: returns low and high (low+high == in). */
static inline void pnp_split(pnp_split_t *s, double in, double *low, double *high) {
    s->zL = in * (1.0 - s->a) + s->zL * s->a;
    *low = s->zL;
    *high = in - s->zL;
}
/* stereo variants keep independent state per channel. */
static inline void pnp_split_L(pnp_split_t *s, double in, double *low, double *high) {
    s->zL = in * (1.0 - s->a) + s->zL * s->a; *low = s->zL; *high = in - s->zL;
}
static inline void pnp_split_R(pnp_split_t *s, double in, double *low, double *high) {
    s->zR = in * (1.0 - s->a) + s->zR * s->a; *low = s->zR; *high = in - s->zR;
}
#endif /* PNP_VOL_H */
