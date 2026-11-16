#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define BONUS_ENTRIES (1UL << 23) /* 64 MiB of doubles */
#define OUTPUT_FILE "sqrts.bin"

int main(void)
{
  int fd = open(OUTPUT_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0) { perror("open"); return 1; }
  const size_t cap = 4096;
  double *buf = malloc(cap * sizeof(*buf));
  if (!buf) { perror("malloc"); close(fd); return 1; }
  for (size_t start = 0; start < BONUS_ENTRIES; start += cap) {
    size_t n = BONUS_ENTRIES - start;
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; ++i)
      buf[i] = sqrt((double)(start + i));
    size_t bytes = n * sizeof(*buf), done = 0;
    while (done < bytes) {
      ssize_t w = write(fd, (char *)buf + done, bytes - done);
      if (w < 0) { if (errno == EINTR) continue; perror("write"); free(buf); close(fd); return 1; }
      done += (size_t)w;
    }
  }
  if (fsync(fd) < 0) { perror("fsync"); free(buf); close(fd); return 1; }
  free(buf);
  close(fd);
  printf("Created %s (%lu bytes)\n", OUTPUT_FILE, (unsigned long)(BONUS_ENTRIES * sizeof(double)));
  return 0;
}
