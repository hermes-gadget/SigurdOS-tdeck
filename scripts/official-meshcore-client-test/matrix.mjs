#!/usr/bin/env node
/**
 * Official MeshCore client interop matrix.
 *
 * Uses Liam Cottle's @liamcottle/meshcore.js — the same library that powers
 * https://app.meshcore.nz/ — over USB serial companion framing (the app's
 * non-BLE transport). This is the highest-confidence autonomous check that
 * SigurdOS CompanionBridge speaks the protocol the official app expects.
 *
 * Usage:
 *   node matrix.mjs --port /dev/ttyUSB0              # Heltec stock companion
 *   node matrix.mjs --port /dev/ttyACM0 --label tdeck # T-Deck companion_usb
 *   node matrix.mjs --port /dev/ttyACM0 --json-out /tmp/out.json
 *
 * Exit: 0 all required cases pass, 1 partial fail, 2 connect/setup fail
 */
import { NodeJSSerialConnection } from "@liamcottle/meshcore.js";
import { parseArgs } from "node:util";
import { writeFileSync } from "node:fs";
import { redactError, sanitize, summarize } from "./privacy.mjs";

const { values: args } = parseArgs({
  options: {
    port: { type: "string", default: process.env.COMPANION_PORT || "/dev/ttyUSB0" },
    label: { type: "string", default: "companion" },
    timeout: { type: "string", default: "8000" },
    "json-out": { type: "string" },
    verbose: { type: "boolean", default: false },
  },
});

const TIMEOUT_MS = Number(args.timeout) || 8000;
const results = [];

// The client package logs raw transport errors and response objects with
// console.log(). Evidence stdout must contain only the allowlisted JSON below.
console.log = () => {};

function log(...a) {
  console.error(...a);
}

function emitPayload(payload) {
  process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`);
}

function add(name, ok, detail = "", data = {}) {
  results.push({ name, ok, detail, data });
  log(`${ok ? "PASS" : "FAIL"}: ${name}${detail ? " — " + detail : ""}`);
}

function withTimeout(promise, ms, label) {
  let timer;
  const t = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`timeout after ${ms}ms: ${label}`)), ms);
  });
  return Promise.race([promise, t]).finally(() => clearTimeout(timer));
}

async function runCase(name, fn) {
  try {
    const data = await withTimeout(fn(), TIMEOUT_MS, name);
    add(name, true, summarize(data), sanitize(data));
    return data;
  } catch (e) {
    add(name, false, redactError(e));
    return null;
  }
}

function connect(port) {
  return new Promise((resolve, reject) => {
    const connection = new NodeJSSerialConnection(port);
    const timer = setTimeout(() => {
      reject(new Error(`connect timeout opening ${port}`));
      try {
        connection.close();
      } catch {
        /* ignore */
      }
    }, TIMEOUT_MS + 4000);

    connection.on("connected", () => {
      clearTimeout(timer);
      resolve(connection);
    });
    connection.on("disconnected", () => {
      if (args.verbose) log("event disconnected");
    });

    connection.connect().catch((e) => {
      clearTimeout(timer);
      reject(e);
    });
  });
}

async function main() {
  const payload = {
    client: "@liamcottle/meshcore.js",
    note: "Same library as https://app.meshcore.nz/ (USB serial transport)",
    target: "companion",
    results: [],
    summary: {},
  };

  log("Connecting official meshcore.js client");

  let connection;
  try {
    connection = await connect(args.port);
    add("connect", true, "deviceQuery ran in onConnected()");
  } catch (e) {
    add("connect", false, redactError(e));
    payload.results = results;
    payload.summary = { total: 1, passed: 0, failed: 1 };
    emitPayload(payload);
    if (args["json-out"]) writeFileSync(args["json-out"], JSON.stringify(payload, null, 2));
    process.exit(2);
  }

  try {
    await runCase("getSelfInfo_appStart", () => connection.getSelfInfo(TIMEOUT_MS));
    await runCase("syncDeviceTime", () => connection.syncDeviceTime());
    await runCase("getDeviceTime", () => connection.getDeviceTime());
    await runCase("getContacts", () => connection.getContacts());
    await runCase("getWaitingMessages", () => connection.getWaitingMessages());
    await runCase("getChannels", () => connection.getChannels());
    await runCase("getBatteryVoltage", () => connection.getBatteryVoltage());

    // Read-only export of local contact/identity advert (no private key).
    await runCase("exportLocalContact", () => connection.exportContact());
  } finally {
    try {
      await connection.close();
      add("disconnect", true);
    } catch (e) {
      add("disconnect", false, e?.message || String(e));
    }
  }

  const required = new Set([
    "connect",
    "getSelfInfo_appStart",
    "syncDeviceTime",
    "getContacts",
    "getWaitingMessages",
    "getChannels",
    "getBatteryVoltage",
  ]);

  payload.results = results;
  payload.summary = {
    total: results.length,
    passed: results.filter((r) => r.ok).length,
    failed: results.filter((r) => !r.ok).length,
  };
  emitPayload(payload);
  if (args["json-out"]) writeFileSync(args["json-out"], JSON.stringify(payload, null, 2));

  const reqFail = results.filter((r) => required.has(r.name) && !r.ok);
  if (reqFail.some((r) => r.name === "connect")) process.exit(2);
  process.exit(reqFail.length ? 1 : results.some((r) => !r.ok) ? 1 : 0);
}

main().catch((e) => {
  console.error(`Official client matrix failed: ${redactError(e)}`);
  process.exit(2);
});
