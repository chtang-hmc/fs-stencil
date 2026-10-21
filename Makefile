CC = gcc
CFLAGS = -Wall -Wextra -g -I. -Iinclude

.PHONY: all fs-maker fs-check empty-iso fs check clean

all: fs-maker fs-check

empty-iso:
	dd if=/dev/zero bs=4096 count=161 > sys/empty-fs.iso

fs-maker: bin/fs-maker

bin/fs-maker: src/fs-maker.c include/svfs.h include/dirent.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ src/fs-maker.c

fs: empty-iso bin/fs-maker
	./bin/fs-maker

fs-check: bin/fs-check

bin/fs-check: test/fs-check.c include/svfs.h include/dirent.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ test/fs-check.c

# build a fresh image, then read it back
check: fs bin/fs-check
	./bin/fs-check

clean:
	rm -rf bin/*
