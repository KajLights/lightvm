import test from "node:test";
import assert from "node:assert/strict";
import { spawn, execFileSync } from "node:child_process";
import { existsSync, readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join as pathJoin } from "node:path";
import { fileURLToPath } from "node:url";
import { crc32, frame, FrameDecoder, LightVMClient, COMMAND } from "../web/protocol.mjs";

test("discovery bootstraps over HTTP, validates addresses and pins the bridge IP", async () => {
  const { DeviceDiscovery } = await import("../web/discovery.mjs");
  const calls = [], payload = { schema: "lightvm.discovery/1", devices: [
    { hostname: "lightvm-123456.local", device_id: "pico-1234", board: "pico_w", ip: "192.168.1.20", port: 8080, url: "https://untrusted.example" },
    { hostname: "lightvm-abcdef.local", device_id: "esp32-abcdef", board: "xiao_esp32c6", ip: "192.168.1.21", port: 80 },
  ], truncated: true };
  const discovery = new DeviceDiscovery({ fetch: async (url, options) => { calls.push({ url, options }); return new Response(JSON.stringify(payload)); } });
  const found = await discovery.find();
  assert.equal(calls[0].url, "http://lightvm.local/api/v1/devices");
  assert.equal(calls[0].options.method, "GET"); assert.equal(calls[0].options.credentials, "omit");
  assert.equal(calls[0].options.targetAddressSpace, "local"); assert.equal(calls[0].options.redirect, "error");
  assert.equal(found.devices[0].url, "http://192.168.1.20:8080"); assert.equal(found.truncated, true);
  await discovery.find(); assert.equal(calls[1].url, "http://192.168.1.20:8080/api/v1/devices");
  assert.equal(calls.some(call => call.url.includes("sessions") || call.url.includes("command")), false);
});

test("discovery falls back from a lost bridge and drops stale lists", async () => {
  const { DeviceDiscovery, deviceList } = await import("../web/discovery.mjs");
  const calls = [], discovery = new DeviceDiscovery({ fetch: async url => {
    calls.push(url); if (!url.includes("lightvm.local")) throw new TypeError("offline");
    return new Response(JSON.stringify({ schema: "lightvm.discovery/1", devices: [] }));
  } });
  discovery.seed = "http://192.168.1.20";
  assert.deepEqual((await discovery.find()).devices, []);
  assert.deepEqual(calls, ["http://192.168.1.20/api/v1/devices", "http://lightvm.local/api/v1/devices"]);
  assert.throws(() => deviceList({ schema: "lightvm.discovery/1", devices: [{ ip: "192.168.1.20", port: 0 }] }), /invalid/);
  assert.throws(() => deviceList({ schema: "lightvm.discovery/1", devices: Array(9).fill({}) }), /valid device list/);
  const offline = new DeviceDiscovery({ fetch: async () => { throw new TypeError("offline"); } });
  offline.seed = "http://192.168.1.20";
  await assert.rejects(offline.find(), /No devices found/); assert.equal(offline.seed, "");
});

test("a stalled discovery read aborts without opening a command session", async () => {
  const { DeviceDiscovery } = await import("../web/discovery.mjs");
  let aborted = false;
  const discovery = new DeviceDiscovery({ timeout: 10, fetch: (url, { signal }) => new Promise((resolve, reject) => {
    signal.addEventListener("abort", () => { aborted = true; reject(new Error("aborted")); });
  }) });
  await assert.rejects(discovery.read("http://lightvm.local"), /aborted/); assert.equal(aborted, true);
});

test("framing agrees with Python zlib, including incremental CRC", () => {
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
  assert.equal(Buffer.from(frame(1, 42, new TextEncoder().encode("hello"))).toString("hex"),
    "5741534d01012a0005000000ef10d19768656c6c6f");
  assert.throws(() => frame(1, 1, new Uint8Array(513)), /frame limit/);
});

test("decoder handles fragmented frames, boot noise, and corrupt responses", () => {
  const messages = [], decoder = new FrameDecoder(message => messages.push(message));
  const bytes = Buffer.concat([Buffer.from("ROM boot\r\nWA"), frame(129, 1, new Uint8Array(8)), frame(131, 2, new Uint8Array([4, 5]))]);
  for (const byte of bytes) decoder.feed(new Uint8Array([byte]));
  assert.equal(messages.length, 2);
  assert.deepEqual(messages[1].payload, new Uint8Array([4, 5]));
  const corrupt = frame(129, 3, new Uint8Array(8)); corrupt[16] = 1;
  assert.throws(() => decoder.feed(corrupt), /checksum/);
});

test("a timeout closes the connection instead of accepting a late upload reply", async () => {
  let closed = false;
  const transport = { write: async () => {}, close: async () => { closed = true; } };
  const client = new LightVMClient(transport, { timeout: 20 });
  await assert.rejects(client.command(COMMAND.STATUS), /timed out/);
  assert.equal(closed, true);
  await assert.rejects(client.command(COMMAND.BEGIN), /Reconnect/);
});

