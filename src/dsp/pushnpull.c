/*
 * PushNPull — tempo-synced bidirectional curve-shaping audio FX.
 * A beat-locked curve (clock.c -> shaper.c) produces m in [-1,1], routed to a
 * volume target (with optional band split) and/or a multimode filter cutoff.
 * Built on schwung-filter's SVF/ladder/smoother DSP.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdint.h>

#include "audio_fx_api_v2.h"   /* host current header: has on_midi */
#include "svf_core.h"
#include "model_ladder.h"
#include "smoother.h"
#include "clock.h"
#include "shaper.h"
#include "curves.h"
#include "vol.h"

#define SAMPLE_RATE 44100.0

/* ---- enums / tables ---- */
/* No Target selector: a target is active iff its depth (volume/filter) is nonzero. */
#define DEPTH_EPS 1e-4f

enum { MODEL_SVF = 0, MODEL_LADDER };
static const char *MODEL_NAMES[] = { "SVF", "Schwoog" };
#define MODEL_COUNT 2

/* Length -> beats per cycle */
static const char  *LEN_NAMES[] = { "1/8", "1/4", "1/2", "1/1" };
static const double LEN_BEATS[] = { 0.5, 1.0, 2.0, 4.0 };
#define LEN_COUNT 4

static int idx_from_string(const char *s, const char **names, int n, int dflt) {
    for (int i = 0; i < n; i++) if (strcasecmp(s, names[i]) == 0) return i;
    return dflt;
}
static int curve_from_string(const char *s) {
    for (int i = 0; i < PNP_CURVE_COUNT; i++)
        if (strcasecmp(s, PNP_CURVE_NAMES[i]) == 0) return i;
    return CURVE_SIDECHAIN1;
}
static int mode_from_string(const char *s) {
    if (!strcasecmp(s,"LP")) return SVF_LP;   if (!strcasecmp(s,"HP")) return SVF_HP;
    if (!strcasecmp(s,"BP")) return SVF_BP;   if (!strcasecmp(s,"Notch")) return SVF_NOTCH;
    if (!strcasecmp(s,"Peak")) return SVF_PEAK; if (!strcasecmp(s,"AP")) return SVF_AP;
    return SVF_LP;
}
static const char* mode_to_string(int m){
    switch(m){case SVF_HP:return "HP";case SVF_BP:return "BP";case SVF_NOTCH:return "Notch";
    case SVF_PEAK:return "Peak";case SVF_AP:return "AP";default:return "LP";}
}

static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float db_to_lin(float db){ return powf(10.0f, db/20.0f); }

#define CUTOFF_LOG_SPAN 6.9077553f   /* logf(1000) — 20..20000 Hz */
static inline float cutoff_norm_to_hz(float n){ return 20.0f * expf(n * CUTOFF_LOG_SPAN); }
#define RES_Q_MAX 18.0f
static inline float resonance_taper(float r){
    if (r<=0.0f) return 0.0f; if (r>1.0f) r=1.0f;
    return 1.0f - powf(2.0f*RES_Q_MAX, -r);
}
static inline float soft_limit(float x){
    const float thr=0.70f, lim=0.98f; float a=fabsf(x);
    if (a<=thr) return x; float range=lim-thr; float s=(x<0.0f)?-1.0f:1.0f;
    return s*(thr + range*tanhf((a-thr)/range));
}

/* ---- minimal JSON helpers (from donor) ---- */
static int json_num(const char *j, const char *k, float *out){
    char pat[64]; snprintf(pat,sizeof(pat),"\"%s\"",k);
    const char *p=strstr(j,pat); if(!p) return -1; p+=strlen(pat);
    while(*p && (*p==' '||*p==':'||*p=='\t')) p++; if(!*p) return -1;
    *out=strtof(p,NULL); return 0;
}
static int json_str(const char *j, const char *k, char *out, int n){
    char pat[64]; snprintf(pat,sizeof(pat),"\"%s\"",k);
    const char *p=strstr(j,pat); if(!p) return -1; p+=strlen(pat);
    while(*p && *p!='"') p++; if(*p!='"') return -1; p++;
    const char *e=strchr(p,'"'); if(!e) return -1; int len=(int)(e-p);
    if(len>=n) len=n-1; memcpy(out,p,len); out[len]='\0'; return 0;
}

