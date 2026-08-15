#!/usr/bin/env python3
import sys
import os
import shutil
import subprocess
import argparse
import glob as globmod

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PS3PY_DIR = os.path.join(SCRIPT_DIR, "ps3py")
SCETOOL_DIR = os.path.join(SCRIPT_DIR, "scetool")
SCETOOL_EXE = os.path.join(SCETOOL_DIR, "scetool.exe")
BUILD_DIR = os.path.join(SCRIPT_DIR, "build")

PKG = {
    "elf":       os.path.join("build", "sacd_flag_tool.elf"),
    "out":       os.path.join("build", "sacd_flag_tool.pkg"),
    "contentid": "UP0001-SACDENABL_00-0000000000000000",
    "title_id":  "SACDENABL",
    "title":     "SACD flag tool",
}


def sign_elf_npdrm(elf_path, self_path, content_id):
    if not os.path.exists(SCETOOL_EXE):
        print(f"[!] scetool not found at {SCETOOL_EXE}")
        return False

    print(f"[*] Signing {os.path.basename(elf_path)} -> NPDRM SELF...")

    rel_elf = os.path.relpath(elf_path, SCETOOL_DIR)
    rel_self = os.path.relpath(self_path, SCETOOL_DIR)

    cmd = [
        SCETOOL_EXE,
        "--sce-type", "SELF",
        "--self-type", "NPDRM",
        "--skip-sections", "FALSE",
        "--compress-data", "TRUE",
        "--key-revision", "0A",
        "--self-auth-id", "1010000001000003",
        "--self-vendor-id", "01000002",
        "--self-fw-version", "0003005500000000",
        "--self-app-version", "0001000000000000",
        "--self-add-shdrs", "TRUE",
        "--np-license-type", "FREE",
        "--np-app-type", "EXEC",
        "--np-content-id", content_id,
        "--np-real-fname", "EBOOT.BIN",
        "--encrypt", rel_elf, rel_self
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=SCETOOL_DIR)

        if result.stdout:
            print(f"    {result.stdout.strip()}")
        if result.stderr:
            print(f"    {result.stderr.strip()}")
        if result.returncode != 0:
            print(f"    exit code: {result.returncode}")
            return False
        if not os.path.exists(self_path):
            print(f"[!] scetool ran but output file not created")
            return False
        print(f"    -> {self_path}")
        return True
    except FileNotFoundError:
        print(f"[!] Cannot execute scetool")
        return False


def ensure_pkgcrypt():
    existing = globmod.glob(os.path.join(PS3PY_DIR, "pkgcrypt.*"))
    existing = [f for f in existing if not f.endswith(".c")]
    if existing:
        return True

    print("[*] Building pkgcrypt C extension...")
    try:
        subprocess.check_call(
            [sys.executable, os.path.join(PS3PY_DIR, "setup.py"),
             "build_ext", "--inplace"],
            cwd=PS3PY_DIR
        )
        return True
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"[!] Failed to build pkgcrypt: {e}")
        return False


def build_sfo(output_path, title=None, title_id=None):
    sfo_xml = os.path.join(PS3PY_DIR, "sfo.xml")
    sfo_py = os.path.join(PS3PY_DIR, "sfo.py")

    if not os.path.exists(sfo_xml) or not os.path.exists(sfo_py):
        print(f"[!] Missing sfo.xml or sfo.py in ps3py/")
        return False

    cmd = [sys.executable, sfo_py]
    if title:
        cmd += ["--title", title]
    if title_id:
        cmd += ["--appid", title_id]
    cmd += ["--fromxml", sfo_xml, output_path]

    print(f"[*] Generating PARAM.SFO...")
    try:
        subprocess.check_call(cmd)
        return True
    except subprocess.CalledProcessError as e:
        print(f"[!] SFO generation failed: {e}")
        return False


