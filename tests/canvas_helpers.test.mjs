// One-shot node check for canvas.js pure helpers. Run: node tests/canvas_helpers.test.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const src = readFileSync(join(here, "..", "src", "canvas.js"), "utf8");
// canvas.js is a plain script that assigns globalThis.pushnpull_canvas; eval it here.
(0, eval)(src);
const T = globalThis.pushnpull_canvas._test;

let failures = 0;
function eq(actual, expected, msg) {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a !== e) { failures++; console.error(`FAIL ${msg}: got ${a}, want ${e}`); }
  else { console.log(`ok   ${msg}`); }
}

// dirFromCC: 1..63 -> +1, 65..127 -> -1, 0 and 64 -> 0
eq(T.dirFromCC(1), 1, "dirFromCC cw min");
eq(T.dirFromCC(63), 1, "dirFromCC cw max");
eq(T.dirFromCC(65), -1, "dirFromCC ccw min");
eq(T.dirFromCC(127), -1, "dirFromCC ccw max");
eq(T.dirFromCC(0), 0, "dirFromCC zero");
eq(T.dirFromCC(64), 0, "dirFromCC sixtyfour");

// accumStep: fires (and resets) when |accum| reaches sens; reversal resets first.
eq(T.accumStep(0, 1, 2), { accum: 1, fire: false }, "accum first tick no fire");
eq(T.accumStep(1, 1, 2), { accum: 0, fire: true }, "accum second tick fires+resets");
eq(T.accumStep(1, -1, 2), { accum: -1, fire: false }, "accum reversal resets then counts");
eq(T.accumStep(0, 1, 1), { accum: 0, fire: true }, "accum sens1 fires every detent");

// cycleEnum: wraps within [0, count)
eq(T.cycleEnum(0, -1, 8), 7, "cycleEnum wraps below to last");
eq(T.cycleEnum(7, 1, 8), 0, "cycleEnum wraps above to first");
eq(T.cycleEnum(3, 1, 8), 4, "cycleEnum normal up");

// clampNum
eq(T.clampNum(2, -1, 1), 1, "clampNum high");
eq(T.clampNum(-2, -1, 1), -1, "clampNum low");
eq(T.clampNum(0.5, -1, 1), 0.5, "clampNum mid");

// LAYOUT order: knobs 1-4 = modulator, 5-8 = targets/output.
eq(T.LAYOUT.map(p => p.key),
   ["curve","length","slope","shift","attack","cutoff","filter","volume"],
   "LAYOUT knob order (top: modulator, bottom: attack/tone/targets)");
eq(T.LAYOUT.map(p => p.abbr),
   ["Curv","Lngth","Slope","Shift","Attak","Freq","Filtr","Volum"],
   "LAYOUT title-case labels");

const byKey = {}; T.LAYOUT.forEach(p => byKey[p.key] = p);

// pnpNextWrite: enum cycles by name, floats step+clamp, returns null at limit.
eq(T.pnpNextWrite(byKey.curve, "Punch", 1), "Sub Bass", "next curve up");
eq(T.pnpNextWrite(byKey.curve, "Sidechain 1", -1), "Pump", "next curve wraps down to last");
eq(T.pnpNextWrite(byKey.volume, "-1.00", 1), "-0.99", "next volume up from floor (1% step)");
eq(T.pnpNextWrite(byKey.volume, "-1.00", -1), null, "next volume at floor -> null");
eq(T.pnpNextWrite(byKey.volume, "1.00", 1), null, "next volume at ceil -> null");
eq(T.pnpNextWrite(byKey.slope, "0.54", 1), "0.55", "next slope up (1% step)");
eq(T.pnpNextWrite(byKey.cutoff, "0.50", 1), "0.51", "next cutoff up (1% step)");
eq(T.pnpNextWrite(byKey.attack, "0.00", 1), "0.01", "next attack up (1% step)");