/* ---- instance ---- */
typedef struct {
    char module_dir[256];
    /* params */
    int   curve;
    int   length;
    float volume;       /* -1..1 bipolar: -=duck, +=pump, 0=off (default -1) */
    float slope;        /* 0..1 */
    float shift;        /* 0..1 (0.5 center) */
    float attack;       /* 0..1 onset ease-in (0 = instant) */
    int   model;
    int   mode;
    float cutoff;       /* 0..1 */
    float resonance;    /* 0..1 */
    float filter;       /* -1..1 bipolar curve->cutoff depth */
    float drive;        /* 0..1 */
    int   band_on;
    float band_freq;    /* Hz */
    float output_db;    /* -24..12 */
    /* dsp */
    pnp_clock_t clk;
    shaper_t    shp;
    svf_t fL, fR;
    ladder_t ladderL, ladderR;
    pnp_split_t split;
    smoother_t sm_cut, sm_res, sm_drive, sm_out, sm_vol, sm_filt;
} pnp_t;

static const host_api_v1_t *g_host = NULL;
static void pnp_log(const char *m){
    if (g_host && g_host->log){ char b[256]; snprintf(b,sizeof(b),"[PushNPull] %s",m); g_host->log(b); }
}

/* ---- lifecycle ---- */
static void* pnp_create(const char *module_dir, const char *config_json){
    (void)config_json;
    pnp_t *I = (pnp_t*)calloc(1,sizeof(pnp_t));
    if (!I) return NULL;
    if (module_dir) strncpy(I->module_dir, module_dir, sizeof(I->module_dir)-1);

    I->curve=CURVE_SIDECHAIN1; I->length=1; /* 1/4 */
    I->volume=-1.0f; I->slope=PNP_CURVE_DEFAULT_SLOPE[CURVE_SIDECHAIN1]; I->shift=0.5f; I->attack=0.0f;
    I->model=MODEL_SVF; I->mode=SVF_LP; I->cutoff=0.5f; I->resonance=0.2f;
    I->filter=0.0f; I->drive=0.0f; I->band_on=0; I->band_freq=320.0f; I->output_db=0.0f;

    pnp_clock_init(&I->clk, SAMPLE_RATE);
    if (g_host && g_host->get_bpm){ float b=g_host->get_bpm(); if (b>=20.0f) pnp_clock_set_fallback_bpm(&I->clk,b); }
    shaper_init(&I->shp); I->shp.curve=I->curve; I->shp.slope=I->slope; I->shp.shift=I->shift; I->shp.attack=I->attack;
    svf_init(&I->fL,SAMPLE_RATE); svf_init(&I->fR,SAMPLE_RATE);
    ladder_init(&I->ladderL,SAMPLE_RATE); ladder_init(&I->ladderR,SAMPLE_RATE);
    pnp_split_init(&I->split, SAMPLE_RATE, I->band_freq);

    smooth_init(&I->sm_cut,SAMPLE_RATE);   smooth_set_tau(&I->sm_cut,0.018);  smooth_reset(&I->sm_cut,I->cutoff);
    smooth_init(&I->sm_res,SAMPLE_RATE);   smooth_set_tau(&I->sm_res,0.020);  smooth_reset(&I->sm_res,I->resonance);
    smooth_init(&I->sm_drive,SAMPLE_RATE); smooth_set_tau(&I->sm_drive,0.012); smooth_reset(&I->sm_drive,0.0);
    smooth_init(&I->sm_out,SAMPLE_RATE);   smooth_set_tau(&I->sm_out,0.012);  smooth_reset(&I->sm_out,1.0);
    smooth_init(&I->sm_vol,SAMPLE_RATE);   smooth_set_tau(&I->sm_vol,0.010);  smooth_reset(&I->sm_vol,I->volume);
    smooth_init(&I->sm_filt,SAMPLE_RATE);  smooth_set_tau(&I->sm_filt,0.010); smooth_reset(&I->sm_filt,I->filter);
    pnp_log("instance created");
    return I;
}
static void pnp_destroy(void *inst){ if (inst) free(inst); }

