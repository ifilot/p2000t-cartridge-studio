"""Build 16 independent 16-KiB P2000T SLOT1 ROMs and one 256-KiB image."""
import hashlib
import json
import subprocess
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
REPOSITORY_ROOT = SOURCE.parents[1]
OUTPUT = REPOSITORY_ROOT / 'firmware' / 'build' / 'bank-test'
SIZE = 16384


def record(text, colour=7, double=False):
    prefix = bytes([colour, 13]) if double else bytes([colour])
    data = prefix + text.encode('ascii')
    assert len(data) <= 36
    return data.ljust(36, b' ')


def build():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    template = OUTPUT / 'template.bin'
    subprocess.run(['z80asm', '-o', str(template), str(SOURCE / 'bank-test.asm')], check=True)
    code = template.read_bytes()
    assert len(code) < 0x200
    assert code[0] == 0x5c and code[1:5] == bytes(4)
    assert all(code[offset] == 0xc3 for offset in (0x10, 0x13, 0x16))
    banks, manifest = [], []
    for bank in range(16):
        rom = bytearray(b'\xff' * SIZE)
        rom[:len(code)] = code
        rom[5:16] = f'BANK{bank:02d} TEST'.encode('ascii')
        messages = {
            0x200: record('P2000T SLOT1 BANK TEST', 6),
            0x240: record(f'BANK {bank:02d}  (00-15)', 7, True),
            0x280: record(f'A17 A16 A15 A14 = {bank:04b}', 3),
            0x340: record('CHANGE BANK, THEN PRESS SOFT RESET', 6),
        }
        for quarter, offset in enumerate((0x300, 0x1000, 0x2000, 0x3000)):
            messages[offset] = record(f'ROM QUARTER {quarter}: BANK {bank:02d}', 2)
        for offset, data in messages.items():
            rom[offset:offset + len(data)] = data
        assert len(rom) == SIZE
        path = OUTPUT / f'bank-{bank:02d}.bin'
        path.write_bytes(rom)
        manifest.append({'bank': bank, 'address_bits_A17_A14': f'{bank:04b}',
                         'image_offset': bank * SIZE, 'size': SIZE,
                         'sha256': hashlib.sha256(rom).hexdigest()})
        banks.append(rom)
    image = b''.join(banks)
    assert len(image) == 262144 and len({bytes(b) for b in banks}) == 16
    (OUTPUT / 'p2000t-bank-test-16x16k.bin').write_bytes(image)
    (OUTPUT / 'manifest.json').write_text(json.dumps({
        'image_size': len(image), 'sha256': hashlib.sha256(image).hexdigest(), 'banks': manifest
    }, indent=2) + '\n')
    print(f'Built {len(banks)} banks: {OUTPUT / "p2000t-bank-test-16x16k.bin"}')
    print(f'SHA256 {hashlib.sha256(image).hexdigest()}')


if __name__ == '__main__':
    build()
