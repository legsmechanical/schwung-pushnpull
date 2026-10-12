/* PushNPull curve graphic for the knob grid.
 *
 * canvas_overlay.drawCell is the "custom:pnpcurve" widget. It spans the four
 * shaping knobs (Curve, Slope, Shift, Attack) as one strip on the Curve page
 * and draws one cycle of the curve with a beat-locked playhead.
 *
 * It reads no parameters: it is handed the page's values. The playhead
 * comes from "ui_phase" ("<phase> <phase per ms> <active>"), which arrives a
 * few times a second and is extrapolated between samples from the frame clock.
 *
 * Curve SHAPE is computed here, mirroring dsp/curves.h and dsp/shaper.c. */

/* ---- curve math (mirror of src/dsp/curves.h) ---- */
var PNP_CURVE_NAMES = ["Sidechain 1","Sidechain 2","Punch","Sub Bass","Gate","Reverse","Pulse","Push",
                       "Trim","Swell","Stutter","Pump"];
var PNP_DEF_SLOPE   = [0.55,0.65,0.40,0.85,0.12,0.40,0.50,0.55,0.18,0.70,0.90,0.80];
/* Which way each curve moves from rest: -1 dips, +1 rises, 0 does both. The
 * picture puts the resting line at the top, bottom or middle accordingly, so a
 * one-sided curve gets the whole height. */
var PNP_CURVE_POLARITY = [-1,-1,-1,-1,-1,-1,-1,+1,-1,+1,-1,0];

function pnpSmoothstep(t){ if(t<=0)return 0; if(t>=1)return 1; return t*t*(3-2*t); }

function pnpCurveEval(cv, p, A){
  if(A<0.02)A=0.02; if(A>1)A=1;
  if(p<0)p=0; if(p>=1)p-=1;
  switch(cv){
    case 0: if(p>=A)return 0; return -(1-pnpSmoothstep(p/A));               /* Sidechain 1 */
    case 1: if(p>=A)return 0; return -Math.sin(Math.PI*(p/A));              /* Sidechain 2 */
    case 2: { if(p>=A)return 0; var x=1-p/A; return -(x*x); }               /* Punch */
    case 3: if(p>=A)return 0; return -(1-p/A);                              /* Sub Bass */
    case 4: { var e=0.05*A; if(p>=A)return 0; if(p<e)return -(p/e);         /* Gate */
              if(p>A-e)return -((A-p)/e); return -1; }
    case 5: { var st=1-A; if(p<st)return 0; var x2=(p-st)/A;                /* Reverse */
              return -pnpSmoothstep(x2); }
    case 6: { var h=(p<0.5)?0:0.5; var lp=(p-h)*2; if(lp>=A)return 0;       /* Pulse */
              return -(1-pnpSmoothstep(lp/A)); }
    case 7: if(p>=A)return 0; return +(1-pnpSmoothstep(p/A));               /* Push */
    case 8: { if(p>=A)return 0; var xt=1-p/A; return -(xt*xt*xt); }         /* Trim */
    case 9: { if(p>=A)return 0; var xs=p/A; return 0.5*(1-Math.cos(2*Math.PI*xs)); } /* Swell */
    case 10:{ if(p>=A)return 0; var ce=(p/A)*4; var lo=ce-Math.floor(ce);   /* Stutter */
              return -(1-pnpSmoothstep(lo)); }
    case 11:{ if(p>=A)return 0; var xp=p/A; return -Math.cos(2*Math.PI*xp)*(1-xp); } /* Pump */
    default: return 0;
  }
}

function pnpShaperEval(cv, p, A, shift, attack){
  var off = shift - 0.5;
  var pp = p - off;
  pp -= Math.floor(pp);
  if (attack && attack > 1e-4){               /* attack stage (mirror shaper.c): */
    var a = attack > 1 ? 1 : attack;
    var onset = pnpCurveEval(cv, 0, A);        /* full onset value (depth preserved) */
    if (pp < a) return onset * pnpSmoothstep(pp / a);  /* ramp rest -> onset */
    return pnpCurveEval(cv, pp - a, A);        /* curve, delayed by attack */
  }
  return pnpCurveEval(cv, pp, A);
}

/* ---- values ---- */

/* An enum may arrive as its label or as its index. -1 = no usable answer. */
function pnpCurveIndex(raw){
  if (raw === null || raw === undefined || raw === "") return -1;
  var i = PNP_CURVE_NAMES.indexOf(String(raw));
  if (i >= 0) return i;
  var n = Number(raw);
  if (isFinite(n) && n >= 0 && n < PNP_CURVE_NAMES.length && Math.floor(n) === n) return n;
  return -1;
}

function pnpNum(raw, dflt){
  if (raw === null || raw === undefined || raw === "") return dflt;
  var v = Number(raw);
  return isFinite(v) ? v : dflt;
}

/* The four shaping values off a page's value map, or null when the curve
 * itself did not answer: a read that did not answer must not become a picture. */
function pnpShape(values){
  if (!values) return null;
  var cv = pnpCurveIndex(values.curve);
  if (cv < 0) return null;
  return { cv: cv,
           slope:  pnpNum(values.slope,  PNP_DEF_SLOPE[cv]),
           shift:  pnpNum(values.shift,  0.5),
           attack: pnpNum(values.attack, 0) };
}

/* ---- the curve as pixel columns ---- */

/* One [top, bottom] row pair per column over one cycle. Each column is
 * supersampled and reaches into the next one's first sample, so a narrow
 * trough keeps its depth and an instant edge leaves no gap. */
