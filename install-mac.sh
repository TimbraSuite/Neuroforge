#!/bin/bash
# Neuroforge installer for macOS.
#   curl -fsSL https://raw.githubusercontent.com/huuba91/neuroforge/main/install-mac.sh | bash
#
# Downloads the newest macOS build from GitHub Releases and installs, for the current user only:
#   VST3  -> ~/Library/Audio/Plug-Ins/VST3
#   AU    -> ~/Library/Audio/Plug-Ins/Components   (Logic Pro, GarageBand)
#   CLAP  -> ~/Library/Audio/Plug-Ins/CLAP
#   App   -> ~/Applications
# Files fetched with curl are not quarantined, so macOS does not block these un-notarized builds.
set -euo pipefail

REPO="huuba91/neuroforge"
PLUGINS="$HOME/Library/Audio/Plug-Ins"
APPS="$HOME/Applications"

echo "==> Neuroforge installer"
[[ "$(uname)" == "Darwin" ]] || { echo "This installer is for macOS."; exit 1; }

url=$(curl -fsSL "https://api.github.com/repos/$REPO/releases" \
      | grep -o '"browser_download_url": *"[^"]*-macos\.zip"' | head -n 1 | sed -E 's/.*"(https[^"]+)"/\1/')
[[ -n "$url" ]] || { echo "Could not find a macOS build on https://github.com/$REPO/releases"; exit 1; }
echo "==> Downloading $(basename "$url")"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fL --progress-bar "$url" -o "$tmp/neuroforge.zip"
ditto -x -k "$tmp/neuroforge.zip" "$tmp/x"

install_item() { # <name in zip> <destination dir>
    local src="$tmp/x/$1" dst="$2/$1"
    [[ -e "$src" ]] || { echo "    (skipped $1: not in this build)"; return; }
    mkdir -p "$2"
    rm -rf "$dst"
    cp -R "$src" "$dst"
    xattr -dr com.apple.quarantine "$dst" 2>/dev/null || true
    echo "    $1 -> $2"
}

echo "==> Installing"
install_item "Neuroforge.vst3"      "$PLUGINS/VST3"
install_item "Neuroforge.component" "$PLUGINS/Components"
install_item "Neuroforge.clap"      "$PLUGINS/CLAP"
install_item "Neuroforge.app"       "$APPS"

# Make Logic/GarageBand notice the new Audio Unit
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "==> Done. Restart your DAW and rescan plug-ins; Neuroforge appears as an instrument."
echo "    Manual: https://github.com/$REPO/blob/main/docs/MANUAL.md"
