#!/usr/bin/env bash
set -euo pipefail

TARGET_CONFIG="${1:-both}"

WINE_PREFIX="${WINEPREFIX:-$HOME/.wine}"
export WINEPREFIX="$WINE_PREFIX"
export WINEDEBUG="-all"

MSDEV="C:\\Program Files\\Microsoft Visual Studio\\Common\\MSDev98\\Bin\\MSDEV.EXE"

if [ -d "$WINE_PREFIX/drive_c" ]; then
    mkdir -p "$WINE_PREFIX/drive_c/windows/temp"
    for udir in "$WINE_PREFIX/drive_c/users"/*; do
        [ -d "$udir" ] && mkdir -p "$udir/AppData/Local/Temp" 2>/dev/null || true
    done
fi

if [ -z "${DISPLAY:-}" ] && command -v Xvfb >/dev/null 2>&1; then
    Xvfb :99 -screen 0 1024x768x16 >/dev/null 2>&1 &
    export DISPLAY=:99
    sleep 0.5
fi

sync_assets() {
    local dst="$1"
    mkdir -p "$dst"
    [ -f deps/unicows.dll ] && cp -u deps/unicows.dll "$dst/" 2>/dev/null || true
    [ -f deps/DCs.dat ] && cp -u deps/DCs.dat "$dst/" 2>/dev/null || true
    [ -f deps/emoji_categories.dat ] && cp -u deps/emoji_categories.dat "$dst/" 2>/dev/null || true
    [ -f deps/emoji_shortcodes.ini ] && cp -u deps/emoji_shortcodes.ini "$dst/" 2>/dev/null || true
    cp -u deps/help.* "$dst/" 2>/dev/null || true
    if [ -d deps/emojis ]; then
        mkdir -p "$dst/emojis"
        cp -ru deps/emojis/* "$dst/emojis/" 2>/dev/null || true
    fi
    if [ -d langs ]; then
        mkdir -p "$dst/langs"
        cp -u langs/*.ini "$dst/langs/" 2>/dev/null || true
    fi
}

build_config() {
    local cfg="$1"
    echo "=== Building Telegacy ($cfg) ==="
    mkdir -p "$cfg"
    wine "$MSDEV" telegacy.dsp /MAKE "telegacy - Win32 $cfg"
    sync_assets "$cfg"
    zip -q -r "${cfg}.zip" "$cfg" -x "$cfg/*.obj" "$cfg/*.sbr" "$cfg/*.idb" "$cfg/*.res" "$cfg/*.pch" "$cfg/*.map"
}

case "$TARGET_CONFIG" in
    Release|release) build_config "Release" ;;
    Debug|debug)     build_config "Debug" ;;
    both|ALL|all)    build_config "Release"; build_config "Debug" ;;
    *) echo "Usage: $0 [Release|Debug|both]"; exit 1 ;;
esac

sync_assets "Debug"
[ -d deps/dlls ] && mkdir -p dlls && cp -u deps/dlls/* dlls/ 2>/dev/null || true
[ -f LICENSE ] && cp -u LICENSE gpl-3.0.txt 2>/dev/null || true

if command -v makensis >/dev/null 2>&1; then
    echo "=== Building NSIS Installer ==="
    makensis -INPUTCHARSET CP1252 telegacy.nsi
fi
