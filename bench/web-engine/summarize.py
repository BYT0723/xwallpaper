#!/usr/bin/env python3
"""Median-of-N summary for wbench JSON lines (runs.jsonl)."""
import json
import statistics
import sys
from collections import defaultdict

KEYS = ["load_ms", "first_frame_ms", "fps", "cpu_pct", "procs", "pss_mb"]
COLS = ["load_ms", "first_frame_ms", "fps", "cpu_pct", "procs", "pss_mb"]


def main(path):
    buckets = defaultdict(list)
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line.startswith("WENCH "):
            continue
        rec = json.loads(line[len("WENCH "):])
        buckets[(rec["mode"], rec["label"])].append(rec)

    print("| mode | engine | n | " + " | ".join(COLS) + " | wasm | webgl |")
    print("|---|---|---|" + "---|" * len(COLS) + "---|---|")
    for (mode, label) in sorted(buckets):
        recs = buckets[(mode, label)]
        cells = []
        for k in KEYS:
            vals = [r[k] for r in recs if r[k] >= 0]
            cells.append(f"{statistics.median(vals):.1f}" if vals else "-")
        wasm = recs[0].get("wasm", "n/a")
        webgl = recs[0].get("webgl", "n/a")
        print(f"| {mode} | {label} | {len(recs)} | " + " | ".join(cells) +
              f" | {wasm} | {webgl} |")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "results/runs.jsonl")
