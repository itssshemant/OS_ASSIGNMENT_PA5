# CSE231 Programming Assignment 5 — Know Your Memory (mmap/madvise)

This project implements user-space demand paging for a square-root lookup table, chunk-based FIFO replacement, `madvise()` experiments, and a file-backed major-fault bonus experiment.

## Files

- `test_mmap.c` — complete implementation for Parts A–C and bonus.
- `run_part_b.sh` — three repetitions of all six required Part B configurations.
- `run_part_c.sh` — three repetitions of no hint, `MADV_DONTNEED`, `MADV_SEQUENTIAL`, `MADV_RANDOM`, and read-ahead.
- `run_bonus.sh` — file-backed experiment with cold/warm attempts and advice variants.
- `REPORT.md` — report structure and theory answers; insert measured results from the CSV files before submission.

## Requirements

Ubuntu/WSL, GCC, GNU make is not required, Python 3 for graph generation, and standard Linux headers.

```bash
sudo apt update
sudo apt install build-essential python3 python3-matplotlib -y
```

## Part A: build and run

```bash
gcc -O2 -Wall -Wextra -DCHUNK_PAGES=1 -DRESIDENT_CHUNKS=1 -o mmap_hw test_mmap.c -lm
./mmap_hw
```

Expected validation line: `All tests passed!`.

## Part B

```bash
./run_part_b.sh
```

Produces `results_part_b.csv`. The script runs each rubric configuration three times. Use the median of the three `wall_seconds` values for the report.

## Part C

```bash
./run_part_c.sh
```

Produces `results_part_c.csv`. `MADVISE_MODE` values: 0 none, 1 DONTNEED before `munmap`, 2 SEQUENTIAL, 3 RANDOM, 4 next-chunk read-ahead (and WILLNEED for file-backed mode).

## Bonus: file-backed major faults

```bash
./run_bonus.sh
```

This creates `sqrts.bin`, a 64 MiB file containing the first 2^23 square roots. The bonus test uses the same 500,000-access mixed random/sequential pattern over that file-backed slice. The script attempts `drop_caches` only when passwordless sudo is available; otherwise it labels the first run as best-effort cold. Run as a fresh process for each measurement.

## Important measurement notes

- `ru_maxrss` is reported in KiB on Linux.
- `ru_minflt` and `ru_majflt` are process resource counters.
- The signal invocation counter counts faults handled by this pager, not all process faults.
- `RLIMIT_AS` is 8 MiB. The program rejects configurations whose chunk-residency budget exceeds half that limit, leaving room for runtime mappings, libraries, and stack.
- Benchmark values are machine- and cache-state-dependent. Do not copy sample numbers from another machine; use the CSV files generated on your WSL system.

## Report completion

Open `REPORT.md` after running the three experiment scripts. Replace each `TO FILL` cell with medians calculated from the corresponding CSV. Generate the required graphs with:

```bash
python3 make_graphs.py results_part_b.csv
```

The report intentionally does not contain fabricated benchmark results: timing, RSS, and cold-cache major-fault counts must be measured on the machine used for submission.