// pnpCellText: compact cell value per param.
eq(T.pnpCellText(byKey.curve, "Sidechain 1"), "SC1", "cell curve abbrev");
eq(T.pnpCellText(byKey.length, "1/4"), "1/4", "cell length raw");
eq(T.pnpCellText(byKey.volume, "-1.00"), "-100%", "cell volume duck");
eq(T.pnpCellText(byKey.volume, "0.80"), "+80%", "cell volume pump");
eq(T.pnpCellText(byKey.volume, "0.00"), "Off", "cell volume 0% -> Off");
eq(T.pnpCellText(byKey.filter, "0.00"), "Off", "cell filter 0% -> Off");
eq(T.pnpCellText(byKey.volume, "0.004"), "Off", "cell volume rounds to 0 -> Off");
eq(T.pnpCellText(byKey.volume, "0.01"), "+1%", "cell volume 1% stays numeric");
eq(T.pnpCellText(byKey.slope, "0.55"), "55%", "cell slope pct");
eq(T.pnpCellText(byKey.cutoff, "0.500"), "50%", "cell cutoff (FREQ) pct");
eq(T.pnpCellText(byKey.attack, "0.20"), "20%", "cell attack pct");

// New curves mirror the DSP (curves.h). Cross-check key behaviors.
eq(T.NAMES.length, 12, "12 curves");
eq(T.ABBR.length, 12, "12 curve abbrevs");
const ci = n => T.NAMES.indexOf(n);
const near = (a, b) => Math.abs(a - b) < 0.05;
eq(T.pnpCurveEval(ci("Trim"), 0, 0.18) < -0.95, true, "Trim full duck at p=0");
eq(near(T.pnpCurveEval(ci("Swell"), 0, 0.70), 0), true, "Swell rests at p=0");
eq(T.pnpCurveEval(ci("Swell"), 0.35, 0.70) > 0.9, true, "Swell boosts mid-window (positive)");
eq(T.pnpCurveEval(ci("Stutter"), 0.225, 0.90) < -0.9, true, "Stutter re-ducks at subdivision");
eq(T.pnpCurveEval(ci("Pump"), 0, 0.80) < -0.9, true, "Pump ducks at p=0");
eq(T.pnpCurveEval(ci("Pump"), 0.40, 0.80) > 0.2, true, "Pump boosts mid-window (bipolar)");
// Attack (5th arg) eases the onset in, like shaper.c.
const sc1 = ci("Sidechain 1");
eq(T.pnpShaperEval(sc1, 0, 0.55, 0.5, 0) < -0.95, true, "attack=0: full duck at onset");
eq(near(T.pnpShaperEval(sc1, 0, 0.55, 0.5, 0.25), 0), true, "attack softens onset to ~rest");
eq(Math.abs(T.pnpShaperEval(sc1, 0.05, 0.55, 0.5, 0.25)) <
   Math.abs(T.pnpCurveEval(sc1, 0.05, 0.55)), true, "attack: eased-in during ramp");
// depth preserved: scan the cycle, trough still reaches ~full -1
let attMin = 0; for (let i = 0; i <= 400; i++) { const m = T.pnpShaperEval(sc1, i/400, 0.55, 0.5, 0.25); if (m < attMin) attMin = m; }
eq(attMin < -0.98, true, "attack preserves full dip depth");

// MCU font must cover every glyph the UI can render (abbrevs, curve abbrevs,
// length names, and value chars + and % and -).
const needed = new Set();
for (const p of T.LAYOUT) for (const ch of p.abbr) needed.add(ch);
for (const a of T.ABBR) for (const ch of a) needed.add(ch);
for (const l of ["1/8","1/4","1/2","1/1","Off"]) for (const ch of l) needed.add(ch);
for (const ch of "0123456789+-.%") needed.add(ch);
const missing = [...needed].filter(ch => !T.MCUFONT[ch]);
eq(missing, [], "MCU font covers all rendered glyphs");

if (failures) { console.error(`\n${failures} failure(s)`); process.exit(1); }
console.log("\nall canvas helper tests passed");
