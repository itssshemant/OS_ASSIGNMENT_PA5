CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS ?= -lm

.PHONY: all clean part-a part-b part-c bonus
all: mmap_hw

mmap_hw: test_mmap.c
	$(CC) $(CFLAGS) -DCHUNK_PAGES=1 -DRESIDENT_CHUNKS=1 -o $@ $< $(LDLIBS)

part-a: mmap_hw
	./mmap_hw

part-b:
	./run_part_b.sh

part-c:
	./run_part_c.sh

bonus:
	./run_bonus.sh

clean:
	rm -f mmap_hw mmap_bonus make_backing_file sqrts.bin results_part_b.csv results_part_c.csv results_bonus.csv *.png
