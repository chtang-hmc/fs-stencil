#include "svfs.h"
#include "dirent.h"

#include <stdio.h>
#include <string.h>

// seek to the start of a disk block and write size bytes of data there.
// returns 0 on success, -1 on failure
static int write_at_block(FILE *f, long block, const void *data, size_t size)
{
    // find the block
    if (fseek(f, block * SV_BLOCK_SIZE, SEEK_SET) != 0)
    {
        perror("Error seeking in disk image");
        return -1;
    }

    // write at block
    if (fwrite(data, size, 1, f) != 1)
    {
        perror("Error writing to disk image");
        return -1;
    }
    return 0;
}

static int write_superblock(FILE *f)
{
    svfs_super_t super;
    memset(&super, 0, sizeof(super));
    super.svs_magic = SV_MAGIC_NO;
    super.svs_free_inode = 1;
    super.svs_nfree = 127;
    super.svs_root_inode = 0;             // pointer to root inode
    super.svs_num_inodes = SV_NUM_INODES; // number of inodes in the system
    super.svs_version = 1;                // version number for consistency

    // fill in free block list
    for (int i = 0; i < SV_NFBLKS_PER_NODE; ++i)
    {
        super.svs_free_blocks[i] = SV_FIRST_FREE_BLOCK + i;
    }

    return write_at_block(f, SV_SUPER_BLOCK, &super, sizeof(super));
}

static int write_inodes(FILE *f)
{
    for (int i = 0; i < SV_NUM_INODES; ++i)
    {
        svfs_inode_t inode;
        memset(&inode, 0, sizeof(inode));

        // write inode fields
        if (i == 0)
        { // first one is root directory
            inode.svi_mode = SV_MODE_DIRECTORY;
            inode.svi_un.svi_size = 2 * sizeof(dirent_t);
            inode.svi_direct_blocks[0] = SV_DATA_START_BLOCK; // first data block after the inodes
            inode.svi_count = 2;                              // "." and ".." since root is itself and its own parent
        }
        else
        {
            inode.svi_mode = SV_MODE_FREE;
            if (i == SV_NUM_INODES - 1)
            {
                // last inode
                inode.svi_un.svi_next_free = -1;
            }
            else
            {
                inode.svi_un.svi_next_free = i + 1;
            }
        }
        inode.svi_number = i;

        if (write_at_block(f, SV_INODE_START_BLOCK + i, &inode, sizeof(inode)) != 0)
        {
            return -1;
        }
    }
    return 0;
}

static int write_root_dir(FILE *f)
{
    dirent_t entries[2];
    memset(entries, 0, sizeof(entries));

    // 0th entry = "."
    entries[0].d_ino = 0; // root itself
    strcpy(entries[0].d_name, ".");
    entries[0].d_off = sizeof(dirent_t); // where ".." starts, i.e. next entry

    // 1st entry = ".."
    entries[1].d_ino = 0; // root itself since it's its own parent
    strcpy(entries[1].d_name, "..");
    entries[1].d_off = 2 * sizeof(dirent_t); // whatever is after that

    return write_at_block(f, SV_DATA_START_BLOCK, entries, sizeof(entries));
}

// use a chain of blocks to store the free list
// since we have 128 total blocks and only 30 spots in the free block list
static int write_free_list(FILE *f)
{
    long c = SV_FIRST_FREE_BLOCK + SV_NFBLKS_PER_NODE - 1; // = 63, first block in the chain of blocks
    while (c != 0)
    {
        long array[SV_NFBLKS_PER_NODE];
        memset(array, 0, sizeof(array));
        for (long j = 0; j < SV_NFBLKS_PER_NODE; ++j)
        {
            if (c + 1 + j > SV_LAST_DATA_BLOCK) // stop at last data block
            {
                break;
            }
            array[j] = c + 1 + j;
        }
        if (write_at_block(f, c, array, sizeof(array)) != 0)
        { // error handling
            return -1;
        }
        c = array[SV_NFBLKS_PER_NODE - 1]; // recurse to the next level
    }

    return 0;
}

int main()
{
    FILE *f = fopen("sys/empty-fs.iso", "r+b");
    if (f == NULL)
    {
        perror("Error opening file");
        return 1;
    }

    // write all required parts
    if (write_superblock(f) != 0 ||
        write_inodes(f) != 0 ||
        write_root_dir(f) != 0 ||
        write_free_list(f) != 0)
    {
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}
