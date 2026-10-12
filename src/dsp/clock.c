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
 *
 * Second source: the host's own transport position (set_host_beat). The host
 * broadcasts clock bytes to chain slots only, so in Master FX no tick ever
 * arrives; there the beat position the host reports is the whole clock. Ticks
 * win whenever they are arriving, so a chain slot behaves exactly as before.
 */
#include "clock.h"

#define TICKS_PER_BEAT 24.0
#define EMA_ALPHA 0.15
#define CLOCK_TIMEOUT_SEC 0.5   /* no tick within this -> clock considered absent */

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

/* Ticks measured and one landed within the timeout window. */
static int ticks_active(const pnp_clock_t *c) {
    if (!c->have_period) return 0;
    uint32_t since = c->sample_pos - c->last_tick_sample;   /* unsigned wrap-safe */
    return (double)since < c->fs * CLOCK_TIMEOUT_SEC;
}

void pnp_clock_set_host_beat(pnp_clock_t *c, double beat, double bpm) {
    if (beat < 0.0) { c->host_locked = 0; return; }
    if (bpm < 20.0) bpm = c->fallback_bpm;
    if (bpm > 400.0) bpm = 400.0;
    c->host_locked = 1;
    c->host_beat = beat;
    c->host_bpm = bpm;
}

int pnp_clock_on_host(const pnp_clock_t *c) {
    return c->host_locked && !ticks_active(c);
}

double pnp_clock_block_start(pnp_clock_t *c) {
    double samples_per_beat;
    if (pnp_clock_on_host(c)) {
        c->beat_pos = c->host_beat;   /* the host interpolates this per block */
        samples_per_beat = c->fs * 60.0 / c->host_bpm;
    } else if (c->running && c->have_period) {
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
    if (pnp_clock_on_host(c)) return c->host_bpm;
    if (c->have_period && c->period_ema > 0.0)
        return c->fs * 60.0 / (c->period_ema * TICKS_PER_BEAT);
    return c->fallback_bpm;
}

/* True while there is a clock to lock to: MIDI clock ticks are arriving, or the
 * host reports a running transport. When neither holds (transport stopped / no
 * clock source), PushNPull passes audio through untouched. */
int pnp_clock_active(const pnp_clock_t *c) {
    return ticks_active(c) || c->host_locked;
}
