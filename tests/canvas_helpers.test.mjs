// Node checks for canvas.js. Run: make -C tests dump_contract && node tests/canvas_helpers.test.mjs
import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
// canvas.js is a plain script that assigns globalThis.canvas_overlay; eval it here.
(0, eval)(readFileSync(join(here, "..", "src", "canvas.js"), "utf8"));
const ov = globalThis.canvas_overlay, T = ov._test;

let failures = 0;
function ok(cond, msg) { if (!cond) { failures++; console.error(`FAIL ${msg}`); } else console.log(`ok   ${msg}`); }
function eq(actual, expected, msg) {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  ok(a === e, a === e ? msg : `${msg}: got ${a}, want ${e}`);
}

// ---- the surfaces the host looks for
eq(ov.widgetKind, "custom:pnpcurve", "the widget registers as custom:pnpcurve");
eq(typeof ov.drawCell, "function", "canvas_overlay.drawCell exists");
eq(typeof ov.draw, "undefined", "no fullscreen draw: the old Visual Edit dive is gone");

// ---- the JS curve is the DSP's curve. Samples come from the C shaper itself.
const samples = JSON.parse(execFileSync(join(here, "dump_contract"), ["curves"], { encoding: "utf8", maxBuffer: 1 << 24 }));
let worst = 0, n = 0;
for (const s of samples) s.m.forEach((m, i) => {
  const d = Math.abs(T.pnpShaperEval(s.cv, (i + 0.37) / 64, s.slope, s.shift, s.attack) - m);
  if (d > worst) worst = d; n++;
});
eq(samples.length, 12 * 3 * 2 * 2, "C samples cover 12 curves x slope x shift x attack");
ok(worst < 1e-4, `canvas.js matches dsp/shaper.c over ${n} points (worst ${worst.toExponential(1)})`);
eq([T.NAMES.length, T.DEF_SLOPE.length, T.POLARITY.length], [12, 12, 12], "12 names, default slopes and polarities");

// ---- polarity table is honest: a curve never leaves the half it is drawn in
for (let cv = 0; cv < 12; cv++) {
  let lo = 0, hi = 0;
  for (const A of [0.05, 0.5, 1]) for (let i = 0; i < 400; i++) {
    const m = T.pnpCurveEval(cv, i / 400, A); if (m < lo) lo = m; if (m > hi) hi = m;
  }
  const pol = T.POLARITY[cv];
  ok(pol < 0 ? hi < 1e-9 : pol > 0 ? lo > -1e-9 : (lo < -0.1 && hi > 0.1), `${T.NAMES[cv]} polarity ${pol}`);
}

// ---- values
eq(T.pnpCurveIndex("Punch"), 2, "curve by label");
eq(T.pnpCurveIndex("11"), 11, "curve by index");
eq([T.pnpCurveIndex(null), T.pnpCurveIndex(""), T.pnpCurveIndex("Nope"), T.pnpCurveIndex(12)], [-1, -1, -1, -1], "no usable curve -> -1");
eq(T.pnpShape({}), null, "no curve value -> no shape");
eq(T.pnpShape({ curve: "Gate" }), { cv: 4, slope: 0.12, shift: 0.5, attack: 0 }, "missing siblings fall back to defaults");
eq(T.pnpShape({ curve: "Gate", slope: "0.30", shift: 0.7, attack: "0.1" }), { cv: 4, slope: 0.3, shift: 0.7, attack: 0.1 }, "string and number values");

// ---- columns: full depth survives a narrow slope; nothing leaves the box
for (const [w, h] of [[128, 15], [96, 15]]) {
  const c = T.pnpColumns({ cv: 0, slope: 0.02, shift: 0.5, attack: 0 }, w, h);
  eq(Math.max(...c.bot), h - 1, `narrow dip still reaches the bottom at ${w}x${h}`);
  ok(c.top.every((y, x) => y >= 0 && y <= c.bot[x] && c.bot[x] <= h - 1), `columns stay inside ${w}x${h}`);
}
eq(T.pnpColumns({ cv: 11, slope: 0.8, shift: 0.5, attack: 0 }, 128, 15).base, 7, "a two-way curve rests on the middle row");
eq(T.pnpColumns({ cv: 7, slope: 0.5, shift: 0.5, attack: 0 }, 128, 15).base, 14, "a rising curve rests on the bottom row");

// ---- playhead
eq(T.pnpParsePhase("0.2500 0.0020000 1"), { phase: 0.25, rate: 0.002, active: true }, "parse phase/rate/active");
eq(T.pnpParsePhase("0.4"), { phase: 0.4, rate: 0, active: true }, "a bare number is a phase");
eq([T.pnpParsePhase(null), T.pnpParsePhase(""), T.pnpParsePhase("x")], [null, null, null], "no answer -> null");
const near = (a, b) => Math.abs(a - b) < 1e-9;
{
  const st = {};
  eq(T.pnpPlayhead(st, null, 0), -1, "no value -> no playhead");
  eq(T.pnpPlayhead(st, "0.5 0.002 0", 0), -1, "inactive clock -> no playhead");
  ok(near(T.pnpPlayhead(st, "0.1 0.002 1", 1000), 0.1), "first sample anchors");
  ok(near(T.pnpPlayhead(st, "0.1 0.002 1", 1100), 0.3), "runs on at the reported rate between samples");
  ok(near(T.pnpPlayhead(st, "0.1 0.002 1", 1500), 0.1), "wraps at the end of the cycle");
  ok(near(T.pnpPlayhead(st, "0.34 0.002 1", 1600), 0.32), "a small disagreement is split, not jumped to");
  ok(near(T.pnpPlayhead(st, "0.9 0.002 1", 1700), 0.9), "a large one re-anchors");
  eq(T.pnpPlayhead(st, "0.9 0.002 1", 1700 + T.STALE_MS + 1), -1, "no fresh sample for a while -> no playhead");
  eq(T.pnpPlayhead(st, "0.2 0.002 0", 9000), -1, "clock stops -> playhead goes");
}
{
  const st = {};
  T.pnpPlayhead(st, "0.98 0.002 1", 0);
  ok(near(T.pnpPlayhead(st, "0.02 0.002 1", 10), 0.01), "correction works across the wrap");
}

// ---- drawing: runs through a recording ctx; nothing outside the frame, no reads
function recorder(w, h) {
  const calls = [];
  return { width: w, height: h, calls,
    fillRect(x, y, rw, rh, c) { calls.push([x, y, rw, rh, c]); },
    print(x, y, t) { calls.push(["print", x, y, t]); }, textWidth: (t) => t.length * 5 };
}
const inside = (r) => r.calls.every((c) => c[0] === "print" || (c[0] >= 0 && c[1] >= 0 && c[0] + c[2] <= r.width && c[1] + c[3] <= r.height && c[2] > 0 && c[3] > 0));
for (const name of T.NAMES) {
  const r = recorder(128, 15);
  ov.drawCell(r, { values: { curve: name, slope: 0.3, shift: 0.9, attack: 0.2, ui_phase: "0.9999 0 1" }, nowMs: 5 });
  ok(r.calls.length > 0 && inside(r), `${name}: widget draws inside 128x15 (${r.calls.length} rects)`);
}
{
  const r = recorder(128, 15);
  ov.drawCell(r, { values: { slope: 0.3 }, nowMs: 5 });
  eq(r.calls.length, 0, "widget draws nothing when the curve did not answer");
}

console.log(failures ? `\n${failures} failure(s)` : "\nall checks passed");
process.exit(failures ? 1 : 0);
