#!/usr/bin/env python3
"""Upload and execute portable bytecode programs over USB CDC, HTTP or a host pipe."""
import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
import select
import struct
import subprocess
import sys
import time
import zlib

MAGIC = b"LVM1"
HEADER = struct.Struct("<4sBBHII")
FRAME_LIMIT = 512
MODULE_LIMIT = 32768
STATUS, BEGIN, CHUNK, FINISH, RESET = 1, 2, 3, 4, 6
PLAY, STOP, FRAME = 7, 8, 9
MANAGE, OBJECT_BEGIN, OBJECT_CHUNK, OBJECT_FINISH, OBJECT_CANCEL = range(11, 16)
RESULT, PIXELS, DATA = 129, 130, 131


class ProtocolError(RuntimeError):
    pass


@dataclass
class Reply:
    code: int
    value: int
    message: str


def frame(kind, request_id, payload=b""):
    if len(payload) > FRAME_LIMIT:
        raise ValueError("payload exceeds frame limit")
    metadata = struct.pack("<BBHI", 1, kind, request_id, len(payload))
    checksum = zlib.crc32(payload, zlib.crc32(metadata))
    return MAGIC + metadata + struct.pack("<I", checksum) + payload


class ProcessTransport:
    """The host port runs the shared handler and interpreter with native pipes."""
    def __init__(self, executable, storage=None):
        argv = [str(executable)]
        if storage is not None:
            argv += ["--storage", str(storage)]
        self.process = subprocess.Popen(argv, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, bufsize=0)

    def write(self, data):
        view = memoryview(data)
        while view:
            written = self.process.stdin.write(view)
            if not written:
                raise ProtocolError("receiver closed its input")
            view = view[written:]

    def read(self, size, timeout):
        if not select.select([self.process.stdout], [], [], timeout)[0]:
            return b""
        data = self.process.stdout.read(size)
        if not data:
            raise ProtocolError("receiver closed its output")
        return data

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()


class SerialTransport:
    def __init__(self, port, no_reset=False):
        import serial
        if no_reset:
            # Set control lines before opening: ESP32 USB Serial/JTAG interprets
            # DTR/RTS transitions as reset/download requests.
            self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
            self.serial.dtr = False
            self.serial.rts = False
            self.serial.port = port
            try:
                self.serial.open()
                # A fresh connection can contain ROM boot text or a reply from
                # an interrupted request. Nothing is requested yet: drain until
                # quiet, with a bound, before parsing this connection's frames.
                deadline = time.monotonic() + 0.5
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    self.serial.timeout = min(0.1, remaining)
                    if not self.serial.read(4096):
                        break
                self.serial.timeout = 0.1
            except Exception:
                self.serial.close()
                raise
        else:
            # Preserve Pico CDC's existing DTR host-connection behavior.
            self.serial = serial.Serial(port, 115200, timeout=0.1, write_timeout=3)

    def write(self, data):
        self.serial.write(data)
        self.serial.flush()

    def read(self, size, timeout):
        self.serial.timeout = min(timeout, 0.1)
        return self.serial.read(size)

    def close(self):
        self.serial.close()


