#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "svf_core.h"
#include "smoother.h"
#include "modulation.h"
#include "model_ladder.h"
#include "curves.h"
#include "shaper.h"
#include "clock.h"
#include "vol.h"

#define FS 44100.0
#define PI 3.14159265358979323846

static int g_fail = 0;
#define CHECK(cond, ...) do { if(!(cond)){ \
    fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr,"\n"); g_fail=1; } \
    else { printf("ok: " __VA_ARGS__); printf("\n"); } } while(0)

/* Steady-state linear gain of one SVF at frequency f_hz. */
static double svf_gain_at(svf_t *s, double f_hz) {
    const int warm = 8192, meas = 8192;
    double w = 2.0*PI*f_hz/FS, ph = 0.0, sumsq = 0.0;
    for (int i = 0; i < warm+meas; i++) {
        double x = sin(ph); ph += w; if (ph > 2*PI) ph -= 2*PI;
        double y = svf_process(s, x);
        if (i >= warm) sumsq += y*y;
    }
    double rms = sqrt(sumsq/meas);
    return rms / sqrt(0.5);   /* normalize: input sine RMS = 1/sqrt(2) */
}

static double gain_db(svf_t *s, double f) { return 20.0*log10(svf_gain_at(s,f)+1e-12); }

/* ladder gain probe at LOW amplitude so the tanh nonlinearity stays ~linear and we
   measure the filter's frequency response (slope/peak), not saturation. */
static double ladder_gain_at(ladder_t *m, double f_hz) {
    const int warm=8192, meas=8192; const double A=0.05;
    double w=2.0*PI*f_hz/FS, ph=0.0, sumsq=0.0;
    for (int i=0;i<warm+meas;i++){ double x=A*sin(ph); ph+=w; if(ph>2*PI)ph-=2*PI;
        double y=ladder_process(m,x); if(i>=warm) sumsq+=y*y; }
    return sqrt(sumsq/meas)/(A*sqrt(0.5));
}
static double ladder_gain_db(ladder_t *m, double f){ return 20.0*log10(ladder_gain_at(m,f)+1e-12); }

/* Max per-sample output jump when cutoff steps 200->8000 Hz mid-stream, probing
   with a steady 300 Hz tone. The tone's own per-sample slew is tiny (~0.02), so a
   large jump isolates the coefficient-step transient (zipper). 300 Hz also sits in
   the gain-transition region across the sweep, where a cutoff step is most audible.
   smoothed=0 steps cutoff instantly; smoothed=1 ramps it in log space. */
static double antizip_max_jump(int smoothed) {
    smoother_t sm; smooth_init(&sm, FS); smooth_set_tau(&sm, 0.018); smooth_reset(&sm, log(200.0));
    svf_t f; svf_init(&f, FS);
    double prev=0.0, mx=0.0, fc=200.0, ph=0.0, w=2.0*PI*300.0/FS;
    for (int i=0;i<8000;i++) {
        if (i==2000) { if (smoothed) smooth_target(&sm, log(8000.0)); else fc=8000.0; }
        double c = smoothed ? exp(smooth_next(&sm)) : fc;
        svf_set(&f, c, 0.7, SVF_LP);
        double x = sin(ph); ph += w; if (ph > 2*PI) ph -= 2*PI;
        double y = svf_process(&f, x);
        double j = fabs(y - prev); if (i>10 && j>mx) mx=j;
        prev = y;
    }
    return mx;
}

