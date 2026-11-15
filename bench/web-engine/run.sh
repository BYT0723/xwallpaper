#!/bin/sh
# Engine-level matrix: build wbench against both WebKit ABIs and sample the
# same synthetic pages on each. One JSON line per run, medians via summarize.py.
#
#   ./run.sh                     # Xvfb + llvmpipe (reproducible, no GPU)
#   DISPLAY_SPEC=:0 ./run.sh     # live session (real GPU; windows flash briefly)
#   REPS=1 MODES="anim webgl" ./run.sh
set -eu
cd "$(dirname "$0")"

SIZE=${SIZE:-1920x1080}
REPS=${REPS:-3}
MODES=${MODES:-"blank anim canvas2d webgl js video"}
DISPLAY_SPEC=${DISPLAY_SPEC:-xvfb}
WARM=${WARM:-1000}
SAMPLE=${SAMPLE:-5000}
OUT=${OUT:-results}
ROOT=$(cd ../.. && pwd)
VIDEO=${VIDEO:-$ROOT/tests/data/bounce.mp4}

mkdir -p "$OUT"

cc -O2 -Wall -Wextra -o wbench-4.1 wbench.c \
  $(pkg-config --cflags --libs webkit2gtk-4.1 gtk+-3.0)
cc -O2 -Wall -Wextra -DWBENCH_GTK4=1 -o wbench-6.0 wbench.c \
  $(pkg-config --cflags --libs webkitgtk-6.0 gtk4)

run_one() {
  engine=$1
  mode=$2
  shift 2
  set -- --label "$engine" --mode "$mode" --size "$SIZE" \
         --warm "$WARM" --sample "$SAMPLE" "$@"
  if [ "$DISPLAY_SPEC" = xvfb ]; then
    xvfb-run -a -s "-screen 0 ${SIZE}x24" \
      env GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 "./wbench-$engine" "$@"
  else
    env GDK_BACKEND=x11 "./wbench-$engine" "$@"
  fi 2>/dev/null
}

for engine in 4.1 6.0; do
  for mode in $MODES; do
    rep=1
    while [ "$rep" -le "$REPS" ]; do
      if [ "$mode" = video ]; then
        run_one "$engine" "$mode" --video "$VIDEO" || true
      else
        run_one "$engine" "$mode" || true
      fi
      rep=$((rep + 1))
    done
  done
done | tee "$OUT/runs.jsonl"

python3 summarize.py "$OUT/runs.jsonl"