/* ---- MIDI: feed clock ---- */
static void pnp_on_midi(void *inst, const uint8_t *msg, int len, int source){
    (void)source;
    pnp_t *I=(pnp_t*)inst; if(!I||!msg||len<1) return;
    pnp_clock_on_midi(&I->clk, msg, len);
}

static void pnp_process(void *inst, int16_t *audio, int frames){
    pnp_t *I=(pnp_t*)inst; if(!I) return;

    double inc = pnp_clock_block_start(&I->clk);   /* beats per sample (resyncs phase) */

    /* No MIDI clock -> effectively bypassed: leave audio untouched and pass through.
     * (Still advance the clock so tick timing/detection stays correct.) */
    if (!pnp_clock_active(&I->clk)) {
        pnp_clock_advance_block(&I->clk, frames);
        return;
    }

    double beats_per_cycle = LEN_BEATS[I->length];
    double beat = I->clk.beat_pos;
    /* No target selector: each target runs only when its depth is nonzero, so a
     * 0% filter is fully bypassed (no tone coloring) and 0% volume is identity. */
    int do_vol    = (fabsf(I->volume) > DEPTH_EPS);
    int do_filt   = (fabsf(I->filter) > DEPTH_EPS);

    for (int i=0;i<frames;i++){
        float base_cut=(float)smooth_next(&I->sm_cut);
        float res_raw =(float)smooth_next(&I->sm_res);
        float res     = resonance_taper(res_raw);
        float drv     =(float)smooth_next(&I->sm_drive);
        float og      =(float)smooth_next(&I->sm_out);
        float vol     =(float)smooth_next(&I->sm_vol);
        float fdepth  =(float)smooth_next(&I->sm_filt);

        /* curve value at this sample's cycle phase */
        double cphase = beat / beats_per_cycle;
        cphase -= floor(cphase);
        float m = (float)shaper_eval(&I->shp, cphase);
        beat += inc;

        float xl = audio[i*2]   / 32768.0f;
        float xr = audio[i*2+1] / 32768.0f;

        /* ---- filter target (runs first) ---- */
        /* sign convention matches volume: -depth = duck (cutoff closes on the
         * curve), +depth = pump (cutoff opens), 0 = off. Hence base - depth*m. */
        if (do_filt){
            float cut_norm = clampf(base_cut - fdepth * m, 0.0f, 1.0f);
            float cut = cutoff_norm_to_hz(cut_norm);
            if (drv > 0.0005f){
                float k=1.0f+drv*7.0f, inv=1.0f/tanhf(k);
                xl=tanhf(xl*k)*inv; xr=tanhf(xr*k)*inv;
            }
            if (I->model==MODEL_LADDER){
                ladder_set(&I->ladderL,cut,res_raw); ladder_set(&I->ladderR,cut,res_raw);
                xl=tanhf((float)ladder_process(&I->ladderL,xl)*1.6f);
                xr=tanhf((float)ladder_process(&I->ladderR,xr)*1.6f);
            } else {
                svf_set(&I->fL,cut,res,(svf_mode_t)I->mode);
                svf_set(&I->fR,cut,res,(svf_mode_t)I->mode);
                float kk=2.0f-2.0f*res, gr=powf(kk*0.5f,0.25f);
                xl=(float)svf_process(&I->fL,xl)*gr;
                xr=(float)svf_process(&I->fR,xr)*gr;
            }
        }

        /* ---- volume target ---- */
        if (do_vol){
            float g = pnp_gain(vol, m);
            if (I->band_on){
                double lo,hi;
                pnp_split_L(&I->split, xl, &lo, &hi); xl=(float)(lo*g + hi);
                pnp_split_R(&I->split, xr, &lo, &hi); xr=(float)(lo*g + hi);
            } else {
                xl*=g; xr*=g;
            }
        }

        xl*=og; xr*=og;
        audio[i*2]   = (int16_t)(soft_limit(xl)*32767.0f);
        audio[i*2+1] = (int16_t)(soft_limit(xr)*32767.0f);
    }

    I->clk.beat_pos = beat;             /* persist phase across blocks */
    pnp_clock_advance_block(&I->clk, frames);
}

