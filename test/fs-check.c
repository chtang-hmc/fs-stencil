#include "svfs.h"

#include <stdio.h>
#include <string.h>
#include <string.h>

int main()
{
    FILE *f = fopen("sys/empty-fs.iso", "rb");
    if (f == NULL)
    {
        perror("Error opening file");
        return 1;
    }
    svfs_super_t super;
    memset(&super, 0, sizeof(super));

    // read from file
    fseek(f, 0, SEEK_SET);
    fread(&super, sizeof(super), 1, f);
    printf("svs_magic      = %ld\n", super.svs_magic);
    printf("svs_free_inode = %ld\n", super.svs_free_inode);
    printf("svs_nfree      = %ld\n", super.svs_nfree);
    printf("svs_root_inode = %ld\n", super.svs_root_inode);
    printf("svs_num_inodes = %ld\n", super.svs_num_inodes);
    printf("svs_version    = %ld\n", super.svs_version);
    fclose(f);
}