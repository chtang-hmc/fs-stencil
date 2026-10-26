#include "mobj.h"

#include "pframe.h"

/*
 * Hints:
 *   - when we write the ops, we want to make sure that we copy the value
 *     of the parameters into the space that we've been provided
 */
void mobj_create(mobj_t *o, mobj_ops_t *ops)
{
    // copy the ops into the mobj
    o->mo_ops = *ops;

    // fill in list and lock
    list_init(&o->mo_pframes);
    spinlock_init(&o->mo_mutex);
}

/*
 * Hints:
 *   - in this function, where do we search for frame?
 *   - if we don't find the frame, set the pfp to NULL
 */
void mobj_find_pframe(mobj_t *o, long pagenum, pframe_t **pfp)
{
    spinlock_lock(&o->mo_mutex);

    // search the list for the page
    *pfp = NULL;
    list_link_t *link = o->mo_pframes.head;
    while (link != NULL)
    {
        pframe_t *pf = (pframe_t *)link->parent;
        if (pf->pf_pagenum == pagenum)
        {
            *pfp = pf;
            break;
        }
        link = link->next;
    }

    spinlock_unlock(&o->mo_mutex);
}

/*
 * Hints:
 *   - the frame might be in our list already...
 *   - we have access to a function to read the pframe from disk!
 *   - check how we get data from disk in sys/disk/disk.{h,c}...
 */
void mobj_get_pframe(mobj_t *o, long pagenum, pframe_t **pfp)
{
    // check if the frame is already loaded
    mobj_find_pframe(o, pagenum, pfp);
    if (*pfp != NULL)
    {
        return;
    }

    // read the frame with read_pframe
    pframe_t *pf = NULL;
    if (o->mo_ops.read_pframe(o, pagenum, &pf) < 0 || pf == NULL)
    { // error out
        *pfp = NULL;
        return;
    }

    pf->pf_pagenum = pagenum;
    list_link_init(&pf->pf_link, pf);

    // add the frame to the list
    spinlock_lock(&o->mo_mutex);
    list_insert_back(&o->mo_pframes, &pf->pf_link);
    spinlock_unlock(&o->mo_mutex);

    *pfp = pf;
}

// find the frame for a page in the list. caller must hold mo_mutex.
// returns the frame, or NULL if it is not loaded
static pframe_t *pframe_at(mobj_t *o, long page)
{
    list_link_t *link = o->mo_pframes.head;
    while (link != NULL)
    {
        pframe_t *pf = (pframe_t *)link->parent;
        if (pf->pf_pagenum == page)
        {
            return pf;
        }
        link = link->next;
    }
    return NULL;
}

/*
 * Hints:
 *   - pos describes where to read from in terms of bytes
 *   - len is the maximum number of bytes to read... when can we not read all
 *     of the way up to len?
 *   - save what we read to buf
 */
long mobj_read(mobj_t *o, long pos, long len, char *buf)
{
    long copied = 0;

    spinlock_lock(&o->mo_mutex);

    while (copied < len)
    {
        // find the page and offset
        long cur = pos + copied;
        long page = cur / FRAME_LEN;
        long off = cur % FRAME_LEN;

        pframe_t *pf = pframe_at(o, page);
        if (pf == NULL)
        { // page not loaded
            spinlock_unlock(&o->mo_mutex);
            return -1;
        }

        // copy from the page into buf
        long n = FRAME_LEN - off;
        if (n > len - copied)
        {
            n = len - copied;
        }
        for (long j = 0; j < n; j++)
        {
            buf[copied + j] = pf->bytes[off + j];
        }
        copied += n;
    }

    spinlock_unlock(&o->mo_mutex);
    return copied;
}

/*
 * Hints:
 *   - pos describes where to write to in terms of bytes
 *   - len is the maximum number of bytes to write... when can we not read all
 *     of the way up to len?
 *   - save what we read to buf
 */
long mobj_write(mobj_t *o, long pos, long len, const char *buf)
{
    // find the page and offset
    long page = pos / FRAME_LEN;
    long off = pos % FRAME_LEN;

    spinlock_lock(&o->mo_mutex);

    pframe_t *pf = pframe_at(o, page);
    if (pf == NULL)
    { // page not loaded
        spinlock_unlock(&o->mo_mutex);
        return -1;
    }

    // copy from buf into the page
    long n = FRAME_LEN - off;
    if (n > len)
    {
        n = len;
    }
    for (long j = 0; j < n; j++)
    {
        pf->bytes[off + j] = buf[j];
    }

    spinlock_unlock(&o->mo_mutex);
    return len;
}

/*
 * Hints:
 *   - make sure that disk is up to date in the places that we care about!
 */
void mobj_flush(mobj_t *o)
{
    spinlock_lock(&o->mo_mutex);

    // write each frame back to disk
    // use write_pframe since svfs sets flush_pframe to NULL
    list_link_t *link = o->mo_pframes.head;
    while (link != NULL)
    {
        pframe_t *pf = (pframe_t *)link->parent;
        o->mo_ops.write_pframe(o, pf);
        link = link->next;
    }

    spinlock_unlock(&o->mo_mutex);
}