/* ---- params ---- */
static void pnp_set_param(void *inst, const char *key, const char *val){
    pnp_t *I=(pnp_t*)inst; if(!I) return;
    float v=atof(val);
    if (!strcmp(key,"curve")){ I->curve=curve_from_string(val); I->shp.curve=I->curve; }
    else if (!strcmp(key,"length")) I->length=idx_from_string(val,LEN_NAMES,LEN_COUNT,1);
    else if (!strcmp(key,"volume")){ I->volume=clampf(v,-1.0f,1.0f); smooth_target(&I->sm_vol,I->volume); }
    else if (!strcmp(key,"slope")){  I->slope=clampf(v,0.0f,1.0f); I->shp.slope=I->slope; }
    else if (!strcmp(key,"shift")){  I->shift=clampf(v,0.0f,1.0f); I->shp.shift=I->shift; }
    else if (!strcmp(key,"attack")){ I->attack=clampf(v,0.0f,1.0f); I->shp.attack=I->attack; }
    else if (!strcmp(key,"model"))   I->model=idx_from_string(val,MODEL_NAMES,MODEL_COUNT,MODEL_SVF);
    else if (!strcmp(key,"mode"))    I->mode=mode_from_string(val);
    else if (!strcmp(key,"cutoff")){ I->cutoff=clampf(v,0.0f,1.0f); smooth_target(&I->sm_cut,I->cutoff); }
    else if (!strcmp(key,"resonance")){ I->resonance=clampf(v,0.0f,1.0f); smooth_target(&I->sm_res,I->resonance); }
    else if (!strcmp(key,"filter")){ I->filter=clampf(v,-1.0f,1.0f); smooth_target(&I->sm_filt,I->filter); }
    else if (!strcmp(key,"drive")){  I->drive=clampf(v,0.0f,1.0f); smooth_target(&I->sm_drive,I->drive); }
    else if (!strcmp(key,"band_on")) I->band_on=(!strcasecmp(val,"On")||atoi(val)!=0)?1:0;
    else if (!strcmp(key,"band_freq")){ I->band_freq=clampf(v,20.0f,5120.0f); pnp_split_set_fc(&I->split,SAMPLE_RATE,I->band_freq); }
    else if (!strcmp(key,"output")){ I->output_db=clampf(v,-24.0f,12.0f); smooth_target(&I->sm_out,db_to_lin(I->output_db)); }
    else if (!strcmp(key,"state")){
        float f; char s[40];
        if (!json_str(val,"curve",s,sizeof(s))){   I->curve=curve_from_string(s); I->shp.curve=I->curve; }
        if (!json_str(val,"length",s,sizeof(s)))   I->length=idx_from_string(s,LEN_NAMES,LEN_COUNT,1);
        if (!json_num(val,"volume",&f)){ I->volume=clampf(f,-1.0f,1.0f); smooth_reset(&I->sm_vol,I->volume); }
        if (!json_num(val,"slope",&f)){ I->slope=clampf(f,0.0f,1.0f); I->shp.slope=I->slope; }
        if (!json_num(val,"shift",&f)){ I->shift=clampf(f,0.0f,1.0f); I->shp.shift=I->shift; }
        if (!json_num(val,"attack",&f)){ I->attack=clampf(f,0.0f,1.0f); I->shp.attack=I->attack; }
        if (!json_str(val,"model",s,sizeof(s)))    I->model=idx_from_string(s,MODEL_NAMES,MODEL_COUNT,MODEL_SVF);
        if (!json_str(val,"mode",s,sizeof(s)))     I->mode=mode_from_string(s);
        if (!json_num(val,"cutoff",&f)){ I->cutoff=clampf(f,0.0f,1.0f); smooth_reset(&I->sm_cut,I->cutoff); }
        if (!json_num(val,"resonance",&f)){ I->resonance=clampf(f,0.0f,1.0f); smooth_reset(&I->sm_res,I->resonance); }
        if (!json_num(val,"filter",&f)){ I->filter=clampf(f,-1.0f,1.0f); smooth_reset(&I->sm_filt,I->filter); }
        if (!json_num(val,"drive",&f)){ I->drive=clampf(f,0.0f,1.0f); smooth_reset(&I->sm_drive,I->drive); }
        if (!json_num(val,"band_on",&f)) I->band_on=(f!=0.0f)?1:0;
        if (!json_num(val,"band_freq",&f)){ I->band_freq=clampf(f,20.0f,5120.0f); pnp_split_set_fc(&I->split,SAMPLE_RATE,I->band_freq); }
        if (!json_num(val,"output",&f)){ I->output_db=clampf(f,-24.0f,12.0f); smooth_reset(&I->sm_out,db_to_lin(I->output_db)); }
    }
}

