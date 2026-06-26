/* PushNPull on-device curve editor. type:"canvas" overlay.
 * Draws the selected curve over one cycle (Slope/Shift applied) with a live
 * playhead, framed by two rows of editable params in a tiny 5x5 MCU font so the
 * graph stays roomy:
 *   - TOP row  (knobs 1-4 / CC71-74): Curve, Length, Slope, Shift   (the modulator)
 *   - BOT row  (knobs 5-8 / CC75-78): Attack, Cutoff(FREQ), Filter, Volume
 *   - jog (CC14): cycle Curve live; the active knob's cell inverts.
 *
 * Loaded by the host as type:"canvas" param "view" (canvas.js#pushnpull_canvas).
 *
 * Curve SHAPE is computed HERE in JS (mirroring dsp/curves.h) from the real
 * params curve/slope/shift, which route through ctx.getParam/setParam exactly
 * like any declared chain param. All drawing is setPixel/fillRect (the built-in
 * ctx.print/drawLine are avoided: print font is too large, drawLine may no-op). */

/* ---- curve math (mirror of src/dsp/curves.h) ---- */
var PNP_CURVE_NAMES = ["Sidechain 1","Sidechain 2","Punch","Sub Bass","Gate","Reverse","Pulse","Push",
                       "Trim","Swell","Stutter","Pump"];
var PNP_CURVE_ABBR  = ["SC1","SC2","Pun","Sub","Gat","Rev","Pul","Psh","Trm","Swl","Stu","Pmp"];
var PNP_DEF_SLOPE   = [0.55,0.65,0.40,0.85,0.12,0.40,0.50,0.55,0.18,0.70,0.90,0.80];
var PNP_LEN_NAMES   = ["1/8","1/4","1/2","1/1"];

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

/* ---- mcufont 5x5 (source: schwung-davebox/assets/fonts/mcufont.h via
 * ui_constants.mjs MCUFONT). Each glyph: 5 rows, bits 4-0 MSB-first, 6px step.
 * '%' added here (not in the original set). Missing glyphs render as blank. */
