#!/usr/bin/env bash
set -euo pipefail
CC=${CC:-gcc}
SRC=${SRC:-test_mmap.c}
OUT=${OUT:-results_part_c.csv}
printf 'experiment,run,sigsegv,random_sigsegv,sequential_sigsegv,wall_seconds,minor_faults,major_faults,maxrss_kib\n' > "$OUT"
for mode in 0 1 2 3 4; do
  # Use 16 pages and four resident chunks to expose chunking and read-ahead behavior.
  "$CC" -O2 -Wall -Wextra -DCHUNK_PAGES=16 -DRESIDENT_CHUNKS=4 -DMADVISE_MODE="$mode" -o mmap_hw "$SRC" -lm
  for run in 1 2 3; do
    output=$(./mmap_hw)
    echo "$output"
    faults=$(sed -n 's/^sigsegv_invocations=//p' <<< "$output")
    read -r random_faults sequential_faults < <(sed -n 's/^random_sigsegv=\([0-9]*\) sequential_sigsegv=\([0-9]*\)$/\1 \2/p' <<< "$output")
    wall=$(sed -n 's/^wall_time_seconds=//p' <<< "$output")
    read -r minor major rss < <(sed -n 's/^minor_faults=\([0-9]*\) major_faults=\([0-9]*\) maxrss_kib=\([0-9]*\)$/\1 \2 \3/p' <<< "$output")
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$mode" "$run" "$faults" "$random_faults" "$sequential_faults" "$wall" "$minor" "$major" "$rss" >> "$OUT"
  done
done
echo "Wrote $OUT (mode 0=none, 1=DONTNEED, 2=SEQUENTIAL, 3=RANDOM, 4=read-ahead)"
