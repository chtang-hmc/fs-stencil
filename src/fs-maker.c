#include "svfs.h"
#include "dirent.h"

#include <stdio.h>
#include <string.h>

int main()
{
    // create the super struct
    svfs_super_t super;
    memset(&super, 0, sizeof(super));
    super.svs_magic = SV_MAGIC_NO;
    super.svs_free_inode = 1;
    super.svs_nfree = 127;
    super.svs_root_inode = 0;  // pointer to root inode
    super.svs_num_inodes = SV_NUM_INODES; // number of inodes in the system
    super.svs_version = 1;     // version number for consistency

    // write super to file
    FILE *f = fopen("sys/empty-fs.iso", "r+b");
    if (f == NULL)
    {
        perror("Error opening file");
        return 1;
    }
    fseek(f, 0, SEEK_SET);
    fwrite(&super, sizeof(super), 1, f);

    // write the inodes
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
            inode.svi_count = 2;             // "." and ".." since root is itself and its own parent
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

        fseek(f, (SV_INODE_START_BLOCK + i) * SV_BLOCK_SIZE, SEEK_SET);
        fwrite(&inode, sizeof(inode), 1, f);
    }

    fclose(f);
}