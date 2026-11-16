#!/usr/bin/env bash
set -euo pipefail
CC=${CC:-gcc}
SRC=${SRC:-test_mmap.c}
OUT=${OUT:-results_part_b.csv}
printf 'CHUNK_PAGES,RESIDENT_CHUNKS,run,sigsegv,random_sigsegv,sequential_sigsegv,wall_seconds,minor_faults,major_faults,maxrss_kib\n' > "$OUT"
configs=('1 1' '4 1' '16 1' '64 1' '1 16' '16 4')
for cfg in "${configs[@]}"; do
  read -r cp rc <<< "$cfg"
  "$CC" -O2 -Wall -Wextra -DCHUNK_PAGES="$cp" -DRESIDENT_CHUNKS="$rc" -o mmap_hw "$SRC" -lm
  for run in 1 2 3; do
    output=$(./mmap_hw)
    echo "$output"
    faults=$(sed -n 's/^sigsegv_invocations=//p' <<< "$output")
    read -r random_faults sequential_faults < <(sed -n 's/^random_sigsegv=\([0-9]*\) sequential_sigsegv=\([0-9]*\)$/\1 \2/p' <<< "$output")
    wall=$(sed -n 's/^wall_time_seconds=//p' <<< "$output")
    read -r minor major rss < <(sed -n 's/^minor_faults=\([0-9]*\) major_faults=\([0-9]*\) maxrss_kib=\([0-9]*\)$/\1 \2 \3/p' <<< "$output")
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$cp" "$rc" "$run" "$faults" "$random_faults" "$sequential_faults" "$wall" "$minor" "$major" "$rss" >> "$OUT"
  done
done
echo "Wrote $OUT"