int main(void) {
    printf("harness up\n");

    svf_t s; svf_init(&s, FS);
    /* res 0.2929 -> k=sqrt(2) -> Butterworth (Q=0.707), the -3dB-at-cutoff point.
       res=0 would be Q=0.5 (critically damped, -6dB at cutoff). */
    svf_set(&s, /*cutoff_hz*/1000.0, /*res 0..1*/0.2929, SVF_LP);
    CHECK(fabs(gain_db(&s, 1000.0) - (-3.0)) < 1.0, "LP -3dB at cutoff (got %.2f)", gain_db(&s,1000.0));
    CHECK(fabs(gain_db(&s, 100.0)) < 0.5,  "LP passband flat at 100Hz (got %.2f)", gain_db(&s,100.0));
    double slope = gain_db(&s, 4000.0) - gain_db(&s, 2000.0);
    CHECK(fabs(slope - (-12.0)) < 2.0, "LP slope ~-12dB/oct (got %.2f)", slope);

    svf_set(&s, 1000.0, 0.0, SVF_HP);
    CHECK(gain_db(&s, 50.0) < -20.0, "HP rejects sub-cutoff (got %.2f)", gain_db(&s,50.0));
    CHECK(fabs(gain_db(&s, 10000.0)) < 0.5, "HP passband flat high");

    /* BP raw band tap peaks at 1/k; res=0.5 -> k=1 -> unity (0 dB) at center.
       (res=0 -> k=2 -> -6 dB at center, since this SVF does not k-normalize the
       band output.) */
    svf_set(&s, 1000.0, 0.5, SVF_BP);
    CHECK(gain_db(&s, 50.0) < -15.0 && gain_db(&s, 20000.0) < -15.0, "BP rejects both ends");
    CHECK(fabs(gain_db(&s, 1000.0)) < 1.0, "BP ~unity at center (got %.2f)", gain_db(&s,1000.0));

    svf_set(&s, 1000.0, 0.5, SVF_NOTCH);
    CHECK(gain_db(&s, 1000.0) < -12.0, "Notch nulls at cutoff (got %.2f)", gain_db(&s,1000.0));

    svf_set(&s, 1000.0, 0.0, SVF_AP);
    CHECK(fabs(gain_db(&s, 500.0)) < 0.5 && fabs(gain_db(&s, 4000.0)) < 0.5, "AP flat magnitude");

    for (double fc=40; fc<18000; fc*=1.5)
      for (double r=0.0; r<=1.0; r+=0.1) {
        svf_set(&s, fc, r, SVF_LP); double acc=0;
        for (int i=0;i<2000;i++) acc += svf_process(&s, (i%2?1.0:-1.0));
        CHECK(isfinite(acc), "stable fc=%.0f res=%.1f", fc, r);
      }

    /* Anti-zipper: the smoothed cutoff step must not glitch, AND the test must
       discriminate — an unsmoothed (instant) step on the same signal MUST exceed
       the threshold, else the test would pass trivially and protect nothing. */
    double az_smooth = antizip_max_jump(1), az_step = antizip_max_jump(0);
    CHECK(az_smooth < 0.3, "smoothed cutoff step: jump %.3f < 0.3", az_smooth);
    CHECK(az_step > 0.6, "unsmoothed step zippers: jump %.3f > 0.6 (test discriminates)", az_step);

    /* ===== M2 modulation: envelope follower ===== */
    {
        envfollow_t e; envf_init(&e, FS); envf_set(&e, 5.0, 200.0);
        double env = 0;
        for (int i=0;i<FS/10;i++) env = envf_process(&e, 1.0);
        CHECK(env > 0.9, "envf rises to ~1 on full input (got %.3f)", env);
        CHECK(env <= 1.0001, "envf bounded <=1 (got %.3f)", env);
        for (int i=0;i<FS/2;i++) env = envf_process(&e, 0.0);
        CHECK(env < 0.1, "envf decays toward 0 on silence (got %.3f)", env);

        /* attack faster than release: samples for 0->0.5 rising vs 1->0.5 falling.
         * Must PRIME env to ~1 before measuring the fall (else it starts at 0.5
         * and crosses immediately). */
        envfollow_t e2; envf_init(&e2, FS); envf_set(&e2, 5.0, 200.0);
        int up=0; while (envf_process(&e2, 1.0) < 0.5 && up < (int)FS) up++;
        for (int i=0;i<FS/10;i++) envf_process(&e2, 1.0);   /* prime to ~1 */
        int dn=0; while (envf_process(&e2, 0.0) > 0.5 && dn < (int)FS) dn++;
        CHECK(up < dn, "envf attack (%d) faster than release (%d)", up, dn);
    }

    /* ===== M2 modulation: LFO ===== */
    {
        /* continuous shapes: bipolar and swing the full range over a cycle */
        int cont[] = { LFO_SINE, LFO_TRI, LFO_SAW, LFO_SQR };
        for (int si=0; si<4; si++) {
            int shp = cont[si];
            lfo_t l; lfo_init(&l, FS); lfo_set_rate_hz(&l, 2.0);
            double lo=2, hi=-2; int ok=1;
            for (int i=0;i<FS;i++){ double v=lfo_process(&l, shp);
                if (v<lo)lo=v; if (v>hi)hi=v; if (!isfinite(v)||v<-1.0001||v>1.0001) ok=0; }
            CHECK(ok, "lfo shape %d stays in [-1,1]", shp);
            CHECK(hi>0.5 && lo<-0.5, "lfo shape %d swings full (lo %.2f hi %.2f)", shp, lo, hi);
        }
        /* square is exactly +/-1 */
        { lfo_t l; lfo_init(&l, FS); lfo_set_rate_hz(&l, 1.0);
          int sq=1; for (int i=0;i<FS;i++){ double v=lfo_process(&l, LFO_SQR);
              if (fabs(fabs(v)-1.0) > 1e-9) sq=0; }
          CHECK(sq, "lfo square is exactly +/-1"); }
        /* 1 Hz period: sine returns near its start value after FS samples */
        { lfo_t l; lfo_init(&l, FS); lfo_set_rate_hz(&l, 1.0);
          double first = lfo_process(&l, LFO_SINE);
          for (int i=1;i<FS;i++) lfo_process(&l, LFO_SINE);
          double after = lfo_process(&l, LFO_SINE);
          CHECK(fabs(after-first) < 0.02, "lfo 1Hz cycle returns to start (%.3f vs %.3f)", after, first); }
        /* S&H: bipolar and varies across many cycles (random, stepwise) */
        { lfo_t l; lfo_init(&l, FS); lfo_set_rate_hz(&l, 200.0);
          double lo=2, hi=-2; int ok=1;
          for (int i=0;i<FS;i++){ double v=lfo_process(&l, LFO_SH);
              if (v<lo)lo=v; if (v>hi)hi=v; if (!isfinite(v)||v<-1.0001||v>1.0001) ok=0; }
          CHECK(ok, "lfo S&H stays in [-1,1]");
          CHECK(hi>0.3 && lo<-0.3, "lfo S&H varies over cycles (lo %.2f hi %.2f)", lo, hi); }
        /* S&H holds a constant value within one cycle */
        { lfo_t l; lfo_init(&l, FS); lfo_set_rate_hz(&l, 4.0); /* cycle = FS/4 */
          double a = lfo_process(&l, LFO_SH); int held = 1;
          for (int i=1;i<FS/8;i++){ double v=lfo_process(&l, LFO_SH);
              if (fabs(v-a) > 1e-12) held = 0; }
          CHECK(held, "lfo S&H holds value within a cycle"); }
    }

    /* ===== M3: ladder ladder model ===== */
    {
        ladder_t m; ladder_init(&m, FS);
        /* low res: flat passband, steep 4-pole slope */
        ladder_set(&m, 1000.0, 0.0);
        CHECK(fabs(ladder_gain_db(&m, 100.0)) < 2.0, "ladder passband flat at low res (got %.2f)", ladder_gain_db(&m,100.0));
        double slope = ladder_gain_db(&m, 4000.0) - ladder_gain_db(&m, 2000.0);
        CHECK(slope < -16.0, "ladder steep (4-pole) slope (got %.1f dB/oct)", slope);

        /* strong resonance just below self-oscillation: resonant peak well above
         * the passband, AND the low end thins out (the ladder "honk" that
         * distinguishes it from the body-keeping SVF). Probe is valid here because
         * the filter isn't self-oscillating yet. */
        ladder_set(&m, 1000.0, 0.7);
        double peak = ladder_gain_db(&m, 1000.0);
        double pass = ladder_gain_db(&m, 100.0);
        CHECK(peak - pass > 7.0, "ladder resonance peaks above passband (+%.1f dB)", peak - pass);
        CHECK(pass < -4.0, "ladder low end thins with resonance (got %.1f dB)", pass);

        /* max resonance self-oscillates: kick it, let input go silent, the tail
         * keeps ringing (a sustained limit cycle, bounded by the saturators). */
        ladder_set(&m, 1000.0, 1.0);
        ladder_process(&m, 1.0);
        for (int i=0;i<4000;i++) ladder_process(&m, 0.0);
        double sq=0; for (int i=0;i<8192;i++){ double y=ladder_process(&m,0.0); sq+=y*y; }
        double osc_rms = sqrt(sq/8192);
        CHECK(osc_rms > 0.01, "ladder self-oscillates at max res (tail rms %.3f)", osc_rms);

        int stable=1;
        for (double fc=60; fc<16000; fc*=1.7)
          for (double r=0.0; r<=1.0; r+=0.2){ ladder_set(&m, fc, r); double acc=0;
            for (int i=0;i<3000;i++) acc += ladder_process(&m, (i%2?0.3:-0.3));
            if (!isfinite(acc)) stable=0; }
        CHECK(stable, "ladder stable across fc/res grid");
    }

    /* ===== PushNPull: curve bank ===== */
    {
        /* bounds + resting: every curve in [-1,1]; all but Push are <= 0 */
        for (int cv = 0; cv < PNP_CURVE_COUNT; cv++) {
            int ok = 1, nonzero = 0;
            for (int i = 0; i <= 1000; i++) {
                double p = i / 1000.0;
                double m = pnp_curve_eval(cv, p, PNP_CURVE_DEFAULT_SLOPE[cv]);
                if (!isfinite(m) || m < -1.0001 || m > 1.0001) ok = 0;
                if (fabs(m) > 0.01) nonzero = 1;
            }
            CHECK(ok, "curve %s stays in [-1,1]", PNP_CURVE_NAMES[cv]);
            CHECK(nonzero, "curve %s is not flat", PNP_CURVE_NAMES[cv]);
        }
        /* Sidechain 1: full duck at the beat, recovered by end of cycle */
        CHECK(pnp_curve_eval(CURVE_SIDECHAIN1, 0.0, 0.55) < -0.95,
              "Sidechain1 fully ducked at p=0");
        CHECK(fabs(pnp_curve_eval(CURVE_SIDECHAIN1, 0.99, 0.55)) < 0.05,
              "Sidechain1 recovered at end");
        /* Push: positive at the beat (bidirectional path) */
        CHECK(pnp_curve_eval(CURVE_PUSH, 0.0, 0.55) > 0.95,
              "Push boosts at p=0");
        /* Reverse: resting at the start, fully ducked AT the downbeat (p->1) */
        CHECK(fabs(pnp_curve_eval(CURVE_REVERSE, 0.0, 0.40)) < 0.05,
              "Reverse resting at start");
        CHECK(pnp_curve_eval(CURVE_REVERSE, 0.999, 0.40) < -0.9,
              "Reverse ducks into the downbeat");
        /* Gate: ducked inside the window, resting outside */
        CHECK(pnp_curve_eval(CURVE_GATE, 0.06, 0.12) < -0.9, "Gate ducked in window");
        CHECK(fabs(pnp_curve_eval(CURVE_GATE, 0.5, 0.12)) < 0.05, "Gate open outside window");
        /* Pulse: ducks at both p=0 and p=0.5 */
        CHECK(pnp_curve_eval(CURVE_PULSE, 0.0, 0.5) < -0.9, "Pulse ducks at p=0");
        CHECK(pnp_curve_eval(CURVE_PULSE, 0.5, 0.5) < -0.9, "Pulse ducks at p=0.5");
        /* Trim: full duck at the beat, recovers fast (cubic) - small by mid-window */
        CHECK(pnp_curve_eval(CURVE_TRIM, 0.0, 0.18) < -0.95, "Trim full duck at p=0");
        CHECK(fabs(pnp_curve_eval(CURVE_TRIM, 0.09, 0.18)) < 0.2, "Trim recovers fast (cubic)");
        /* Swell: positive boost bump - rests at edges, peaks mid-window */
        CHECK(fabs(pnp_curve_eval(CURVE_SWELL, 0.0, 0.70)) < 0.05, "Swell rests at p=0");
        CHECK(pnp_curve_eval(CURVE_SWELL, 0.35, 0.70) > 0.9, "Swell boosts at mid-window");
        /* Stutter: re-ducks at each of 4 subdivisions (p=0 and p=A/4) */
        CHECK(pnp_curve_eval(CURVE_STUTTER, 0.0, 0.90) < -0.9, "Stutter ducks at subdivision 0");
        CHECK(pnp_curve_eval(CURVE_STUTTER, 0.225, 0.90) < -0.9, "Stutter re-ducks at subdivision 1");
        /* Pump: bipolar - ducks on the beat AND boosts mid-window */
        CHECK(pnp_curve_eval(CURVE_PUMP, 0.0, 0.80) < -0.9, "Pump ducks at p=0");
        CHECK(pnp_curve_eval(CURVE_PUMP, 0.40, 0.80) > 0.2, "Pump boosts mid-window (bipolar)");
    }

    /* ===== PushNPull: shaper (shift + curve dispatch) ===== */
    {
        shaper_t sh; shaper_init(&sh);
        sh.curve = CURVE_SIDECHAIN1; sh.slope = 0.55; sh.shift = 0.5;   /* no offset */
        CHECK(shaper_eval(&sh, 0.0) < -0.95, "shaper passes curve through at shift=center");
        /* shift > center moves the duck LATER: the trough that was at p=0 now sits near p=+offset */
        sh.shift = 0.75;                                  /* +0.25 cycle offset */
        CHECK(shaper_eval(&sh, 0.25) < -0.95, "shaper shift delays the trough to p=0.25");
        CHECK(fabs(shaper_eval(&sh, 0.0)) < 0.2, "shaper shift moved energy off p=0");
        /* wrap-around: shift below center wraps cleanly, output stays bounded */
        sh.shift = 0.1; int ok = 1;
        for (int i = 0; i <= 1000; i++) {
            double m = shaper_eval(&sh, i / 1000.0);
            if (!isfinite(m) || m < -1.0001 || m > 1.0001) ok = 0;
        }
        CHECK(ok, "shaper bounded across all phases with wrap");
    }

    /* ===== PushNPull: attack (onset ease-in) ===== */
    {
        shaper_t sa; shaper_init(&sa);
        sa.curve = CURVE_SIDECHAIN1; sa.slope = 0.55; sa.shift = 0.5;
        sa.attack = 0.0;
        CHECK(shaper_eval(&sa, 0.0) < -0.95, "attack=0: instant full duck at onset");
        sa.attack = 0.25;
        CHECK(fabs(shaper_eval(&sa, 0.0)) < 0.05, "attack softens onset: ~rest at p=0");
        /* DEPTH PRESERVED: the dip still reaches full -1 somewhere in the cycle */
        double mn = 0.0;
        for (int i = 0; i <= 2000; i++) { double m = shaper_eval(&sa, i / 2000.0); if (m < mn) mn = m; }
        CHECK(mn < -0.98, "attack preserves full dip depth (min %.3f)", mn);
        /* within the attack window the duck is eased in (shallower than raw curve) */
        double raw05 = pnp_curve_eval(CURVE_SIDECHAIN1, 0.05, 0.55);
        CHECK(fabs(shaper_eval(&sa, 0.05)) < fabs(raw05),
              "attack: eased-in (shallower) during ramp (%.3f vs raw %.3f)", shaper_eval(&sa,0.05), raw05);
        /* past the window the curve resumes, delayed by the attack time */
        CHECK(fabs(shaper_eval(&sa, 0.5) - pnp_curve_eval(CURVE_SIDECHAIN1, 0.25, 0.55)) < 1e-9,
              "attack: curve resumes delayed by attack");
        /* bounded everywhere with attack engaged */
        int aok = 1;
        for (int i = 0; i <= 1000; i++) { double m = shaper_eval(&sa, i / 1000.0);
            if (!isfinite(m) || m < -1.0001 || m > 1.0001) aok = 0; }
        CHECK(aok, "attack: bounded across all phases");
    }

    /* ===== PushNPull: clock (MIDI-clock phase lock) ===== */
    {
        /* 120 BPM => 918.75 samples per tick (24 PPQN) at 44.1k. Feed exact ticks
         * by setting sample_pos at each tick boundary (white-box). */
        double spt = FS * 60.0 / (120.0 * 24.0);
        pnp_clock_t c; pnp_clock_init(&c, FS); pnp_clock_set_fallback_bpm(&c, 100.0);
        uint8_t start = 0xFA, tick = 0xF8;
        pnp_clock_on_midi(&c, &start, 1);
        CHECK(c.running == 1 && c.tick_count == 0, "clock: Start sets running + resets origin");
        for (int t = 1; t <= 48; t++) {           /* 2 beats of ticks */
            c.sample_pos = (uint32_t)llround(t * spt);
            pnp_clock_on_midi(&c, &tick, 1);
        }
        CHECK(fabs(pnp_clock_bpm(&c) - 120.0) < 1.0,
              "clock: derives 120 BPM from ticks (got %.2f)", pnp_clock_bpm(&c));
        CHECK(c.tick_count == 48, "clock: counted 48 ticks (got %u)", c.tick_count);

        /* Phase origin: at a whole number of beats the 1/4 cycle phase is ~0.
         * block_start resyncs beat_pos to tick_count/24 = 2.0 -> phase 0. */
        pnp_clock_block_start(&c);
        double beats_per_cycle = 1.0;            /* 1/4 */
        double phase = c.beat_pos / beats_per_cycle;
        phase -= (double)(long)phase;            /* frac */
        CHECK(fabs(phase) < 0.001 || fabs(phase - 1.0) < 0.001,
              "clock: downbeat phase ~0 after 2 beats (got %.4f)", phase);

        /* Per-sample increment matches 1/(samples per beat). */
        double inc = pnp_clock_block_start(&c);
        double expect_inc = 1.0 / (spt * 24.0);
        CHECK(fabs(inc - expect_inc) / expect_inc < 0.02,
              "clock: per-sample beat inc correct (got %.3e want %.3e)", inc, expect_inc);

        /* Stop clears running; with no clock we fall back to fallback BPM. */
        uint8_t stop = 0xFC; pnp_clock_on_midi(&c, &stop, 1);
        CHECK(c.running == 0, "clock: Stop clears running");
        pnp_clock_t f; pnp_clock_init(&f, FS); pnp_clock_set_fallback_bpm(&f, 140.0);
        double finc = pnp_clock_block_start(&f);
        double fexpect = 1.0 / (FS * 60.0 / 140.0);   /* 1 / samples-per-beat at 140 */
        CHECK(fabs(finc - fexpect) / fexpect < 0.02,
              "clock: free-run uses fallback BPM when stopped (got %.3e want %.3e)", finc, fexpect);

        /* ---- clock-active gate (bypass when no clock) ---- */
        pnp_clock_t a; pnp_clock_init(&a, FS);
        CHECK(pnp_clock_active(&a) == 0, "active: false before any clock (fresh)");
        uint8_t st2 = 0xFA, tk2 = 0xF8;
        pnp_clock_on_midi(&a, &st2, 1);
        CHECK(pnp_clock_active(&a) == 0, "active: still false on Start alone (no period yet)");
        for (int t = 1; t <= 8; t++) { a.sample_pos = (uint32_t)llround(t * spt); pnp_clock_on_midi(&a, &tk2, 1); }
        CHECK(pnp_clock_active(&a) == 1, "active: true while ticks are arriving");
        /* advance well past the timeout with no further ticks -> inactive (bypass) */
        a.sample_pos += (uint32_t)(FS * 1.0);   /* 1s gap > 0.5s timeout */
        CHECK(pnp_clock_active(&a) == 0, "active: false after clock stops (timeout -> bypass)");
        /* a fresh tick re-activates */
        pnp_clock_on_midi(&a, &tk2, 1);
        CHECK(pnp_clock_active(&a) == 1, "active: re-activates when ticks resume");
    }

    /* ===== PushNPull: volume gain + band split ===== */
    {
        /* gain = 1 - volume*m, soft-clamped to >=0. volume: 0=off, -=duck (default), +=pump */
        CHECK(fabs(pnp_gain(0.0, -1.0) - 1.0) < 1e-9, "gain unity at volume=0 (off)");
        CHECK(fabs(pnp_gain(-1.0, -1.0) - 0.0) < 1e-9, "gain full duck at volume=-1 (default), duck curve");
        CHECK(pnp_gain(1.0, -1.0) > 1.5, "positive volume pumps/boosts on a duck curve");
        CHECK(fabs(pnp_gain(0.5, 0.0) - 1.0) < 1e-9, "gain unity at m=0 regardless of volume");
        CHECK(pnp_gain(1.0, 5.0) >= 0.0, "gain never negative");

        /* band split: low + high == input when no gain applied (unity recombine) */
        pnp_split_t sp; pnp_split_init(&sp, FS, 320.0);
        double maxerr = 0.0, ph = 0.0, w = 2.0*PI*1000.0/FS;
        for (int i = 0; i < 4000; i++) {
            double x = sin(ph); ph += w; if (ph > 2*PI) ph -= 2*PI;
            double lo, hi; pnp_split(&sp, x, &lo, &hi);
            double err = fabs((lo + hi) - x);
            if (i > 200 && err > maxerr) maxerr = err;
        }
        CHECK(maxerr < 1e-6, "band split recombines to unity (max err %.2e)", maxerr);

        /* a 50 Hz tone is mostly in the LOW band; a 5 kHz tone mostly in HIGH */
        pnp_split_t s2; pnp_split_init(&s2, FS, 320.0);
        double lo_e = 0, hi_e = 0, p2 = 0, wl = 2.0*PI*50.0/FS;
        for (int i = 0; i < 8000; i++){ double x=sin(p2); p2+=wl; if(p2>2*PI)p2-=2*PI;
            double lo,hi; pnp_split(&s2,x,&lo,&hi); if(i>2000){lo_e+=lo*lo; hi_e+=hi*hi;} }
        CHECK(lo_e > hi_e * 4.0, "50Hz lands in low band");
    }

    /* ===== PushNPull: integration — Sidechain1 ducks a tone on the beat ===== */
    {
        shaper_t sh; shaper_init(&sh); sh.curve=CURVE_SIDECHAIN1; sh.slope=0.55; sh.shift=0.5;
        /* energy in the first 1/8 of the cycle (ducked) should be far below the
         * last 1/8 (recovered) for a full-depth volume duck. */
        double e_early=0, e_late=0, ph=0, w=2.0*PI*440.0/FS;
        int N=2000;
        for (int i=0;i<N;i++){
            double cphase=(double)i/N;
            double x=sin(ph); ph+=w; if(ph>2*PI)ph-=2*PI;
            double g=pnp_gain(-1.0f,(float)shaper_eval(&sh,cphase));  /* -1 = full duck */
            double y=x*g;
            if (cphase<0.125) e_early+=y*y;
            if (cphase>0.875) e_late +=y*y;
        }
        CHECK(e_late > e_early*5.0, "Sidechain1 ducks early-cycle energy vs late (%.3f vs %.3f)", e_early, e_late);
    }

    return g_fail;
}
