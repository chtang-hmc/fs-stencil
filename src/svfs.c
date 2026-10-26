#include "svfs.h"

#include "dirent.h"
#include "sys/disk/disk.h"
#include "util/string.h"
#include "slab_allocator.h"
#include <stdio.h>

fs_ops_t svfs_fsops = {.fs_init = svfs_init,
                       .read_vnode = svfs_read_vnode};

static vnode_ops_t svfs_dir_vops = {.read = NULL,
                                    .write = NULL,
                                    .mknod = svfs_mknod,
                                    .mkdir = svfs_mkdir,
                                    .lookup = svfs_lookup,
                                    .readdir = svfs_readdir,
                                    .read_pframe = svfs_read_pframe,
                                    .write_pframe = svfs_write_pframe,
                                    .flush_pframe = NULL};

static vnode_ops_t svfs_file_vops = {.read = svfs_read,
                                     .write = svfs_write,
                                     .mknod = NULL,
                                     .mkdir = NULL,
                                     .lookup = NULL,
                                     .readdir = NULL,
                                     .read_pframe = svfs_read_pframe,
                                     .write_pframe = svfs_write_pframe,
                                     .flush_pframe = NULL};

static svfs_t svfs;
static slab_allocator_t svfs_node_allocator;

/*
 * TODO: implement me!
 *
 * Hints:
 *   - what is the main file system reference? how should our svfs instance
 *     refer to this main system?
 *   - it is your job here to implement both the fields of the svfs_t struct
 *     and the fs_t struct!
 */
void svfs_init()
{
    // read the super block
    char buf[SV_BLOCK_SIZE];
    read_disk_block(SV_SUPER_BLOCK, buf);
    memcpy(&svfs.svfs_super, buf, sizeof(svfs_super_t));
    if (svfs_check_super(&svfs.svfs_super) != 0)
    {
        fprintf(stderr, "File system failed to initiate\n");
        return;
    }

    // fill in svfs
    spinlock_init(&svfs.svfs_mutex);
    svfs.svfs_fs = &vfs_root_fs;

    // fill in vfs_root_fs
    vfs_root_fs.fs_ops = &svfs_fsops;
    list_init(&vfs_root_fs.vnode_list);
    spinlock_init(&vfs_root_fs.vnode_list_mutex);
    slab_allocator_init(&svfs_node_allocator, sizeof(svfs_node_t));
    vfs_root_fs.vnode_allocator = &svfs_node_allocator;

    // create the root vnode
    svfs_node_t *root = slab_obj_alloc(svfs_node_allocator);
    if (root == NULL)
        return;

    root->inode.svi_number = svfs.svfs_super.svs_root_inode;
    svfs_read_vnode(&vfs_root_fs, &root->vnode);

    if (root->inode.svi_mode != SV_MODE_DIRECTORY)
        return;

    vfs_root_fs.fs_root = &root->vnode;

    spinlock_lock(&vfs_root_fs.vnode_list_mutex);
    list_insert_back(&vfs_root_fs.vnode_list, &root->vnode.vn_link);
    spinlock_unlock(&vfs_root_fs.vnode_list_mutex);
}

/*
 * Initialize a svfs_node/vnode after reading a file from disk.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - how do we get data from disk?
 *   - what should the svfs_node/vnode fields be for a given file?
 *   - think about the distinction between normal file and directory!
 */
void svfs_read_vnode(fs_t *fs, vnode_t *vn)
{ 
    if (!fs || !vn) return;

    svfs_node_t *node = (svfs_node_t *)vn;
    long num = node->inode.svi_number;

    if (num < 0 || num >= SV_NUM_INODES) return;

    char buf[SV_BLOCK_SIZE];
    read_disk_block(SV_INODE_START_BLOCK + num, buf);
    memcpy(&node->inode, buf, sizeof(node->inode));

    if (node->inode.svi_number != num ||
        (node->inode.svi_mode != SV_MODE_REGULAR &&
         node->inode.svi_mode != SV_MODE_DIRECTORY))
        return;

    vn->vn_fs = fs;
    vn->vn_len = node->inode.svi_un.svi_size;

    if (node->inode.svi_mode == SV_MODE_DIRECTORY)
    {
        vn->vn_mode = DIRECTORY;
        vn->vn_ops = &svfs_dir_vops;
    }
    else
    {
        vn->vn_mode = REGULAR;
        vn->vn_ops = &svfs_file_vops;
    }

    vn->parent = node;
    node->dirtied_inode = 0;
    list_link_init(&vn->vn_link, vn);

    mobj_ops_t ops = {
        vnode_read_pframe,
        vnode_write_pframe,
        vnode_write_pframe
    };

    mobj_create(&vn->vn_mobj, &ops);
    vn->vn_mobj.parent = vn;
}


