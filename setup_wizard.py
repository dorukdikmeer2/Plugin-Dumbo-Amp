#!/usr/bin/env python3
"""
DumbDock Amp - setup wizard (build + install + verify)

Walks you through everything needed to get the VST3 into Reaper:
  1. checks your tools (Python, CMake, git, C++ compiler, Linux dev libraries)
  2. finds / downloads JUCE 7.0.12
  3. configures and builds the plugin (Release)
  4. installs the .vst3 into your VST3 folder
  5. verifies the installed file and tells you how to load it in Reaper

Usage
  python3 setup_wizard.py                 interactive
  python3 setup_wizard.py --yes           no questions, sensible defaults
  python3 setup_wizard.py --check         only check prerequisites
  python3 setup_wizard.py --dry-run       show what would be run
  python3 setup_wizard.py --juce-dir PATH use an existing JUCE checkout
  python3 setup_wizard.py --standalone    also build the Standalone app
  python3 setup_wizard.py --skip-build    only (re)install an already built plugin

Nothing is installed outside the VST3 folder and the ./build directory.
"""
import argparse
import os
import platform
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PLUGIN_NAME = "DumbDock Amp"
BUNDLE = PLUGIN_NAME + ".vst3"
SYSTEM = platform.system()          # Windows / Darwin / Linux
LOG = ROOT / "setup_wizard.log"

USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def c(txt, code):
    return f"\033[{code}m{txt}\033[0m" if USE_COLOR else txt


OK, WARN, BAD = c("[ OK ]", "32"), c("[WARN]", "33"), c("[FAIL]", "31")


def log(msg):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(msg + "\n")


def say(msg=""):
    print(msg)
    log(msg)


def step(n, title):
    say("")
    say(c(f"== Step {n}: {title}", "1;36"))


def ask(question, default=True, assume_yes=False):
    if assume_yes or not sys.stdin.isatty():
        return default
    suffix = " [Y/n] " if default else " [y/N] "
    while True:
        a = input(question + suffix).strip().lower()
        if not a:
            return default
        if a in ("y", "yes"):
            return True
        if a in ("n", "no"):
            return False


def run(cmd, env=None, cwd=None, dry=False, stream=True):
    say("  $ " + " ".join(str(x) for x in cmd))
    if dry:
        return 0
    e = os.environ.copy()
    if env:
        e.update(env)
    p = subprocess.Popen([str(x) for x in cmd], cwd=cwd, env=e, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors="replace")
    last = time.time()
    for line in p.stdout:
        log(line.rstrip())
        if stream and (line.startswith("[") or "error" in line.lower() or time.time() - last > 5):
            print("    " + line.rstrip()[:150])
            last = time.time()
    return p.wait()


