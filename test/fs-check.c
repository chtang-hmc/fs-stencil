#include "svfs.h"
#include "dirent.h"

#include <stdio.h>
#include <string.h>

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

static int print_superblock(FILE *f)
{
    svfs_super_t super;
    memset(&super, 0, sizeof(super));

    if (read_at_block(f, SV_SUPER_BLOCK, &super, sizeof(super)) != 0)
    { // error out
        return -1;
    }

    printf("svs_magic      = %ld\n", super.svs_magic);
    printf("svs_free_inode = %ld\n", super.svs_free_inode);
    printf("svs_nfree      = %ld\n", super.svs_nfree);
    printf("svs_root_inode = %ld\n", super.svs_root_inode);
    printf("svs_num_inodes = %ld\n", super.svs_num_inodes);
    printf("svs_version    = %ld\n", super.svs_version);
    return 0;
}

static int print_root_dir(FILE *f)
{
    dirent_t entries[2];
    memset(entries, 0, sizeof(entries));

    if (read_at_block(f, SV_DATA_START_BLOCK, entries, sizeof(entries)) != 0)
    { // error out
        return -1;
    }

    for (int i = 0; i < 2; ++i)
    {
        printf("dirent %d: ino=%ld off=%ld name=\"%s\"\n",
               i, entries[i].d_ino, entries[i].d_off, entries[i].d_name);
    }
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

    if (print_superblock(f) != 0 ||
        print_root_dir(f) != 0)
    {
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}
