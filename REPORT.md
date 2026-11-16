# CSE231 Programming Assignment 5 — Know Your Memory!

**Name:** Hemant Meena
**Roll/Group ID:** 7 , 2025227 2025047
**Machine:** AMD Ryzen 3 5300U with Radeon Graphics, 8 logical CPUs
**Environment:** Ubuntu 24.04 on WSL2
**Kernel:** Linux 6.6.87.2-microsoft-standard-WSL2
**Compiler:** GCC 13.3.0
**Page size:** 4096 bytes

## Part A — Fault Handler

The SIGSEGV handler identifies the faulting address using `si->si_addr` and verifies that it belongs to the square-root table. It aligns the address to the required chunk, evicts the oldest resident chunk when necessary, maps the missing chunk at its exact virtual address, and calculates the required square roots. Invalid addresses and mapping failures terminate the program.

**Why is one resident page sufficient for sequential accesses?**
When `pos + 1` crosses a page boundary, the new access faults and the handler maps the next page. The previous element is not accessed again, so keeping the previous page is unnecessary.

**Why is printf unsafe in a signal handler?**
`printf()` uses buffered I/O and internal locks, which may be inconsistent if a signal interrupts the program while those resources are in use. The implementation uses `write()` and `_exit()` for fatal reporting. Note that the mapping operations required by this assignment are not generally guaranteed to be async-signal-safe by POSIX.

**What happens without address validation?**
An unrelated segmentation fault could be treated as a table miss. The handler might map memory at an invalid address, conceal a programming error, or corrupt the process address space.

## Part B — Chunk Granularity

Each configuration was tested three times. The following table reports median values.

| Chunk pages | Resident chunks | Handler calls | Median time (s) | Peak RSS (KiB) |
| ----------: | --------------: | ------------: | --------------: | -------------: |
|           1 |               1 |       250,456 |          13.334 |          2,180 |
|           4 |               1 |       250,109 |          21.334 |          2,188 |
|          16 |               1 |       250,007 |          53.832 |          2,188 |
|          64 |               1 |       249,932 |         184.510 |          2,188 |
|           1 |              16 |       250,440 |          14.557 |          2,188 |
|          16 |               4 |       249,958 |          55.496 |          2,188 |

Larger chunks reduce sequential faults because one fault loads multiple adjacent pages. Sequential faults fell from 456 with one page to 7 with 64 pages. Random faults remained close to 250,000 because random accesses rarely reuse recently loaded chunks.

The fastest tested configuration was **CHUNK_PAGES=1, RESIDENT_CHUNKS=1**. Larger chunks increased computation and memory work per fault, outweighing the reduction in handler calls.

Increasing resident chunks provided little benefit for random accesses. For the 16-page, 4-chunk configuration, the approximate random hit probability is:

`(16 × 4 × 4096 / 8) / 2^27 ≈ 0.0244%`

The largest resident budget among the tested configurations was 256 KiB. All tested configurations passed under the 8 MiB address-space limit. Configurations beyond the tested set were not benchmarked, so no untested maximum is claimed.

## Part C — Memory Advice

All experiments used 16 pages per chunk and four resident chunks. Values below are medians of three runs.

| Experiment        | Handler calls | Time (s) | Minor faults | Major faults | RSS (KiB) |
| ----------------- | ------------: | -------: | -----------: | -----------: | --------: |
| No advice         |       249,958 |   56.246 |    3,999,458 |            2 |     2,176 |
| DONTNEED + munmap |       249,958 |   60.468 |    3,999,463 |            2 |     2,188 |
| SEQUENTIAL        |       249,958 |   57.345 |    3,999,463 |            2 |     2,188 |
| RANDOM            |       249,958 |   57.185 |    3,999,462 |            2 |     2,188 |
| Read-ahead        |       249,924 |  110.293 |    7,997,270 |            2 |     2,188 |

**C1 — MADV_DONTNEED:** Its effect was negligible because the implementation calls `munmap()` immediately afterwards. Unmapping already releases the mapping. Using `MADV_DONTNEED` without unmapping would retain the virtual mapping but discard its anonymous contents; the next access would receive zero-filled pages. Retaining the full 1 GiB mapping would conflict with the 8 MiB address-space limit.

**C2 — Access-pattern hints:** `MADV_SEQUENTIAL` and `MADV_RANDOM` produced almost identical results to the baseline. These are anonymous mappings, so there is no file-backed page-cache read-ahead stream to exploit.

**C3 — Read-ahead:** Prefetching reduced handler calls slightly and eliminated sequential faults, but doubled execution time and increased minor faults substantially. The implementation also prefetches after random faults, where the next adjacent chunk is unlikely to be useful. This demonstrates that indiscriminate prefetching can reduce performance.

## Bonus — File-backed Mapping

A 64 MiB file containing the first 2^23 square roots was generated. Chunks were mapped using `MAP_PRIVATE`, `MAP_FIXED`, and `PROT_READ`. Each advice mode was tested three times.

| Advice mode         | Median time (s) | Minor faults | Major faults | Peak RSS (KiB) |
| ------------------- | --------------: | -----------: | -----------: | -------------: |
| No advice           |          18.819 |      246,825 |        2,411 |          2,184 |
| SEQUENTIAL          |          19.850 |      246,795 |        2,443 |          2,192 |
| RANDOM              |          27.571 |      232,925 |       16,386 |          2,196 |
| WILLNEED read-ahead |          29.803 |      248,331 |        1,345 |          2,356 |

The first run of each mode attempted to clear the Linux page cache using `sync` and `drop_caches`; subsequent runs used the warmed cache. Cache clearing succeeded without a reported error, but the cold-cache condition is treated as best-effort.

**Cold versus warm access:** A cold file-backed page may require storage I/O and generate a major fault. When the page is already cached, access generally generates a minor fault instead.

**Effect of advice:** File-backed mappings use the page cache and filesystem readahead, so advice can affect storage behavior. `MADV_RANDOM` performed poorly for this mixed workload, while `MADV_SEQUENTIAL` showed little improvement because half the accesses were random. `MADV_WILLNEED` reduced median major faults but increased execution time, showing that fewer major faults do not necessarily mean faster execution.

**MAP_PRIVATE versus MAP_SHARED:** `MAP_PRIVATE` provides copy-on-write behavior. Since the mapping is read-only, file contents are not modified. `MAP_SHARED | PROT_WRITE` would allow modifications to be visible through shared mappings and eventually written to the backing file.

## Graphs

The following graphs use the median measurements from Part B:

1. `page_faults_vs_chunk_pages.png`
2. `wall_time_vs_chunk_pages.png`

They show that increasing chunk size reduces handler calls slightly but substantially increases execution time for this workload.

## Bugs and Observations

The benchmark initially produced compiler warnings about an unused `backing_fd` variable in non-file-backed builds and the ignored return value of `write()`. These warnings did not prevent execution or affect the passing tests. Matplotlib was also missing initially; installing the Ubuntu `python3-matplotlib` package resolved graph generation.

All tested Part A, Part B, Part C, and bonus runs completed with **All tests passed!**.

**Conclusion:** The experiments show that memory-management optimizations depend strongly on access patterns. Larger chunks reduce sequential faults but increase unnecessary work for random accesses. Advice and prefetching can help in specific situations, but applying them indiscriminately can reduce performance.

