/*
 * A Remote Heap.  Remote means that we don't touch the memory that the
 * heap points to. Normal heap implementations use the memory they manage
 * to place their list. We cannot do that because the memory we manage may
 * have special properties, for example it is uncachable or of different
 * endianess.
 *
 * Author: Pantelis Antoniou <panto@intracom.gr>
 *
 * 2004 (c) INTRACOM S.A. Greece. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <linux/fsl/rheap.h>

/*
 * Fixup a list_head, needed when copying lists.  If the pointers fall
 * between s and e, apply the delta.  This assumes that
 * sizeof(struct list_head *) == sizeof(unsigned long *).
 */
static inline void fixup(unsigned long s, unsigned long e, int d,
			 struct list_head *l)
{
	unsigned long *pp;

	pp = (unsigned long *)&l->next;
	if (*pp >= s && *pp < e)
		*pp += d;

	pp = (unsigned long *)&l->prev;
	if (*pp >= s && *pp < e)
		*pp += d;
}

/* Grow the allocated blocks */
static int grow(struct _rh_info *info, int max_blocks)
{
	struct _rh_block *block, *blk;
	int i, new_blocks;
	int delta;
	unsigned long blks, blke;

	if (max_blocks <= info->max_blocks)
		return -EINVAL;

	new_blocks = max_blocks - info->max_blocks;

	block = kmalloc(sizeof(struct _rh_block) * max_blocks, GFP_ATOMIC);
	if (block == NULL)
		return -ENOMEM;

	if (info->max_blocks > 0) {

		/* copy old block area */
		memcpy(block, info->block,
		       sizeof(struct _rh_block) * info->max_blocks);

		delta = (char *)block - (char *)info->block;

		/* and fixup list pointers */
		blks = (unsigned long)info->block;
		blke = (unsigned long)(info->block + info->max_blocks);

		for (i = 0, blk = block; i < info->max_blocks; i++, blk++)
			fixup(blks, blke, delta, &blk->list);

		fixup(blks, blke, delta, &info->empty_list);
		fixup(blks, blke, delta, &info->free_list);
		fixup(blks, blke, delta, &info->taken_list);

		/* free the old allocated memory */
		if ((info->flags & RHIF_STATIC_BLOCK) == 0)
			kfree(info->block);
	}

	info->block = block;
	info->empty_slots += new_blocks;
	info->max_blocks = max_blocks;
	info->flags &= ~RHIF_STATIC_BLOCK;

	/* add all new blocks to the free list */
	blk = block + info->max_blocks - new_blocks;
	for (i = 0; i < new_blocks; i++, blk++)
		list_add(&blk->list, &info->empty_list);

	return 0;
}

/*
 * Assure at least the required amount of empty slots.  If this function
 * causes a grow in the block area then all pointers kept to the block
 * area are invalid!
 */
static int assure_empty(struct _rh_info *info, int slots)
{
	int max_blocks;

	/* This function is not meant to be used to grow uncontrollably */
	if (slots >= 4)
		return -EINVAL;

	/* Enough space */
	if (info->empty_slots >= slots)
		return 0;

	/* Next 16 sized block */
	max_blocks = ((info->max_blocks + slots) + 15) & ~15;

	return grow(info, max_blocks);
}

static struct _rh_block *get_slot(struct _rh_info *info)
{
	struct _rh_block *blk;

	/* If no more free slots, and failure to extend. */
	/* XXX: You should have called assure_empty before */
	if (info->empty_slots == 0) {
		pr_err("rh: out of slots; crash is imminent.\n");
		return NULL;
	}

	/* Get empty slot to use */
	blk = list_entry(info->empty_list.next, struct _rh_block, list);
	list_del_init(&blk->list);
	info->empty_slots--;

	/* Initialize */
	blk->start = 0;
	blk->size = 0;
	blk->owner = NULL;

	return blk;
}

static inline void release_slot(struct _rh_info *info, struct _rh_block *blk)
{
	list_add(&blk->list, &info->empty_list);
	info->empty_slots++;
}

