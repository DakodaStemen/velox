#!/usr/bin/env bash
# Regenerates docs/demo.gif from a build made with `DEMO=1 SELFTEST=0 ./build.sh`.
# Needs ffmpeg plus the JetBrains Mono and Inter fonts (the script renders with them).
#   docs/make-demo.sh [frames-dir]   keeps the PNG frames in frames-dir
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
frames=${1:-$(mktemp -d)}
[ -n "${1:-}" ] || trap 'rm -rf "$frames"' EXIT
mkdir -p "$frames"
QT_QPA_PLATFORM=offscreen build/app/bin/velox --demo "$frames"
# One palette for the whole clip keeps colors from shimmering; the dark UI
# is mostly flat so 128 colors with error diffusion stay clean and small.
ffmpeg -y -loglevel error -framerate 12 -i "$frames/frame_%04d.png" \
  -vf "scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a:diff_mode=rectangle" \
  -loop 0 docs/demo.gif
ls -lh docs/demo.gif