long svfs_check_super(svfs_super_t *super)
{
    if (super->svs_magic != SV_MAGIC_NO ||
        super->svs_num_inodes != SV_NUM_INODES ||
        super->svs_root_inode != 0 ||
        !(super->svs_free_inode == -1 ||
          (super->svs_free_inode >= 0 &&
           super->svs_free_inode < SV_NUM_INODES)) ||
        super->svs_nfree < 0 ||
        super->svs_nfree > SV_NUM_DATA_BLOCKS)
    {
        return -1;
    }
    return 0;
}


/*
 * On file creation, this is called to attach an inode to the newly created
 * file. Choose an unused inode from the file system. Returns 0 on succes,
 * -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - what is the next available inode?
 *   - where do we find the next available inode after we allocate this inode?
 */
long svfs_alloc_inode(svfs_t *fs)
{
   if (!fs) return -1;

    spinlock_lock(&fs->svfs_mutex);

    long num = fs->svfs_super.svs_free_inode;

    if (num <= 0 || num >= SV_NUM_INODES)
    {
        spinlock_unlock(&fs->svfs_mutex);
        return -1;
    }

    char buf[SV_BLOCK_SIZE];
    svfs_inode_t inode;

    read_disk_block(SV_INODE_START_BLOCK + num, buf);
    memcpy(&inode, buf, sizeof(inode));

    if (inode.svi_mode != SV_MODE_FREE || inode.svi_number != num)
    {
        spinlock_unlock(&fs->svfs_mutex);
        return -1;
    }

    // move to the next free inode
    fs->svfs_super.svs_free_inode = inode.svi_un.svi_next_free;

    read_disk_block(SV_SUPER_BLOCK, buf);
    memcpy(buf, &fs->svfs_super, sizeof(fs->svfs_super));
    write_disk_block(SV_SUPER_BLOCK, buf);

    spinlock_unlock(&fs->svfs_mutex);
    return 0;
}

/*
 * Find a directory entry from a svfs_node with the input name/namelen and
 * pass the associated svfs_node/vnode to the ent pointer. Return 0 on success,
 * else -1.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - how should we read a directory entry?
 */
long svfs_find_dirent(svfs_node_t *dir, const char *name, long namelen, svfs_node_t *ent)
{
    if (!dir || !name || !ent || namelen <= 0 ||
        namelen > NAME_LEN || dir->vnode.vn_mode != DIRECTORY)
        return -1;

    long pos = 0;

    while (pos < dir->vnode.vn_len)
    {
        dirent_t *d = NULL;

        if (svfs_readdir(&dir->vnode, pos, &d) != 0)
            return -1;

        long i = 0;
        while (i < namelen && d->d_name[i] == name[i])
            i++;

        if (i == namelen && d->d_name[i] == '\0')
        {
            if (d->d_ino < 0 || d->d_ino >= SV_NUM_INODES)
                return -1;

            char buf[SV_BLOCK_SIZE];
            read_disk_block(SV_INODE_START_BLOCK + d->d_ino, buf);
            memcpy(&ent->inode, buf, sizeof(ent->inode));

            if (ent->inode.svi_mode == SV_MODE_FREE ||
                ent->inode.svi_number != d->d_ino)
                return -1;

            return 0;
        }

        if (d->d_off <= pos)
            return -1;

        pos = d->d_off;
    }

    return -1;
}

/*
 * Read len bytes from the file associated with the vnode_t from position pos
 * and save the result into the buffer. Return 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - where is the data?
 *   - how do we access data from a svfs_node/vnode?
 */
