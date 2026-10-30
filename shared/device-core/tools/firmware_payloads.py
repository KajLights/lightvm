"""Validated Pico UF2 payload boundaries shared by release and local installers."""
import struct

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

