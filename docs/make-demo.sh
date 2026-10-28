#!/usr/bin/env bash
# Regenerates docs/demo.gif: runs the scripted demo (tests/demo.cpp) offscreen
# to render captioned frames, then assembles them with ffmpeg.
# Needs a built tree (./build.sh) and ffmpeg.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
frames=$(mktemp -d)
trap 'rm -rf "$frames"' EXIT
QT_QPA_PLATFORM=offscreen build/app/bin/velox --demo "$frames"
# One shared palette keeps colors stable between frames; fps=10 is plenty for
# a slide-style demo and keeps the file small.
ffmpeg -y -loglevel error -f concat -safe 0 -i "$frames/frames.txt" \
  -vf "fps=10,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" \
  -loop 0 docs/demo.gif
ls -lh docs/demo.gif
