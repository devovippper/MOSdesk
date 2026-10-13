#!/bin/sh
# Scripted session on the REAL mosWM + Settings client: first-run and sign-in
# screens, desktop, start menu, Settings (theme change, drag, maximize), and an
# mp4/gif of it.  Needs: gcc, python3 + Pillow, ffmpeg (for mp4/gif).
#   ./mosdesk/tools/host_preview/anim.sh [outdir]
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/preview-out}
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
CF="-std=gnu17 -O1 -w -iquote $HERE/stubs -I$ROOT -I$HERE -I$ROOT/mosdesk/libraries/ttrend/include"
for f in anim_tu settings_tu sdk_impl pwhash_stub anim_driver; do gcc $CF -c "$HERE/$f.c" -o "$WORK/$f.o"; done
gcc $CF -c "$ROOT/mosdesk/libraries/ttrend/src/ttrend.c" -o "$WORK/ttrend.o"
gcc "$WORK"/*.o -lm -o "$WORK/harness"
S="$WORK/sandbox"
mkdir -p "$S/0:/programs/mosdesk/fonts" "$S/0:/programs/mosWM/assets/fonts" "$S/0:/programs/mosWM/assets/cursors" "$S/0:/users"
cp "$ROOT"/mosdesk/fonts/*.otf "$ROOT/mosdesk/fonts/default.font" "$S/0:/programs/mosdesk/fonts/"
cp "$ROOT"/mosdesk/fonts/*.otf "$S/0:/programs/mosWM/assets/fonts/"
cp -r "$ROOT"/mosWM/assets/cursors/* "$S/0:/programs/mosWM/assets/cursors/"
mkdir -p "$OUT"
MP4=
if command -v ffmpeg >/dev/null 2>&1; then MP4="$OUT/mosdesk_demo.mp4"; fi
(cd "$S" && "$WORK/harness" "$OUT" $MP4)
python3 - "$OUT" <<'PY'
import sys, glob
from PIL import Image
for f in glob.glob(sys.argv[1] + "/*.ppm"):
    Image.open(f).save(f[:-4] + ".png")
PY
rm -f "$OUT"/*.ppm
if [ -n "$MP4" ]; then
  ffmpeg -loglevel error -y -i "$MP4" -vf "fps=15,scale=854:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" "$OUT/mosdesk_demo.gif"
fi
echo "output in $OUT"
