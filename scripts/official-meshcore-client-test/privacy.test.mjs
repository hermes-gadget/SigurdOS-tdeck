import assert from "node:assert/strict";
import test from "node:test";

import { redactError, sanitize, summarize } from "./privacy.mjs";

const SECRETS = [
  "Alice TDeck",
  "deadbeefcafebabe",
  "private-message-text",
  "home-network",
  "correct horse battery staple",
  "/dev/ttyUSB-identity-42",
  "192.0.2.55",
];

function assertNoSecrets(value) {
  const rendered = JSON.stringify(value);
  for (const secret of SECRETS) assert.equal(rendered.includes(secret), false, secret);
}

test("identity and credential fields are excluded by the evidence allowlist", () => {
  const input = {
    name: SECRETS[0],
    publicKey: Buffer.from(SECRETS[1]),
    privateKey: SECRETS[1],
    message: SECRETS[2],
    ssid: SECRETS[3],
    password: SECRETS[4],
    port: SECRETS[5],
    address: SECRETS[6],
    nested: { contactName: SECRETS[0], payload: SECRETS[2] },
    epochSecs: 1_752_432_000,
    batteryMilliVolts: 3_987,
    connected: true,
    version: "1.13.0",
  };

  const safe = sanitize(input);
  assert.deepEqual(safe, {
    epochSecs: 1_752_432_000,
    batteryMilliVolts: 3_987,
    connected: true,
    version: "1.13.0",
  });
  assertNoSecrets(safe);
  assertNoSecrets(summarize(input));
});

test("arrays expose only counts and primitive strings are discarded", () => {
  assert.deepEqual(sanitize([{ name: SECRETS[0] }, { message: SECRETS[2] }]), { count: 2 });
  assert.deepEqual(sanitize(SECRETS[2]), {});
  assert.equal(summarize(SECRETS[2]), "response received");
});

test("error details are reduced to non-identifying categories", () => {
  const detail = redactError(
    new Error(`failed to open ${SECRETS[5]} using password ${SECRETS[4]}`),
  );
  assert.equal(detail, "transport unavailable");
  assertNoSecrets(detail);
});
