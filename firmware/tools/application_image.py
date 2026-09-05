"""Stamp and validate transactional P2000T application firmware images."""

import binascii
import sys
from pathlib import Path

from hex_image import read_hex, write_hex


MANIFEST_ADDRESS = 0x6FF0
MANIFEST_SIZE = 16
MANIFEST_MAGIC = b"P2FW"
MANIFEST_FORMAT = 1
PROTOCOL_VERSION = (1, 0)


def image_bytes(memory, length):
    return bytes(memory.get(address, 0xFF) for address in range(length))


def manifest_for(memory):
    if not memory or min(memory) != 0:
        raise ValueError("Application must contain the reset vector at address 0000")
    if max(memory) >= MANIFEST_ADDRESS:
        raise ValueError("Application data overlaps the manifest at 0x6FF0")
    length = max(memory) + 1
    if length < 2 or (memory.get(0, 0xFF) == 0xFF and memory.get(1, 0xFF) == 0xFF):
        raise ValueError("Application reset vector is blank")
    crc = binascii.crc32(image_bytes(memory, length)) & 0xFFFFFFFF
    return (MANIFEST_MAGIC + bytes((MANIFEST_FORMAT, *PROTOCOL_VERSION, 0xFF)) +
            length.to_bytes(4, "little") + crc.to_bytes(4, "little"))


def stamp(input_path, output_path):
    memory = read_hex(input_path)
    manifest = manifest_for(memory)
    for offset, value in enumerate(manifest):
        memory[MANIFEST_ADDRESS + offset] = value
    write_hex(memory, output_path)
    print(f"Stamped application ({int.from_bytes(manifest[8:12], 'little')} bytes, "
          f"CRC32 {int.from_bytes(manifest[12:16], 'little'):08X}).")


def validate(memory):
    if any(MANIFEST_ADDRESS + offset not in memory for offset in range(MANIFEST_SIZE)):
        raise ValueError("Application manifest is incomplete")
    raw = bytes(memory.get(MANIFEST_ADDRESS + offset, 0xFF)
                for offset in range(MANIFEST_SIZE))
    if raw[:4] != MANIFEST_MAGIC or raw[4] != MANIFEST_FORMAT:
        raise ValueError("Application manifest is missing or unsupported")
    if tuple(raw[5:7]) != PROTOCOL_VERSION:
        raise ValueError("Application protocol version is unsupported")
    length = int.from_bytes(raw[8:12], "little")
    expected = int.from_bytes(raw[12:16], "little")
    if length < 2 or length > MANIFEST_ADDRESS:
        raise ValueError("Application manifest contains an invalid length")
    if any(memory.get(address, 0xFF) != 0xFF
           for address in range(length, MANIFEST_ADDRESS)):
        raise ValueError("Application contains data outside its CRC-protected length")
    actual = binascii.crc32(image_bytes(memory, length)) & 0xFFFFFFFF
    if actual != expected:
        raise ValueError(f"Application CRC32 mismatch: expected {expected:08X}, got {actual:08X}")
    return length, actual


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: application_image.py INPUT.hex OUTPUT.hex")
    stamp(Path(sys.argv[1]), Path(sys.argv[2]))
