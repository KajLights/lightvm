#!/usr/bin/env python3
"""Gate Pico RAM using the linked SDK geometry and compiled resource profile."""
import json
import subprocess
import sys
from pathlib import Path

def read_symbols(nm: str, image: str) -> dict[str, int]:
    symbols = {}
    for line in subprocess.check_output([nm, "--defined-only", image], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    return symbols


def ram_report(symbols: dict[str, int]) -> dict:
    s = symbols
    if s.get("__lightvm_dcp_enabled", 0) and any(
        "__wrap___aeabi_" + op not in s for op in ("dadd", "dsub", "dmul", "ddiv")
    ):
        raise ValueError("Pico DCP enabled but arithmetic wrappers were not linked")
    # __end__ includes the vector table, RAM code/data, BSS, TLS and alignment.
    # Scratch allocations are separate; dummy stack sections reserve no data.
    static = s["__end__"] - s["__lightvm_sram_start"]
    for bank in ("x", "y"):
        static += s[f"__scratch_{bank}_end__"] - s[f"__scratch_{bank}_start__"]
    stack = s["__StackTop"] - s["__StackBottom"] + s["__StackOneTop"] - s["__StackOneBottom"]
    sram = s["__lightvm_sram_end"] - s["__lightvm_sram_start"]
    if not s["__lightvm_sram_start"] <= s["__end__"] <= s["__HeapLimit"] <= s["__StackBottom"] < s["__StackTop"] <= s["__lightvm_sram_end"]:
        raise ValueError("invalid Pico heap/stack boundaries")
    if s["__StackOneBottom"] != s["__StackOneTop"] and not (
        s["__HeapLimit"] <= s["__StackOneBottom"] < s["__StackOneTop"] <= s["__StackBottom"]
    ):
        raise ValueError("invalid Pico core-1 heap/stack boundaries")
    # The compiled slot reservation includes CYW43 sharing on the chosen chip.
    budgets = {"static_bytes": static, "vm_heap_ceiling": s["__lightvm_vm_heap"],
               "native_stack": stack, "pixel_driver_max": s["__lightvm_output_slots"] * (s["__lightvm_pixel_capacity"] * 4 + 256),
               "operating_headroom": s["__lightvm_native_headroom"]}
    if min(sram, *budgets.values()) <= 0:
        raise ValueError("invalid Pico RAM geometry or reservations")
    # Check usable contiguous heap too: unallocated scratch SRAM cannot pay for
    # VM/output allocations made through malloc.
    heap_remaining = s["__HeapLimit"] - s["__end__"] - sum(
        budgets[key] for key in ("vm_heap_ceiling", "pixel_driver_max", "operating_headroom"))
    return {"sram_bytes": sram, **budgets,
            "remaining_bytes_after_reservations": min(sram - sum(budgets.values()), heap_remaining),
            "dynamic_costs": "lwIP pools are static; driver budget covers pio0/pio1 at the compiled pixel capacity; dynamic output/config/pin records and other native allocations use operating headroom"}


def main() -> None:
    nm, image = sys.argv[1:]
    report = ram_report(read_symbols(nm, image))
    Path(image).with_name("ram-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    if report["remaining_bytes_after_reservations"] < 0:
        raise SystemExit("Pico RAM reservations exceed SRAM; reduce the resource profile or native buffers")


if __name__ == "__main__":
    main()
