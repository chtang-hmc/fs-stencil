#include "svfs.h"

#include <stdio.h>
#include <string.h>

// same length on purpose, so the second write does not grow the file
static const char saved[] = "kept for later";
static const char dirty[] = "lost in memory";
static const long payload_len = sizeof(saved) - 1;

static const char *filename = "draft";
static const char *path = "/draft";

// start the file system and hand back its root. returns NULL on failure
static vnode_t *launch(void)
{
    svfs_init();

    vnode_t *root = vfs_root_fs.fs_root;
    if (root == NULL || root->vn_ops == NULL)
    {
        fprintf(stderr, "file system did not come up\n");
        return NULL;
    }
    return root;
}

// save one line, then scribble over it in memory and leave that unsaved
static int test_write(void)
{
    if (sizeof(saved) != sizeof(dirty))
    {
        fprintf(stderr, "the two lines have to be the same length\n");
        return 1;
    }

    vnode_t *root = launch();
    if (root == NULL || root->vn_ops->mknod == NULL)
    { // error out
        return 1;
    }

    vnode_t *file = NULL;
    if (root->vn_ops->mknod(root, filename, 5, &file) != 0 || file == NULL)
    {
        fprintf(stderr, "could not create %s\n", filename);
        return 1;
    }

    if (file->vn_ops == NULL || file->vn_ops->write == NULL ||
        file->vn_ops->write(file, 0, saved, payload_len) != 0)
    {
        fprintf(stderr, "could not write %s\n", filename);
        return 1;
    }

    // this is the version a later run should still see
    mobj_flush(&file->vn_mobj);
    mobj_flush(&root->vn_mobj);

    if (file->vn_ops->write(file, 0, dirty, payload_len) != 0)
    {
        fprintf(stderr, "could not overwrite %s\n", filename);
        return 1;
    }

    printf("flushed \"%s\", left \"%s\" unsaved\n", saved, dirty);
    return 0;
}

// start over and check that only the flushed line survived
static int test_read(void)
{
    vnode_t *root = launch();
    if (root == NULL)
    { // error out
        return 1;
    }

    vnode_t *file = NULL;
    if (namev_open(root, path, &file) != 0 || file == NULL)
    {
        fprintf(stderr, "could not open %s\n", path);
        return 1;
    }

    char buf[sizeof(saved)];
    memset(buf, 0, sizeof(buf));

    if (file->vn_ops == NULL || file->vn_ops->read == NULL ||
        file->vn_ops->read(file, 0, buf, payload_len) != 0)
    {
        fprintf(stderr, "could not read %s\n", path);
        return 1;
    }

    if (memcmp(buf, saved, payload_len) != 0)
    {
        fprintf(stderr, "read back \"%s\", wanted \"%s\"\n", buf, saved);
        return 1;
    }

    printf("later run kept \"%s\"\n", saved);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "write") == 0)
    {
        return test_write();
    }
    if (argc == 2 && strcmp(argv[1], "read") == 0)
    {
        return test_read();
    }

    fprintf(stderr, "usage: %s write|read\n", argv[0]);
    return 1;
}