static void attach_free_block(struct _rh_info *info, struct _rh_block *blkn)
{
	struct _rh_block *blk;
	struct _rh_block *before;
	struct _rh_block *after;
	struct _rh_block *next;
	int size;
	unsigned long s, e, bs, be;
	struct list_head *l;

	/* We assume that they are aligned properly */
	size = blkn->size;
	s = blkn->start;
	e = s + size;

	/* Find the blocks immediately before and after the given one
	 * (if any) */
	before = NULL;
	after = NULL;
	next = NULL;

	list_for_each(l, &info->free_list) {
		blk = list_entry(l, struct _rh_block, list);

		bs = blk->start;
		be = bs + blk->size;

		if (next == NULL && s >= bs)
			next = blk;

		if (be == s)
			before = blk;

		if (e == bs)
			after = blk;

		/* If both are not null, break now */
		if (before != NULL && after != NULL)
			break;
	}

	/* Now check if they are really adjacent */
	if (before && s != (before->start + before->size))
		before = NULL;

	if (after && e != after->start)
		after = NULL;

	/* No coalescing; list insert and return */
	if (before == NULL && after == NULL) {

		if (next != NULL)
			list_add(&blkn->list, &next->list);
		else
			list_add(&blkn->list, &info->free_list);

		return;
	}

	/* We don't need it anymore */
	release_slot(info, blkn);

	/* Grow the before block */
	if (before != NULL && after == NULL) {
		before->size += size;
		return;
	}

	/* Grow the after block backwards */
	if (before == NULL && after != NULL) {
		after->start -= size;
		after->size += size;
		return;
	}

	/* Grow the before block, and release the after block */
	before->size += size + after->size;
	list_del(&after->list);
	release_slot(info, after);
}

static void attach_taken_block(struct _rh_info *info, struct _rh_block *blkn)
{
	struct _rh_block *blk;
	struct list_head *l;

	/* Find the block immediately before the given one (if any) */
	list_for_each(l, &info->taken_list) {
		blk = list_entry(l, struct _rh_block, list);
		if (blk->start > blkn->start) {
			list_add_tail(&blkn->list, &blk->list);
			return;
		}
	}

	list_add_tail(&blkn->list, &info->taken_list);
}

/*
 * Create a remote heap dynamically.  Note that no memory for the blocks
 * are allocated.  It will upon the first allocation
 */
struct _rh_info *rh_create(unsigned int alignment)
{
	struct _rh_info *info;

	/* Alignment must be a power of two */
	if ((alignment & (alignment - 1)) != 0)
		return ERR_PTR(-EINVAL);

	info = kmalloc(sizeof(*info), GFP_ATOMIC);
	if (info == NULL)
		return ERR_PTR(-ENOMEM);

	info->alignment = alignment;

	/* Initially everything as empty */
	info->block = NULL;
	info->max_blocks = 0;
	info->empty_slots = 0;
	info->flags = 0;

	INIT_LIST_HEAD(&info->empty_list);
	INIT_LIST_HEAD(&info->free_list);
	INIT_LIST_HEAD(&info->taken_list);

	return info;
}
EXPORT_SYMBOL_GPL(rh_create);

/*
 * Destroy a dynamically created remote heap.  Deallocate only if the areas
 * are not static
 */
void rh_destroy(struct _rh_info *info)
{
	if ((info->flags & RHIF_STATIC_BLOCK) == 0 && info->block != NULL)
		kfree(info->block);

	if ((info->flags & RHIF_STATIC_INFO) == 0)
		kfree(info);
}
EXPORT_SYMBOL_GPL(rh_destroy);

/*
 * Initialize in place a remote heap info block.  This is needed to support
 * operation very early in the startup of the kernel, when it is not yet safe
 * to call kmalloc.
 */
void rh_init(struct _rh_info *info, unsigned int alignment, int max_blocks,
	     struct _rh_block *block)
{
	int i;
	struct _rh_block *blk;

