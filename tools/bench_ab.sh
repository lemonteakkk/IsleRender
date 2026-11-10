#!/usr/bin/env bash
# Runs the fixed 4-view benchmark with optimisations enabled one at a time.
# Run on AC power: iGPU clocks on battery make GPU timings swing by 2x.
set -e
EXE="$(dirname "$0")/../build/IsleRender.exe"
SIZE="${SIZE:-1280x720}"
run() { echo; echo "### $1"; "$EXE" --bench --size "$SIZE" --time 15 $2 2>&1 | grep -E '^\|'; }
run "0. all optimisations off"           "--nolod --nocull --oldwater"
run "1. + terrain tile culling & LOD"     "--nocull --oldwater"
run "2. + prop instance culling"          "--oldwater"
run "3. + precomputed water detail"       ""
run "ref: GPU culling (compute + MDI)"    "--gpucull"
run "ref: terrain via glMultiDrawElements" "--multidraw"