var MCUFONT = {
  'A':[0b01110,0b10001,0b11111,0b10001,0b10001], 'B':[0b11110,0b10001,0b11110,0b10001,0b11110],
  'C':[0b01111,0b10000,0b10000,0b10000,0b01111], 'D':[0b11110,0b10001,0b10001,0b10001,0b11110],
  'E':[0b11111,0b10000,0b11100,0b10000,0b11111], 'F':[0b11111,0b10000,0b11100,0b10000,0b10000],
  'G':[0b01111,0b10000,0b10011,0b10001,0b01111], 'H':[0b10001,0b10001,0b11111,0b10001,0b10001],
  'I':[0b11111,0b00100,0b00100,0b00100,0b11111], 'J':[0b11111,0b00010,0b00010,0b10010,0b01100],
  'K':[0b10010,0b10100,0b11000,0b10100,0b10010], 'L':[0b10000,0b10000,0b10000,0b10000,0b11111],
  'M':[0b11111,0b10101,0b10101,0b10001,0b10001], 'N':[0b10001,0b11001,0b10101,0b10011,0b10001],
  'O':[0b01110,0b10001,0b10001,0b10001,0b01110], 'P':[0b11110,0b10001,0b11110,0b10000,0b10000],
  'Q':[0b01110,0b10001,0b10001,0b10010,0b01101], 'R':[0b11110,0b10001,0b11110,0b10010,0b10001],
  'S':[0b01111,0b10000,0b01110,0b00001,0b11110], 'T':[0b11111,0b00100,0b00100,0b00100,0b00100],
  'U':[0b10001,0b10001,0b10001,0b10001,0b01110], 'V':[0b10001,0b10001,0b01010,0b01010,0b00100],
  'W':[0b10001,0b10001,0b10101,0b10101,0b11011], 'X':[0b10001,0b01010,0b00100,0b01010,0b10001],
  'Y':[0b10001,0b01010,0b00100,0b00100,0b00100], 'Z':[0b11111,0b00010,0b00100,0b01000,0b11111],
  'a':[0b00000,0b01111,0b10001,0b10001,0b01111], 'b':[0b10000,0b11110,0b10001,0b10001,0b11110],
  'c':[0b00000,0b01111,0b10000,0b10000,0b01111], 'd':[0b00001,0b01111,0b10001,0b10001,0b01111],
  'e':[0b00000,0b01110,0b11111,0b10000,0b01111], 'f':[0b00000,0b01111,0b10000,0b11110,0b10000],
  'g':[0b00000,0b01110,0b11111,0b00001,0b11110], 'h':[0b10000,0b10000,0b11110,0b10001,0b10001],
  'i':[0b00100,0b00000,0b01100,0b00100,0b01110], 'j':[0b00010,0b00000,0b00010,0b10010,0b01100],
  'k':[0b10000,0b10000,0b10110,0b11000,0b10110], 'l':[0b00000,0b10000,0b10000,0b10000,0b01111],
  'm':[0b00000,0b11110,0b10101,0b10101,0b10001], 'n':[0b00000,0b11110,0b10001,0b10001,0b10001],
  'o':[0b00000,0b01110,0b10001,0b10001,0b01110], 'p':[0b00000,0b11110,0b10001,0b11110,0b10000],
  'q':[0b00000,0b01111,0b10001,0b01111,0b00001], 'r':[0b00000,0b01110,0b10000,0b10000,0b10000],
  's':[0b00000,0b01110,0b11000,0b00110,0b11100], 't':[0b00000,0b11111,0b00100,0b00100,0b00100],
  'u':[0b00000,0b10001,0b10001,0b10001,0b01110], 'v':[0b00000,0b10001,0b10001,0b01010,0b00100],
  'w':[0b00000,0b10001,0b10101,0b10101,0b01110], 'x':[0b00000,0b10010,0b01100,0b01100,0b10010],
  'y':[0b00000,0b10010,0b01110,0b00010,0b01100], 'z':[0b00000,0b11110,0b00100,0b01000,0b11110],
  '0':[0b01110,0b10001,0b10101,0b10001,0b01110], '1':[0b01100,0b10100,0b00100,0b00100,0b11111],
  '2':[0b01110,0b10001,0b00110,0b01000,0b11111], '3':[0b11111,0b00001,0b01110,0b00001,0b11110],
  '4':[0b10010,0b10010,0b11111,0b00010,0b00010], '5':[0b11111,0b10000,0b01110,0b00001,0b11110],
  '6':[0b01110,0b10000,0b11110,0b10001,0b01110], '7':[0b11111,0b00010,0b00100,0b01000,0b01000],
  '8':[0b01110,0b10001,0b01110,0b10001,0b01110], '9':[0b11111,0b10001,0b11111,0b00001,0b00001],
  '-':[0b00000,0b00000,0b01110,0b00000,0b00000], '+':[0b00000,0b00100,0b01110,0b00100,0b00000],
  '.':[0b00000,0b00000,0b00000,0b00000,0b01000], ',':[0b00000,0b00000,0b00000,0b00100,0b01000],
  '?':[0b01110,0b10001,0b00110,0b00000,0b00100], '!':[0b00100,0b00100,0b00100,0b00000,0b00100],
  ':':[0b00000,0b01000,0b00000,0b01000,0b00000], '=':[0b00000,0b01110,0b00000,0b01110,0b00000],
  "'":[0b00100,0b00100,0b00000,0b00000,0b00000], '#':[0b01010,0b11111,0b01010,0b11111,0b01010],
  '/':[0b00001,0b00010,0b00100,0b01000,0b10000], '(':[0b00110,0b01000,0b01000,0b01000,0b00110],
  ')':[0b01100,0b00010,0b00010,0b00010,0b01100], '<':[0b00010,0b00100,0b01000,0b00100,0b00010],
  '>':[0b01000,0b00100,0b00010,0b00100,0b01000], '%':[0b10001,0b00010,0b00100,0b01000,0b10001]
};

