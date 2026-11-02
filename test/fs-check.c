#include "svfs.h"
#include "dirent.h"

#include <stdio.h>
#include <string.h>

#define SV_NUM_BLOCKS (SV_LAST_DATA_BLOCK + 1) // every block on the disk

static int failures = 0;

// count a failed check (ok is 0)
static void check(int ok)
{
    if (!ok)
    {
        ++failures;
    }
}

// seek to the start of a disk block and read size bytes from there into data.
// returns 0 on success, -1 on failure
static int read_at_block(FILE *f, long block, void *data, size_t size)
{
    // find the block
    if (fseek(f, block * SV_BLOCK_SIZE, SEEK_SET) != 0)
    {
        perror("Error seeking in disk image");
        return -1;
    }

    // read from block
    if (fread(data, size, 1, f) != 1)
    {
        perror("Error reading from disk image");
        return -1;
    }
    return 0;
}

// test 1: the superblock's fields match
static int test_superblock(FILE *f)
{
    svfs_super_t super;
    memset(&super, 0, sizeof(super));
    if (read_at_block(f, SV_SUPER_BLOCK, &super, sizeof(super)) != 0)
    { // error out
        return -1;
    }

    check(super.svs_magic == SV_MAGIC_NO);
    check(super.svs_num_inodes == SV_NUM_INODES);
    check(super.svs_root_inode >= 0 && super.svs_root_inode < SV_NUM_INODES);
    check(super.svs_free_inode >= 0 && super.svs_free_inode < SV_NUM_INODES);
    check(super.svs_free_inode != super.svs_root_inode);
    check(super.svs_nfree == SV_NUM_DATA_BLOCKS - 1);
    return 0;
}

static int read_inode(FILE *f, long n, svfs_inode_t *inode)
{
    memset(inode, 0, sizeof(*inode));
    return read_at_block(f, SV_INODE_START_BLOCK + n, inode, sizeof(*inode));
}

// test 2: check inode free list
static int test_inode_free_list(FILE *f)
{
    svfs_super_t super;
    memset(&super, 0, sizeof(super));
    if (read_at_block(f, SV_SUPER_BLOCK, &super, sizeof(super)) != 0)
    { // error out
        return -1;
    }

    int seen[SV_NUM_INODES] = {0};
    int count = 0;
    int bad = 0;

    long n = super.svs_free_inode;
    while (n != -1)
    {
        if (n < 0 || n >= SV_NUM_INODES)
        {
            printf("    inode %ld is out of range\n", n);
            bad = 1;
            break;
        }
        if (seen[n])
        {
            printf("    inode %ld appears twice (cycle)\n", n);
            bad = 1;
            break;
        }
        seen[n] = 1;
        ++count;

        svfs_inode_t inode;
        if (read_inode(f, n, &inode) != 0)
        { // error out
            return -1;
        }
        if (inode.svi_mode != SV_MODE_FREE)
        {
            printf("    inode %ld is on the free list but its mode is %ld\n", n, inode.svi_mode);
            bad = 1;
        }
        if (inode.svi_number != n)
        {
            printf("    inode %ld has svi_number %ld\n", n, inode.svi_number);
            bad = 1;
        }
        n = inode.svi_un.svi_next_free;
    }

    check(!bad);
    check(count == SV_NUM_INODES - 1);
    check(super.svs_root_inode >= 0 && super.svs_root_inode < SV_NUM_INODES &&
              !seen[super.svs_root_inode]);
    return 0;
}

// test 3: test free block list
// the block free list is a chain of blocks with the first 28 being free blocks
// and the last being in indirections to other blocks
// this way we can fit all 128 blocks
static int test_block_free_list(FILE *f)
{
    svfs_super_t super;
    memset(&super, 0, sizeof(super));
    if (read_at_block(f, SV_SUPER_BLOCK, &super, sizeof(super)) != 0)
    { // error out
        return -1;
    }

    int seen[SV_NUM_BLOCKS] = {0};
    long found = 0;
    int chain_blocks = 0;
    int bad = 0;

    long list[SV_NFBLKS_PER_NODE];
    memcpy(list, super.svs_free_blocks, sizeof(list));

    while (1)
    {
        // slots 0 to 28 are free blocks; 0 means an empty slot
        for (int j = 0; j < SV_NFBLKS_PER_NODE - 1; ++j)
        {
            long b = list[j];
            if (b == 0)
            {
                continue;
            }
            if (b < SV_FIRST_FREE_BLOCK || b > SV_LAST_DATA_BLOCK)
            {
                printf("    block %ld is not a free data block\n", b);
                bad = 1;
            }
            else if (seen[b])
            {
                printf("    block %ld appears twice\n", b);
                bad = 1;
            }
            else
            {
                seen[b] = 1;
                ++found;
            }
        }

        // the last slot points to the next chain block
        // 0 would indicate the end of the list
        long next = list[SV_NFBLKS_PER_NODE - 1];
        if (next == 0)
        {
            break;
        }
        if (next < SV_FIRST_FREE_BLOCK || next > SV_LAST_DATA_BLOCK || seen[next])
        {
            printf("    chain pointer to block %ld is invalid or repeated\n", next);
            bad = 1;
            break;
        }
        seen[next] = 1; // chain blocks should be free blocks too
        ++found;
        ++chain_blocks;

        memset(list, 0, sizeof(list));
        if (read_at_block(f, next, list, sizeof(list)) != 0)
        { // error out
            return -1;
        }
    }

    int missing = 0;
    for (long b = SV_FIRST_FREE_BLOCK; b <= SV_LAST_DATA_BLOCK; ++b)
    {
        if (!seen[b])
        {
            printf("    block %ld is never reached\n", b);
            missing = 1;
        }
    }

    check(!bad);
    check(!missing);
    check(found == super.svs_nfree);
    check(chain_blocks >= 1);
    return 0;
}

// test 4: root is a directory and holds "." and ".."
static int test_root_dir(FILE *f)
{
    svfs_super_t super;
    memset(&super, 0, sizeof(super));
    if (read_at_block(f, SV_SUPER_BLOCK, &super, sizeof(super)) != 0)
    { // error out
        return -1;
    }

    long root = super.svs_root_inode;
    svfs_inode_t inode;
    if (read_inode(f, root, &inode) != 0)
    { // error out
        return -1;
    }
    check(inode.svi_mode == SV_MODE_DIRECTORY);
    check(inode.svi_number == root);
    check(inode.svi_un.svi_size == 2 * (long)sizeof(dirent_t));
    check(inode.svi_count == 2);
    check(inode.svi_direct_blocks[0] == SV_DATA_START_BLOCK);

    dirent_t entries[2];
    memset(entries, 0, sizeof(entries));
    if (read_at_block(f, inode.svi_direct_blocks[0], entries, sizeof(entries)) != 0)
    { // error out
        return -1;
    }
    check(strcmp(entries[0].d_name, ".") == 0 && entries[0].d_ino == root);
    check(strcmp(entries[1].d_name, "..") == 0 && entries[1].d_ino == root);
    check(entries[0].d_off == (long)sizeof(dirent_t) &&
              entries[1].d_off == 2 * (long)sizeof(dirent_t));
    return 0;
}

int main()
{
    FILE *f = fopen("sys/empty-fs.iso", "rb");
    if (f == NULL)
    {
        perror("Error opening file");
        return 1;
    }

    if (test_superblock(f) != 0 ||
        test_inode_free_list(f) != 0 ||
        test_block_free_list(f) != 0 ||
        test_root_dir(f) != 0)
    {
        fclose(f);
        return 1;
    }

    fclose(f);

    if (failures > 0)
    {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
