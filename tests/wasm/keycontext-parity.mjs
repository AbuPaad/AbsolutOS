import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

import {
  NUMOS_KEY_CONTEXTS,
  NUMOS_KEY_CONTEXT_SLUGS,
} from "../../wasm/numos-keycontext.js";
import { NUMOS_LOGICAL_KEYS } from "../../wasm/numos-keypad.js";

/**
 * Proves the C++ context table and the GENERATED JavaScript table are identical.
 *
 * wasm/numos-keycontext.js is generated from src/ui/KeyContext.h so the web pad
 * and the on-device soft-key bar cannot disagree about which keys mean something
 * in which app. "Cannot disagree" is only true if something checks, and the
 * failure it prevents is invisible in a screenshot: a key that looks dead on the
 * web while the firmware still routes it.
 *
 * Both sides are compared in the FIRMWARE's key-code space (not logical ids), so
 * this also catches an id the web catalog cannot send — a role the pad could
 * never act on.
 *
 * Needs g++ on PATH (the same bare-host requirement as tests/host/*.cpp).
 */

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

const cppDump = execFileSync(
  "bash",
  ["-c",
    `cd ${JSON.stringify(repo)} && ` +
    // A scratch path outside the tree: this test must not leave a binary behind.
    'out=$(mktemp -t numos_key_context_dump.XXXXXX) && ' +
    "g++ -std=gnu++17 -I src tests/host/key_context_test.cpp -o \"$out\" && " +
    '"$out" --dump; rc=$?; rm -f "$out"; exit $rc'],
  { encoding: "utf8" },
).trim().split("\n");

/** slug -> ordered [{ code, role }] as the firmware sees it. */
const firmware = new Map();
for (const line of cppDump) {
  const separator = line.indexOf(":");
  assert.ok(separator > 0, `unparsable firmware context line: ${line}`);
  const slug = line.slice(0, separator);
  const rest = line.slice(separator + 1);
  firmware.set(slug, rest
    ? rest.split(";").map((pair) => {
      const [code, role] = pair.split("=");
      return { code: Number(code), role };
    })
    : []);
}

assert.equal(firmware.size, NUMOS_KEY_CONTEXT_SLUGS.length,
  "the firmware and the generated module must list the same contexts");

const codeById = new Map(NUMOS_LOGICAL_KEYS.map((key) => [key.id, key.code]));

for (const slug of NUMOS_KEY_CONTEXT_SLUGS) {
  assert.ok(firmware.has(slug),
    `context "${slug}" is in the generated module but not in the firmware table`);
  const generated = NUMOS_KEY_CONTEXTS[slug].map((entry) => {
    const code = codeById.get(entry.id);
    assert.notEqual(code, undefined,
      `${slug}:${entry.id} has no code in the web logical-key catalog, so the ` +
      "pad cannot send it — add the key to wasm/numos-keypad.js");
    return { code, role: entry.role };
  });
  assert.deepEqual(generated, firmware.get(slug),
    `${slug}: the generated web table and src/ui/KeyContext.h disagree`);
}

// The generated file also mirrors defaultRole() from the header; a stale copy
// would silently change which keys are alive in EVERY context.
const defaults = execFileSync(
  "bash",
  ["-c",
    `cd ${JSON.stringify(repo)} && ` +
    "python3 scripts/gen_key_context.py --check"],
  { encoding: "utf8" },
);

console.log(`NumOS key contexts: ${NUMOS_KEY_CONTEXT_SLUGS.length} contexts ` +
  `match src/ui/KeyContext.h in the firmware key-code space`);
console.log(defaults.trim());