function mcuPrint(ctx, x, y, text, color){
  for (var ci = 0; ci < text.length; ci++){
    var g = MCUFONT[text[ci]];
    if (!g) continue;
    for (var row = 0; row < 5; row++){
      var bits = g[row];
      for (var col = 0; col < 5; col++)
        if (bits & (1 << (4 - col))) ctx.setPixel(x + ci * 6 + col, y + row, color);
    }
  }
}
function mcuPrintC(ctx, cx, y, text, color){
  mcuPrint(ctx, cx - Math.floor((text.length * 6 - 1) / 2), y, text, color);
}

/* ---- pure input helpers (unit-tested off-device) ---- */

/* Relative-encoder sign: d2 1..63 = CW (+1), 65..127 = CCW (-1), else 0. */
function dirFromCC(d2){
  if (d2 >= 1 && d2 <= 63) return 1;
  if (d2 >= 65 && d2 <= 127) return -1;
  return 0;
}

/* Encoder accumulator: count detents until |accum| reaches sens, then fire+reset.
 * A direction reversal discards the pending accumulation first (davebox feel). */
function accumStep(accum, dir, sens){
  if ((accum > 0 && dir < 0) || (accum < 0 && dir > 0)) accum = 0;
  accum += dir;
  if (Math.abs(accum) >= sens) return { accum: 0, fire: true };
  return { accum: accum, fire: false };
}

/* Wrap an enum index by dir into [0, count). */
function cycleEnum(idx, dir, count){ return ((idx + dir) % count + count) % count; }

/* Clamp a float into [min, max]. */
function clampNum(v, lo, hi){ return v < lo ? lo : (v > hi ? hi : v); }

/* ---- knob layout: index (CC-71) -> param ----
 * Two cohesive rows (uppercase labels). Top = the modulator (curve shape +
 * timing); bottom = the onset/tone/target controls. Output stays menu-only.
 * Editing types: enum -> cycle OPTIONS (wrap); pct/pctbip -> float step+clamp.
 * disp drives the compact on-screen value. sens=detents/step; dec=decimals. */
var PNP_LAYOUT = [
  { key:"curve",  abbr:"CURV",  type:"enum",   opts:PNP_CURVE_NAMES, sens:6,                              disp:"curve"  },
  { key:"length", abbr:"LNGTH", type:"enum",   opts:PNP_LEN_NAMES,   sens:6,                              disp:"raw"    },
  { key:"slope",  abbr:"SLOPE", type:"pct",    min:0,  max:1, step:0.01, dec:2, sens:3, fb:0.55,          disp:"pct"    },
  { key:"shift",  abbr:"SHIFT", type:"pct",    min:0,  max:1, step:0.01, dec:2, sens:3, fb:0.5,           disp:"pct"    },
  { key:"attack", abbr:"ATTAK", type:"pct",    min:0,  max:1, step:0.01, dec:2, sens:3, fb:0,             disp:"pct"    },
  { key:"cutoff", abbr:"FREQ",  type:"pct",    min:0,  max:1, step:0.01, dec:2, sens:3, fb:0.5,           disp:"pct"    },
  { key:"filter", abbr:"FILTR", type:"pctbip", min:-1, max:1, step:0.01, dec:2, sens:3, fb:0,  zeroOff:1, disp:"pctbip" },
  { key:"volume", abbr:"VOLUM", type:"pctbip", min:-1, max:1, step:0.01, dec:2, sens:3, fb:-1, zeroOff:1, disp:"pctbip" }
];