long svfs_read(vnode_t *vn, long pos, void *buf, long len)
{
    if (!vn || pos < 0 || len < 0 || vn->vn_mode != REGULAR)
        return -1;

    if (len == 0 || pos >= vn->vn_len)
        return 0;

    if (!buf) return -1;

    if (len > vn->vn_len - pos)
        len = vn->vn_len - pos;

    long last = (pos + len - 1) / FRAME_LEN;

    // load the pages we need
    for (long i = pos / FRAME_LEN; i <= last; i++)
    {
        pframe_t *pf = NULL;
        mobj_get_pframe(&vn->vn_mobj, i, &pf);

        if (!pf) return -1;
    }

    if (mobj_read(&vn->vn_mobj, pos, len, (char *)buf) != len)
        return -1;

    return 0;
}

/*
 * Write len bytes from the buffer into the file at position pos. If the write
 * requires more bytes than are currently available, svfs should allocate
 * another data block to the file. Returns 0 on success, else -1.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - where do new data blocks come from?
 *   - where should we put the data relative to the vnode?
 */

 
static long svfs_get_block(void)
{
    spinlock_lock(&svfs.svfs_mutex);

    svfs_super_t *s = &svfs.svfs_super;
    long block = s->svs_free_blocks[0];

    if (s->svs_nfree <= 0 || block < SV_FIRST_FREE_BLOCK ||
        block > SV_LAST_DATA_BLOCK)
    {
        spinlock_unlock(&svfs.svfs_mutex);
        return -1;
    }

    // load more blocks if we reached the end of this list
    if (s->svs_nfree > 1 && s->svs_free_blocks[1] == 0)
    {
        char buf[SV_BLOCK_SIZE];
        read_disk_block(block, buf);
        memcpy(s->svs_free_blocks, buf, sizeof(s->svs_free_blocks));
    }
    else
    {
        for (int i = 0; i < SV_NFBLKS_PER_NODE - 1; i++)
            s->svs_free_blocks[i] = s->svs_free_blocks[i + 1];

        s->svs_free_blocks[SV_NFBLKS_PER_NODE - 1] = 0;
    }

    s->svs_nfree--;

    char buf[SV_BLOCK_SIZE];
    read_disk_block(SV_SUPER_BLOCK, buf);
    memcpy(buf, s, sizeof(*s));
    write_disk_block(SV_SUPER_BLOCK, buf);

    spinlock_unlock(&svfs.svfs_mutex);
    return block;
}

static long svfs_data_block(svfs_node_t *node, long page, int create)
{
    if (page < 0 ||
        page >= SV_NDIRECT_BLKS + SV_BLOCK_SIZE / sizeof(long))
        return -1;

    long block = 0;
    int fresh = 0;

    if (page < SV_NDIRECT_BLKS)
    {
        block = node->inode.svi_direct_blocks[page];

        if (block == 0 && create)
        {
            block = svfs_get_block();
            if (block < 0) return -1;

            node->inode.svi_direct_blocks[page] = block;
            fresh = 1;
        }
    }
    else
    {
        long indirect = node->inode.svi_indirect_block;

        if (indirect == 0 && create)
        {
            indirect = svfs_get_block();
            if (indirect < 0) return -1;

            node->inode.svi_indirect_block = indirect;

            char zeros[SV_BLOCK_SIZE] = {0};
            write_disk_block(indirect, zeros);
        }

        if (indirect < SV_DATA_START_BLOCK ||
            indirect > SV_LAST_DATA_BLOCK)
            return -1;

        long blocks[SV_BLOCK_SIZE / sizeof(long)];
        read_disk_block(indirect, (char *)blocks);

        long i = page - SV_NDIRECT_BLKS;
        block = blocks[i];

        if (block == 0 && create)
        {
            block = svfs_get_block();
            if (block < 0) return -1;

            blocks[i] = block;
            write_disk_block(indirect, (char *)blocks);
            fresh = 1;
        }
    }

    if (block < SV_DATA_START_BLOCK || block > SV_LAST_DATA_BLOCK)
        return -1;

    if (fresh)
    {
        char zeros[SV_BLOCK_SIZE] = {0};
        write_disk_block(block, zeros);
    }

    return block;
}


