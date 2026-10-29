#!/usr/bin/env bash
# Measures Velox startup time, resident memory and large-file open time.
#   scripts/bench.sh [runs]        (default 10 runs; BIN=path overrides build/app/bin/velox)
# Runs headless (QT_QPA_PLATFORM=offscreen) with an isolated HOME. Prints a Markdown table.
# Timing uses `velox --screenshot`, which renders the window after a fixed 700 ms and exits,
# so that constant is subtracted: result = process start -> files opened and window ready.
# The page cache is warm (dropping it needs root), so "cold start" means a fresh process.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
runs="${1:-10}"
bin="$(realpath "${BIN:-build/app/bin/velox}")"
[ -x "$bin" ] || { echo "build first: ./build.sh" >&2; exit 1; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen HOME="$work/home" XDG_RUNTIME_DIR="$work/run"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
settle_ms=700

# 100 MiB of ASCII text, ~80 chars per line.
big="$work/big.txt"
python3 - "$big" <<'P'
import sys
line = "The quick brown fox jumps over the lazy dog 0123456789 abcdefghijklmnopqrstuvwxyz\n"
n = (100 * 1024 * 1024) // len(line)
with open(sys.argv[1], "w") as f:
    for _ in range(n // 1000):
        f.write(line * 1000)
P

wall_ms() { # args: files...; prints wall time in ms minus the screenshot delay
  local s e; s=$(date +%s%N)
  "$bin" --new-instance --fresh --screenshot "$work/shot.png" "$@" >/dev/null 2>&1
  e=$(date +%s%N); echo $(( (e - s) / 1000000 - settle_ms ))
}
median() { sort -n | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:int((a[NR/2]+a[NR/2+1])/2)}'; }
minmax() { sort -n | awk 'NR==1{lo=$1} {hi=$1} END{print lo "–" hi}'; }

wall_ms >/dev/null # warm-up
start=(); open=()
for _ in $(seq "$runs"); do start+=("$(wall_ms)"); done
for _ in $(seq "$runs"); do open+=("$(wall_ms "$big")"); done

rss() { # args: file; prints "VmRSS VmHWM" in MiB after the window has settled
  "$bin" --new-instance --fresh "$@" >/dev/null 2>&1 & local pid=$!
  sleep 6
  awk '/VmRSS|VmHWM/{printf "%d ", $2/1024}' "/proc/$pid/status"
  kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null || true; echo
}
read -r r_idle h_idle <<<"$(rss)"
read -r r_big h_big <<<"$(rss "$big")"

echo "| Metric | Median | Range (n=$runs) |"
echo "| --- | ---: | ---: |"
echo "| Start to ready, empty window | $(printf '%s\n' "${start[@]}" | median) ms | $(printf '%s\n' "${start[@]}" | minmax) ms |"
echo "| Start to ready, 100 MiB file open | $(printf '%s\n' "${open[@]}" | median) ms | $(printf '%s\n' "${open[@]}" | minmax) ms |"
echo "| RSS, empty window | $r_idle MiB (peak $h_idle) | single sample |"
echo "| RSS, 100 MiB file open | $r_big MiB (peak $h_big) | single sample |"
echo
echo "Host: $(lscpu | sed -n 's/^Model name: *//p' | sed 's/^ *//'), $(nproc) threads, $(free -g | awk '/Mem:/{print $2}') GiB RAM, load average $(cut -d' ' -f1-3 /proc/loadavg), $(uname -sr)"