/* Compact cell value text from a raw getParam string. */
function pnpCellText(pm, raw){
  if (raw === null || raw === undefined || raw === "") return "-";
  if (pm.disp === "raw") return raw;
  if (pm.disp === "curve"){ var i = PNP_CURVE_NAMES.indexOf(raw); return i >= 0 ? PNP_CURVE_ABBR[i] : raw; }
  if (pm.disp === "freq") return (raw === "--") ? "--" : raw;
  var v = parseFloat(raw);
  if (isNaN(v)) return "-";
  if (pm.zeroOff && Math.round(v * 100) === 0) return "Off";   /* 0% target = inert */
  if (pm.disp === "pct")    return Math.round(v * 100) + "%";
  if (pm.disp === "pctbip") return (v > 0 ? "+" : "") + Math.round(v * 100) + "%";
  if (pm.disp === "db")     return (v >= 0 ? "+" : "") + v.toFixed(1);
  return raw;
}

/* Next value string to write, or null if no change. dir = +/-1. */
function pnpNextWrite(pm, raw, dir){
  if (pm.type === "enum"){
    var idx = pm.opts.indexOf(raw);
    if (idx < 0) idx = 0;
    return pm.opts[cycleEnum(idx, dir, pm.opts.length)];
  }
  var cur = parseFloat(raw);
  if (isNaN(cur)) cur = pm.fb;               /* e.g. band_freq reads "--" when split off */
  var nv = clampNum(cur + dir * pm.step, pm.min, pm.max);
  if (nv === cur) return null;
  return (pm.dec === 0) ? String(Math.round(nv)) : nv.toFixed(pm.dec);
}

function pnpNum(ctx, key, dflt){
  var raw = ctx.getParam(key);
  if (raw === null || raw === undefined || raw === "") return dflt;
  var v = parseFloat(raw);
  return isNaN(v) ? dflt : v;
}

function pnpState(ctx){
  var s = ctx.state;
  if (!s.init){ s.init = true; s.accum = [0,0,0,0,0,0,0,0]; s.active = -1; }
  return s;
}

/* ---- layout geometry (128x64) ----
 * TOP cell rows 0..10, curve band 12..50 (mid 31), BOT cell rows 52..62. */
var PNP_BAND_TOP = 12, PNP_BAND_MID = 31, PNP_BAND_AMP = 18;
var PNP_CELL_W = 32, PNP_CELL_H = 11;
function pnpCellTopY(k){ return k < 4 ? 0 : 52; }      /* label baseline */
function pnpCellValY(k){ return (k < 4 ? 0 : 52) + 6; } /* value baseline */
function pnpCellCX(k){ return (k % 4) * PNP_CELL_W + 16; }

