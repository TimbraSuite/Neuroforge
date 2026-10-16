"""Package a Neuroforge build into a release zip.

  python tools/package.py --platform windows|macos|linux --version 0.3.0 --artefacts build/Neuroforge_artefacts/Release --out dist

Unix file modes are preserved in the zip, so macOS bundles keep their executable bits.
"""
import argparse
import os
import stat
import zipfile

REPO = "https://github.com/TimbraSuite/neuroforge"

WINDOWS_INSTALL = """NEUROFORGE {version}-beta (Windows x64)
====================================

VST3  (Ableton Live, Cubase, FL Studio, Studio One, Reaper, ...)
  Copy the folder "Neuroforge.vst3" to:  C:\\Program Files\\Common Files\\VST3\\

CLAP  (Bitwig, Reaper, FL Studio, Studio One, ...)
  Copy the file "Neuroforge.clap" to:    C:\\Program Files\\Common Files\\CLAP\\

Then rescan plug-ins in your DAW. Neuroforge appears as an instrument.
(Ableton: Settings > Plug-ins > Use VST3 Plug-in System Folders > Rescan)

Standalone
  Run Neuroforge.exe (choose your audio/MIDI device under Options).

Play basslines around F1-F2. Press FORGE for a new patch, MUTATE to vary it, UNDO to go back.
Manual & tutorials: {repo}/blob/main/docs/MANUAL.md
Bugs & feedback:    {repo}/issues

This is a beta and the binaries are not code-signed; Windows SmartScreen may warn.
Licensed under the GNU AGPLv3 (see LICENSE.txt). Source: {repo}
Third-party licenses are in the "third-party-licenses" folder.
"""

MACOS_INSTALL = """NEUROFORGE {version}-beta (macOS 10.15+, Apple Silicon + Intel)
=================================================================

EASIEST: one line in Terminal installs everything and avoids the macOS warnings:

  curl -fsSL https://raw.githubusercontent.com/TimbraSuite/neuroforge/main/install-mac.sh | bash

MANUAL INSTALL (from this zip)
  Copy each format to its folder (in Finder: Go > Go to Folder...):
    Neuroforge.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/        (Ableton, Bitwig, Reaper, FL, Cubase, ...)
    Neuroforge.component  ->  ~/Library/Audio/Plug-Ins/Components/  (Logic Pro, GarageBand)
    Neuroforge.clap       ->  ~/Library/Audio/Plug-Ins/CLAP/        (Bitwig, Reaper, ...)
    Neuroforge.app        ->  /Applications or ~/Applications       (standalone)

  These builds are not notarized by Apple, so macOS blocks them after a browser download.
  Run this once in Terminal after copying, then restart your DAW:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/*/Neuroforge.* ~/Applications/Neuroforge.app /Applications/Neuroforge.app 2>/dev/null; killall -9 AudioComponentRegistrar 2>/dev/null; true

Play basslines around F1-F2. Press FORGE for a new patch, MUTATE to vary it, UNDO to go back.
Manual & tutorials: {repo}/blob/main/docs/MANUAL.md
Bugs & feedback:    {repo}/issues

Licensed under the GNU AGPLv3 (see LICENSE.txt). Source: {repo}
Third-party licenses are in the "third-party-licenses" folder.
"""

LINUX_INSTALL = """NEUROFORGE {version}-beta (Linux x86_64)
=======================================

VST3  (Bitwig, Reaper, Ardour, ...)
  Copy the folder "Neuroforge.vst3" to:  ~/.vst3/

CLAP  (Bitwig, Reaper, ...)
  Copy the file "Neuroforge.clap" to:    ~/.clap/

  mkdir -p ~/.vst3 ~/.clap
  cp -r Neuroforge.vst3 ~/.vst3/ && cp Neuroforge.clap ~/.clap/

Then rescan plug-ins in your DAW. Neuroforge appears as an instrument.

Standalone
  Run ./Neuroforge from anywhere (choose your audio/MIDI device under Options).
  If the executable bit was lost on extraction:  chmod +x Neuroforge

Runtime libraries (most desktop distros already have them): ALSA (libasound2), FreeType (libfreetype6),
Fontconfig, and X11 (libx11, libxext, libxrandr, libxinerama, libxcursor). On Debian/Ubuntu:

  sudo apt install libasound2 libfreetype6 libfontconfig1 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1

User presets are stored in ~/.config/Neuroforge/Presets.

Play basslines around F1-F2. Press FORGE for a new patch, MUTATE to vary it, UNDO to go back.
Manual & tutorials: {repo}/blob/main/docs/MANUAL.md
Bugs & feedback:    {repo}/issues

The Linux build is made by CI and is untested on real Linux hosts (Bitwig, Reaper, Ardour). Testers
and bug reports are very welcome.
Licensed under the GNU AGPLv3 (see LICENSE.txt). Source: {repo}
Third-party licenses are in the "third-party-licenses" folder.
"""


