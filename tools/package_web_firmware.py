#!/usr/bin/env python3
"""Package ESP-IDF, RP2040 and RP2350 Arm firmware for the browser installer."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lightvm.release import VERSION


CHIPS = {
    "xiao_esp32c3": "ESP32-C3",
    "xiao_esp32s3": "ESP32-S3",
    "xiao_esp32c6": "ESP32-C6",
}
EXPECTED_FILES = {
    0x0: "bootloader/bootloader.bin",
    0x8000: "partition_table/partition-table.bin",
    0x10000: "lightvm.bin",
}
PICO_BOARDS = {"pico": "Pico", "pico_w": "Pico W", "pico2_w": "Pico 2 W"}
PICO_FAMILIES = {"pico": 0xE48BFF56, "pico_w": 0xE48BFF56, "pico2_w": 0xE48BFF59}
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
PICO_FLASH_START = 0x10000000


def validate_pico_uf2(data: bytes, firmware_end: int, board: str) -> None:
    if board not in PICO_FAMILIES:
        raise ValueError(f"unsupported Pico board: {board}")
    flash_bytes = 4 * 1024 * 1024 if board == "pico2_w" else 2 * 1024 * 1024
    if (type(firmware_end) is not int or firmware_end % 4096 or
            not PICO_FLASH_START < firmware_end <= PICO_FLASH_START + flash_bytes):
        raise ValueError("invalid Pico storage boundary")
    if not data or len(data) % 512:
        raise ValueError("Pico UF2 is empty or not 512-byte aligned")
    # Picotool 2.3.1 prepends an RP2350-E10 workaround block. Its ignore tag
    # tells the ROM not to program it; it is not part of the application image.
    first = struct.unpack_from("<8I", data)
    if board == "pico2_w" and first[7] == 0xE48BFF57:
        if (first != (UF2_MAGIC[0], UF2_MAGIC[1], 0xA000, 0x10FFFF00, 256, 0, 2, 0xE48BFF57) or
                data[32:288] != b"\xef" * 256 or
                struct.unpack_from("<I", data, 288)[0] != 0x9957E304 or
                data[292:508] != bytes(216) or
                struct.unpack_from("<I", data, 508)[0] != UF2_MAGIC[2]):
            raise ValueError("invalid Pico RP2350 ignored workaround block")
        data = data[512:]
    if not data:
        raise ValueError("Pico UF2 has no application blocks")
    count = len(data) // 512
    for block_number in range(count):
        block = data[block_number * 512:(block_number + 1) * 512]
        magic0, magic1, flags, address, size, index, total, family = struct.unpack_from("<8I", block)
        end_magic, = struct.unpack_from("<I", block, 508)
        expected_address = PICO_FLASH_START + block_number * 256
        if (magic0, magic1, end_magic) != UF2_MAGIC or flags != 0x2000 or family != PICO_FAMILIES[board]:
            raise ValueError(f"Pico UF2 block {block_number}: invalid header or {board} family")
        if size != 256 or index != block_number or total != count or address != expected_address:
            raise ValueError(f"Pico UF2 block {block_number}: invalid order or payload")
        if address + size > firmware_end:
            raise ValueError("Pico UF2 overlaps reserved flash storage")


def validate_esp_profile(board: str, directory: Path) -> None:
    cache = (directory / "CMakeCache.txt").read_text().splitlines()
    settings = dict(line.split("=", 1) for line in cache if line.startswith("LIGHTVM_") and "=" in line)
    expected = {"LIGHTVM_BOARD:STRING": board,
                "LIGHTVM_WIFI:BOOL": "ON",
                "LIGHTVM_HTTP:BOOL": "ON",
                "LIGHTVM_WEBSOCKET:BOOL": "ON",
                "LIGHTVM_BOOT_SEQUENCE:BOOL": "ON"}
    for key, value in expected.items():
        if settings.get(key) != value:
            raise ValueError(f"{board}: installer requires {key}={value}; found {settings.get(key, 'missing')}")


def package(output: Path, board_dirs: dict[str, Path], pico_files: dict[str, Path] | None = None, build_id: str | None = None) -> dict:
    pico_files = pico_files or {}
    if not board_dirs and not pico_files:
        raise ValueError("at least one board build is required")
    output.mkdir(parents=True, exist_ok=True)
    for name in ("manifest.json", "pico.json"):
        (output / name).unlink(missing_ok=True)
    # This is a generated bundle: do not ship files left by a previous build.
    for name in (*CHIPS, *PICO_BOARDS, "licenses"):
        directory = output / name
        if directory.exists():
            shutil.rmtree(directory)
    builds = []
    for board, directory in board_dirs.items():
        if board not in CHIPS:
            raise ValueError(f"unsupported board: {board}")
        validate_esp_profile(board, directory)
        args = json.loads((directory / "flasher_args.json").read_text())
        chip = args["extra_esptool_args"]["chip"].upper().replace("ESP32", "ESP32-")
        if chip != CHIPS[board]:
            raise ValueError(f"{board}: flasher chip {chip} does not match board")
        if args["flash_settings"]["flash_mode"] != "dio" or args["flash_settings"]["flash_freq"] != "40m":
            raise ValueError(f"{board}: unexpected flash settings")
        files = {int(offset, 0): path for offset, path in args["flash_files"].items()}
        if files != EXPECTED_FILES:
            raise ValueError(f"{board}: unexpected flash layout: {files}")
        parts = []
        for offset, relative in sorted(files.items()):
            source = directory / relative
            if not source.is_file() or source.stat().st_size == 0:
                raise ValueError(f"{board}: missing or empty {relative}")
            if offset == 0x10000 and source.stat().st_size > 0x200000:
                raise ValueError(f"{board}: app exceeds its 2 MiB partition")
            destination = output / board / Path(relative).name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            parts.append({"path": f"{board}/{destination.name}", "offset": offset})
        builds.append({"chipFamily": CHIPS[board], "improv": False, "parts": parts})
    manifest = {
        "name": "LightVM",
        "version": VERSION, "build": build_id,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": builds,
    }
    if builds:
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    pico_builds = []
    for board, source in pico_files.items():
        if board not in PICO_BOARDS:
            raise ValueError(f"unsupported Pico board: {board}")
        data = source.read_bytes()
        layout = json.loads(source.with_name("storage-layout.json").read_text())
        firmware_end = layout["firmware_end"]
        expected_flash = (4 if board == "pico2_w" else 2) * 1024 * 1024
        if (layout.get("board") != board or
                any(type(layout[key]) is not int for key in ("storage_start", "storage_bytes", "flash_bytes", "firmware_end")) or
                layout["flash_bytes"] != expected_flash or
                layout["storage_bytes"] < 16384 or layout["storage_bytes"] % 8192 or
                layout["storage_start"] + layout["storage_bytes"] != PICO_FLASH_START + layout["flash_bytes"] or
                firmware_end not in (layout["storage_start"], layout["storage_start"] - 8192)):
            raise ValueError("inconsistent Pico storage layout")
        validate_pico_uf2(data, firmware_end, board)
        path = f"{board}/lightvm.uf2"
        destination = output / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        pico_builds.append({"board": board, "name": PICO_BOARDS[board], "path": path,
                            "sha256": hashlib.sha256(data).hexdigest(), "firmware_end": firmware_end,
                            "chip": "RP2350" if board == "pico2_w" else "RP2040"})
    if pico_builds:
        (output / "pico.json").write_text(json.dumps({"version": VERSION, "build": build_id, "builds": pico_builds}, indent=2) + "\n")
    root = Path(__file__).resolve().parents[1]
    shutil.copyfile(root / "LICENSE", output / "LICENSE")
    shutil.copyfile(root / "THIRD_PARTY_NOTICES.md", output / "THIRD_PARTY_NOTICES.md")
    shutil.copytree(root / "licenses", output / "licenses", dirs_exist_ok=True)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/editor-assets/firmware"))
    parser.add_argument("--build-id", help="optional commit/build identifier; compatibility uses lightvm/release.json")
    parser.add_argument("--board", action="append", default=[], metavar="NAME=BUILD_DIR")
    parser.add_argument("--pico", action="append", default=[], metavar="NAME=UF2_FILE")
    args = parser.parse_args()
    board_dirs = {}
    for item in args.board:
        name, sep, directory = item.partition("=")
        if not sep or name in board_dirs:
            parser.error(f"invalid or repeated board: {item}")
        board_dirs[name] = Path(directory)
    pico_files = {}
    for item in args.pico:
        name, sep, path = item.partition("=")
        if not sep or name in pico_files:
            parser.error(f"invalid or repeated Pico board: {item}")
        pico_files[name] = Path(path)
    package(args.output, board_dirs, pico_files, args.build_id)
    print(args.output)


if __name__ == "__main__":
    main()
