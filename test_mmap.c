#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>

#ifndef CHUNK_PAGES
#define CHUNK_PAGES 1
#endif
#ifndef RESIDENT_CHUNKS
#define RESIDENT_CHUNKS 1
#endif
/* 0=none, 1=DONTNEED before munmap, 2=SEQUENTIAL, 3=RANDOM,
 * 4=read-ahead next chunk (+ WILLNEED for file-backed mappings). */
#ifndef MADVISE_MODE
#define MADVISE_MODE 0
#endif
#ifndef BACKING_FILE
#define BACKING_FILE 0
#endif
#ifndef BONUS_ENTRIES
#define BONUS_ENTRIES (1UL << 23) /* 64 MiB file-backed slice */
#endif

#define AS_LIMIT       (1UL << 23)
#define MAX_SQRTS      (1UL << 27)
#define BONUS_FILE     "sqrts.bin"
#define MAX_TRACKED    RESIDENT_CHUNKS

#if CHUNK_PAGES < 1
#error CHUNK_PAGES must be at least 1
#endif
#if RESIDENT_CHUNKS < 1
#error RESIDENT_CHUNKS must be at least 1
#endif
#if BACKING_FILE
#define TABLE_ENTRIES BONUS_ENTRIES
#else
#define TABLE_ENTRIES MAX_SQRTS
#endif

static size_t page_size;
static size_t chunk_bytes;
static double *sqrts;
static int backing_fd = -1;
static uintptr_t fifo[MAX_TRACKED];
static int fifo_count = 0;
static int fifo_head = 0;
static volatile sig_atomic_t sigsegv_count = 0;
static volatile sig_atomic_t random_faults = 0;
static volatile sig_atomic_t sequential_faults = 0;
static volatile sig_atomic_t access_phase = 0;
static volatile sig_atomic_t map_error = 0;
static struct timespec test_start, test_end;

#define align_down(x, a) ((x) & ~((typeof(x))(a) - 1))

static void
calculate_sqrts(double *dst, size_t start, size_t nr)
{
  for (size_t i = 0; i < nr; i++)
    dst[i] = sqrt((double)(start + i));
}

static void
fatal_signal(const char *msg, size_t len)
{
  (void)write(STDERR_FILENO, msg, len);
  _exit(EXIT_FAILURE);
}

static int
find_resident(uintptr_t base)
{
  for (int i = 0; i < fifo_count; i++) {
    int slot = (fifo_head + i) % MAX_TRACKED;
    if (fifo[slot] == base)
      return slot;
  }
  return -1;
}