	/* Alignment must be a power of two */
	if ((alignment & (alignment - 1)) != 0)
		return;

	info->alignment = alignment;

	/* Initially everything as empty */
	info->block = block;
	info->max_blocks = max_blocks;
	info->empty_slots = max_blocks;
	info->flags = RHIF_STATIC_INFO | RHIF_STATIC_BLOCK;

	INIT_LIST_HEAD(&info->empty_list);
	INIT_LIST_HEAD(&info->free_list);
	INIT_LIST_HEAD(&info->taken_list);

	/* Add all new blocks to the free list */
	for (i = 0, blk = block; i < max_blocks; i++, blk++)
		list_add(&blk->list, &info->empty_list);
}
EXPORT_SYMBOL_GPL(rh_init);

/* Attach a free memory region, coalesces regions if adjuscent */
int rh_attach_region(struct _rh_info *info, unsigned long start, int size)
{
	struct _rh_block *blk;
	unsigned long s, e, m;
	int r;

	/* The region must be aligned */
	s = start;
	e = s + size;
	m = info->alignment - 1;

	/* Round start up */
	s = (s + m) & ~m;

	/* Round end down */
	e = e & ~m;

	if (IS_ERR_VALUE(e) || (e < s))
		return -ERANGE;

	/* Take final values */
	start = s;
	size = e - s;

	/* Grow the blocks, if needed */
	r = assure_empty(info, 1);
	if (r < 0)
		return r;

	blk = get_slot(info);
	blk->start = start;
	blk->size = size;
	blk->owner = NULL;

	attach_free_block(info, blk);

	return 0;
}
EXPORT_SYMBOL_GPL(rh_attach_region);

/* Detatch given address range, splits free block if needed. */
unsigned long rh_detach_region(struct _rh_info *info, unsigned long start,
			       int size)
{
	struct list_head *l;
	struct _rh_block *blk, *newblk;
	unsigned long s, e, m, bs, be;

	/* Validate size */
	if (size <= 0)
		return (unsigned long) -EINVAL;

	/* The region must be aligned */
	s = start;
	e = s + size;
	m = info->alignment - 1;

	/* Round start up */
	s = (s + m) & ~m;

	/* Round end down */
	e = e & ~m;

	if (assure_empty(info, 1) < 0)
		return (unsigned long) -ENOMEM;

	blk = NULL;
	list_for_each(l, &info->free_list) {
		blk = list_entry(l, struct _rh_block, list);
		/* The range must lie entirely inside one free block */
		bs = blk->start;
		be = blk->start + blk->size;
		if (s >= bs && e <= be)
			break;
		blk = NULL;
	}

	if (blk == NULL)
		return (unsigned long) -ENOMEM;

	/* Perfect fit */
	if (bs == s && be == e) {
		/* Delete from free list, release slot */
		list_del(&blk->list);
		release_slot(info, blk);
		return s;
	}

	/* blk still in free list, with updated start and/or size */
	if (bs == s || be == e) {
		if (bs == s)
			blk->start += size;
		blk->size -= size;

	} else {
		/* The front free fragment */
		blk->size = s - bs;

		/* the back free fragment */
		newblk = get_slot(info);
		newblk->start = e;
		newblk->size = be - e;

		list_add(&newblk->list, &blk->list);
	}

	return s;
}
EXPORT_SYMBOL_GPL(rh_detach_region);

/* Allocate a block of memory at the specified alignment.  The value returned
 * is an offset into the buffer initialized by rh_init(), or a negative number
 * if there is an error.
 */
