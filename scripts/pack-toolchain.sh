#!/usr/bin/env bash
set -euo pipefail

WINE_PREFIX="${1:-${WINEPREFIX:-$HOME/.wine}}"
OUTPUT_DIR="${2:-$(pwd)/docker}"
ARCHIVE_NAME="vc6-toolchain.tar.gz"

if [ ! -d "$WINE_PREFIX" ]; then
    echo "Error: Wine prefix not found at '$WINE_PREFIX'" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
TEMP_DIR=$(mktemp -d -t vc6-pack-XXXXXX)
trap 'rm -rf "$TEMP_DIR"' EXIT

DRIVE_C="$WINE_PREFIX/drive_c"
VS_DIR="$DRIVE_C/Program Files/Microsoft Visual Studio"
SDK_DIR="$DRIVE_C/Program Files/Microsoft SDK"
TARGET_PF="$TEMP_DIR/drive_c/Program Files"

mkdir -p "$TARGET_PF/Microsoft Visual Studio/Common" \
         "$TARGET_PF/Microsoft Visual Studio/VC98" \
         "$TARGET_PF/Microsoft SDK"

cp -a "$VS_DIR/Common/MSDev98" "$TARGET_PF/Microsoft Visual Studio/Common/"
[ -d "$VS_DIR/Common/Tools" ] && cp -a "$VS_DIR/Common/Tools" "$TARGET_PF/Microsoft Visual Studio/Common/"

cp -a "$VS_DIR/VC98/Bin" "$TARGET_PF/Microsoft Visual Studio/VC98/"
cp -a "$VS_DIR/VC98/Include" "$TARGET_PF/Microsoft Visual Studio/VC98/"
cp -a "$VS_DIR/VC98/Lib" "$TARGET_PF/Microsoft Visual Studio/VC98/"
if [ -d "$VS_DIR/VC98/MFC" ]; then
    mkdir -p "$TARGET_PF/Microsoft Visual Studio/VC98/MFC"
    [ -d "$VS_DIR/VC98/MFC/Include" ] && cp -a "$VS_DIR/VC98/MFC/Include" "$TARGET_PF/Microsoft Visual Studio/VC98/MFC/"
    [ -d "$VS_DIR/VC98/MFC/Lib" ] && cp -a "$VS_DIR/VC98/MFC/Lib" "$TARGET_PF/Microsoft Visual Studio/VC98/MFC/"
fi
if [ -d "$VS_DIR/VC98/ATL" ]; then
    mkdir -p "$TARGET_PF/Microsoft Visual Studio/VC98/ATL"
    [ -d "$VS_DIR/VC98/ATL/Include" ] && cp -a "$VS_DIR/VC98/ATL/Include" "$TARGET_PF/Microsoft Visual Studio/VC98/ATL/"
fi

cp -a "$SDK_DIR/Include" "$TARGET_PF/Microsoft SDK/"
cp -a "$SDK_DIR/lib" "$TARGET_PF/Microsoft SDK/"
if [ -d "$SDK_DIR/Bin" ]; then
    mkdir -p "$TARGET_PF/Microsoft SDK/Bin"
    rsync -a --exclude="Win64" --exclude="Debug" "$SDK_DIR/Bin/" "$TARGET_PF/Microsoft SDK/Bin/"
fi

mkdir -p "$TEMP_DIR/drive_c/windows/system32"
SYS32="$DRIVE_C/windows/system32"
for dll in mfc42.dll mfc42u.dll mfc42d.dll msvcirt.dll msvcp60.dll msvcrt.dll msvcrtd.dll riched20.dll; do
    if [ -f "$SYS32/$dll" ]; then
        cp -a "$SYS32/$dll" "$TEMP_DIR/drive_c/windows/system32/"
        cp -a "$SYS32/$dll" "$TARGET_PF/Microsoft Visual Studio/Common/MSDev98/Bin/"
    fi
done

mkdir -p "$TEMP_DIR/wine_reg"
cp "$WINE_PREFIX/system.reg" "$TEMP_DIR/wine_reg/"
cp "$WINE_PREFIX/userdef.reg" "$TEMP_DIR/wine_reg/"

sed -E \
    -e 's|[A-Za-z]:\\\\.+libs\\\\|C:\\\\libs\\\\|g' \
    -e 's|users\\\\[a-zA-Z0-9_-]+|users\\\\root|g' \
    "$WINE_PREFIX/user.reg" > "$TEMP_DIR/wine_reg/user.reg"

tar -czf "$OUTPUT_DIR/$ARCHIVE_NAME" -C "$TEMP_DIR" drive_c wine_reg
