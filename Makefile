CC = gcc
CFLAGS = -Wall -Wextra -g -I. -Iinclude

# everything a running file system is linked from. not util/string.c:
# that file does not compile, and the C library supplies memcpy
FS_SRCS = src/svfs.c src/namev.c src/mobj.c src/vnode.c \
          src/slab_allocator.c sys/mem/mem.c sys/disk/disk.c \
          util/list.c util/spinlock.c

.PHONY: all fs-maker fs-check flush empty-iso fs check clean

all: fs-maker fs-check flush

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

flush: bin/flush

bin/flush: test/flush.c $(FS_SRCS) include/svfs.h include/mobj.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ test/flush.c $(FS_SRCS)

# build a fresh image, then read it back
check: fs bin/fs-check
	./bin/fs-check

clean:
	rm -rf bin/*
