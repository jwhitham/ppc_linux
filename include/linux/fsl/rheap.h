/*
 * include/asm-ppc/rheap.h
 *
 * Header file for the implementation of a remote heap.
 *
 * Author: Pantelis Antoniou <panto@intracom.gr>
 *
 * 2004 (c) INTRACOM S.A. Greece. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASM_PPC_RHEAP_H__
#define __ASM_PPC_RHEAP_H__

#include <linux/list.h>

struct _rh_block {
	struct list_head list;
	unsigned long start;
	int size;
	const char *owner;
};

struct _rh_info {
	unsigned int alignment;
	int max_blocks;
	int empty_slots;
	struct _rh_block *block;
	struct list_head empty_list;
	struct list_head free_list;
	struct list_head taken_list;
	unsigned int flags;
};

#define RHIF_STATIC_INFO	0x1
#define RHIF_STATIC_BLOCK	0x2

struct _rh_stats {
	unsigned long start;
	int size;
	const char *owner;
};

#define RHGS_FREE	0
#define RHGS_TAKEN	1

/* Create a remote heap dynamically */
extern struct _rh_info *rh_create(unsigned int alignment);

/* Destroy a remote heap, created by rh_create() */
extern void rh_destroy(struct _rh_info *info);

/* Initialize in place a remote info block */
extern void rh_init(struct _rh_info *info, unsigned int alignment,
		int max_blocks, struct _rh_block *block);

/* Attach a free region to manage */
extern int rh_attach_region(struct _rh_info *info, unsigned long start,
			    int size);

/* Detach a free region */
extern unsigned long rh_detach_region(struct _rh_info *info,
				      unsigned long start, int size);

/* Allocate the given size from the remote heap (with alignment) */
extern unsigned long rh_alloc_align(struct _rh_info *info, int size,
				    int alignment, const char *owner);

/* Allocate the given size from the remote heap */
extern unsigned long rh_alloc(struct _rh_info *info, int size,
			      const char *owner);

/* Allocate the given size from the given address */
extern unsigned long rh_alloc_fixed(struct _rh_info *info, unsigned long start,
				   int size, const char *owner);

/* Free the allocated area */
extern int rh_free(struct _rh_info *info, unsigned long start);

/* Get stats for debugging purposes */
extern int rh_get_stats(struct _rh_info *info, int what, int max_stats,
			struct _rh_stats *stats);

/* Simple dump of remote heap info */
extern void rh_dump(struct _rh_info *info);

/* Set owner of taken block */
extern int rh_set_owner(struct _rh_info *info, unsigned long start,
			const char *owner);

#endif				/* __ASM_PPC_RHEAP_H__ */
