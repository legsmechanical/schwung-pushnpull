#!/usr/bin/env node
/* help.json must be the shape the host reads (top-level `children`), ASCII,
 * and every line must fit the screen: a help line is drawn, never wrapped. */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
const repo = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const { createFramebuffer, drawContext } = await import(pathToFileURL(
    path.resolve(repo, process.env.SCHWUNG_SRC || "../schwung-current", "tools/param-pages/harness.mjs")).href);
const ctx = drawContext(createFramebuffer());
const help = JSON.parse(fs.readFileSync(path.join(repo, "src/help.json"), "utf8"));
let failures = 0, lines = 0;
if (!Array.isArray(help.children) || !help.children.length) { console.error("FAIL no top-level children"); failures++; }
(function walk(nodes, trail) {
    for (const n of nodes || []) {
        const here = trail + "/" + n.title;
        if (n.children) walk(n.children, here);
        for (const l of n.lines || []) {
            lines++;
            if (/[^\x20-\x7e]/.test(l)) { console.error(`FAIL non-ASCII in ${here}: "${l}"`); failures++; }
            const w = 4 + ctx.textWidth(l);
            if (w > 128) { console.error(`FAIL ${w}px wide in ${here}: "${l}"`); failures++; }
        }
    }
})(help.children, "");
console.log(failures ? `${failures} failure(s)` : `help.json: ${lines} lines, all fit`);
process.exit(failures ? 1 : 0);
