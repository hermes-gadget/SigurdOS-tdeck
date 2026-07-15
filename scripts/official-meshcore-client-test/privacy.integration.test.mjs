import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import test from "node:test";
import { fileURLToPath } from "node:url";

test("matrix stdout and stderr exclude the device port and label", () => {
  const port = "/dev/ttyUSB-identity-42";
  const label = "Alice TDeck";
  const result = spawnSync(
    process.execPath,
    ["matrix.mjs", "--port", port, "--label", label, "--timeout", "10"],
    { cwd: fileURLToPath(new URL(".", import.meta.url)), encoding: "utf8", timeout: 10_000 },
  );

  assert.equal(result.status, 2, result.stderr);
  const output = `${result.stdout}\n${result.stderr}`;
  assert.equal(output.includes(port), false);
  assert.equal(output.includes(label), false);

  const payload = JSON.parse(result.stdout);
  assert.deepEqual(payload.results[0].data, {});
  assert.equal(payload.results[0].detail, "operation timed out");
});
