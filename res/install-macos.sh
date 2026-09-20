#!/bin/sh
# Installs Moonstruck on macOS without the "Apple could not verify" prompt.
#
#   curl -fsSL https://github.com/Shayanmh2/RoguelikeCardgame/releases/latest/download/install-macos.sh | sh
#
# Why this works without a paid Apple account: Gatekeeper only checks files
# that carry the quarantine flag, and only browsers and similar apps set that
# flag. A file fetched with curl never gets it. The app is ad-hoc signed, which
# is all Apple Silicon needs to run it, and it is a universal build, so the same
# app runs on Intel Macs too.
set -eu

URL="https://github.com/Shayanmh2/RoguelikeCardgame/releases/latest/download/Moonstruck-macos.zip"

# /Applications if it can be written without sudo, otherwise the user's own.
if [ -w /Applications ]; then
    DEST="/Applications"
else
    DEST="$HOME/Applications"
    mkdir -p "$DEST"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "Downloading Moonstruck..."
curl -fL --progress-bar "$URL" -o "$tmp/Moonstruck.zip"
ditto -x -k "$tmp/Moonstruck.zip" "$tmp"

app="$(find "$tmp" -maxdepth 3 -name 'Moonstruck.app' -type d | head -n 1)"
if [ -z "$app" ]; then
    echo "The download did not contain Moonstruck.app." >&2
    exit 1
fi

rm -rf "$DEST/Moonstruck.app"
ditto "$app" "$DEST/Moonstruck.app"
# Nothing should have set it, but clear it in case the zip came in some other way.
xattr -dr com.apple.quarantine "$DEST/Moonstruck.app" 2>/dev/null || true

echo "Installed to $DEST/Moonstruck.app"
open "$DEST/Moonstruck.app"