long svfs_write(vnode_t *vn, long pos, const void *buf, long len)
{
 if (!vn || pos < 0 || len < 0 || vn->vn_mode != REGULAR)
        return -1;

    if (len == 0) return 0;
    if (!buf) return -1;

    long max = (SV_NDIRECT_BLKS + SV_BLOCK_SIZE / sizeof(long))
               * SV_BLOCK_SIZE;

    if (pos > max || len > max - pos)
        return -1;

    svfs_node_t *node = (svfs_node_t *)vn;
    long end = pos + len;

    // allocate blocks if needed
    for (long i = 0; i <= (end - 1) / FRAME_LEN; i++)
    {
        if (svfs_data_block(node, i, 1) < 0)
            return -1;
    }

    // write the data to each page
    long written = 0;

    while (written < len)
    {
        long at = pos + written;
        long amount = FRAME_LEN - (at % FRAME_LEN);

        if (amount > len - written)
            amount = len - written;

        pframe_t *pf = NULL;
        mobj_get_pframe(&vn->vn_mobj, at / FRAME_LEN, &pf);

        if (!pf) return -1;

        if (mobj_write(&vn->vn_mobj, at, amount,
                       (const char *)buf + written) != amount)
            return -1;

        written += amount;
    }

    if (end > vn->vn_len)
    {
        vn->vn_len = end;
        node->inode.svi_un.svi_size = end;
    }

    // save the inode
    char data[SV_BLOCK_SIZE];
    read_disk_block(SV_INODE_START_BLOCK + node->inode.svi_number, data);
    memcpy(data, &node->inode, sizeof(node->inode));
    write_disk_block(SV_INODE_START_BLOCK + node->inode.svi_number, data);

    return 0;
}

/*
 * Creates a new entry for a file (e.g., a call to "touch") for a file that
 * does not yet exist. Creates the file in the provided directory with the
 * the provided name and passes the newly created vnode to the out parameter.
 * Returns 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - don't forget to add the newly created vnode to any global tracking
 *     mechanisms!
 */
 
long svfs_mknod(vnode_t *dir, const char *name, long namelen, vnode_t **out)
{
    if (!dir || !name || !out || !dir->vn_fs ||
        dir->vn_mode != DIRECTORY || namelen < 1 || namelen > NAME_LEN)
        return -1;

    *out = NULL;

    for (long i = 0; i < namelen; i++)
        if (name[i] == '/' || name[i] == '\0') return -1;

    if ((namelen == 1 && name[0] == '.') ||
        (namelen == 2 && name[0] == '.' && name[1] == '.'))
        return -1;

    svfs_node_t found;
    if (svfs_find_dirent((svfs_node_t *)dir, name, namelen, &found) == 0)
        return -1;

    // get a free inode
    long num = svfs.svfs_super.svs_free_inode;
    if (num <= 0 || num >= SV_NUM_INODES || svfs_alloc_inode(&svfs) != 0)
        return -1;

    svfs_node_t *node = slab_obj_alloc(svfs_node_allocator);
    if (!node) return -1;

    *node = (svfs_node_t){0};
    node->inode.svi_number = num;
    node->inode.svi_mode = SV_MODE_REGULAR;
    node->inode.svi_count = 1;

    char buf[SV_BLOCK_SIZE] = {0};
    memcpy(buf, &node->inode, sizeof(node->inode));
    write_disk_block(SV_INODE_START_BLOCK + num, buf);

    svfs_read_vnode(dir->vn_fs, &node->vnode);
    if (!node->vnode.vn_ops) return -1;

    // add the file to the parent directory
    long pos = dir->vn_len;
    long off = pos % SV_BLOCK_SIZE;

    if (off + sizeof(dirent_t) > SV_BLOCK_SIZE)
        pos += SV_BLOCK_SIZE - off;

    long page = pos / SV_BLOCK_SIZE;
    if (svfs_data_block((svfs_node_t *)dir, page, 1) < 0)
        return -1;

    pframe_t *pf = NULL;
    mobj_get_pframe(&dir->vn_mobj, page, &pf);
    if (!pf) return -1;

    dirent_t d = {0};
    d.d_ino = num;

    for (long i = 0; i < namelen; i++)
        d.d_name[i] = name[i];

    d.d_off = pos + sizeof(d);
    long next = d.d_off % SV_BLOCK_SIZE;

    if (next + sizeof(d) > SV_BLOCK_SIZE)
        d.d_off += SV_BLOCK_SIZE - next;

    memcpy(pf->bytes + pos % SV_BLOCK_SIZE, &d, sizeof(d));
    mobj_flush(&dir->vn_mobj);

    // update parent directory size
    dir->vn_len = pos + sizeof(d);
    svfs_node_t *parent = (svfs_node_t *)dir;
    parent->inode.svi_un.svi_size = dir->vn_len;

    read_disk_block(SV_INODE_START_BLOCK + parent->inode.svi_number, buf);
    memcpy(buf, &parent->inode, sizeof(parent->inode));
    write_disk_block(SV_INODE_START_BLOCK + parent->inode.svi_number, buf);

    spinlock_lock(&dir->vn_fs->vnode_list_mutex);
    list_insert_back(&dir->vn_fs->vnode_list, &node->vnode.vn_link);
    spinlock_unlock(&dir->vn_fs->vnode_list_mutex);

    *out = &node->vnode;
    return 0;
}


