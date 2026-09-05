"""Execute all banks with a pinned external Z80 emulator supplied by path."""
import argparse
import subprocess
from pathlib import Path
from build import OUTPUT, SOURCE, build

parser = argparse.ArgumentParser()
parser.add_argument('--emulator-dir', type=Path, required=True)
args = parser.parse_args()
revision = subprocess.check_output(['git', '-C', str(args.emulator_dir), 'rev-parse', 'HEAD'], text=True).strip()
assert revision == 'd64fe10a2274e5e40019b1086bf7d8990cbc5f23', revision
build()
exe = OUTPUT / 'test-z80'
subprocess.run(['gcc', '-std=c99', '-O2', '-Wall', '-Wextra', '-I', str(args.emulator_dir),
                str(SOURCE / 'test_z80.c'), str(args.emulator_dir / 'z80.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe), str(OUTPUT / 'p2000t-bank-test-16x16k.bin')], check=True)
