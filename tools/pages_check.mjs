#!/usr/bin/env node
/* Runs the contract the plugin SERVES through a host's own planner, validator
 * and viz resolver, and writes a fixture for the host's preview tool.
 *
 *   make -C tests dump_contract && node tools/pages_check.mjs
 *   SCHWUNG_SRC=../dbxhost node tools/pages_check.mjs      # the other host
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..");
const hostSrc = path.resolve(repo, process.env.SCHWUNG_SRC || "../schwung-current");
const lib = (f) => import(pathToFileURL(path.join(hostSrc, "src/shared/param_pages", f)).href);

let failures = 0;
const bad = (m) => { failures++; console.error("FAIL " + m); };
const eq = (a, e, m) => { if (JSON.stringify(a) !== JSON.stringify(e)) bad(`${m}: got ${JSON.stringify(a)}, want ${JSON.stringify(e)}`); else console.log("ok   " + m); };

const served = JSON.parse(execFileSync(path.join(repo, "tests/dump_contract"), ["contract"], { encoding: "utf8" }));
const hierarchy = served.ui_hierarchy, chainParams = served.chain_params;
const mod = JSON.parse(fs.readFileSync(path.join(repo, "src/module.json"), "utf8"));
console.log(`host library: ${hostSrc}`);

/* Saved state is keyed by these; a rename silently drops a user's setting. */
const STATE_KEYS = ["curve","length","volume","slope","shift","attack","model","mode","cutoff",
                    "resonance","filter","drive","band_on","band_freq","output"];
const declared = chainParams.map((p) => p.key);
eq(STATE_KEYS.filter((k) => !declared.includes(k)), [], "every saved-state key is still declared");
eq(declared.filter((k) => !STATE_KEYS.includes(k)), ["ui_phase"], "the only extra key is the read-only playhead");
if (!/^\d+$/.test(served.band_freq)) bad(`band_freq reads back "${served.band_freq}", want a number`); else console.log("ok   band_freq reads back a number with the split off");
if (!/^[\d.]+ [\d.]+ [01]$/.test(served.ui_phase)) bad(`ui_phase reads back "${served.ui_phase}"`); else console.log("ok   ui_phase is \"<phase> <rate> <active>\"");

const { validateContract } = await lib("validate_contract.mjs");
const { planPages, PAGE_KNOBS } = await lib("page_plan.mjs");
const { buildMetaIndex } = await lib("param_meta.mjs");
const { resolveViz } = await lib("viz.mjs");
const { registerOverlayWidgets, clearWidgets } = await lib("widget_registry.mjs");

const { findings } = validateContract({ id: mod.id, hierarchy, chainParams,
    capabilities: { ...mod.capabilities, canvas_script: "canvas.js" } });
for (const f of findings) {
    /* "" in a knobs array is a deliberate blank slot; it names no param. */
    if (f.rule === "undeclared-knob-params" && /:[\s,]*$/.test(f.message)) continue;
    const line = `[${f.level}] ${f.rule}: ${f.message}`;
    if (f.level === "error" || /^viz-declared-/.test(f.rule)) bad(line); else console.log("  " + line);
}

const { pages } = planPages({ hierarchy, chainParams });
for (const p of pages) console.log(`    ${String(p.level ?? "-").padEnd(8)} ${p.kind} "${p.name}"  ${(p.keys || []).map((k) => k || "-").join(" ")}`);
eq(pages.map((p) => [p.kind, p.name]), [[PAGE_KNOBS, "Curve"], [PAGE_KNOBS, "Filter"], [PAGE_KNOBS, "Output"]], "three knob pages: Curve, Filter, Output");
eq(pages.map((p) => p.keys), [
    ["length","volume","filter","","curve","slope","shift","attack"],
    ["model","drive","","","cutoff","resonance","mode",""],
    ["band_on","band_freq","output",""]], "knob slots");

(0, eval)(fs.readFileSync(path.join(repo, "src/canvas.js"), "utf8"));
clearWidgets();
registerOverlayWidgets(globalThis.canvas_overlay);
const metaIndex = buildMetaIndex({ hierarchy, chainParams });
const groupsOf = (keys) => resolveViz({ keys, metaIndex }).groups.map((g) => [g.kind, g.slotStart, g.slotSpan]);
eq(groupsOf(pages[0].keys), [["custom:pnpcurve", 4, 4]], "Curve page: the curve picture spans the bottom row");
eq(groupsOf(pages[1].keys), [["filter", 4, 3]], "Filter page: the response spans cutoff/resonance/mode");
eq(groupsOf(pages[2].keys), [["switch", 0, 1], ["fader", 2, 1]], "Output page: a switch and a fader");

/* Schwoog has no mode: Mode hides, and nothing visible may move. */
const ladder = planPages({ hierarchy, chainParams, visible: (c) => !(c && c.param === "model") }).pages[1];
eq(ladder.keys.slice(0, 6), ["model","drive","","","cutoff","resonance"], "Filter page with Mode hidden keeps every other slot");
eq(groupsOf(ladder.keys), [["filter", 4, 2]], "…and still draws the response");

/* An older host has no custom widget: the four knobs must fall back to plain cells. */
clearWidgets();
eq(groupsOf(pages[0].keys), [], "without the widget the Curve page is four ordinary cells");

fs.mkdirSync(path.join(repo, "dist/tests"), { recursive: true });
fs.writeFileSync(path.join(repo, "dist/tests/pushnpull-fixture.json"), JSON.stringify({
    _source: "schwung-pushnpull tools/pages_check.mjs — the served contract",
    generated_at: new Date().toISOString(), module_count: 1, not_captured: [],
    modules: [{ id: mod.id, category: "audio_fx", component_key: "fx1", status: "ok",
                name: mod.name, version: mod.version,
                ui_hierarchy: hierarchy, chain_params: chainParams, presets: null }],
}, null, 1));
console.log(failures ? `\n${failures} failure(s)` : "\nall checks passed");
process.exit(failures ? 1 : 0);
