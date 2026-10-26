#include "vfs.h"
#include "vnode.h"
#include "dirent.h"

/*
 * TODO: implement me!
 */
long namev_lookup(vnode_t *dir, const char *name, long namelen,
                  vnode_t **out) {
    if (!out) return -1;
    *out = NULL;

    if (!dir || !name || namelen < 1 || namelen > NAME_LEN ||
        dir->vn_mode != DIRECTORY || !dir->vn_ops || !dir->vn_ops->lookup)
        return -1;

    return dir->vn_ops->lookup(dir, name, namelen, out);
}

/*
 * TODO: implement me!
 */
long namev_open(vnode_t *base, const char *path, vnode_t **res_vnode) {
    if (!res_vnode) return -1;
    *res_vnode = NULL;

    if (!path || !path[0]) return -1;

    vnode_t *cur = base;

    // start at root if path is absolute
    if (path[0] == '/')
        cur = base && base->vn_fs ? base->vn_fs->fs_root : vfs_root_fs.fs_root;

    if (!cur) return -1;

    long i = 0;

    while (path[i])
    {
        while (path[i] == '/') i++;
        if (!path[i]) break;

        long start = i;
        while (path[i] && path[i] != '/') i++;

        vnode_t *next = NULL;

        if (namev_get_child(cur, path + start, i - start, &next) != 0 || !next)
            return -1;

        cur = next;
    }

    if (path[i - 1] == '/' && cur->vn_mode != DIRECTORY)
        return -1;

    *res_vnode = cur;
    return 0;
}

/*
 * TODO: implement me!
 */
long namev_get_child(vnode_t *base, const char *name, long namelen,
                     vnode_t **out) {
     return namev_lookup(base, name, namelen, out);
}

/*
 * TODO: implement me!
 */
long namev_get_parent(vnode_t *base, vnode_t **out) {
   return namev_lookup(base, "..", 2, out);
}

/*
 * TODO: implement me!
 */
long namev_is_descendent(vnode_t *a, vnode_t *b) {
    if (!a || !b) return -1;
    if (a == b) return 1;
    if (a->vn_fs != b->vn_fs) return 0;

    if (b->vn_mode != DIRECTORY || !b->vn_ops || !b->vn_ops->readdir)
        return 0;

    long pos = 0;

    while (pos < b->vn_len)
    {
        dirent_t *d = NULL;

        if (b->vn_ops->readdir(b, pos, &d) != 0 || !d || d->d_off <= pos)
            return -1;

        pos = d->d_off;

        // skip "." and ".."
        if (d->d_name[0] == '.' &&
            (d->d_name[1] == '\0' ||
             (d->d_name[1] == '.' && d->d_name[2] == '\0')))
            continue;

        long len = 0;
        while (len < NAME_LEN && d->d_name[len]) len++;

        vnode_t *child = NULL;

        if (namev_get_child(b, d->d_name, len, &child) != 0 || !child)
            return -1;

        if (child == a) return 1;

        if (child->vn_mode == DIRECTORY)
        {
            long found = namev_is_descendent(a, child);
            if (found != 0) return found;
        }
    }

    return 0;
}