def build_pkg(pkg_content_dir, content_id, output_pkg):
    pkg_py = os.path.join(PS3PY_DIR, "pkg.py")

    if not os.path.exists(pkg_py):
        print(f"[!] Missing {pkg_py}")
        return False

    print(f"[*] Building .pkg...")
    env = os.environ.copy()
    env["PYTHONPATH"] = PS3PY_DIR + os.pathsep + env.get("PYTHONPATH", "")

    try:
        subprocess.check_call([
            sys.executable, pkg_py,
            "--contentid", content_id,
            pkg_content_dir, output_pkg
        ], env=env, cwd=SCRIPT_DIR)
        return True
    except subprocess.CalledProcessError as e:
        print(f"[!] Package build failed: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Build the sacd_enable PS3 .pkg")
    parser.add_argument("--elf", default=None, help="Path to the PPU ELF")
    parser.add_argument("--fself", action="store_true", help="Use the existing fself next to the ELF instead of scetool signing")
    parser.add_argument("--out", default=None, help="Output .pkg filename")
    parser.add_argument("--contentid", default=None, help="Content ID for the package")
    args = parser.parse_args()

    elf_name   = args.elf       or PKG["elf"]
    out_name   = args.out       or PKG["out"]
    contentid  = args.contentid or PKG["contentid"]

    if args.fself:
        self_name = os.path.splitext(elf_name)[0] + ".self"
        self_path = os.path.join(SCRIPT_DIR, self_name)
        if not os.path.exists(self_path):
            print(f"[!] Cannot find {self_path}")
            sys.exit(1)
        npdrm_self = self_path
        intermediate_self = False
        print(f"[*] Using fake-signed {self_name}")
    else:
        elf_path = os.path.join(SCRIPT_DIR, elf_name)
        if not os.path.exists(elf_path):
            print(f"[!] Cannot find {elf_path}")
            print("    Build the project first to generate the ELF.")
            sys.exit(1)

        os.makedirs(BUILD_DIR, exist_ok=True)
        npdrm_self = os.path.join(BUILD_DIR, "sacd_flag_tool_npdrm.self")
        if not sign_elf_npdrm(elf_path, npdrm_self, contentid):
            print("[!] NPDRM signing failed.")
            print("    Check that scetool/data/keys and curves are present.")
            print("    Use --fself flag to fall back to fake-signing.")
            sys.exit(1)
        intermediate_self = True

    if not ensure_pkgcrypt():
        print("[!] Cannot proceed without pkgcrypt.")
        sys.exit(1)

    pkg_dir = os.path.join(SCRIPT_DIR, "pkg_staging")
    usrdir = os.path.join(pkg_dir, "USRDIR")
    os.makedirs(usrdir, exist_ok=True)

    eboot_path = os.path.join(usrdir, "EBOOT.BIN")
    print(f"[*] Staging EBOOT.BIN")
    shutil.copy2(npdrm_self, eboot_path)

    sfo_path = os.path.join(pkg_dir, "PARAM.SFO")
    if not build_sfo(sfo_path, title=PKG["title"], title_id=PKG["title_id"]):
        sys.exit(1)

    icon_src = os.path.join(PS3PY_DIR, "ICON0.PNG")
    if os.path.exists(icon_src):
        shutil.copy2(icon_src, os.path.join(pkg_dir, "ICON0.PNG"))
        print(f"[*] Using ICON0.PNG")

    out_pkg = os.path.join(SCRIPT_DIR, out_name)
    if not build_pkg(pkg_dir + "/", contentid, out_pkg):
        sys.exit(1)

    shutil.rmtree(pkg_dir, ignore_errors=True)
    if intermediate_self and os.path.exists(npdrm_self):
        os.remove(npdrm_self)

    pkg_size = os.path.getsize(out_pkg)
    mode = "fake-signed" if args.fself else "NPDRM"
    print(f"\n[OK] {out_name} ({pkg_size / 1024:.0f} KB) [{mode}]")
    print(f"     Content ID: {contentid}")
    print(f"     TITLE_ID:   {PKG['title_id']}  TITLE: {PKG['title']}")

if __name__ == "__main__":
    main()