/*
 * Creates a new entry for a directory for a file that does not yet exist.
 * Creates the file in the provided directory with theprovided name and passes
 * the newly created vnode to the out parameter.  Returns 0 on success, -1 on
 * failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - don't forget to add the newly created vnode to any global tracking
 *     mechanisms!
 */
long svfs_mkdir(vnode_t *dir, const char *name, long namelen, vnode_t **out)
{
    if (!out) return -1;
    *out = NULL;

    vnode_t *vn = NULL;
    if (svfs_mknod(dir, name, namelen, &vn) != 0)
        return -1;

    svfs_node_t *node = (svfs_node_t *)vn;

    // change the new file into a directory
    node->inode.svi_mode = SV_MODE_DIRECTORY;
    node->inode.svi_count = 2;
    vn->vn_mode = DIRECTORY;
    vn->vn_ops = &svfs_dir_vops;

    // give the directory its first data block
    if (svfs_data_block(node, 0, 1) < 0)
        return -1;

    pframe_t *pf = NULL;
    mobj_get_pframe(&vn->vn_mobj, 0, &pf);
    if (!pf) return -1;

    // create "." and ".."
    dirent_t entries[2] = {0};

    entries[0].d_ino = node->inode.svi_number;
    entries[0].d_off = sizeof(dirent_t);
    entries[0].d_name[0] = '.';

    entries[1].d_ino = ((svfs_node_t *)dir)->inode.svi_number;
    entries[1].d_off = 2 * sizeof(dirent_t);
    entries[1].d_name[0] = '.';
    entries[1].d_name[1] = '.';

    memcpy(pf->bytes, entries, sizeof(entries));
    mobj_flush(&vn->vn_mobj);

    vn->vn_len = sizeof(entries);
    node->inode.svi_un.svi_size = vn->vn_len;

    // save the new directory inode
    char buf[SV_BLOCK_SIZE];
    read_disk_block(SV_INODE_START_BLOCK + node->inode.svi_number, buf);
    memcpy(buf, &node->inode, sizeof(node->inode));
    write_disk_block(SV_INODE_START_BLOCK + node->inode.svi_number, buf);

    // update parent's link count
    svfs_node_t *parent = (svfs_node_t *)dir;
    parent->inode.svi_count++;

    read_disk_block(SV_INODE_START_BLOCK + parent->inode.svi_number, buf);
    memcpy(buf, &parent->inode, sizeof(parent->inode));
    write_disk_block(SV_INODE_START_BLOCK + parent->inode.svi_number, buf);

    *out = vn;
    return 0;

}

/*
 * Search for a given entry within a directory. If found, entry is passed back
 * via the out pointer. Returns 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - recall that the data in a directory is a series of dirents (defined in
 *     dirent.h)
 *   - what happens if we can't find the directory?
 */
