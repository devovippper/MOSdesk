#!/bin/sh
# Renders every Posy cursor state through the REAL mosWM cursor loader and
# writes showcase images (sheet, in-context crops, animated GIFs).
# Needs: gcc, python3 with Pillow.   ./mosdesk/tools/host_preview/cursors.sh [outdir]
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/preview-out}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CF="-std=gnu17 -O1 -Wall -Wno-unused-function -iquote $HERE/stubs -I$ROOT -I$ROOT/mosdesk/libraries/ttrend/include"
for f in wm_tu settings_tu sdk_impl driver; do gcc $CF -c "$HERE/$f.c" -o "$WORK/$f.o"; done
gcc $CF -c "$ROOT/mosdesk/libraries/ttrend/src/ttrend.c" -o "$WORK/ttrend.o"
gcc "$WORK"/*.o -o "$WORK/harness"
S="$WORK/sandbox"
mkdir -p "$S/0:/programs/mosdesk/fonts" "$S/0:/programs/mosWM/assets/fonts" "$S/0:/programs/mosWM/assets/cursors"
cp "$ROOT"/mosdesk/fonts/*.otf "$ROOT/mosdesk/fonts/default.font" "$S/0:/programs/mosdesk/fonts/"
cp "$ROOT"/mosdesk/fonts/*.otf "$S/0:/programs/mosWM/assets/fonts/"
cp -r "$ROOT"/mosWM/assets/cursors/* "$S/0:/programs/mosWM/assets/cursors/"
mkdir -p "$WORK/dump" "$OUT"
(cd "$S" && "$WORK/harness" "$WORK/dump" cursors)
python3 "$HERE/cursor_showcase.py" "$WORK/dump" "$ROOT/mosWM/assets/cursors/Posy" "$OUT"