unsigned long rh_alloc_align(struct _rh_info *info, int size,
			    int alignment, const char *owner)
{
	struct list_head *l;
	struct _rh_block *blk;
	struct _rh_block *newblk;
	unsigned long start, sp_size;

	/* Validate size, and alignment must be power of two */
	if (size <= 0 || (alignment & (alignment - 1)) != 0)
		return (unsigned long) -EINVAL;

	/* Align to configured alignment */
	size = (size + (info->alignment - 1)) & ~(info->alignment - 1);

	if (assure_empty(info, 2) < 0)
		return (unsigned long) -ENOMEM;

	blk = NULL;
	list_for_each(l, &info->free_list) {
		blk = list_entry(l, struct _rh_block, list);
		if (size <= blk->size) {
			start = (blk->start + alignment - 1) & ~(alignment - 1);
			if (start + size <= blk->start + blk->size)
				break;
		}
		blk = NULL;
	}

	if (blk == NULL)
		return (unsigned long) -ENOMEM;

	/* Just fits */
	if (blk->size == size) {
		/* Move from free list to taken list */
		list_del(&blk->list);
		newblk = blk;
	} else {
		/* Fragment caused, split if needed */
		/* Create block for fragment in the beginning */
		sp_size = start - blk->start;
		if (sp_size) {
			struct _rh_block *spblk;

			spblk = get_slot(info);
			spblk->start = blk->start;
			spblk->size = sp_size;
			/* add before the blk */
			list_add(&spblk->list, blk->list.prev);
		}
		newblk = get_slot(info);
		newblk->start = start;
		newblk->size = size;

		/* blk still in free list, with updated start and size
		 * for fragment in the end */
		blk->start = start + size;
		blk->size -= sp_size + size;
		/* No fragment in the end, remove blk */
		if (blk->size == 0) {
			list_del(&blk->list);
			release_slot(info, blk);
		}
	}

	newblk->owner = owner;
	attach_taken_block(info, newblk);

	return start;
}
EXPORT_SYMBOL_GPL(rh_alloc_align);

/* Allocate a block of memory at the default alignment.  The value returned is
 * an offset into the buffer initialized by rh_init(), or a negative number if
 * there is an error.
 */
unsigned long rh_alloc(struct _rh_info *info, int size, const char *owner)
{
	return rh_alloc_align(info, size, info->alignment, owner);
}
EXPORT_SYMBOL_GPL(rh_alloc);

/* Allocate a block of memory at the given offset, rounded up to the default
 * alignment.  The value returned is an offset into the buffer initialized by
 * rh_init(), or a negative number if there is an error.
 */
unsigned long rh_alloc_fixed(struct _rh_info *info, unsigned long start,
			    int size, const char *owner)
{
	struct list_head *l;
	struct _rh_block *blk, *newblk1, *newblk2;
	unsigned long s, e, m, bs = 0, be = 0;

	/* Validate size */
	if (size <= 0)
		return (unsigned long) -EINVAL;

	/* The region must be aligned */
	s = start;
	e = s + size;
	m = info->alignment - 1;

	/* Round start up */
	s = (s + m) & ~m;

	/* Round end down */
	e = e & ~m;

	if (assure_empty(info, 2) < 0)
		return (unsigned long) -ENOMEM;

	blk = NULL;
	list_for_each(l, &info->free_list) {
		blk = list_entry(l, struct _rh_block, list);
		/* The range must lie entirely inside one free block */
		bs = blk->start;
		be = blk->start + blk->size;
		if (s >= bs && e <= be)
			break;
		blk = NULL;
	}

	if (blk == NULL)
		return (unsigned long) -ENOMEM;

	/* Perfect fit */
	if (bs == s && be == e) {
		/* Move from free list to taken list */
		list_del(&blk->list);
		blk->owner = owner;

		start = blk->start;
		attach_taken_block(info, blk);

		return start;

	}

	/* blk still in free list, with updated start and/or size */
	if (bs == s || be == e) {
		if (bs == s)
			blk->start += size;
		blk->size -= size;

	} else {
		/* The front free fragment */
		blk->size = s - bs;

		/* The back free fragment */
		newblk2 = get_slot(info);
		newblk2->start = e;
		newblk2->size = be - e;

		list_add(&newblk2->list, &blk->list);
	}

	newblk1 = get_slot(info);
	newblk1->start = s;
	newblk1->size = e - s;
	newblk1->owner = owner;

	start = newblk1->start;
	attach_taken_block(info, newblk1);

	return start;
}
EXPORT_SYMBOL_GPL(rh_alloc_fixed);

