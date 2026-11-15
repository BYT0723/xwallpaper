#!/bin/sh
# What each backend costs inside xwallpaperd, one fresh daemon per backend so
# leftover heap from a previous backend cannot pollute the next number.
#
#   ./daemon-cost.sh              # Xvfb + llvmpipe (video skipped: vo=gpu aborts)
#   DISPLAY_SPEC=:0 ./daemon-cost.sh
#
# Output: tab-separated lines in results/daemon-cost.txt (PSS is the number to
# read; RSS double counts shared library pages across processes).
set -eu
cd "$(dirname "$0")"

ROOT=$(cd ../.. && pwd)
OUT=${OUT:-results}
SIZE=${SIZE:-1920x1080}
DISPLAY_SPEC=${DISPLAY_SPEC:-xvfb}
D=""

# One Xvfb for the whole run; the outer invocation re-execs itself inside it.
if [ "$DISPLAY_SPEC" = xvfb ] && [ "${XW_BENCH_IN_XVFB:-}" != 1 ]; then
  XW_BENCH_IN_XVFB=1 OUT="$OUT" SIZE="$SIZE" DISPLAY_SPEC="$DISPLAY_SPEC" \
    exec xvfb-run -a -s "-screen 0 ${SIZE}x24" \
    env GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 sh "$0" "$@"
fi

if [ ! -x "$ROOT/xwallpaperd" ]; then
  echo "daemon-cost: build first (make -C $ROOT)" >&2
  exit 1
fi

mkdir -p "$OUT"
rm -f "$OUT/daemon-cost.txt"

start_daemon() { # $1 = scenario name; retries once (first Xvfb daemon can race)
  attempt=1
  while [ "$attempt" -le 2 ]; do
    rm -rf "$OUT/xdg/$1"
    mkdir -p "$OUT/xdg/$1/state" "$OUT/xdg/$1/config" "$OUT/xdg/$1/cache"
    XDG_STATE_HOME="$OUT/xdg/$1/state" XDG_CONFIG_HOME="$OUT/xdg/$1/config" \
      XDG_CACHE_HOME="$OUT/xdg/$1/cache" \
      "$ROOT/xwallpaperd" >"$OUT/daemon-$1.log" 2>&1 &
    D=$!
    i=0
    while [ $i -lt 60 ]; do
      "$ROOT/xwallpaper" --list >/dev/null 2>&1 && break
      i=$((i + 1))
      sleep 0.1
    done
    sleep 0.3
    if kill -0 "$D" 2>/dev/null; then
      return 0
    fi
    attempt=$((attempt + 1))
    sleep 1
  done
  echo "daemon-cost: daemon for '$1' died on start (see $OUT/daemon-$1.log)" >&2
  return 1
}

stop_daemon() {
  "$ROOT/xwallpaper" exit >/dev/null 2>&1 || true
  wait 2>/dev/null || true
  sleep 1
}

measure() { # $1 = label
  if kill -0 "$D" 2>/dev/null; then
    python3 treestat.py "$D" "$1" | tee -a "$OUT/daemon-cost.txt"
  else
    echo "$1	DAEMON_DIED (see $OUT/daemon-$1.log)" | tee -a "$OUT/daemon-cost.txt"
  fi
}

run_scenario() { # $1 = name, $2 = settle seconds, rest = setup commands
  name=$1
  settle=$2
  shift 2
  start_daemon "$name"
  "$@" >/dev/null 2>&1 || true
  sleep "$settle"
  measure "$name"
  stop_daemon
}

G="-g ${SIZE}+0+0 --name bench"
X="$ROOT/xwallpaper"

run_scenario idle 2 true
run_scenario image 2 "$X" set $G "$ROOT/tests/data/red.png"
if [ "$DISPLAY_SPEC" = xvfb ]; then
  echo "video	skipped: libmpv vo=gpu aborts under llvmpipe (no DRI3); rerun with DISPLAY_SPEC=:0" |
    tee -a "$OUT/daemon-cost.txt"
else
  run_scenario video 3 "$X" set $G --video "$ROOT/tests/data/bounce.mp4" --mute
fi

# web, then clear inside the same daemon: the residual line is what a user sees
# after switching a web wallpaper off.
start_daemon web
"$X" set $G --web "$ROOT/tests/data/webdir" >/dev/null 2>&1 || true
sleep 4
measure web-static
"$X" set $G --web "$ROOT/tests/data/webunity" >/dev/null 2>&1 || true
sleep 4
measure web-wasm
"$X" clear >/dev/null 2>&1 || true
sleep 15
measure web-cleared
stop_daemon

