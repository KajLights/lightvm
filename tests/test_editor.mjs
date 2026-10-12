import test from "node:test";
import assert from "node:assert/strict";
import { spawn, execFileSync } from "node:child_process";
import { existsSync, readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join as pathJoin } from "node:path";
import { fileURLToPath } from "node:url";
import { crc32, frame, FrameDecoder, LightVMClient, COMMAND } from "../web/protocol.mjs";

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
test("browser protocol uploads and plays a real animation on wasm3", { skip: !existsSync(receiver) || !existsSync(python) }, async () => {
  const directory = mkdtempSync(pathJoin(tmpdir(), "lightvm-editor-"));
  const program = pathJoin(directory, "rainbow.wasm");
  execFileSync(python, ["-m", "lightvm.compiler", "samples/python/rainbow.py", "-o", program], { cwd: root });
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
    ], targets: { A: { device: "lights" } } }));
    await client.command(12, Buffer.from(JSON.stringify({ kind: "config", size: config.length, crc32: crc32(config) })));
    const chunk = Buffer.alloc(4 + config.length); config.copy(chunk, 4);
    await client.command(13, chunk); await client.command(14);
    await client.manage("config_validate");
    await client.manage("config_apply", { expected_revision: description.active_revision });
    const progress = [];
    await client.upload(readFileSync(program), description.module_limit, value => progress.push(value));
    assert.equal(progress.at(-1), 1);
    assert.equal((await client.command(COMMAND.PLAY)).value, 5);
    assert.equal((await client.status()).mode, "wasm");
    const snapshot = await client.manage("program_snapshot");
    assert.equal(snapshot.pixels.length, 15);
    await client.command(COMMAND.STOP);
    assert.equal((await client.status()).mode, "idle");
  } finally { await client.close(); process.kill(); rmSync(directory, { recursive: true }); }
});