/* Deallocate the memory previously allocated by one of the rh_alloc functions.
 * The return value is the size of the deallocated block, or a negative number
 * if there is an error.
 */
int rh_free(struct _rh_info *info, unsigned long start)
{
	struct _rh_block *blk, *blk2;
	struct list_head *l;
	int size;

	/* Linear search for block */
	blk = NULL;
	list_for_each(l, &info->taken_list) {
		blk2 = list_entry(l, struct _rh_block, list);
		if (start < blk2->start)
			break;
		blk = blk2;
	}

	if (blk == NULL || start > (blk->start + blk->size))
		return -EINVAL;

	/* Remove from taken list */
	list_del(&blk->list);

	/* Get size of freed block */
	size = blk->size;
	attach_free_block(info, blk);

	return size;
}
EXPORT_SYMBOL_GPL(rh_free);

int rh_get_stats(struct _rh_info *info, int what, int max_stats,
		 struct _rh_stats *stats)
{
	struct _rh_block *blk;
	struct list_head *l;
	struct list_head *h;
	int nr;

	switch (what) {

	case RHGS_FREE:
		h = &info->free_list;
		break;

	case RHGS_TAKEN:
		h = &info->taken_list;
		break;

	default:
		return -EINVAL;
	}

	/* Linear search for block */
	nr = 0;
	list_for_each(l, h) {
		blk = list_entry(l, struct _rh_block, list);
		if (stats != NULL && nr < max_stats) {
			stats->start = blk->start;
			stats->size = blk->size;
			stats->owner = blk->owner;
			stats++;
		}
		nr++;
	}

	return nr;
}
EXPORT_SYMBOL_GPL(rh_get_stats);

int rh_set_owner(struct _rh_info *info, unsigned long start, const char *owner)
{
	struct _rh_block *blk, *blk2;
	struct list_head *l;
	int size;

	/* Linear search for block */
	blk = NULL;
	list_for_each(l, &info->taken_list) {
		blk2 = list_entry(l, struct _rh_block, list);
		if (start < blk2->start)
			break;
		blk = blk2;
	}

	if (blk == NULL || start > (blk->start + blk->size))
		return -EINVAL;

	blk->owner = owner;
	size = blk->size;

	return size;
}
EXPORT_SYMBOL_GPL(rh_set_owner);

void rh_dump(struct _rh_info *info)
{
	static struct _rh_stats st[32];	/* XXX maximum 32 blocks */
	int maxnr;
	int i, nr;

	maxnr = ARRAY_SIZE(st);

	pr_info("info @0x%p (%d slots empty / %d max)\n",
	       info, info->empty_slots, info->max_blocks);

	pr_info("  Free:\n");
	nr = rh_get_stats(info, RHGS_FREE, maxnr, st);
	if (nr > maxnr)
		nr = maxnr;
	for (i = 0; i < nr; i++)
		pr_info("    0x%lx-0x%lx (%u)\n",
		       st[i].start, st[i].start + st[i].size,
		       st[i].size);
	pr_info("\n");

	pr_info("  Taken:\n");
	nr = rh_get_stats(info, RHGS_TAKEN, maxnr, st);
	if (nr > maxnr)
		nr = maxnr;
	for (i = 0; i < nr; i++)
		pr_info("    0x%lx-0x%lx (%u) %s\n",
		       st[i].start, st[i].start + st[i].size,
		       st[i].size, st[i].owner != NULL ? st[i].owner : "");
	pr_info("\n");
}
EXPORT_SYMBOL_GPL(rh_dump);

void rh_dump_blk(struct _rh_info *info, struct _rh_block *blk)
{
	pr_info("blk @0x%p: 0x%lx-0x%lx (%u)\n",
	       blk, blk->start, blk->start + blk->size, blk->size);
}
EXPORT_SYMBOL_GPL(rh_dump_blk);