class HttpTransport:
    """One logical session across independent HTTP exchanges; never replay commands."""
    def __init__(self, url, timeout=5):
        from urllib.parse import urlsplit
        from urllib.request import build_opener, ProxyHandler
        parsed = urlsplit(url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
            raise ValueError("--url must be a output HTTP origin, e.g. http://192.168.1.20")
        self.url = url.rstrip("/")
        self.timeout = timeout
        self.opener = build_opener(ProxyHandler({}))
        self.buffer = bytearray()
        self.session = None
        self.identity = json.loads(self.exchange("/api/v1/sessions", b""))
        self.session = self.identity["session_id"]

    def exchange(self, path, data=None, method="POST"):
        from urllib.request import Request
        from urllib.error import URLError
        headers = {"Content-Type": "application/octet-stream"}
        if self.session:
            headers["X-LightVM-Session"] = self.session
        try:
            with self.opener.open(Request(self.url + path, data=data, headers=headers, method=method), timeout=self.timeout) as response:
                result = response.read(4 * 1024 * 1024 + 1)
                if len(result) > 4 * 1024 * 1024:
                    raise ProtocolError("HTTP response exceeds client limit")
                return result
        except URLError as error:
            raise ProtocolError(f"HTTP request failed: {error}; reconnect and inspect state before retrying") from error

    def write(self, data):
        self.buffer = bytearray(self.exchange("/api/v1/command", data))

    def read(self, size, timeout):
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def close(self):
        if self.session:
            try:
                self.exchange("/api/v1/sessions/" + self.session, method="DELETE")
            except ProtocolError:
                pass
            self.session = None


class Client:
    def __init__(self, transport, timeout=5):
        self.transport = transport
        self.timeout = timeout
        self.request_id = 0
        self.pixel_snapshot = bytearray()
        self.data_reply = bytearray()

    def read_exact(self, size, deadline):
        result = bytearray()
        while len(result) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("timed out waiting for receiver")
            result.extend(self.transport.read(size - len(result), remaining))
        return bytes(result)

    def command(self, kind, payload=b"", slot=None, output=None, layer=None):
        if slot is not None and slot not in ("A", "B"):
            raise ValueError("slot must be A or B")
        if slot is not None or output is not None or layer is not None:
            if kind not in (STATUS, PLAY, STOP, RESET, FRAME):
                raise ValueError("slot routing supports STATUS/PLAY/STOP/RESET/FRAME")
            fields = {"op": "output_command", "slot": slot or "A", "command": kind}
            if output is not None:
                fields["output"] = output
            if layer is not None:
                fields["layer"] = layer
            if kind == FRAME:
                if len(payload) != 4:
                    raise ValueError("FRAME requires a four-byte delta")
                fields["delta_ms"] = struct.unpack("<I", payload)[0]
                self.pixel_snapshot.clear()
            elif payload:
                raise ValueError("output command takes no payload")
            return self.command(MANAGE, json.dumps(fields, separators=(",", ":")).encode())
        self.data_reply.clear()
        if kind == FRAME:
            self.pixel_snapshot.clear()
        self.request_id = self.request_id % 65535 + 1
        request_id = self.request_id
        self.transport.write(frame(kind, request_id, payload))
        return self.receive_reply(request_id)

    def receive_reply(self, request_id):
        deadline = time.monotonic() + self.timeout
        while True:
            header = self.read_exact(HEADER.size, deadline)
            magic, version, kind, identity, size, checksum = HEADER.unpack(header)
            if magic != MAGIC or version != 1 or size > FRAME_LIMIT:
                raise ProtocolError("invalid response header")
            payload = self.read_exact(size, deadline)
            if zlib.crc32(payload, zlib.crc32(header[4:12])) != checksum:
                raise ProtocolError("response checksum mismatch")
            if identity != request_id:
                raise ProtocolError("unexpected response identity")
            if kind == PIXELS:
                self.pixel_snapshot.extend(payload)
            elif kind == DATA:
                self.data_reply.extend(payload)
            elif kind == RESULT and len(payload) >= 8:
                code, value = struct.unpack("<Ii", payload[:8])
                return Reply(code, value, payload[8:].decode("utf-8", errors="replace"))
            else:
                raise ProtocolError("invalid response type or status payload")

    @staticmethod
    def require_ok(reply):
        if reply.code:
            raise ProtocolError(f"receiver status {reply.code}: {reply.message}")
        return reply

    def status_fields(self, slot=None, output=None, layer=None):
        reply = self.require_ok(self.command(STATUS, slot=slot, output=output, layer=layer))
        # animation_error may contain spaces; capability/limit tokens do not.
        return dict(item.split("=", 1) for item in reply.message.split() if "=" in item)

    def upload(self, bytecode, slot=None, output=None, settings=None, inputs=None):
        fields = self.status_fields(slot=slot, output=output)
        try:
            limit = int(fields.get("module_limit", MODULE_LIMIT))
        except ValueError as error:
            raise ProtocolError("receiver reported an invalid module limit") from error
        if limit < 8:
            raise ProtocolError("receiver reported an invalid module limit")
        if not 8 <= len(bytecode) <= limit:
            raise ValueError(f"module must be 8..{limit} bytes for this receiver")
        invocation = {**({'settings': settings} if settings else {}), **({'inputs': inputs} if inputs else {})}
        arguments = json.dumps(invocation, separators=(',', ':'), allow_nan=False).encode() if invocation else b''
        if len(arguments) > FRAME_LIMIT:
            raise ValueError('preview settings exceed command size limit')
        self.require_ok(self.command(BEGIN, struct.pack("<II", len(bytecode), zlib.crc32(bytecode)) + (output.encode("ascii") if output is not None else b"")))
        for offset in range(0, len(bytecode), FRAME_LIMIT - 4):
            chunk = bytecode[offset:offset + FRAME_LIMIT - 4]
            reply = self.require_ok(self.command(CHUNK, struct.pack("<I", offset) + chunk))
            if reply.value != offset + len(chunk):
                raise ProtocolError("receiver acknowledged an unexpected upload offset")
        return self.require_ok(self.command(FINISH, arguments))


    def manage(self, operation, **fields):
        payload = json.dumps({"op": operation, **fields}, separators=(",", ":")).encode()
        return self.require_ok(self.command(MANAGE, payload))

    def describe(self):
        self.manage("describe")
        description = json.loads(self.data_reply)
        self.connections = description.get("connections", 0)
        return description

    def get_config(self, saved=False):
        self.manage("config_saved" if saved else "config_get")
        return json.loads(self.data_reply)

    def upload_object(self, kind, data, name=None):
        capabilities = self.describe()
        limit = capabilities["module_limit" if kind == "program" else "config_limit"]
        if not (8 if kind == "program" else 1) <= len(data) <= limit:
            raise ValueError(f"{kind} exceeds receiver size limit {limit}")
        metadata = {"kind": kind, "size": len(data), "crc32": zlib.crc32(data)}
        if name is not None:
            metadata["name"] = name
        self.require_ok(self.command(OBJECT_BEGIN, json.dumps(metadata).encode()))
        for offset in range(0, len(data), FRAME_LIMIT - 4):
            chunk = data[offset:offset + FRAME_LIMIT - 4]
            reply = self.require_ok(self.command(OBJECT_CHUNK, struct.pack("<I", offset) + chunk))
            if reply.value != offset + len(chunk):
                raise ProtocolError("receiver acknowledged an unexpected object offset")
        return self.require_ok(self.command(OBJECT_FINISH))

    def configure(self, config, save=False):
        description = self.describe()
        revision = description["active_revision"]
        data = json.dumps(config, separators=(",", ":")).encode()
        self.upload_object("config", data)
        self.manage("config_validate")
        reply = self.manage("config_apply", expected_revision=revision)
        if save:
            self.manage("config_save", expected_revision=reply.value & 0xffffffff,
                        expected_saved_revision=description["saved_revision"])
        return reply

    def close(self):
        if getattr(self, "connections", 0) and isinstance(self.transport, SerialTransport):
            try:
                self.manage("session_close")
            except (ProtocolError, OSError):
                pass
        self.transport.close()

    def network_config(self):
        self.manage("network_get")
        return json.loads(self.data_reply)

    def network_status(self):
        self.manage("network_status")
        return json.loads(self.data_reply)

    def configure_network(self, config, validate=False):
        revision = self.describe()["network_revision"]
        self.upload_object("network", json.dumps(config, ensure_ascii=False, separators=(",", ":")).encode())
        reply = self.manage("network_validate")
        if validate:
            self.require_ok(self.command(OBJECT_CANCEL))
            return reply
        return self.manage("network_commit", expected_revision=revision)

    def save_config(self):
        description = self.describe()
        return self.manage("config_save", expected_revision=description["active_revision"],
                           expected_saved_revision=description["saved_revision"])

    def save_program(self, name, data):
        revision = self.describe()["catalog_revision"]
        self.upload_object("program", data, name)
        return self.manage("program_save", name=name, expected_revision=revision)

    def programs(self, program_type=None):
        self.manage("program_list", **({"type": program_type} if program_type else {}))
        return json.loads(self.data_reply)

    def get_program(self, name, program_type=None):
        self.manage("program_get", name=name, **({"type": program_type} if program_type else {}))
        return bytes(self.data_reply)

    def delete_program(self, name, program_type=None):
        revision = self.describe()["catalog_revision"]
        return self.manage("program_delete", name=name, expected_revision=revision, **({"type": program_type} if program_type else {}))

    def activate(self, name, action="play", slot=None, settings=None, output=None, layer=None, inputs=None):
        if layer is not None:
            if action != 'play': raise ValueError('composition layers support play activation')
            composition = self.composition(output=output, slot=slot)
            state = next((item for item in composition.get('layers', []) if item['id'] == layer), None)
            if state is None: raise ValueError('composition layer is not configured')
            fields = {'slot':slot or 'A', 'name':name, 'layer':layer, 'expected_revision':composition['revision'], 'expected_generation':state['generation']}
            if output is not None: fields['output'] = output
            if settings is not None: fields['settings'] = settings
            if inputs is not None: fields['inputs'] = inputs
            return self.manage('composition_layer_activate', **fields)
        fields = {"name": name, "action": action}
        if output is not None:
            fields["output"] = output
        if slot is not None:
            fields["slot"] = slot
        if settings is not None:
            fields["settings"] = settings
        if inputs is not None: fields['inputs'] = inputs
        return self.manage("program_activate", **fields)

    def program_state(self, slot=None, output=None, layer=None, target=None):
        self.manage("program_state", **({"slot": slot} if slot is not None else {}), **({"output": output} if output is not None else {}), **({"layer": layer} if layer is not None else {}), **({'target':target} if target is not None else {}))
        return json.loads(self.data_reply)

    def set_inputs(self, inputs, slot=None, output=None, layer=None, target=None):
        state = self.program_state(slot=slot, output=output, layer=layer, target=target)
        self.manage('program_inputs', inputs=inputs, expected_generation=state['generation'],
                    expected_inputs_revision=state['inputs_revision'],
                    **({'slot': slot} if slot is not None else {}),
                    **({'output': output} if output is not None else {}),
                    **({'layer': layer} if layer is not None else {}),
                    **({'target': target} if target is not None else {}))
        return json.loads(self.data_reply)

    def claim_inputs(self, signals):
        self.manage('input_claim', signals=[{'name': name, 'type': typ} for name, typ in signals.items()])
        return json.loads(self.data_reply)

    def publish_inputs(self, publisher, sequence, values):
        return self.manage('input_publish', boot_id=publisher['boot_id'], generation=publisher['generation'], sequence=sequence, values=values)

    def reset_program(self, settings=None, slot=None, output=None, layer=None, inputs=None):
        state = self.program_state(slot=slot, output=output, layer=layer)
        fields = {"expected_generation": state["generation"]}
        if output is not None:
            fields["output"] = output
        if slot is not None:
            fields["slot"] = slot
        if settings is not None:
            fields["settings"] = settings
        if layer is not None:
            fields["layer"] = layer
        if inputs is not None: fields['inputs'] = inputs
        self.manage("program_reset", **fields)
        return json.loads(self.data_reply)

    def pause_program(self, slot=None, output=None, layer=None):
        state = self.program_state(slot=slot, output=output, layer=layer)
        self.manage("program_pause", expected_generation=state["generation"], **({"slot": slot} if slot is not None else {}), **({"output": output} if output is not None else {}), **({"layer": layer} if layer is not None else {}))
        return json.loads(self.data_reply)

    def resume_program(self, slot=None, output=None, layer=None):
        state = self.program_state(slot=slot, output=output, layer=layer)
        self.manage("program_resume", expected_generation=state["generation"], **({"slot": slot} if slot is not None else {}), **({"output": output} if output is not None else {}), **({"layer": layer} if layer is not None else {}))
        return json.loads(self.data_reply)

    def snapshot(self, slot=None, output=None, layer=None, target=None):
        self.pixel_snapshot.clear()
        self.manage("program_snapshot", **({"slot": slot} if slot is not None else {}), **({"output": output} if output is not None else {}), **({"layer": layer} if layer is not None else {}), **({'target':target} if target is not None else {}))
        return json.loads(self.data_reply), bytes(self.pixel_snapshot)

    def composition(self, output=None, slot="A"):
        self.manage('composition_get', slot=slot or 'A', **({'output':output} if output is not None else {}))
        return json.loads(self.data_reply)

    def configure_composition(self, layers, output=None, slot="A"):
        revision = self.composition(output=output, slot=slot)['revision']
        document = {'schema':'lightvm.composition', 'layers':layers}
        self.upload_object('composition', json.dumps(document, separators=(',',':')).encode())
        return self.manage('composition_apply', slot=slot, expected_revision=revision, **({'output':output} if output is not None else {}))

    def composition_snapshot(self, output=None, slot=None):
        self.pixel_snapshot.clear()
        self.manage('composition_snapshot', **({'slot':slot} if slot is not None else {}), **({'output':output} if output is not None else {}))
        return json.loads(self.data_reply), bytes(self.pixel_snapshot)

    def transition(self, output=None):
        self.manage('transition_get', **({'output':output} if output is not None else {}))
        return json.loads(self.data_reply)

    def _transition_control(self, operation, output=None, **fields):
        self.manage(operation, **fields, **({'output':output} if output is not None else {}))
        return json.loads(self.data_reply)

    def set_progress(self, progress, output=None):
        return self._transition_control('transition_set', output, progress=progress)

    def run_transition(self, target, duration_ms, output=None, interval_ms=0):
        return self._transition_control('transition_run', output, target=target, duration_ms=duration_ms, interval_ms=interval_ms)

    def hold_transition(self, output=None):
        return self._transition_control('transition_hold', output)

    def assign_transition_source(self, source, output=None):
        return self._transition_control('transition_assign', output, source=source)

    def update_transition_source(self, source, assignment, progress=None, output=None):
        if isinstance(progress, float) and not math.isfinite(progress): progress = None
        return bool(self.manage('transition_update', source=source, assignment=assignment, progress=progress,
                               **({'output':output} if output is not None else {})).value)

    def select_transition(self, program, settings=None, seed=0, output=None, inputs=None):
        fields = {'program':program, 'seed':seed, 'expected_revision':self.transition(output)['revision']}
        if settings is not None: fields['settings'] = settings
        if inputs is not None: fields['inputs'] = inputs
        return self._transition_control('transition_select', output, **fields)

    def select_transition_effect(self, effect='crossfade', seed=0, mask=None, paint=None, output=None):
        """Compatibility control for the original firmware effect table."""
        fields = {'effect':effect, 'seed':seed, 'expected_revision':self.transition(output)['revision']}
        if mask is not None: fields['mask'] = mask
        if paint is not None: fields['paint'] = paint
        return self._transition_control('transition_select', output, **fields)

    def pause_composition(self, output=None, slot='A', paused=True):
        return self.manage('composition_pause' if paused else 'composition_resume', slot=slot,
                           expected_revision=self.composition(output, slot)['revision'],
                           **({'output':output} if output is not None else {}))

    def presets(self):
        self.manage('preset_list')
        return json.loads(self.data_reply)

    def preset(self, name):
        self.manage('preset_get', name=name)
        return json.loads(self.data_reply)

    def save_preset(self, name, layers, output=None, entry_transition=None):
        revision = self.describe()['catalog_revision']
        document = {'schema': 'lightvm.preset', 'layers': layers}
        if entry_transition is not None:
            document['entry_transition'] = entry_transition
        try:
            self.upload_object('preset', json.dumps(document, separators=(',', ':')).encode(), name)
            return self.manage('preset_save', name=name, expected_revision=revision,
                               **({'output': output} if output is not None else {}))
        except Exception:
            self.command(OBJECT_CANCEL)
            raise

    def activate_preset(self, name, output=None, slot=None):
        revision = self.composition(output=output, slot=slot)['revision']
        return self.manage('preset_activate', **({'slot':slot} if slot is not None else {}), name=name, expected_revision=revision,
                           **({'output': output} if output is not None else {}))

    def recall_preset(self, name, output=None):
        revision = self.transition(output)['revision']
        return self.manage('preset_recall', name=name, expected_revision=revision,
                           **({'output': output} if output is not None else {}))

    def set_power(self, on, output=None, preset=None):
        revision = self.transition(output)['revision']
        return self.manage('output_power', action='on' if on else 'off', expected_revision=revision,
                           **({'name': preset} if preset is not None else {}),
                           **({'output': output} if output is not None else {}))

    def light_state(self, output=None):
        self.manage('output_light_get', **({'output': output} if output is not None else {}))
        return json.loads(self.data_reply)

    def set_light(self, *, brightness=None, color=None, power=None, preset=None, output=None):
        state = self.light_state(output)
        fields = {key: value for key, value in dict(brightness=brightness, color=color, power=power, preset=preset).items() if value is not None}
        if power is not None or preset is not None:
            fields['expected_composition_revision'] = state['composition_revision']
        self.manage('output_light_set', expected_revision=state['revision'],
                    **({'output': output} if output is not None else {}), **fields)
        return json.loads(self.data_reply)

    def swap_transition(self, output=None):
        revision = self.transition(output)['revision']
        return self._transition_control('transition_swap', output, expected_revision=revision)

    def delete_preset(self, name):
        revision = self.describe()['catalog_revision']
        return self.manage('preset_delete', name=name, expected_revision=revision)

    def layer_opacity(self, layer, opacity, output=None, slot="A"):
        revision = self.composition(output=output, slot=slot)['revision']
        return self.manage('composition_layer_opacity', slot=slot, layer=layer, opacity=opacity, expected_revision=revision,
                           **({'output':output} if output is not None else {}))

    def remove_layer(self, layer, output=None, slot="A"):
        revision = self.composition(output=output, slot=slot)['revision']
        return self.manage('composition_layer_remove', slot=slot, layer=layer, expected_revision=revision,
                           **({'output':output} if output is not None else {}))

    def layer_blend(self, layer, blend, output=None, slot="A"):
        revision = self.composition(output=output, slot=slot)['revision']
        return self.manage('composition_layer_blend', slot=slot, layer=layer, blend=blend, expected_revision=revision,
                           **({'output':output} if output is not None else {}))

    def clear_composition(self, output=None, slot="A"):
        revision = self.composition(output=output, slot=slot)['revision']
        return self.manage('composition_clear', slot=slot, expected_revision=revision, **({'output':output} if output is not None else {}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port", help="USB serial port, e.g. /dev/cu.usbmodem...")
    transport.add_argument("--url", help="controller HTTP origin, e.g. http://192.168.1.20")
    transport.add_argument("--process", type=Path, help="desktop receiver executable")
    parser.add_argument("--no-reset", action="store_true", help="deassert DTR/RTS before opening an ESP32 USB serial port")
    parser.add_argument("--storage", type=Path, help="host persistent store file")
    parser.add_argument("--save", action="store_true", help="save an applied --config for reboot")
    parser.add_argument("--saved", action="store_true", help="read saved rather than active configuration")
    parser.add_argument("--action", choices=("play",), default="play")
    parser.add_argument("--save-to", type=Path, help="binary program readback destination")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--network", type=Path, help="validate, save and apply network JSON")
    action.add_argument("--validate-network", type=Path, help="validate network JSON without saving")
    action.add_argument("--get-network", action="store_true", help="read network settings; passwords omitted")
    action.add_argument("--network-status", action="store_true")
    action.add_argument("--describe", action="store_true")
    action.add_argument("--config", type=Path, help="apply JSON configuration in RAM")
    action.add_argument("--validate-config", type=Path, help="stage and validate JSON without applying")
    action.add_argument("--get-config", action="store_true")
    action.add_argument("--save-config", action="store_true")
    action.add_argument("--store-program", metavar="NAME", help="save the supplied module under this name")
    action.add_argument("--programs", action="store_true")
    action.add_argument("--get-program", metavar="NAME")
    action.add_argument("--delete-program", metavar="NAME")
    action.add_argument("--activate", metavar="NAME")
    action.add_argument("--composition", type=Path, help="apply a lightvm.composition JSON document in RAM")
    action.add_argument("--get-composition", action="store_true")
    action.add_argument("--composition-snapshot", action="store_true")
    action.add_argument("--clear-composition", action="store_true")
    action.add_argument("--layer-opacity", type=int, metavar="U8")
    action.add_argument("--layer-blend", choices=("over", "add", "screen", "multiply"))
    action.add_argument("--remove-layer", action="store_true")
    action.add_argument("--recall-preset", metavar="NAME", help="recall a preset using its entry transition")
    action.add_argument("--power", choices=("on", "off"), help="transition output power")
    action.add_argument("--swap-transition", action="store_true", help="swap A/B at an endpoint")
    action.add_argument("--get-transition", action="store_true")
    action.add_argument("--progress", type=float, help="set an absolute percentage and release its driver")
    action.add_argument("--run-to", type=float, metavar="PERCENT", help="run from current progress to target")
    action.add_argument("--hold-transition", action="store_true")
    action.add_argument("--transition-effect", choices=("crossfade","wipe","soft_wipe","dissolve","dip_black","white_flash"))
    action.add_argument("--transition", metavar="NAME", help="select a stored Python transition program")
    action.add_argument("--assign-source", metavar="NAME")
    action.add_argument("--source-value", type=float, metavar="PERCENT")
    parser.add_argument("--source-name", help="assigned source name for --source-value")
    parser.add_argument("--assignment", type=int, help="assignment token returned by --assign-source")
    parser.add_argument("--duration-ms", type=float, default=2000)
    parser.add_argument("--interval-ms", type=float, default=0, help="repeat alternating fades at this interval; zero runs once")
    parser.add_argument("--transition-seed", type=int, default=0)
    parser.add_argument("--settings", help="JSON initialization settings for a named transition")
    parser.add_argument("--slot", choices=("A", "B"), help="composition slot (defaults to A)")
    parser.add_argument("--output", help="configured output ID for upload, activation and controls")
    parser.add_argument("--layer", help="composition layer ID for activation, state and controls")
    action.add_argument("--cancel-upload", action="store_true")
    action.add_argument("--program-state", action="store_true", help="read settings and program state")
    action.add_argument("--pause", action="store_true", help="pause control program time")
    action.add_argument("--resume", action="store_true", help="resume the initialized control program")
    action.add_argument("--snapshot", action="store_true", help="read output without stepping playback")
    action.add_argument("--status", action="store_true", help="query receiver without uploading")
    action.add_argument("--animate", action="store_true", help="start animation playback on the output")
    action.add_argument("--stop", action="store_true", help="stop animation and clear the LEDs")
    action.add_argument("--frame-ms", type=int, help="render one frame with this delta and report an RGB preview")
    parser.add_argument("--program-type", choices=("animation", "transition"), help="Filter or disambiguate stored programs")
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("module", nargs="?", type=Path)
    args = parser.parse_args()
    if args.storage and not args.process:
        parser.error("--storage requires --process")
    if args.save and not args.config:
        parser.error("--save requires --config")
    if args.saved and not args.get_config:
        parser.error("--saved requires --get-config")
    if args.save_to and not args.get_program:
        parser.error("--save-to requires --get-program")
    if args.store_program and args.module is None:
        parser.error("--store-program requires a .lbc program")
    management = (args.recall_preset or args.power or args.swap_transition or args.network or args.validate_network or args.get_network or args.network_status or args.describe or args.config or args.validate_config or args.get_config or args.save_config or
                  args.store_program or args.programs or args.get_program or args.delete_program or
                  args.activate or args.cancel_upload or args.program_state or
                  args.pause or args.resume or args.snapshot or args.composition or args.get_composition or args.composition_snapshot or args.clear_composition or args.layer_opacity is not None or args.layer_blend or args.remove_layer or args.get_transition or args.progress is not None or args.run_to is not None or args.hold_transition or args.transition_effect or args.transition or args.assign_source or args.source_value is not None)
    if args.source_value is not None and (not args.source_name or args.assignment is None):
        parser.error("--source-value requires --source-name and --assignment")
    if (args.layer_opacity is not None or args.layer_blend or args.remove_layer) and not args.layer:
        parser.error('--layer is required for a layer update or removal')
    if management and args.module is not None and not args.store_program:
        parser.error("module argument only applies to --store-program or playback")
    if args.no_reset and not args.port:
        parser.error("--no-reset requires --port")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.frame_ms is not None and not 0 <= args.frame_ms <= 1000:
        parser.error("--frame-ms must be 0..1000")
    if args.module is None and not (args.status or args.animate or args.stop or args.frame_ms is not None or management):
        parser.error("provide a .lbc program or a status/animation command")
    connection = None
    client = None
    try:
        connection = SerialTransport(args.port, no_reset=args.no_reset) if args.port else HttpTransport(args.url, args.timeout) if args.url else ProcessTransport(args.process, storage=args.storage)
        client = Client(connection, timeout=args.timeout)
        if management:
            if args.recall_preset:
                print(client.recall_preset(args.recall_preset, output=args.output))
            elif args.power:
                print(client.set_power(args.power == 'on', output=args.output))
            elif args.swap_transition:
                print(json.dumps(client.swap_transition(output=args.output), indent=2))
            elif args.get_transition:
                print(json.dumps(client.transition(output=args.output),indent=2))
            elif args.progress is not None:
                print(json.dumps(client.set_progress(args.progress,output=args.output),indent=2))
            elif args.run_to is not None:
                print(json.dumps(client.run_transition(args.run_to,args.duration_ms,output=args.output,interval_ms=args.interval_ms),indent=2))
            elif args.hold_transition:
                print(json.dumps(client.hold_transition(output=args.output),indent=2))
            elif args.transition_effect:
                print(json.dumps(client.select_transition_effect(args.transition_effect,seed=args.transition_seed,output=args.output),indent=2))
            elif args.transition:
                print(json.dumps(client.select_transition(args.transition,settings=json.loads(args.settings) if args.settings else None,seed=args.transition_seed,output=args.output),indent=2))
            elif args.assign_source:
                print(json.dumps(client.assign_transition_source(args.assign_source,output=args.output),indent=2))
            elif args.source_value is not None:
                print(json.dumps({'applied':client.update_transition_source(args.source_name,args.assignment,args.source_value,output=args.output)}))
            elif args.composition:
                document=json.loads(args.composition.read_text())
                if document.get('schema') != 'lightvm.composition': raise ValueError('expected lightvm.composition')
                client.configure_composition(document['layers'],output=args.output,slot=args.slot or 'A')
            elif args.get_composition:
                print(json.dumps(client.composition(output=args.output,slot=args.slot or 'A'),indent=2))
            elif args.composition_snapshot:
                state,rgb=client.composition_snapshot(output=args.output,slot=args.slot)
                print(json.dumps({**state,'rgb':rgb.hex()},indent=2))
            elif args.clear_composition:
                client.clear_composition(output=args.output,slot=args.slot or 'A')
            elif args.layer_opacity is not None:
                client.layer_opacity(args.layer,args.layer_opacity,output=args.output,slot=args.slot or 'A')
            elif args.layer_blend:
                client.layer_blend(args.layer,args.layer_blend,output=args.output,slot=args.slot or 'A')
            elif args.remove_layer:
                client.remove_layer(args.layer,output=args.output,slot=args.slot or 'A')
            elif args.network or args.validate_network:
                path = args.network or args.validate_network
                client.configure_network(json.loads(path.read_text()), validate=bool(args.validate_network))
            elif args.get_network:
                print(json.dumps(client.network_config(), indent=2, ensure_ascii=False))
            elif args.network_status:
                print(json.dumps(client.network_status(), indent=2))
            elif args.describe:
                print(json.dumps(client.describe(), indent=2))
            elif args.get_config:
                print(json.dumps(client.get_config(saved=args.saved), indent=2))
            elif args.programs:
                print(json.dumps(client.programs(args.program_type), indent=2))
            elif args.get_program:
                data = client.get_program(args.get_program, args.program_type)
                if args.save_to:
                    args.save_to.write_bytes(data)
                else:
                    sys.stdout.buffer.write(data)
            elif args.config:
                client.configure(json.loads(args.config.read_text()), save=args.save)
            elif args.validate_config:
                client.upload_object("config", args.validate_config.read_bytes())
                client.manage("config_validate")
            elif args.save_config:
                client.save_config()
            elif args.store_program:
                client.save_program(args.store_program, args.module.read_bytes())
            elif args.delete_program:
                client.delete_program(args.delete_program, args.program_type)
            elif args.activate:
                client.activate(args.activate, args.action, args.slot, output=args.output, layer=args.layer)
            elif args.cancel_upload:
                client.require_ok(client.command(OBJECT_CANCEL))
            elif args.program_state:
                print(json.dumps(client.program_state(slot=args.slot, output=args.output, layer=args.layer), indent=2))
            elif args.pause:
                print(json.dumps(client.pause_program(slot=args.slot, output=args.output, layer=args.layer), indent=2))
            elif args.resume:
                print(json.dumps(client.resume_program(slot=args.slot, output=args.output, layer=args.layer), indent=2))
            elif args.snapshot:
                state, rgb = client.snapshot(slot=args.slot, output=args.output, layer=args.layer)
                print(json.dumps({**state, "rgb": rgb.hex()}, indent=2))
            return 0
        if args.module is not None:
            loaded = client.upload(args.module.read_bytes(), slot=args.slot, output=args.output)
            print(f"Loaded {loaded.value} bytes", file=sys.stderr)
        if args.animate:
            reply = client.command(PLAY, slot=args.slot, output=args.output, layer=args.layer)
        elif args.stop:
            reply = client.command(STOP, slot=args.slot, output=args.output, layer=args.layer)
        elif args.frame_ms is not None:
            reply = client.command(FRAME, struct.pack("<I", args.frame_ms), slot=args.slot, output=args.output, layer=args.layer)
            if client.pixel_snapshot:
                rgba = args.layer is not None and client.program_state(output=args.output, layer=args.layer).get('pixel_format') == 'rgba8-premul'
                stride = 4 if rgba else 3
                colors = [tuple(client.pixel_snapshot[index:index+stride])
                          for index in range(0, min(5*stride, len(client.pixel_snapshot)), stride)]
                print(f"{'RGBA' if rgba else 'RGB'} snapshot: {len(client.pixel_snapshot)//stride} pixels; first five {colors}", file=sys.stderr)
        elif args.status or args.module is None:
            reply = client.command(STATUS, slot=args.slot, output=args.output, layer=args.layer)
        else:
            reply = client.command(PLAY, slot=args.slot, output=args.output, layer=args.layer)
        print(f"Status {reply.code}, result {reply.value}: {reply.message}", file=sys.stderr)
        return 0 if reply.code == 0 else 1
    except (OSError, ValueError, ProtocolError, ImportError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    finally:
        if client:
            client.close()
        elif connection:
            connection.close()


if __name__ == "__main__":
    raise SystemExit(main())
