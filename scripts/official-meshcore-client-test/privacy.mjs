const SAFE_NUMERIC_FIELDS = new Set([
  "batteryMilliVolts",
  "channelCount",
  "contactCount",
  "count",
  "epochSecs",
  "level",
  "messageCount",
  "millivolts",
]);

const SAFE_BOOLEAN_FIELDS = new Set(["connected", "ok", "success"]);
const SAFE_VERSION_FIELDS = new Set(["firmwareVersion", "firmwareVer", "version"]);
const SAFE_VERSION_VALUE = /^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$/;

export function sanitize(data) {
  if (data == null) return {};
  if (Array.isArray(data)) return { count: data.length };
  if (typeof data === "number" && Number.isFinite(data)) return { value: data };
  if (typeof data === "boolean") return { value: data };
  if (typeof data !== "object") return {};

  const out = {};
  for (const [key, value] of Object.entries(data)) {
    if (SAFE_NUMERIC_FIELDS.has(key) && typeof value === "number" && Number.isFinite(value)) {
      out[key] = value;
    } else if (SAFE_BOOLEAN_FIELDS.has(key) && typeof value === "boolean") {
      out[key] = value;
    } else if (
      SAFE_VERSION_FIELDS.has(key) &&
      typeof value === "string" &&
      SAFE_VERSION_VALUE.test(value)
    ) {
      out[key] = value;
    }
  }
  return out;
}

export function summarize(data) {
  const safe = sanitize(data);
  if (Object.keys(safe).length === 0) return data == null ? "" : "response received";
  return Object.entries(safe)
    .map(([key, value]) => `${key}=${value}`)
    .join(" ");
}

export function redactError(error) {
  const message = String(error?.message || error || "").toLowerCase();
  if (message.includes("timeout")) return "operation timed out";
  if (
    message.includes("connect") ||
    message.includes("serial") ||
    message.includes("open") ||
    message.includes("device")
  ) {
    return "transport unavailable";
  }
  return "operation failed";
}
