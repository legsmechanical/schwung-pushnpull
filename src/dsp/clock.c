/*
 * clock.c — MIDI-clock-driven beat phasor for PushNPull.
 *
 * The Schwung host has no reliable transport getter, so we lock to the MIDI
 * clock the host broadcasts to audio-FX slots (see shadow_midi.c "ducker"
 * broadcast). 24 PPQN: 0xF8 tick, 0xFA Start, 0xFB Continue, 0xFC Stop.
 *
 * Timing model: process_block advances sample_pos by `frames` each block and
 * on_midi (called at block boundaries) measures samples between ticks to EMA a
 * period (samples/tick). beat_pos is fractional quarter-notes since the last
 * Start/Continue; block_start() resyncs it to tick_count/24 whenever a tick has
 * arrived, and returns the per-sample beat increment. When the transport is not
 * running (or no clock seen), we free-run from the fallback BPM.
 */
#include "clock.h"

#define TICKS_PER_BEAT 24.0
#define EMA_ALPHA 0.15

static void zero(pnp_clock_t *c) {
    char *p = (char*)c;
    for (unsigned i = 0; i < sizeof(*c); i++) p[i] = 0;
}

void pnp_clock_init(pnp_clock_t *c, double fs) {
    zero(c);
    c->fs = fs;
    c->fallback_bpm = 120.0;
}

void pnp_clock_set_fallback_bpm(pnp_clock_t *c, double bpm) {
    if (bpm < 20.0) bpm = 20.0;
    if (bpm > 400.0) bpm = 400.0;
    c->fallback_bpm = bpm;
}

void pnp_clock_on_midi(pnp_clock_t *c, const uint8_t *msg, int len) {
    if (len < 1) return;
    switch (msg[0]) {
        case 0xF8: {  /* timing clock tick */
            if (c->have_period) {
                double delta = (double)(c->sample_pos - c->last_tick_sample);
                if (delta > 1.0)
                    c->period_ema += EMA_ALPHA * (delta - c->period_ema);
            } else {
                double delta = (double)(c->sample_pos - c->last_tick_sample);
                if (delta > 1.0) { c->period_ema = delta; c->have_period = 1; }
            }
            c->last_tick_sample = c->sample_pos;
            c->tick_count++;
            c->resync_pending = 1;
            break;
        }
        case 0xFA:  /* Start */
        case 0xFB:  /* Continue */
            c->running = 1;
            c->tick_count = 0;
            c->beat_pos = 0.0;
            c->last_tick_sample = c->sample_pos;
            c->resync_pending = 1;
            break;
        case 0xFC:  /* Stop */
            c->running = 0;
            break;
        default:
            break;
    }
}

double pnp_clock_block_start(pnp_clock_t *c) {
    double samples_per_beat;
    if (c->running && c->have_period) {
        if (c->resync_pending) {
            c->beat_pos = (double)c->tick_count / TICKS_PER_BEAT;
            c->resync_pending = 0;
        }
        samples_per_beat = c->period_ema * TICKS_PER_BEAT;
    } else {
        samples_per_beat = c->fs * 60.0 / c->fallback_bpm;  /* free-run */
    }
    if (samples_per_beat < 1.0) samples_per_beat = 1.0;
    return 1.0 / samples_per_beat;   /* beats per sample */
}

void pnp_clock_advance_block(pnp_clock_t *c, int frames) {
    c->sample_pos += (uint32_t)frames;
}

double pnp_clock_bpm(const pnp_clock_t *c) {
    if (c->have_period && c->period_ema > 0.0)
        return c->fs * 60.0 / (c->period_ema * TICKS_PER_BEAT);
    return c->fallback_bpm;
}
