#!/usr/bin/env bash
set -euo pipefail
CC=${CC:-gcc}
SRC=${SRC:-test_mmap.c}
OUT=${OUT:-results_bonus.csv}
printf 'cache_state,advice_mode,run,sigsegv,random_sigsegv,sequential_sigsegv,wall_seconds,minor_faults,major_faults,maxrss_kib\n' > "$OUT"
"$CC" -O2 -Wall -Wextra -o make_backing_file make_backing_file.c -lm
./make_backing_file
for mode in 0 2 3 4; do
  "$CC" -O2 -Wall -Wextra -DBACKING_FILE=1 -DCHUNK_PAGES=16 -DRESIDENT_CHUNKS=4 -DMADVISE_MODE="$mode" -o mmap_bonus "$SRC" -lm
  for run in 1 2 3; do
    # Best-effort cold cache: requires sudo permission. If unavailable, first-run vs repeat is documented.
    state=warm
    if [[ "$run" == 1 ]]; then
      state=cold_best_effort
      sync
      if sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'; then :; else echo "Cache drop unavailable; first run is not guaranteed cold."; fi
    fi
    output=$(./mmap_bonus)
    echo "$output"
    faults=$(sed -n 's/^sigsegv_invocations=//p' <<< "$output")
    read -r random_faults sequential_faults < <(sed -n 's/^random_sigsegv=\([0-9]*\) sequential_sigsegv=\([0-9]*\)$/\1 \2/p' <<< "$output")
    wall=$(sed -n 's/^wall_time_seconds=//p' <<< "$output")
    read -r minor major rss < <(sed -n 's/^minor_faults=\([0-9]*\) major_faults=\([0-9]*\) maxrss_kib=\([0-9]*\)$/\1 \2 \3/p' <<< "$output")
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$state" "$mode" "$run" "$faults" "$random_faults" "$sequential_faults" "$wall" "$minor" "$major" "$rss" >> "$OUT"
  done
done
echo "Wrote $OUT"
