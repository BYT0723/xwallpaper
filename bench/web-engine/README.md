# web-engine bench

Manual measurement rig for `docs/specs/web-engine-eval.md`. Not wired into
`make`, no new build dependencies — it links the same WebKit/GTK the daemon
already uses.

| file | what it does |
|---|---|
| `wbench.c` | one synthetic page → `load_ms / first_frame_ms / fps / cpu_pct / procs / pss_mb` as one JSON line. Builds against both ABIs (`-DWBENCH_GTK4=1` for webkitgtk-6.0 + GTK4) |
| `run.sh` | engine × mode × reps matrix, medians via `summarize.py` |
| `daemon-cost.sh` | real `xwallpaperd`, one fresh daemon per backend, PSS of the whole process tree |
| `treestat.py` | PSS/RSS/CPU of a pid subtree |

```sh
cd bench/web-engine
REPS=3 ./run.sh                              # Xvfb + llvmpipe
DISPLAY_SPEC=:0 REPS=3 ./run.sh              # live GPU session (windows flash)
DISPLAY_SPEC=:0 ./daemon-cost.sh
```

Modes: `blank`, `anim`, `canvas2d`, `webgl`, `js` (JIT + wasm probe), `video`.
Read **PSS**, not RSS: RSS counts shared library pages once per process
(453MB RSS vs 250MB PSS for the same page).
