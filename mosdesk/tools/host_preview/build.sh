#!/bin/sh
# Renders the REAL mosWM compositor + MOSdesk Settings client on the host, with
# the Minimal-OS syscalls stubbed out, and writes PNG screenshots.
# Needs: gcc, python3 with Pillow. No SDK, no Minimal-OS.
#   ./mosdesk/tools/host_preview/build.sh [outdir]
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/preview-out}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CF="-std=gnu17 -O1 -Wall -Wno-unused-function -iquote $HERE/stubs -I$ROOT -I$ROOT/mosdesk/libraries/ttrend/include"
for f in wm_tu settings_tu sdk_impl pwhash_stub driver; do gcc $CF -c "$HERE/$f.c" -o "$WORK/$f.o"; done
gcc $CF -c "$ROOT/mosdesk/libraries/ttrend/src/ttrend.c" -o "$WORK/ttrend.o"
gcc "$WORK"/*.o -o "$WORK/harness"
render() { # name, fonts(1/0)
  S="$WORK/$1"; mkdir -p "$S/0:/programs/mosdesk/fonts" \
    "$S/0:/programs/mosWM/assets/fonts" \
    "$S/0:/programs/mosWM/assets/cursors/Posy" \
    "$S/0:/programs/mosWM/assets/cursors/Classic"
  cp "$ROOT/mosdesk/fonts/default.font" "$S/0:/programs/mosdesk/fonts/"
  cp "$ROOT/mosWM/assets/cursors/Posy/arrow.cur" \
    "$S/0:/programs/mosWM/assets/cursors/Posy/"
  cp "$ROOT/mosWM/assets/cursors/Classic/arrow.cur" \
    "$S/0:/programs/mosWM/assets/cursors/Classic/"
  if [ "$2" = 1 ]; then
    cp "$ROOT"/mosdesk/fonts/*.otf "$S/0:/programs/mosdesk/fonts/"
    cp "$ROOT"/mosdesk/fonts/*.otf "$S/0:/programs/mosWM/assets/fonts/"
  fi
  (cd "$S" && "$WORK/harness" "$1.ppm" "$3" >/dev/null)
}
mkdir -p "$OUT"
render smooth 1 1; render smooth_menu_closed 1 0; render bitmap_fallback 0 1
python3 - "$WORK" "$OUT" <<'PY'
import sys
from PIL import Image
w,o=sys.argv[1:3]
for n in ("smooth","smooth_menu_closed","bitmap_fallback"):
    Image.open(f"{w}/{n}/{n}.ppm").save(f"{o}/desktop_{n}.png")
PY
echo "screenshots in $OUT"