static int pnp_get_param(void *inst, const char *key, char *buf, int n){
    pnp_t *I=(pnp_t*)inst; if(!I) return -1;
    if(!strcmp(key,"curve"))     return snprintf(buf,n,"%s",PNP_CURVE_NAMES[I->curve]);
    if(!strcmp(key,"length"))    return snprintf(buf,n,"%s",LEN_NAMES[I->length]);
    if(!strcmp(key,"volume"))    return snprintf(buf,n,"%.2f",I->volume);
    if(!strcmp(key,"slope"))     return snprintf(buf,n,"%.2f",I->slope);
    if(!strcmp(key,"shift"))     return snprintf(buf,n,"%.2f",I->shift);
    if(!strcmp(key,"attack"))    return snprintf(buf,n,"%.2f",I->attack);
    if(!strcmp(key,"model"))     return snprintf(buf,n,"%s",MODEL_NAMES[I->model]);
    if(!strcmp(key,"mode"))      return snprintf(buf,n,"%s",mode_to_string(I->mode));
    if(!strcmp(key,"cutoff"))    return snprintf(buf,n,"%.3f",I->cutoff);
    if(!strcmp(key,"resonance")) return snprintf(buf,n,"%.2f",I->resonance);
    if(!strcmp(key,"filter"))    return snprintf(buf,n,"%.2f",I->filter);
    if(!strcmp(key,"drive"))     return snprintf(buf,n,"%.2f",I->drive);
    if(!strcmp(key,"band_on"))   return snprintf(buf,n,"%s",I->band_on?"On":"Off");
    if(!strcmp(key,"band_freq")) return I->band_on ? snprintf(buf,n,"%.0f",I->band_freq)
                                                   : snprintf(buf,n,"--");
    if(!strcmp(key,"output"))    return snprintf(buf,n,"%.1f",I->output_db);
    if(!strcmp(key,"name"))      return snprintf(buf,n,"PushNPull");
    if(!strcmp(key,"_phase")){
        double bpc = LEN_BEATS[I->length];
        double cp  = I->clk.beat_pos / bpc; cp -= floor(cp);
        return snprintf(buf,n,"%.4f", cp);
    }
    if(!strcmp(key,"_curve")){            /* whole curve, 128 comma-sep samples (no ':' in key) */
        int off=0; const int N=128;
        for(int i=0;i<N && off < n-12;i++){
            double p=(double)i/(double)(N-1);
            off += snprintf(buf+off, n-off, "%s%.3f", i?",":"", shaper_eval(&I->shp,p));
        }
        return off;
    }
    if(!strcmp(key,"state")){
        return snprintf(buf,n,
          "{\"curve\":\"%s\",\"length\":\"%s\",\"volume\":%.2f,"
          "\"slope\":%.2f,\"shift\":%.2f,\"attack\":%.2f,\"model\":\"%s\",\"mode\":\"%s\","
          "\"cutoff\":%.3f,\"resonance\":%.2f,\"filter\":%.2f,\"drive\":%.2f,"
          "\"band_on\":%d,\"band_freq\":%.0f,\"output\":%.1f}",
          PNP_CURVE_NAMES[I->curve],LEN_NAMES[I->length],I->volume,
          I->slope,I->shift,I->attack,MODEL_NAMES[I->model],mode_to_string(I->mode),
          I->cutoff,I->resonance,I->filter,I->drive,I->band_on,I->band_freq,I->output_db);
    }
    if(!strcmp(key,"ui_hierarchy")){
        const char *h = "{\"modes\":null,\"levels\":{"
          "\"root\":{\"children\":null,"
            "\"knobs\":[\"curve\",\"length\",\"volume\",\"filter\",\"slope\",\"shift\",\"band_freq\",\"output\"],"
            "\"params\":[{\"key\":\"view\",\"label\":\"Visual Edit\"},"
              "\"curve\",\"length\",\"volume\",\"filter\",\"slope\",\"shift\",\"attack\","
              "\"band_on\",\"band_freq\",\"output\","
              "{\"level\":\"filtset\",\"label\":\"Filter\"}]},"
          "\"filtset\":{\"label\":\"Filter\",\"children\":null,"
            "\"knobs\":[\"cutoff\",\"resonance\",\"drive\",\"mode\",\"model\"],"
            "\"params\":[\"model\",\"mode\",\"cutoff\",\"resonance\",\"drive\"]}}}";
        int len=strlen(h); if(len<n){ strcpy(buf,h); return len; } return -1;
    }
    if(!strcmp(key,"chain_params")){
        const char *p = "["
          "{\"key\":\"curve\",\"name\":\"Curve\",\"type\":\"enum\",\"options\":[\"Sidechain 1\",\"Sidechain 2\",\"Punch\",\"Sub Bass\",\"Gate\",\"Reverse\",\"Pulse\",\"Push\",\"Trim\",\"Swell\",\"Stutter\",\"Pump\"],\"default\":\"Sidechain 1\"},"
          "{\"key\":\"length\",\"name\":\"Length\",\"type\":\"enum\",\"options\":[\"1/8\",\"1/4\",\"1/2\",\"1/1\"],\"default\":\"1/4\"},"
          "{\"key\":\"volume\",\"name\":\"Volume\",\"type\":\"float\",\"min\":-1,\"max\":1,\"default\":-1,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"slope\",\"name\":\"Slope\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.55,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"shift\",\"name\":\"Shift\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"model\",\"name\":\"Model\",\"type\":\"enum\",\"options\":[\"SVF\",\"Schwoog\"],\"default\":\"SVF\"},"
          "{\"key\":\"mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"LP\",\"HP\",\"BP\",\"Notch\",\"Peak\",\"AP\"],\"default\":\"LP\"},"
          "{\"key\":\"cutoff\",\"name\":\"Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"resonance\",\"name\":\"Resonance\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.2,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"filter\",\"name\":\"Filter\",\"type\":\"float\",\"min\":-1,\"max\":1,\"default\":0,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0,\"step\":0.02,\"unit\":\"%\"},"
          "{\"key\":\"band_on\",\"name\":\"Band Split\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":\"Off\"},"
          "{\"key\":\"band_freq\",\"name\":\"Band Freq\",\"type\":\"float\",\"min\":20,\"max\":5120,\"default\":320,\"step\":10,\"unit\":\"Hz\"},"
          "{\"key\":\"output\",\"name\":\"Output\",\"type\":\"float\",\"min\":-24,\"max\":12,\"default\":0,\"step\":0.5,\"unit\":\"dB\"},"
          "{\"key\":\"view\",\"name\":\"Visual Edit\",\"type\":\"canvas\",\"canvas_script\":\"canvas.js#pushnpull_canvas\",\"show_footer\":false,\"show_value\":false}"
          "]";
        int len=strlen(p); if(len<n){ strcpy(buf,p); return len; } return -1;
    }
    return -1;
}

/* ---- entry point ---- */
static audio_fx_api_v2_t g_api;
audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host){
    g_host = host;
    memset(&g_api,0,sizeof(g_api));
    g_api.api_version     = AUDIO_FX_API_VERSION_2;
    g_api.create_instance = pnp_create;
    g_api.destroy_instance= pnp_destroy;
    g_api.process_block   = pnp_process;
    g_api.set_param       = pnp_set_param;
    g_api.get_param       = pnp_get_param;
    g_api.on_midi         = pnp_on_midi;
    pnp_log("PushNPull v2 initialized");
    return &g_api;
}
