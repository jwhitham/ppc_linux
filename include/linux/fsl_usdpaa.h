/* Copyright 2011-2012 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef FSL_USDPAA_H
#define FSL_USDPAA_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __KERNEL__
/* This interface is needed in a few places and though it's not specific to
 * USDPAA as such, creating a new header for it doesn't make any sense. The
 * qbman kernel driver implements this interface and uses it as the backend for
 * both the FQID and BPID allocators.
 */
struct dpa_alloc {
	struct list_head list;
	spinlock_t lock;
};
#define DECLARE_DPA_ALLOC(name) \
	struct dpa_alloc name = { \
		.list = { \
			.prev = &name.list, \
			.next = &name.list \
		}, \
		.lock = __SPIN_LOCK_UNLOCKED(name.lock) \
	}
static inline void dpa_alloc_init(struct dpa_alloc *alloc)
{
	INIT_LIST_HEAD(&alloc->list);
	spin_lock_init(&alloc->lock);
}
int dpa_alloc_new(struct dpa_alloc *alloc, u32 *result, u32 count, u32 align,
		  int partial);
void dpa_alloc_free(struct dpa_alloc *alloc, u32 base_id, u32 count);
/* Like 'new' but specifies the desired range, returns -ENOMEM if the entire
 * desired range is not available, or 0 for success. */
int dpa_alloc_reserve(struct dpa_alloc *alloc, u32 base_id, u32 count);
/* Pops and returns contiguous ranges from the allocator. Returns -ENOMEM when
 * 'alloc' is empty. */
int dpa_alloc_pop(struct dpa_alloc *alloc, u32 *result, u32 *count);
#endif /* __KERNEL__ */

#ifdef __cplusplus
}
#endif

#endif /* FSL_USDPAA_H */