def add_path(z: zipfile.ZipFile, src: str, arc: str) -> None:
    """Add a file or directory tree, preserving unix permissions and symlinks."""
    if os.path.isdir(src) and not os.path.islink(src):
        for name in sorted(os.listdir(src)):
            add_path(z, os.path.join(src, name), f"{arc}/{name}")
        return
    st = os.lstat(src)
    info = zipfile.ZipInfo(arc)
    info.compress_type = zipfile.ZIP_DEFLATED
    if os.path.islink(src):
        info.external_attr = (stat.S_IFLNK | 0o755) << 16
        z.writestr(info, os.readlink(src))
    else:
        info.external_attr = (stat.S_IFREG | (st.st_mode & 0o777 or 0o644)) << 16
        with open(src, "rb") as f:
            z.writestr(info, f.read())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", choices=["windows", "macos", "linux"], required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--artefacts", required=True, help="JUCE artefacts dir for the Release config")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--cje", default="libs/clap-juce-extensions")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    suffix = {"windows": "win64", "macos": "macos", "linux": "linux-x86_64"}[a.platform]
    out = os.path.join(a.out, f"Neuroforge-{a.version}-beta-{suffix}.zip")
    art = a.artefacts

    if a.platform == "windows":
        items = [("VST3/Neuroforge.vst3", "Neuroforge.vst3"), ("CLAP/Neuroforge.clap", "Neuroforge.clap"),
                 ("Standalone/Neuroforge.exe", "Neuroforge.exe")]
        install = WINDOWS_INSTALL.format(version=a.version, repo=REPO).replace("\n", "\r\n")
    elif a.platform == "linux":
        items = [("VST3/Neuroforge.vst3", "Neuroforge.vst3"), ("CLAP/Neuroforge.clap", "Neuroforge.clap"),
                 ("Standalone/Neuroforge", "Neuroforge")]
        install = LINUX_INSTALL.format(version=a.version, repo=REPO)
    else:
        items = [("VST3/Neuroforge.vst3", "Neuroforge.vst3"), ("AU/Neuroforge.component", "Neuroforge.component"),
                 ("CLAP/Neuroforge.clap", "Neuroforge.clap"), ("Standalone/Neuroforge.app", "Neuroforge.app")]
        install = MACOS_INSTALL.format(version=a.version, repo=REPO)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in items:
            path = os.path.join(art, src)
            if not os.path.exists(path):
                raise SystemExit(f"missing build output: {path}")
            add_path(z, path, arc)
        info = zipfile.ZipInfo("INSTALL.txt")
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (stat.S_IFREG | 0o644) << 16
        z.writestr(info, install)
        z.write("LICENSE", "LICENSE.txt")
        z.write("assets/fonts/LICENSE-Apache-2.0.txt", "third-party-licenses/RobotoCondensed-LICENSE-Apache-2.0.txt")
        z.write(f"{a.cje}/LICENSE.md", "third-party-licenses/clap-juce-extensions-LICENSE.txt")
        z.write(f"{a.cje}/clap-libs/clap/LICENSE", "third-party-licenses/clap-LICENSE.txt")
        z.write(f"{a.cje}/clap-libs/clap-helpers/LICENSE", "third-party-licenses/clap-helpers-LICENSE.txt")
    print(out, os.path.getsize(out) // 1024, "KB")


if __name__ == "__main__":
    main()