globalThis.pushnpull_canvas = {
  onOpen: function(ctx){
    var s = ctx.state;
    s.init = false;            /* drop transient edit state on each open */
    pnpState(ctx);
  },

  onMidi: function(ctx, payload){
    var d = payload && payload.data;
    if (!d || d.length < 3) return;
    var s = pnpState(ctx);
    var status = d[0] & 0xF0;

    /* capacitive knob touch: notes 0-7 = knob 1-8. Highlight that cell. */
    if (status === 0x90 || status === 0x80){
      var note = d[1];
      if (note <= 7) s.active = (status === 0x90 && d[2] >= 64) ? note : -1;
      return;
    }

    if (status !== 0xB0) return;             /* CC only below */
    var cc = d[1], val = d[2];

    if (cc === 14){                          /* jog -> cycle Curve live */
      var jd = dirFromCC(val);
      if (jd){
        s.active = 0;
        var nw = pnpNextWrite(PNP_LAYOUT[0], ctx.getParam("curve"), jd);
        if (nw !== null) ctx.setParam("curve", nw);
      }
      return;
    }

    if (cc >= 71 && cc <= 78){               /* encoder -> edit mapped param */
      var k = cc - 71;
      var pm = PNP_LAYOUT[k];
      if (!pm) return;
      var dir = dirFromCC(val);
      if (!dir) return;
      s.active = k;
      var res = accumStep(s.accum[k], dir, pm.sens);
      s.accum[k] = res.accum;
      if (!res.fire) return;
      var w = pnpNextWrite(pm, ctx.getParam(pm.key), dir);
      if (w !== null) ctx.setParam(pm.key, w);   /* one write per onMidi (no race) */
    }
  },

  draw: function (ctx){
    var W = ctx.width, H = ctx.height;
    ctx.clear();
    var s = pnpState(ctx);

    /* curve band: dotted baseline (resting/unity) at the band centre */
    for (var bx = 0; bx < W; bx += 4) ctx.setPixel(bx, PNP_BAND_MID, 1);

    /* resolve curve selection + shaping from real (routed) params */
    var name = ctx.getParam("curve");
    var cv = PNP_CURVE_NAMES.indexOf(name);
    if (cv < 0) cv = 0;
    var slope = pnpNum(ctx, "slope", PNP_DEF_SLOPE[cv]);
    var shift = pnpNum(ctx, "shift", 0.5);
    var attack = pnpNum(ctx, "attack", 0);

    /* Supersample each column and draw its min..max envelope, so a narrow
     * (small-Slope) trough/peak still renders at full depth instead of being
     * skipped between pixel samples. Depth never changes with Slope — only the
     * width does — so this keeps the viz honest to the audio. */
    var lo = PNP_BAND_TOP, hi = PNP_BAND_MID + PNP_BAND_AMP;   /* 12..49 */
    var SS = 6;
    for (var x = 0; x < W; x++){
      var mmin = 2, mmax = -2;
      for (var k = 0; k < SS; k++){
        var pp = (x + k / SS) / (W - 1);
        if (pp > 1) pp = 1;
        var mm = pnpShaperEval(cv, pp, slope, shift, attack);  /* [-1,1] */
        if (mm < mmin) mmin = mm;
        if (mm > mmax) mmax = mm;
      }
      var yTop = Math.round(PNP_BAND_MID - mmax * PNP_BAND_AMP);  /* +1 -> highest */
      var yBot = Math.round(PNP_BAND_MID - mmin * PNP_BAND_AMP);  /* -1 -> lowest  */
      if (yTop < lo) yTop = lo; if (yTop > hi) yTop = hi;
      if (yBot < lo) yBot = lo; if (yBot > hi) yBot = hi;
      for (var yy = yTop; yy <= yBot; yy++) ctx.setPixel(x, yy, 1);
    }

    /* playhead (dotted vertical, band only) — phase via synthetic key if routed */
    var phRaw = ctx.getParam("_phase");
    if (phRaw !== null && phRaw !== undefined && phRaw !== ""){
      var ph = parseFloat(phRaw);
      if (!isNaN(ph)){
        if (ph < 0) ph = 0; if (ph > 1) ph = 1;
        var px = Math.round(ph * (W - 1));
        for (var py = lo; py <= hi; py += 3) ctx.setPixel(px, py, 1);
      }
    }

    /* two param rows in MCU font; active cell inverts (white box, black text) */
    for (var k = 0; k < PNP_LAYOUT.length; k++){
      var pm = PNP_LAYOUT[k];
      var cellX = (k % 4) * PNP_CELL_W;
      var active = (k === s.active);
      if (active) ctx.fillRect(cellX, pnpCellTopY(k), PNP_CELL_W - 1, PNP_CELL_H, 1);
      var color = active ? 0 : 1;
      var cx = pnpCellCX(k);
      mcuPrintC(ctx, cx, pnpCellTopY(k), pm.abbr, color);
      mcuPrintC(ctx, cx, pnpCellValY(k), pnpCellText(pm, ctx.getParam(pm.key)), color);
    }
  },

  _test: {
    dirFromCC: dirFromCC, accumStep: accumStep, cycleEnum: cycleEnum,
    clampNum: clampNum, pnpNextWrite: pnpNextWrite, pnpCellText: pnpCellText,
    pnpCurveEval: pnpCurveEval, pnpShaperEval: pnpShaperEval,
    NAMES: PNP_CURVE_NAMES, ABBR: PNP_CURVE_ABBR,
    LAYOUT: PNP_LAYOUT, MCUFONT: MCUFONT
  }
};
