#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent
BUILD_DIR    = PROJECT_ROOT / 'build'
SOURCES      = ['main.c', 'gcm.c', 'ui.c', 'applog.c', 'lv2patch.c', 'sacd.c']
OUT_ELF      = BUILD_DIR / 'sacd_flag_tool.elf'
OUT_SELF     = BUILD_DIR / 'sacd_flag_tool.self'


def _normalize_path(s: str) -> str:
    if len(s) >= 3 and s[0] == '/' and s[2] == '/' and s[1].isalpha():
        return s[1] + ':/' + s[3:]
    return s

CELL_SDK    = Path(_normalize_path(os.environ.get('CELL_SDK', r'C:\usr\local\cell')))
CELL_HOST   = CELL_SDK / 'host-win32'
PPU_CC      = CELL_HOST / 'ppu' / 'bin' / 'ppu-lv2-gcc.exe'
PPU_TARGET  = CELL_SDK / 'target' / 'ppu'

PPU_CFLAGS  = ['-W', '-Wall', '-O2', '-g', '-m64']
DBGFONT_LIB = PPU_TARGET / 'lib' / 'libdbgfont_gcm.a'
PPU_LIBS    = [
    '-lgcm_cmdasm', '-lgcm_sys_stub', '-lsysutil_stub',
    '-lsysmodule_stub', '-lio_stub', '-lfs_stub', '-lm',
]

def run(cmd, cwd=None) -> None:
    scmd = [str(c) for c in cmd]
    echo = ' '.join(Path(a).name if os.path.isabs(a) else a for a in scmd)
    print(f'  $ {echo}', flush=True)
    subprocess.run(scmd, cwd=cwd, check=True)

def find_make_fself():
    for name in ('make_fself.exe', 'make_fself_npdrm.exe'):
        p = CELL_HOST / 'bin' / name
        if p.exists():
            return p
    return None

def compile_elf() -> Path:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    if not PPU_CC.exists():
        sys.exit(f'[!] ppu-lv2-gcc not found at {PPU_CC}')
    missing = [s for s in SOURCES if not (PROJECT_ROOT / s).exists()]
    if missing:
        sys.exit(f'[!] missing source file(s): {", ".join(missing)}')
    if not DBGFONT_LIB.exists():
        sys.exit(f'[!] {DBGFONT_LIB} not found (needed for the on-screen UI)')
    print(f'[*] compiling {len(SOURCES)} source files -> {OUT_ELF.name}')
    run([
        PPU_CC, *PPU_CFLAGS,
        f'-I{PPU_TARGET / "include"}',
        f'-L{PPU_TARGET / "lib"}',
        '-o', OUT_ELF,
        *[PROJECT_ROOT / s for s in SOURCES],
        DBGFONT_LIB,
        *PPU_LIBS,
    ])
    return OUT_ELF

def sign_fself(elf_path: Path) -> Path:
    mk = find_make_fself()
    if not mk:
        sys.exit('[!] make_fself not found in host-win32/bin')
    print(f'[*] fake-signing {elf_path.name} -> {OUT_SELF.name}')
    run([mk, elf_path, OUT_SELF])
    return OUT_SELF

def clean() -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    print('[*] cleaned build/')

def main() -> int:
    ap = argparse.ArgumentParser(description='sacd_flag_tool build driver')
    ap.add_argument('mode', nargs='?', default='elf', choices=('elf', 'fself', 'clean'), help='elf: compile only (default); fself: fake-signed self; ' 'clean: remove build/')
    args = ap.parse_args()

    if args.mode == 'clean':
        clean()
        return 0

    elf = compile_elf()

    if args.mode == 'elf':
        print()
        print(f'[OK] ELF: {elf}')
        print('     Run make_pkg.py to wrap it into an installable .pkg.')
        return 0

    self_path = sign_fself(elf)

    print()
    print(f'[OK] ELF : {elf}')
    print(f'     SELF: {self_path}')
    print('     Run make_pkg.py --fself to wrap it into an installable .pkg.')
    return 0


if __name__ == '__main__':
    sys.exit(main())