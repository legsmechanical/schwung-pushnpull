#!/usr/bin/env node
/* Renders the curve widget and the curve card through a host's own
 * framebuffer, frame context and card frame, as PNGs. 1-bit art at this size
 * cannot be reviewed from its source.
 *
 *   node tools/widget_sheet.mjs            -> dist/tests/widget_sheet.png, card_sheet.png
 *   SCHWUNG_SRC=../dbxhost node tools/widget_sheet.mjs
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..");
const hostSrc = path.resolve(repo, process.env.SCHWUNG_SRC || "../schwung-current");
/* The node framebuffer only exists upstream; the frame and card code under
 * test come from whichever host SCHWUNG_SRC names. */
const harnessSrc = path.resolve(repo, "../schwung-current");
const lib = (f) => import(pathToFileURL(path.join(hostSrc, "src/shared/param_pages", f)).href);
const { frameCtx } = await lib("frame_ctx.mjs");
const { drawParamCard } = await lib("param_card.mjs");
const { createFramebuffer, drawContext } =
    await import(pathToFileURL(path.join(harnessSrc, "tools/param-pages/harness.mjs")).href);

(0, eval)(fs.readFileSync(path.join(repo, "src/canvas.js"), "utf8"));
const ov = globalThis.canvas_overlay, T = ov._test;
const scale = Number(process.env.SCALE || 4);
const outDir = path.join(repo, "dist/tests");
fs.mkdirSync(outDir, { recursive: true });
let clipped = 0;

/* Every curve at its default slope (left) and narrow + shifted + eased (right),
 * in the real 128x15 strip, with a playhead partway through. */
{
    const W = 128, H = 15, GAP = 4, N = T.NAMES.length;
    const fb = createFramebuffer(W * 2 + GAP, N * (H + GAP) + GAP);
    const ctx = drawContext(fb);
    T.NAMES.forEach((name, i) => {
        const y = GAP + i * (H + GAP);
        const variants = [
            { curve: name, slope: T.DEF_SLOPE[i], shift: 0.5, attack: 0, ui_phase: `${(i + 1) / 13} 0 1` },
            { curve: name, slope: 0.3, shift: 0.7, attack: 0.15, ui_phase: "0.5 0 0" },
        ];
        variants.forEach((values, j) =>
            ov.drawCell(frameCtx(ctx, { x: j * (W + GAP), y, w: W, h: H }),
                        { values, group: { keys: ["curve", "slope", "shift", "attack"] }, nowMs: i * 10000 + j }));
    });
    clipped += fb.clipped();
    fs.writeFileSync(path.join(outDir, "widget_sheet.png"), fb.toPng(scale));
}

/* The card as the host frames it, on a 128x64 panel. */
{
    const cases = [
        { name: "Slope",  value: "55%", raw: "0.55", values: { curve: "Sidechain 1", slope: 0.55, shift: 0.5, attack: 0, ui_phase: "0.3 0 1" } },
        { name: "Shift",  value: "70%", raw: "0.70", values: { curve: "Pump", slope: 0.8, shift: 0.7, attack: 0, ui_phase: "0.6 0 1" } },
        { name: "Attack", value: "20%", raw: "0.20", values: { curve: "Stutter", slope: 0.9, shift: 0.5, attack: 0.2, ui_phase: "0 0 0" } },
        { name: "Slope",  value: "",    raw: null,   values: { } },
    ];
    const GAP = 4, fb = createFramebuffer(128 * 2 + GAP, 64 * 2 + GAP);
    const ctx = drawContext(fb);
    cases.forEach((c, i) => {
        const frame = { x: (i % 2) * (128 + GAP), y: (i >> 1) * (64 + GAP), w: 128, h: 64 };
        drawParamCard(ctx, { meta: { card_w: 120, card_h: 44 }, frame, draw: globalThis.pnp_curve_card,
                             name: c.name, value: c.value, raw: c.raw, values: c.values, nowMs: 50000 + i,
                             onError: (e) => { console.error("card threw: " + e); process.exitCode = 1; } });
    });
    clipped += fb.clipped();
    fs.writeFileSync(path.join(outDir, "card_sheet.png"), fb.toPng(scale));
}

console.log(`wrote dist/tests/widget_sheet.png and card_sheet.png (host: ${path.basename(hostSrc)}), clipped pixels: ${clipped}`);
if (clipped) process.exitCode = 1;
