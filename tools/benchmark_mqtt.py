#!/usr/bin/env python3
"""Buffered firmware experiment: preserve ceilings and report failed admissions honestly."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shlex
import socket
import statistics
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source
from tools.client import Client, HttpTransport, ProcessTransport, SerialTransport, PLAY, STOP, MANAGE


def percentile(values, percent):
    return sorted(values)[min(len(values)-1, (len(values)*percent+99)//100-1)] if values else None


def state(client):
    client.manage("service_status")
    return json.loads(client.data_reply)


def samples(client, kind, count):
    result = []
    for offset in range(0, count, 32):
        client.manage("service_samples", kind=kind, offset=offset)
        result.extend(json.loads(client.data_reply))
    return result


def measure(client, seconds, action=None):
    client.manage("service_measure_start")
    started = time.monotonic()
    while time.monotonic() - started < seconds:
        elapsed = time.monotonic() - started
        if action:
            action(elapsed)
        time.sleep(.005)
    client.manage("service_measure_stop")
    metrics = state(client)
    gaps = samples(client, "gaps", metrics["gap_count"])
    latencies = samples(client, "latencies", metrics["latency_count"])
    return {"metrics":metrics,"fps":metrics["frames"]/(metrics["elapsed_us"]/1e6),
            "frame_gaps_us":{"median":statistics.median(gaps) if gaps else None,"p95":percentile(gaps,95),
                             "p99":percentile(gaps,99),"maximum":max(gaps) if gaps else None},
            "parse_to_commit_latency_us":{"median":statistics.median(latencies) if latencies else None,
                                          "p95":percentile(latencies,95),"maximum":max(latencies) if latencies else None},
            "raw_frame_gaps_us":gaps,"raw_parse_to_commit_latency_us":latencies}


def benchmark(client, service, seconds, repetitions, warmup, broker_ip, broker_port, url=None, restart_command=None, previous=None, checkpoint_path=None):
    capabilities = client.describe()
    if not capabilities.get("mqtt_experiment"):
        raise RuntimeError("Firmware was built without LIGHTVM_MQTT_EXPERIMENT")
    original = client.get_config()
    original_program = capabilities.get("active_procedure")
    animation = compile_source((ROOT / "samples/python/rainbow.py").read_text()).wasm
    config = {"schema":"lightvm.config/1","devices":[{"id":"benchmark","type":"ws2812b","config":{
        "pin":"D1" if capabilities["board"] == "xiao_esp32c3" else "GP16" if capabilities["platform"] in ("host","pico") else "D0",
        "count":100,"order":"GRB","brightness_limit":32}}],"targets":{"A":{"device":"benchmark"}}}
    result = {"timestamp_utc":datetime.now(timezone.utc).isoformat(),"board":capabilities["board"],
              "vm_heap_limit":capabilities["vm_heap_limit"],"pixels":100,"visible_strip_verified":False,
              "native_heap_metrics_available":capabilities["platform"]=="esp32",
              "output_timing":"native configured driver; physical light output unverified",
              "animation_bytes":len(animation),"animation_sha256":hashlib.sha256(animation).hexdigest(),
              "service_bytes":len(service),"service_sha256":hashlib.sha256(service).hexdigest(),
              "window_seconds":seconds,"repetitions":repetitions,"warmup_seconds":warmup,
              "windows":[],"admissions":[],"unavailable":[],"http_replacement":None}
    if previous:
        for key in ("board","service_sha256","animation_sha256","window_seconds","repetitions"):
            if previous[key]!=result[key]:raise RuntimeError(f"Cannot resume different experiment: {key}")
        result["windows"]=previous["windows"]
        completed={w["repetition"] for w in result["windows"] if w["scenario"]=="service_admission_attempt"}
        result["admissions"]=[a for a in previous["admissions"] if a["repetition"] in completed]
        result["resumed_from_timestamp_utc"]=previous["timestamp_utc"]
    def done(scenario,repetition):return any(w["scenario"]==scenario and w["repetition"]==repetition for w in result["windows"])
    def checkpoint():
        if checkpoint_path:
            checkpoint_path.parent.mkdir(parents=True,exist_ok=True)
            temporary=checkpoint_path.with_suffix(".partial.json")
            temporary.write_text(json.dumps(result,indent=2)+"\n"); temporary.replace(checkpoint_path)
    mqtt_client = None
    replacement = None
    try:
        client.manage("service_unload")
        client.configure(config); client.upload(animation); client.require_ok(client.command(PLAY))
        for repetition in range(repetitions):
            if done("animation_alone",repetition+1):continue
            time.sleep(warmup)
            result["windows"].append({"scenario":"animation_alone","repetition":repetition+1,**measure(client,seconds)})
            print(f"baseline {repetition+1}/{repetitions} buffered", flush=True)
            checkpoint()
        for repetition in range(repetitions):
            if done("service_admission_attempt",repetition+1):continue
            time.sleep(warmup)
            attempt={}
            def admit(elapsed):
                if attempt:return
                client.upload_object("service", service)
                reply=client.command(MANAGE,b'{"op":"service_load"}')
                attempt.update(status=reply.code,message=reply.message)
            window=measure(client,seconds,admit)
            result["windows"].append({"scenario":"service_admission_attempt","repetition":repetition+1,**window})
            result["admissions"].append({"repetition":repetition+1,**attempt,"metrics":window["metrics"],"animation":client.status_fields()})
            print(f"admission {repetition+1}/{repetitions}: status {attempt['status']} buffered",flush=True)
            checkpoint()
        if any(a["status"] for a in result["admissions"]):
            result["unavailable"] = [{"scenario":scenario,"reason":"service admission failed under unchanged VM/native budget"}
                for scenario in ("loaded_disconnected","connected_idle","traffic_and_burst","broker_restart_connection_loss_replacement")]
            result["recommendation"] = "Separate WASM MQTT service is not feasible in this firmware profile; optimize interpreter footprint or evaluate native MQTT."
            result["run_complete"]=True
            return result
        if url:
            replacement = Client(HttpTransport(url))
            if replacement.describe()["device_id"] != capabilities["device_id"]:
                raise RuntimeError("HTTP replacement target does not match measured device")
        if not state(client)["loaded"]:
            client.upload_object("service",service); client.manage("service_load")
        for repetition in range(repetitions):
            if done("loaded_disconnected",repetition+1):continue
            client.manage("service_stop"); time.sleep(warmup)
            result["windows"].append({"scenario":"loaded_disconnected","repetition":repetition+1,**measure(client,seconds)})
        ipv4 = int.from_bytes(socket.inet_aton(broker_ip),"big")
        def start():
            client.manage("service_start",ipv4=ipv4,port=broker_port)
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                if state(client)["guest_state"]==4:
                    return
                time.sleep(.1)
            raise RuntimeError("MQTT connection failed; timing scenarios cannot be classified")
        start()
        for repetition in range(repetitions):
            if done("connected_idle",repetition+1):continue
            time.sleep(warmup)
            result["windows"].append({"scenario":"connected_idle","repetition":repetition+1,**measure(client,seconds)})
        import paho.mqtt.client as mqtt
        mqtt_client=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        mqtt_client.connect(broker_ip,broker_port); mqtt_client.loop_start()
        topic=f"lightvm/{capabilities['device_id']}/speed/set"
        for repetition in range(repetitions):
            if done("traffic_and_burst",repetition+1):continue
            sent=0; burst=False
            def traffic(elapsed):
                nonlocal sent,burst
                target=int(elapsed*10)
                while sent<target:
                    mqtt_client.publish(topic,str((sent%100)/10),qos=1); sent+=1
                if elapsed>=seconds/2 and not burst:
                    for i in range(100):mqtt_client.publish(topic,str(i/10),qos=1)
                    burst=True
            time.sleep(warmup)
            window=measure(client,seconds,traffic)
            result["windows"].append({"scenario":"traffic_and_burst","repetition":repetition+1,"published":sent+100,**window})
        for repetition in range(repetitions):
            if done("broker_restart_connection_loss_replacement",repetition+1):continue
            events=set()
            def disruptions(elapsed):
                if elapsed>=seconds/4 and "restart" not in events:
                    events.add("restart")
                    if restart_command:subprocess.run(shlex.split(restart_command),check=True,stdout=subprocess.DEVNULL)
                    else:client.manage("service_stop"); client.manage("service_start",ipv4=ipv4,port=broker_port)
                if elapsed>=seconds/2 and "replacement" not in events:
                    events.add("replacement")
                    target=replacement or client
                    target.upload_object("service",service); target.manage("service_load"); target.manage("service_start",ipv4=ipv4,port=broker_port)
            time.sleep(warmup)
            window=measure(client,seconds,disruptions)
            result["windows"].append({"scenario":"broker_restart_connection_loss_replacement","repetition":repetition+1,"broker_restart":bool(restart_command),**window})
        result["http_replacement"] = bool(replacement)
        baseline=statistics.median(w["fps"] for w in result["windows"] if w["scenario"]=="animation_alone")
        baseline_max=max(w["frame_gaps_us"]["maximum"] for w in result["windows"] if w["scenario"]=="animation_alone")
        for window in result["windows"]:
            ratio=window["fps"]/baseline
            target=.95 if window["scenario"]=="connected_idle" else .90
            window["fps_baseline_ratio"]=ratio
            window["performance_pass"]=ratio>=target and window["frame_gaps_us"]["maximum"]<=baseline_max+window["metrics"]["frame_interval_us"] and not window["metrics"]["gap_dropped"]
        result["recommendation"]="Review raw timings and all performance_pass fields before claiming feasibility."
        result["run_complete"]=True
        return result
    except Exception as error:
        result["run_error"]=str(error); result["run_complete"]=False; checkpoint()
        raise
    finally:
        if mqtt_client:mqtt_client.disconnect(); mqtt_client.loop_stop()
        if replacement:replacement.close()
        client.manage("service_unload"); client.require_ok(client.command(STOP)); client.configure(original)
        if original_program:client.activate(original_program)


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    transport=parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port"); transport.add_argument("--process",type=Path); transport.add_argument("--url")
    parser.add_argument("--http-replacement-url")
    parser.add_argument("--service",type=Path,default=ROOT/"build/mqtt/service.wasm")
    parser.add_argument("--seconds",type=float,default=30); parser.add_argument("--repetitions",type=int,default=3)
    parser.add_argument("--warmup",type=float,default=5); parser.add_argument("--broker-ip",default="127.0.0.1")
    parser.add_argument("--broker-port",type=int,default=18883); parser.add_argument("--restart-command")
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--resume",action="store_true",help="retain completed buffered windows from --output after reconnecting")
    args=parser.parse_args()
    if args.seconds<=0 or args.seconds>33 or args.repetitions<1 or args.warmup<0:parser.error("use 0 < seconds <= 33, repetitions >= 1, warmup >= 0 for bounded samples")
    connection=SerialTransport(args.port,no_reset=True) if args.port else HttpTransport(args.url) if args.url else ProcessTransport(args.process)
    client=Client(connection)
    previous=json.loads(args.output.read_text()) if args.resume else None
    try:result=benchmark(client,args.service.read_bytes(),args.seconds,args.repetitions,args.warmup,args.broker_ip,args.broker_port,args.http_replacement_url or args.url,args.restart_command,previous,args.output)
    finally:client.close()
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"board":result["board"],"windows":len(result["windows"]),"admissions":[a["status"] for a in result["admissions"]],"recommendation":result["recommendation"]}))
