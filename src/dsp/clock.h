#ifndef PNP_CLOCK_H
#define PNP_CLOCK_H
#include <stdint.h>
typedef struct {
    double   fs;
    uint32_t sample_pos;        /* advanced by the host block loop */
    uint32_t last_tick_sample;
    double   period_ema;        /* EMA of samples per MIDI tick (24 PPQN) */
    int      have_period;
    int      running;           /* transport state from 0xFA/0xFB/0xFC */
    uint32_t tick_count;        /* ticks since last Start/Continue (phase origin) */
    int      resync_pending;    /* a tick arrived since last block_start */
    double   beat_pos;          /* fractional quarter notes since origin */
    double   fallback_bpm;
} pnp_clock_t;

void   pnp_clock_init(pnp_clock_t *c, double fs);
void   pnp_clock_set_fallback_bpm(pnp_clock_t *c, double bpm);
void   pnp_clock_on_midi(pnp_clock_t *c, const uint8_t *msg, int len);
double pnp_clock_block_start(pnp_clock_t *c);   /* resync; returns per-sample beat increment */
void   pnp_clock_advance_block(pnp_clock_t *c, int frames);
double pnp_clock_bpm(const pnp_clock_t *c);
int    pnp_clock_active(const pnp_clock_t *c);   /* 1 if a clock tick arrived recently */
#endif
