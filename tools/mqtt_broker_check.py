#!/usr/bin/env python3
"""Validate the identical WASM service's HA exchange against isolated Mosquitto."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from experiments.mqtt.host import Guest
import paho.mqtt.client as mqtt


def check(module, host, port, restart_command=None):
    messages = []
    monitor = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="lightvm-isolated-validation")
    monitor.on_connect = lambda client, *_: client.subscribe([("homeassistant/#", 1), ("lightvm/#", 1)])
    monitor.on_message = lambda client, userdata, message: messages.append({"topic":message.topic,
        "payload":message.payload.decode(), "qos":message.qos, "retained":message.retain, "time":time.monotonic()})
    monitor.connect(host, port)
    monitor.loop_start()
    guest = Guest(module, real_network=True)
    guest.start(host, port)

    def until(predicate, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            guest.step()
            if predicate():
                return
            time.sleep(.001)
        raise AssertionError("Timed out waiting for expected MQTT exchange")

    def configs():
        return [m for m in messages if m["topic"].startswith("homeassistant/") and m["topic"].endswith("/config")]

    try:
        until(lambda: len(configs()) >= 4)
        for entry in configs()[:4]:
            discovery = json.loads(entry["payload"])
            assert discovery["device"]["identifiers"] == [guest.identity]
            assert discovery["availability_topic"] == f"lightvm/{guest.identity}/availability"
        until(lambda: any(m["payload"] == "online" and m["topic"].endswith("/availability") for m in messages))
        started = time.monotonic()
        for i in range(10):
            monitor.publish(guest.command_topic, str(i / 10), qos=1)
            until(lambda: guest.commands == i + 1)
            while time.monotonic() < started + (i+1)/10:
                guest.step()
                time.sleep(.001)
        for i in range(100):
            monitor.publish(guest.command_topic, str(i / 10), qos=1)
        until(lambda: guest.commands == 110)
        assert abs(guest.speed - 9.9) < 1e-8
        monitor.publish(guest.command_topic, json.dumps({"speed":1,"generation":0,"revision":0}), qos=1)
        until(lambda: guest.rejected == 1)
        guest.socket.close()  # Abrupt connection loss must exercise retained Last Will.
        until(lambda: any(m["payload"] == "offline" and m["topic"].endswith("/availability") for m in messages))
        count = len(configs())
        until(lambda: len(configs()) >= count + 4)
        restart_test = False
        if restart_command:
            count = len(configs())
            subprocess.run(shlex.split(restart_command), check=True, stdout=subprocess.DEVNULL)
            until(lambda: len(configs()) >= count + 4, 20)
            restart_test = True
        count = len(configs())
        guest.start(host, port)  # Independent module lifecycle/reinitialization.
        until(lambda: len(configs()) >= count + 4)
        # A new subscriber must receive retained discovery and online availability.
        retained = []
        reader = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="lightvm-retained-validation")
        reader.on_connect = lambda client, *_: client.subscribe([("homeassistant/#",1), (f"lightvm/{guest.identity}/availability",1)])
        reader.on_message = lambda client, userdata, msg: retained.append((msg.topic, msg.payload, msg.retain))
        reader.connect(host, port); reader.loop_start()
        try:
            until(lambda: len(retained) >= 5)
            assert all(r[2] for r in retained)
        finally:
            reader.disconnect(); reader.loop_stop()
        return {"timestamp_utc":datetime.now(timezone.utc).isoformat(), "runtime":"Wasmtime protocol harness; no firmware performance claim",
                "module_sha256":hashlib.sha256(module.read_bytes()).hexdigest(), "commands_applied":guest.commands,
                "stale_commands_rejected":guest.rejected, "discovery_configs":len(configs()), "retained_discovery_and_availability":True,
                "last_will_observed":True, "broker_restart":restart_test, "messages":messages}
    finally:
        guest.close_connection(); monitor.disconnect(); monitor.loop_stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--service", type=Path, default=ROOT / "build/mqtt/service.wasm")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18883)
    parser.add_argument("--restart-command")
    parser.add_argument("--output", type=Path, default=ROOT / "build/mqtt/mosquitto.json")
    args = parser.parse_args()
    result = check(args.service, args.host, args.port, args.restart_command)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k:v for k,v in result.items() if k != "messages"}))