long svfs_lookup(vnode_t *dir, const char *name, long namelen, vnode_t **out)
{
    if (!dir || !name || !out || !dir->vn_fs ||
        dir->vn_mode != DIRECTORY || namelen < 1 || namelen > NAME_LEN)
        return -1;

    *out = NULL;

    svfs_node_t ent;
    if (svfs_find_dirent((svfs_node_t *)dir, name, namelen, &ent) != 0)
        return -1;

    fs_t *fs = dir->vn_fs;
    long num = ent.inode.svi_number;

    // check if vnode is already loaded
    spinlock_lock(&fs->vnode_list_mutex);

    list_link_t *link = fs->vnode_list.head;
    while (link)
    {
        vnode_t *vn = link->parent;

        if (((svfs_node_t *)vn)->inode.svi_number == num)
        {
            *out = vn;
            spinlock_unlock(&fs->vnode_list_mutex);
            return 0;
        }

        link = link->next;
    }

    spinlock_unlock(&fs->vnode_list_mutex);

    // otherwise create a vnode for it
    svfs_node_t *node = slab_obj_alloc(svfs_node_allocator);
    if (!node) return -1;

    *node = (svfs_node_t){0};
    node->inode.svi_number = num;

    svfs_read_vnode(fs, &node->vnode);
    if (!node->vnode.vn_ops) return -1;

    spinlock_lock(&fs->vnode_list_mutex);
    list_insert_back(&fs->vnode_list, &node->vnode.vn_link);
    spinlock_unlock(&fs->vnode_list_mutex);

    *out = &node->vnode;
    return 0;
}

/*
 * Retrieve the directory entry at the location specified by pos in the
 * provided directory. Return 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - index the data associated with the directory
 *   - what happens if pos is not aligned to the dirent?
 */
long svfs_readdir(vnode_t *vn, long pos, dirent_t **out)
{
    if (!vn || !out || vn->vn_mode != DIRECTORY || pos < 0)
        return -1;

    *out = NULL;

    if (pos >= vn->vn_len ||
        sizeof(dirent_t) > vn->vn_len - pos)
        return -1;

    long page = pos / SV_BLOCK_SIZE;
    long offset = pos % SV_BLOCK_SIZE;

    // entries should not cross block boundaries
    if (offset % sizeof(dirent_t) != 0 ||
        offset + sizeof(dirent_t) > SV_BLOCK_SIZE)
        return -1;

    pframe_t *pf = NULL;
    mobj_get_pframe(&vn->vn_mobj, page, &pf);

    if (!pf) return -1;

    *out = (dirent_t *)(pf->bytes + offset);
    return 0;
}

/*
 * Read the data associated with a file from disk. Return 0 on success, -1 on
 * error.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - are there helper functions that can help us out?
 *   - how do we access these helpers?
 */
long svfs_read_pframe(vnode_t *vnode, long pagenum, pframe_t **pfp)
{
    if (vnode == NULL || pfp == NULL || pagenum < 0)
        return -1;

    *pfp = NULL;
    svfs_node_t *node = (svfs_node_t *)vnode;
    long block;

    // finds which disk block holds this page
    if (pagenum < SV_NDIRECT_BLKS)
    {
        block = node->inode.svi_direct_blocks[pagenum];
    }
    else
    {
        long idx = pagenum - SV_NDIRECT_BLKS;
        if (idx >= SV_BLOCK_SIZE / sizeof(long))
            return -1;

        long indirect = node->inode.svi_indirect_block;
        if (indirect < SV_DATA_START_BLOCK || indirect > SV_LAST_DATA_BLOCK)
            return -1;

        long block_nums[SV_BLOCK_SIZE / sizeof(long)];
        read_disk_block(indirect, (char *)block_nums);
        block = block_nums[idx];
    }

    if (block < SV_DATA_START_BLOCK || block > SV_LAST_DATA_BLOCK)
        return -1;

    // makes a new frame to store the data
    slab_allocator_t alloc;
    slab_allocator_init(&alloc, sizeof(pframe_t));
    pframe_t *pf = slab_obj_alloc(alloc);

    if (pf == NULL)
        return -1;

    // reads the disk block into the frame
    read_disk_block(block, pf->bytes);
    pf->pf_pagenum = pagenum;
    pf->pf_addr = pf->bytes;

    *pfp = pf;
    return 0;
}

/*
 * Write pframe to disk. Return 0 on success, -1 on error.
 *
 * TODO: implement me!
 */
long svfs_write_pframe(vnode_t *vn, pframe_t *pf)
{
    
    if (!vn || !pf) return -1;

    long block = svfs_data_block((svfs_node_t *)vn, pf->pf_pagenum, 0);

    if (block < 0) return -1;

    write_disk_block(block, pf->bytes);
    return 0;

}
