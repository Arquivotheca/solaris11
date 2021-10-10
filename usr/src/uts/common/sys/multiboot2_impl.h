/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MULTIBOOT2_IMPL_H
#define	_MULTIBOOT2_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/multiboot.h>
#include <sys/vbe.h>

extern struct multiboot2_info_header *mb2_info;

#define	RNDUP(x, y)	((x) + ((y) - 1ul) & ~((y) - 1ul))

/* Multiboot 2 functions */
#define	MB2I_1ST_TAG(mbi2h) (&(mbi2h)->tags[0])

/*
 * IMPORTANT: Tags are 8-byte aligned. It's not sufficient to just add the
 * size of the tag to the tag's base address to get to the next tag. That
 * sum MUST be rounded up to the next 8-byte boundary.
 */
#define	MB2_MBI_TAG_ADVANCE(t) \
	(((t)->type == MULTIBOOT_TAG_TYPE_END) ? NULL : \
	    ((struct multiboot_tag *)RNDUP((uintptr_t)((char *)(t) + \
	    (t)->size), 8)))

extern struct multiboot_tag_mmap *dboot_multiboot2_get_mmap_tagp(struct
    multiboot2_info_header *);

extern void *boot_multiboot2_find_tag_impl(struct multiboot2_info_header *,
    struct multiboot_tag *, uint32_t);

extern void *dboot_multiboot2_find_tag(struct multiboot2_info_header *,
    uint32_t);

extern int dboot_multiboot2_iterate(struct multiboot2_info_header *mb2_info,
    uint32_t type, int index, void *arg);

extern paddr_t dboot_multiboot2_highest_addr(struct multiboot2_info_header *);

extern char *dboot_multiboot2_cmdline(struct multiboot2_info_header *);

extern int dboot_multiboot2_modcount(struct multiboot2_info_header *);

extern uint32_t dboot_multiboot2_modstart(struct multiboot2_info_header *, int);

extern uint32_t dboot_multiboot2_modend(struct multiboot2_info_header *, int);

extern char *dboot_multiboot2_modcmdline(struct multiboot2_info_header *, int);

extern int dboot_multiboot2_basicmeminfo(struct multiboot2_info_header *,
    uint32_t *, uint32_t *);

extern int dboot_multiboot2_mmap_entries(struct multiboot2_info_header *,
    struct multiboot_tag_mmap *);

extern uint64_t dboot_multiboot2_mmap_get_base(struct multiboot2_info_header *,
	struct multiboot_tag_mmap *, int);

extern uint64_t dboot_multiboot2_mmap_get_length(struct
    multiboot2_info_header *, struct multiboot_tag_mmap *, int);

extern uint32_t dboot_multiboot2_mmap_get_type(struct multiboot2_info_header *,
    struct multiboot_tag_mmap *, int);

extern void dboot_vesa_info_to_fb_info(struct VbeInfoBlock *,
    struct ModeInfoBlock *, struct multiboot_tag_framebuffer *);

extern void dboot_fb_info_to_vesa_info(struct multiboot_tag_framebuffer *,
    struct VbeInfoBlock *, struct ModeInfoBlock *);



/* kernel only */
#ifdef _KERNEL
extern int dboot_multiboot2_set_modstart(struct multiboot2_info_header *,
    int, uint32_t);
extern int dboot_multiboot2_set_modend(struct multiboot2_info_header *, int,
    uint32_t);
extern int dboot_multiboot2_set_cmdline(struct multiboot2_info_header *,
    char *);
extern int dboot_multiboot2_set_modcmdline(struct multiboot2_info_header *,
    int, char *);
extern int dboot_multiboot2_delete_entry(struct multiboot2_info_header *,
    struct multiboot_tag *);
extern struct multiboot_tag *dboot_multiboot2_add_entry(struct
    multiboot2_info_header *, size_t);
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _MULTIBOOT2_IMPL_H */