function pnpColumns(shape, w, h){
  var pol = PNP_CURVE_POLARITY[shape.cv];
  var base = pol < 0 ? 0 : (pol > 0 ? h - 1 : (h - 1) >> 1);
  var amp  = pol === 0 ? (h - 1) >> 1 : h - 1;
  var SS = 4, top = new Array(w), bot = new Array(w);
  for (var x = 0; x < w; x++){
    var mmin = 2, mmax = -2;
    for (var k = 0; k <= SS; k++){
      var pp = (x + k / SS) / w;
      if (pp > 0.9999) pp = 0.9999;
      var m = pnpShaperEval(shape.cv, pp, shape.slope, shape.shift, shape.attack);
      if (m < mmin) mmin = m;
      if (m > mmax) mmax = m;
    }
    var yT = Math.round(base - mmax * amp), yB = Math.round(base - mmin * amp);
    if (yT < 0) yT = 0; if (yT > h - 1) yT = h - 1;
    if (yB < 0) yB = 0; if (yB > h - 1) yB = h - 1;
    top[x] = yT; bot[x] = yB;
  }
  return { top: top, bot: bot, base: base };
}

/* Columns are rebuilt only when what they depend on changes; a frame is then
 * a handful of rects. */
function pnpCachedColumns(cache, shape, w, h){
  var sig = shape.cv + "|" + shape.slope + "|" + shape.shift + "|" + shape.attack + "|" + w + "|" + h;
  if (cache.sig !== sig){ cache.sig = sig; cache.cols = pnpColumns(shape, w, h); }
  return cache.cols;
}

/* ---- playhead ---- */

function pnpWrap01(p){ p -= Math.floor(p); return p; }

/* "<phase> <phase per ms> <active>"; a bare number is a phase with no rate. */
function pnpParsePhase(raw){
  if (raw === null || raw === undefined || raw === "") return null;
  var parts = String(raw).trim().split(/\s+/);
  var ph = Number(parts[0]);
  if (!isFinite(ph)) return null;
  var rate = parts.length > 1 ? Number(parts[1]) : 0;
  var active = parts.length > 2 ? Number(parts[2]) !== 0 : true;
  return { phase: pnpWrap01(ph), rate: isFinite(rate) ? rate : 0, active: active };
}

var PNP_PHASE_STALE_MS = 2500;   /* no fresh sample for this long -> no playhead */

/* Phase to draw now, or -1 for none. `st` persists between frames. A fresh
 * sample re-anchors the estimate; in between it runs on at the reported rate.
 * A small disagreement is split rather than jumped to, so the line glides. */
function pnpPlayhead(st, raw, nowMs){
  if (raw !== st.raw){
    st.raw = raw;
    var s = pnpParsePhase(raw);
    if (!s || !s.active){ st.on = false; return -1; }
    var target = s.phase;
    if (st.on){
      var pred = pnpWrap01(st.p0 + (nowMs - st.t0) * st.rate);
      var err = target - pred;
      if (err > 0.5) err -= 1; else if (err < -0.5) err += 1;
      if (Math.abs(err) < 0.2) target = pnpWrap01(pred + err * 0.5);
    }
    st.on = true; st.p0 = target; st.t0 = nowMs; st.rate = s.rate; st.seen = nowMs;
  }
  if (!st.on) return -1;
  if (nowMs - st.seen > PNP_PHASE_STALE_MS) return -1;
  return pnpWrap01(st.p0 + (nowMs - st.t0) * st.rate);
}

/* ---- drawing ---- */

function pnpDrawCurve(ctx, x0, y0, w, h, cols, phase){
  var x, xs;
  for (x = 0; x < w; x += 4) ctx.fillRect(x0 + x, y0 + cols.base, 1, 1, 1);   /* resting level */
  for (xs = 0, x = 1; x <= w; x++){
    if (x < w && cols.top[x] === cols.top[xs] && cols.bot[x] === cols.bot[xs]) continue;
    ctx.fillRect(x0 + xs, y0 + cols.top[xs], x - xs, cols.bot[xs] - cols.top[xs] + 1, 1);
    xs = x;
  }
  if (phase >= 0){
    var px = Math.floor(phase * w);
    if (px > w - 1) px = w - 1;
    for (var y = 0; y < h; y += 2) ctx.fillRect(x0 + px, y0 + y, 1, 1, 1);
  }
}

var pnpCellCache = {}, pnpCellPhase = {};

globalThis.canvas_overlay = {
  widgetKind: "custom:pnpcurve",

  drawCell: function(ctx, o){
    var values = o && o.values;
    var shape = pnpShape(values);
    if (!shape) return;
    var w = ctx.width, h = ctx.height;
    var now = (o && typeof o.nowMs === "number") ? o.nowMs : Date.now();
    var cols = pnpCachedColumns(pnpCellCache, shape, w, h);
    pnpDrawCurve(ctx, 0, 0, w, h, cols, pnpPlayhead(pnpCellPhase, values.ui_phase, now));
  },

  _test: {
    pnpCurveEval: pnpCurveEval, pnpShaperEval: pnpShaperEval, pnpCurveIndex: pnpCurveIndex,
    pnpShape: pnpShape, pnpColumns: pnpColumns, pnpParsePhase: pnpParsePhase,
    pnpPlayhead: pnpPlayhead, NAMES: PNP_CURVE_NAMES, DEF_SLOPE: PNP_DEF_SLOPE,
    POLARITY: PNP_CURVE_POLARITY, STALE_MS: PNP_PHASE_STALE_MS
  }
};
