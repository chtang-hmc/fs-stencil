#include "svfs.h"

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
    super.svs_root_inode = 0; // pointer to root inode
    super.svs_num_inodes = 32; // number of inodes in the system
    super.svs_version = 1; // version number for consistency

    // write super to file
    FILE *f = fopen("sys/empty-fs.iso", "r+b");
    if (f == NULL)
    {
        perror("Error opening file");
        return 1;
    }
    fseek(f, 0, SEEK_SET);
    fwrite(&super, sizeof(super), 1, f);
    fclose(f);
}