static void
map_chunk(uintptr_t base)
{
  if (base < (uintptr_t)sqrts || base + chunk_bytes > (uintptr_t)sqrts + TABLE_ENTRIES * sizeof(double))
    return;
  if (find_resident(base) >= 0)
    return;

  if (fifo_count == MAX_TRACKED) {
    uintptr_t old = fifo[fifo_head];
#if MADVISE_MODE == 1
    (void)madvise((void *)old, chunk_bytes, MADV_DONTNEED);
#endif
    if (munmap((void *)old, chunk_bytes) == -1)
      map_error = errno ? errno : EINVAL;
    fifo_head = (fifo_head + 1) % MAX_TRACKED;
    fifo_count--;
  }

  void *mapped;
#if BACKING_FILE
  off_t offset = (off_t)(base - (uintptr_t)sqrts);
  mapped = mmap((void *)base, chunk_bytes, PROT_READ,
                MAP_PRIVATE | MAP_FIXED, backing_fd, offset);
#else
  mapped = mmap((void *)base, chunk_bytes, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
  if (mapped == MAP_FAILED) {
    map_error = errno ? errno : ENOMEM;
    return;
  }
#if !BACKING_FILE
  size_t start = (base - (uintptr_t)sqrts) / sizeof(double);
  size_t count = chunk_bytes / sizeof(double);
  if (start + count > TABLE_ENTRIES)
    count = TABLE_ENTRIES - start;
  calculate_sqrts((double *)mapped, start, count);
#endif
#if MADVISE_MODE == 2
  (void)madvise(mapped, chunk_bytes, MADV_SEQUENTIAL);
#elif MADVISE_MODE == 3
  (void)madvise(mapped, chunk_bytes, MADV_RANDOM);
#endif
  int tail = (fifo_head + fifo_count) % MAX_TRACKED;
  fifo[tail] = base;
  fifo_count++;
}

static void
handle_sigsegv(int sig, siginfo_t *si, void *ctx)
{
  (void)sig;
  (void)ctx;
  sigsegv_count++;
  if (access_phase == 1) random_faults++;
  else if (access_phase == 2) sequential_faults++;
  uintptr_t fault = (uintptr_t)si->si_addr;
  uintptr_t begin = (uintptr_t)sqrts;
  uintptr_t end = begin + TABLE_ENTRIES * sizeof(double);
  if (fault < begin || fault >= end)
    fatal_signal("SIGSEGV outside square-root table; aborting\n",
                 sizeof("SIGSEGV outside square-root table; aborting\n") - 1);

  uintptr_t chunk_base = align_down(fault, chunk_bytes);
  /* Align relative to the table base, not the absolute address. The table
   * base is page-aligned and chunk_bytes is a page multiple. */
  size_t offset = fault - begin;
  chunk_base = begin + (offset / chunk_bytes) * chunk_bytes;
  map_chunk(chunk_base);
  if (map_error)
    fatal_signal("Pager failed to map a chunk\n", sizeof("Pager failed to map a chunk\n") - 1);

#if MADVISE_MODE == 4
  if (RESIDENT_CHUNKS > 1) {
    uintptr_t next = chunk_base + chunk_bytes;
    if (next < end) {
      map_chunk(next);
      if (!map_error)
        (void)madvise((void *)next, chunk_bytes, MADV_WILLNEED);
      if (map_error)
        fatal_signal("Pager read-ahead failed\n", sizeof("Pager read-ahead failed\n") - 1);
    }
  }
#endif
}

#if BACKING_FILE
static void
open_backing_file(void)
{
  backing_fd = open(BONUS_FILE, O_RDONLY);
  if (backing_fd == -1) {
    fprintf(stderr, "Cannot open %s; run ./make_backing_file first: %s\n",
            BONUS_FILE, strerror(errno));
    exit(EXIT_FAILURE);
  }
  struct stat st;
  if (fstat(backing_fd, &st) == -1 ||
      (uint64_t)st.st_size < TABLE_ENTRIES * sizeof(double)) {
    fprintf(stderr, "%s is missing data; run ./make_backing_file again\n", BONUS_FILE);
    exit(EXIT_FAILURE);
  }
}
#endif

static void
setup_sqrt_region(void)
{
  struct rlimit lim = {AS_LIMIT, AS_LIMIT};
  struct sigaction act;
  size_t table_bytes = TABLE_ENTRIES * sizeof(double);
  sqrts = mmap(NULL, table_bytes + AS_LIMIT, PROT_NONE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sqrts == MAP_FAILED) {
    fprintf(stderr, "Couldn't reserve sqrt table: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (munmap(sqrts, table_bytes + AS_LIMIT) == -1) {
    fprintf(stderr, "Couldn't release reservation: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (setrlimit(RLIMIT_AS, &lim) == -1) {
    fprintf(stderr, "Couldn't set RLIMIT_AS: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  memset(&act, 0, sizeof(act));
  act.sa_sigaction = handle_sigsegv;
  act.sa_flags = SA_SIGINFO;
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGSEGV, &act, NULL) == -1) {
    fprintf(stderr, "sigaction: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static void
test_sqrt_region(void)
{
  size_t pos = 0;
  double expected;
  printf("Validating square root table contents...\n");
  srand(0xDEADBEEF);
  for (int i = 0; i < 500000; i++) {
    if (i % 2 == 0) {
      access_phase = 1;
      pos = (size_t)rand() % (TABLE_ENTRIES - 1);
    } else {
      access_phase = 2;
      pos++;
    }
    calculate_sqrts(&expected, pos, 1);
    if (sqrts[pos] != expected) {
      fprintf(stderr, "Incorrect sqrt at %zu: expected %.12f, got %.12f\n",
              pos, expected, sqrts[pos]);
      exit(EXIT_FAILURE);
    }
  }
  printf("All tests passed!\n");
}

int
main(void)
{
  struct rusage ru;
  struct timespec elapsed;
  page_size = (size_t)sysconf(_SC_PAGESIZE);
  chunk_bytes = page_size * (size_t)CHUNK_PAGES;
  if (page_size == 0 || chunk_bytes % page_size != 0 || chunk_bytes > AS_LIMIT / 2 ||
      chunk_bytes * (size_t)RESIDENT_CHUNKS > AS_LIMIT / 2) {
    fprintf(stderr, "Invalid/unsafe configuration: chunk=%zu bytes, resident=%zu\n",
            chunk_bytes, chunk_bytes * (size_t)RESIDENT_CHUNKS);
    return EXIT_FAILURE;
  }
  printf("page_size=%zu CHUNK_PAGES=%d RESIDENT_CHUNKS=%d MADVISE_MODE=%d BACKING_FILE=%d\n",
         page_size, CHUNK_PAGES, RESIDENT_CHUNKS, MADVISE_MODE, BACKING_FILE);
#if BACKING_FILE
  open_backing_file();
#endif
  setup_sqrt_region();
  clock_gettime(CLOCK_MONOTONIC, &test_start);
  test_sqrt_region();
  clock_gettime(CLOCK_MONOTONIC, &test_end);
  elapsed.tv_sec = test_end.tv_sec - test_start.tv_sec;
  elapsed.tv_nsec = test_end.tv_nsec - test_start.tv_nsec;
  if (elapsed.tv_nsec < 0) { elapsed.tv_sec--; elapsed.tv_nsec += 1000000000L; }
  double seconds = (double)elapsed.tv_sec + (double)elapsed.tv_nsec / 1e9;
  getrusage(RUSAGE_SELF, &ru);
  printf("sigsegv_invocations=%d\n", (int)sigsegv_count);
  printf("random_sigsegv=%d sequential_sigsegv=%d\n", (int)random_faults, (int)sequential_faults);
  printf("wall_time_seconds=%.6f\n", seconds);
  printf("minor_faults=%ld major_faults=%ld maxrss_kib=%ld\n",
         ru.ru_minflt, ru.ru_majflt, ru.ru_maxrss);
#if BACKING_FILE
  close(backing_fd);
#endif
  return EXIT_SUCCESS;
}
