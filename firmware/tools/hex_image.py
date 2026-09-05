"""Validate address regions and combine AVR application/bootloader Intel HEX."""
import sys
from pathlib import Path


def read_hex(path):
    result, base, ended = {}, 0, False
    for line in Path(path).read_text().splitlines():
        if not line:
            continue
        if ended or not line.startswith(':'):
            raise ValueError('Bad HEX framing')
        row = bytes.fromhex(line[1:])
        if len(row) < 5 or len(row) != row[0] + 5 or sum(row) & 255:
            raise ValueError('Bad HEX length/checksum')
        addr, kind, data = int.from_bytes(row[1:3], 'big'), row[3], row[4:-1]
        if kind == 0:
            for offset, value in enumerate(data):
                absolute = base + addr + offset
                if absolute in result:
                    raise ValueError('Overlapping HEX records')
                result[absolute] = value
        elif kind == 1:
            ended = True
        elif kind == 2 and len(data) == 2:
            base = int.from_bytes(data, 'big') << 4
        elif kind == 4 and len(data) == 2:
            base = int.from_bytes(data, 'big') << 16
        elif kind in (3, 5) and len(data) == 4:
            pass  # start-address metadata; physical flash addresses are in data records
        else:
            raise ValueError(f'Unsupported HEX record {kind}')
    if not ended or not result:
        raise ValueError('Missing EOF or empty image')
    return result


def combine(app_path, boot_path, output):
    app, boot = read_hex(app_path), read_hex(boot_path)
    if min(app) != 0 or max(app) >= 0x7000:
        raise ValueError('Application must fit below bootloader at 0x7000')
    if min(boot) != 0x7000 or max(boot) >= 0x8000:
        raise ValueError('Bootloader must fit within 0x7000..0x7fff')
    memory = app | boot
    lines = []
    for address in range(0, 0x8000, 16):
        if not any(a in memory for a in range(address, address + 16)):
            continue
        data = bytes(memory.get(a, 255) for a in range(address, address + 16))
        row = bytes([16, address >> 8, address & 255, 0]) + data
        lines.append(':' + (row + bytes([-sum(row) & 255])).hex().upper())
    Path(output).write_text('\n'.join(lines + [':00000001FF']) + '\n')
    print(f'Validated app ({len(app)} bytes) and bootloader ({len(boot)} bytes).')


if __name__ == '__main__':
    combine(*sys.argv[1:])