const root = fileURLToPath(new URL("../", import.meta.url));
const receiver = process.env.LIGHTVM_RECEIVER || pathJoin(root, "build/host/lightvm");
const python = pathJoin(root, "build/venv/bin/python");
test("browser protocol uploads and plays a real animation on bytecode", { skip: !existsSync(receiver) || !existsSync(python) }, async () => {
  const directory = mkdtempSync(pathJoin(tmpdir(), "lightvm-editor-"));
  const program = pathJoin(directory, "rainbow.lbc");
  execFileSync(python, ["-m", "lightvm.compiler", "examples/animations/rainbow.py", "-o", program], { cwd: root });
  const process = spawn(receiver, [], { stdio: ["pipe", "pipe", "pipe"] });
  const transport = {
    write: bytes => new Promise((resolve, reject) => process.stdin.write(bytes, error => error ? reject(error) : resolve())),
    close: async () => { process.stdin.end(); },
  };
  const client = new LightVMClient(transport);
  process.stdout.on("data", bytes => transport.onData(bytes));
  process.stdout.on("end", () => transport.onDisconnect());
  process.stdin.on("error", error => transport.onDisconnect(error));
  process.on("error", error => transport.onDisconnect(error));
  try {
    const description = await client.describe();
    assert.equal(description.control_abi, 2);
    const config = Buffer.from(JSON.stringify({ schema: "lightvm.config/1", devices: [
      { id: "lights", type: "ws2812b", config: { pin: "GP16", count: 5 } },
    ] }));
    await client.command(12, Buffer.from(JSON.stringify({ kind: "config", size: config.length, crc32: crc32(config) })));
    const chunk = Buffer.alloc(4 + config.length); config.copy(chunk, 4);
    await client.command(13, chunk); await client.command(14);
    await client.manage("config_validate");
    await client.manage("config_apply", { expected_revision: description.active_revision });
    const progress = [];
    await client.upload(readFileSync(program), description.module_limit, value => progress.push(value));
    assert.equal(progress.at(-1), 1);
    assert.equal((await client.command(COMMAND.PLAY)).value, 5);
    assert.equal((await client.status()).mode, "bytecode");
    const snapshot = await client.manage("program_snapshot");
    assert.equal(snapshot.pixels.length, 15);
    await client.command(COMMAND.STOP);
    assert.equal((await client.status()).mode, "idle");
  } finally { await client.close(); process.kill(); rmSync(directory, { recursive: true }); }
});

test("HTTP transport preserves command frames and never retries uncertain mutations", async () => {
  const { HttpTransport } = await import("../web/transports.mjs");
  const calls = [];
  const transport = new HttpTransport("http://192.168.1.2", { fetch: async function(url, options) {
    assert.equal(this, globalThis);
    calls.push({ url, options });
    if (url.endsWith("/sessions")) return new Response(JSON.stringify({ session_id: "1".repeat(32), device_id: "board", boot_id: "boot" }));
    if (options.method === "DELETE") return new Response(null, { status: 204 });
    throw new TypeError("Connection lost after execution");
  } });
  await transport.open();
  await assert.rejects(transport.write(frame(COMMAND.STOP, 1)), /local-network permission/);
  assert.equal(calls.filter(call => call.url.endsWith("/command")).length, 1);
  assert.equal(calls[1].options.credentials, "omit");
  assert.equal(calls[1].options.headers["X-LightVM-Session"], "1".repeat(32));
  assert.deepEqual(calls[1].options.body, frame(COMMAND.STOP, 1));
  await transport.close();
});

test("path selection rejects other devices and boot epochs and does not fail over", async () => {
  const { Connections } = await import("../web/connections.mjs");
  const paths = new Connections(), usb = {}, http = {}, description = { device_id: "one", boot_id: "boot" };
  paths.attach("usb", usb, description);
  assert.throws(() => paths.attach("http", http, { device_id: "two", boot_id: "boot" }), /different device/);
  assert.throws(() => paths.attach("http", http, { device_id: "one", boot_id: "next" }), /rebooted/);
  paths.attach("http", http, description);
  const pinned = paths.active.client; paths.select("usb"); assert.equal(pinned, http);
  paths.detach("usb"); assert.equal(paths.active, null); assert.equal(paths.paths.get("http").client, http);
});

test("configuration save retains the revisions from its own apply operation", async () => {
  const client = new LightVMClient({ write: async () => {}, close: async () => {} });
  let reads = 0;
  client.describe = async () => { reads++; return { active_revision: 12, saved_revision: 3 }; };
  client.uploadObject = async () => {};
  const calls = [];
  client.manage = async (op, fields) => {
    calls.push({ op, fields });
    if (op === "config_apply") return { value: -2147483648 };
    if (op === "config_save") throw new Error("stale revision from another client");
    return {};
  };
  await assert.rejects(client.configure({}, { save: true }), /stale revision/);
  assert.equal(reads, 1);
  assert.deepEqual(calls.at(-1), { op: "config_save", fields: { expected_revision: 2147483648, expected_saved_revision: 3 } });
  assert.equal(calls.filter(call => call.op === "config_apply").length, 1);
});

test("revision conflicts stay connected and expose the need to refresh state", async () => {
  const transport = { write: async bytes => {
    const id = new DataView(bytes.buffer, bytes.byteOffset).getUint16(6, true);
    transport.onData(frame(131, id, new TextEncoder().encode('{"error":"stale_revision"}')));
    const status = new Uint8Array(8); new DataView(status.buffer).setUint32(0, 7, true);
    transport.onData(frame(129, id, status));
  }, close: async () => {} };
  const client = new LightVMClient(transport);
  await assert.rejects(client.manage("config_save"), error => error.code === 7 && error.conflict);
  assert.equal(client.closed, false);
});


test("parameter JSON preserves i64 boundaries and quoted numeric text", async () => {
  const { parseJSON, encodeJSON } = await import("../web/protocol.mjs");
  const source = '{"values":{"low":-9223372036854775808,"high":9223372036854775807,"small":42,"real":1.25},"label":"9223372036854775807"}';
  const parsed = parseJSON(source);
  assert.equal(parsed.values.low, -9223372036854775808n);
  assert.equal(parsed.values.high, 9223372036854775807n);
  assert.equal(parsed.values.small, 42);
  assert.equal(parsed.values.real, 1.25);
  assert.equal(parsed.label, "9223372036854775807");
  assert.equal(encodeJSON(parsed), source);
  assert.throws(() => encodeJSON({ value: Infinity }), /finite/);
});