def version_of(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout
        return out.strip().splitlines()[0] if out.strip() else ""
    except Exception:
        return ""


# ----------------------------------------------------------------------------
def vst3_dirs():
    """(preferred, fallback) VST3 folders for this OS."""
    home = Path.home()
    if SYSTEM == "Windows":
        sysdir = Path(os.environ.get("COMMONPROGRAMFILES", r"C:\Program Files\Common Files")) / "VST3"
        user = Path(os.environ.get("LOCALAPPDATA", home / "AppData/Local")) / "Programs" / "Common" / "VST3"
        return sysdir, user
    if SYSTEM == "Darwin":
        return home / "Library/Audio/Plug-Ins/VST3", Path("/Library/Audio/Plug-Ins/VST3")
    return home / ".vst3", Path("/usr/lib/vst3")


def check_prereqs():
    problems = []
    py = sys.version_info
    (OK if py >= (3, 8) else BAD)
    say(f"  {OK if py >= (3, 8) else BAD} Python {py.major}.{py.minor}.{py.micro}")
    if py < (3, 8):
        problems.append("Python 3.8 or newer is required")

    cm = shutil.which("cmake")
    if cm:
        v = version_of([cm, "--version"])
        try:
            nums = [int(x) for x in v.split()[2].split("-")[0].split(".")[:2]]
            good = nums >= [3, 22]
        except Exception:
            good = True
        say(f"  {OK if good else BAD} {v}")
        if not good:
            problems.append("CMake 3.22+ is required (https://cmake.org/download/)")
    else:
        say(f"  {BAD} CMake not found")
        problems.append("Install CMake 3.22+ (https://cmake.org/download/)")

    git = shutil.which("git")
    say(f"  {OK if git else WARN} git {'found' if git else 'not found (only needed to download JUCE automatically)'}")

    if SYSTEM == "Windows":
        cl = shutil.which("cl")
        vsw = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        found = bool(cl) or vsw.exists()
        say(f"  {OK if found else BAD} Visual Studio 2019/2022 C++ build tools {'found' if found else 'not found'}")
        if not found:
            problems.append("Install 'Visual Studio Build Tools' with the 'Desktop development with C++' workload")
    elif SYSTEM == "Darwin":
        xc = subprocess.run(["xcode-select", "-p"], capture_output=True).returncode == 0
        say(f"  {OK if xc else BAD} Xcode command line tools {'found' if xc else 'not found'}")
        if not xc:
            problems.append("Run: xcode-select --install")
    else:
        cxx = shutil.which("g++") or shutil.which("clang++")
        say(f"  {OK if cxx else BAD} C++ compiler {'(' + Path(cxx).name + ')' if cxx else 'not found'}")
        if not cxx:
            problems.append("sudo apt install build-essential")
        if shutil.which("pkg-config"):
            missing = [p for p in ("alsa", "freetype2", "x11", "xrandr", "xinerama", "xcursor", "gl")
                       if subprocess.run(["pkg-config", "--exists", p], capture_output=True).returncode != 0]
            if missing:
                say(f"  {WARN} missing dev packages: {', '.join(missing)}")
                problems.append("sudo apt install libasound2-dev libfreetype-dev libx11-dev libxrandr-dev "
                                "libxinerama-dev libxcursor-dev libgl1-mesa-dev pkg-config")
            else:
                say(f"  {OK} Linux development libraries found")
        else:
            say(f"  {WARN} pkg-config not found - cannot check Linux dev libraries")
    return problems


def can_reach_github():
    try:
        socket.create_connection(("github.com", 443), timeout=5).close()
        return True
    except OSError:
        return False


def find_built_bundle(build_dir):
    hits = [p for p in build_dir.rglob(BUNDLE) if p.is_dir() or p.is_file()]
    hits.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return hits[0] if hits else None


def binary_inside(bundle):
    if bundle.is_file():
        return bundle
    for sub in ("Contents/x86_64-linux", "Contents/aarch64-linux", "Contents/x86_64-win", "Contents/arm64-win",
                "Contents/MacOS"):
        d = bundle / sub
        if d.exists():
            for f in d.iterdir():
                if f.suffix in (".so", ".vst3", "") and f.is_file():
                    return f
    return None


def verify_installed(bundle):
    ok = True
    exe = binary_inside(bundle)
    if exe is None or exe.stat().st_size < 100_000:
        say(f"  {BAD} plugin binary missing or suspiciously small inside {bundle}")
        return False
    say(f"  {OK} binary: {exe.name} ({exe.stat().st_size // 1024} KB)")
    if SYSTEM == "Linux" and shutil.which("nm"):
        out = subprocess.run(["nm", "-D", str(exe)], capture_output=True, text=True).stdout
        for sym in ("GetPluginFactory", "ModuleEntry"):
            has = sym in out
            say(f"  {OK if has else BAD} exports {sym}")
            ok &= has
    return ok


# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="DumbDock Amp setup wizard")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--juce-dir")
    ap.add_argument("--standalone", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--build-dir", default=str(ROOT / "build"))
    ap.add_argument("--install-dir", help="override the VST3 install folder")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 0))
    a = ap.parse_args()

    LOG.write_text(f"setup_wizard {time.ctime()} on {platform.platform()}\n", encoding="utf-8")
    say(c("DumbDock Amp - setup wizard", "1;32"))
    say("Builds the VST3 from source and installs it for Reaper. Log: " + str(LOG))

    # ---- 1 prerequisites
    step(1, "Checking your system")
    problems = check_prereqs()
    if problems:
        say("")
        say("Things to fix first:")
        for p in problems:
            say("   - " + p)
        if a.check or not a.skip_build:
            return 2
    if a.check:
        say(f"\n{OK} Everything needed is installed.")
        return 0

    build_dir = Path(a.build_dir)
    juce_dir = a.juce_dir or os.environ.get("JUCE_DIR")
    install_pref, install_alt = vst3_dirs()
    target_dir = Path(a.install_dir) if a.install_dir else install_pref

    if not a.skip_build:
        # ---- 2 JUCE
        step(2, "Locating JUCE 7.0.12")
        if juce_dir and (Path(juce_dir) / "CMakeLists.txt").exists():
            say(f"  {OK} using JUCE at {juce_dir}")
        elif (ROOT / "JUCE" / "CMakeLists.txt").exists():
            say(f"  {OK} using ./JUCE")
            juce_dir = None
        else:
            if not shutil.which("git"):
                say(f"  {BAD} JUCE is not present and git is missing. Install git, or download JUCE 7.0.12 from "
                    "https://github.com/juce-framework/JUCE/releases and pass --juce-dir PATH")
                return 2
            if not can_reach_github():
                say(f"  {BAD} cannot reach github.com. Download JUCE 7.0.12 manually and pass --juce-dir PATH")
                return 2
            say(f"  {OK} JUCE will be downloaded automatically by CMake (about 100 MB, one time)")
            juce_dir = None

        # ---- 3 build
        step(3, "Building (this takes 3-10 minutes the first time)")
        cfg = ["cmake", "-S", ROOT, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release",
               f"-DDUMBDOCK_STANDALONE={'ON' if a.standalone else 'OFF'}"]
        if juce_dir:
            cfg.append(f"-DJUCE_DIR={juce_dir}")
        if SYSTEM == "Darwin":
            cfg.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13")
        if run(cfg, dry=a.dry_run) != 0:
            say(f"  {BAD} configure failed - see {LOG}")
            return 1
        bld = ["cmake", "--build", build_dir, "--config", "Release", "--target", "DumbDockAmp_VST3", f"-j{a.jobs}"]
        if run(bld, dry=a.dry_run) != 0:
            say(f"  {BAD} build failed - see {LOG} (the last 'error' lines show the cause)")
            return 1
        if a.standalone:
            run(["cmake", "--build", build_dir, "--config", "Release", "--target", "DumbDockAmp_Standalone", f"-j{a.jobs}"],
                dry=a.dry_run)
        if a.dry_run:
            say("\n(dry run - nothing was built or installed)")
            return 0
    else:
        step(2, "Skipping build (--skip-build)")

    # ---- 4 install
    step(4, "Installing the plugin")
    built = find_built_bundle(build_dir)
    if built is None:
        say(f"  {BAD} no {BUNDLE} found under {build_dir}")
        return 1
    say(f"  built plugin: {built}")
    dest_root = target_dir
    for attempt in (dest_root, install_alt if not a.install_dir else None):
        if attempt is None:
            break
        try:
            attempt.mkdir(parents=True, exist_ok=True)
            dest = attempt / BUNDLE
            if dest.exists():
                (shutil.rmtree if dest.is_dir() else os.remove)(dest)
            (shutil.copytree if built.is_dir() else shutil.copy2)(built, dest)
            dest_root = attempt
            say(f"  {OK} installed to {dest}")
            break
        except PermissionError:
            say(f"  {WARN} no permission for {attempt} (try an administrator / sudo shell, or --install-dir)")
            dest = None
    else:
        say(f"  {BAD} could not install - copy {built} into your VST3 folder manually")
        return 1

    # ---- 5 verify
    step(5, "Verifying the installed plugin")
    if not verify_installed(dest_root / BUNDLE):
        return 1

    say("")
    say(c("All done. Load it in Reaper:", "1;32"))
    say("  1. Options > Preferences > Plug-ins > VST: make sure this folder is listed in 'VST plug-in paths':")
    say(f"       {dest_root}")
    say("  2. Click 'Re-scan' (or 'Clear cache and re-scan').")
    say("  3. On a track: FX > Add > VST3: 'DumbDock Amp' (DorukAudio). Arm the track + enable input monitoring.")
    say("  4. A short welcome overview of the amps and options shows when the plugin window opens.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\ncancelled")
        sys.exit(130)
