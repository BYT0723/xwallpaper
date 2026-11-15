#!/usr/bin/env python3
"""PSS/RSS/CPU of a process and its whole subtree, for one xwallpaperd scenario."""
import os
import sys


def children(pid):
    try:
        with open(f"/proc/{pid}/task/{pid}/children") as f:
            return [int(x) for x in f.read().split()]
    except OSError:
        return []


def tree(pid):
    out = [pid]
    for kid in children(pid):
        out.extend(tree(kid))
    return out


def pss_kb(pid):
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Pss:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def rss_kb(pid):
    try:
        with open(f"/proc/{pid}/statm") as f:
            return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") // 1024
    except OSError:
        return 0


def cpu_ticks(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            raw = f.read()
        fields = raw[raw.rindex(")") + 2:].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, ValueError, IndexError):
        return 0


def main():
    if len(sys.argv) < 2:
        print("usage: treestat.py <pid> [label]")
        return 2
    pid = int(sys.argv[1])
    label = sys.argv[2] if len(sys.argv) > 2 else "scenario"
    pids = tree(pid)
    pss = sum(pss_kb(p) for p in pids)
    rss = sum(rss_kb(p) for p in pids)
    cpu = sum(cpu_ticks(p) for p in pids)
    print(f"{label}\tprocs={len(pids)}\tpss_mb={pss / 1024:.1f}\t"
          f"rss_mb={rss / 1024:.1f}\tcpu_ticks={cpu}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
