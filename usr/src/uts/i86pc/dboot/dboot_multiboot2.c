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


#include <sys/types.h>
#include <sys/machparam.h>
#include <sys/x86_archext.h>
#include <sys/systm.h>
#include <sys/mach_mmu.h>
#include <sys/multiboot.h>
#include <sys/multiboot2.h>
#include <sys/multiboot2_impl.h>


#include <sys/inttypes.h>
#include <sys/bootinfo.h>
#include <sys/mach_mmu.h>
#include <sys/boot_console.h>
#include <sys/vbe.h>

/* Standard 16-color VGA Palette */
struct multiboot_color vga16_palette[MULTIBOOT_MAX_COLORS] = {
/* Black */	{ 0, 0, 0 },
/* Blue */	{ 0, 0, 168 },
/* Green */	{ 0, 168, 0 },
/* Cyan */	{ 0, 168, 168 },
/* Red */	{ 168, 0, 0 },
/* Magenta */	{ 168, 0, 168 },
/* Brown */	{ 168, 84, 0 },
/* Lt Gray */	{ 168, 168, 168 },
/* Dk Gray */	{ 84, 84, 84 },
/* Br Blue */	{ 84, 84, 252 },
/* Br Green */	{ 84, 252, 84 },
/* Br Cyan */	{ 84, 252, 252 },
/* Br Red */	{ 254, 84, 84 },
/* Br Magnta */	{ 252, 84, 252 },
/* Yellow */	{ 252, 252, 84 },
/* White */	{ 252, 252, 252 }
};


#ifdef _KERNEL

extern void fastboot_dboot_dprintf(char *fmt, ...);
#define	DBG_MSG(s)	fastboot_dboot_dprintf(s)
#define	DBG(x)		fastboot_dboot_dprintf("%s is %" PRIx64 "\n", #x,\
			    (uint64_t)(x));
#define	dprintf	fastboot_dboot_dprintf

#else /* _KERNEL */

#include "dboot_printf.h"
#include "dboot_xboot.h"
#define	dprintf dboot_printf

#endif /* _KERNEL */


void *
dboot_multiboot2_find_tag_impl(struct multiboot_tag *tagp, uint32_t findee)
{
	while (tagp != NULL && tagp->type != findee) {
		DBG(tagp->type);
		DBG(tagp->size);
		tagp = MB2_MBI_TAG_ADVANCE(tagp);
	}

	return ((tagp != NULL && tagp->type == findee) ? tagp : NULL);
}

void *
dboot_multiboot2_find_tag(struct multiboot2_info_header *mb2_info,
    uint32_t findee)
{
	return (dboot_multiboot2_find_tag_impl(MB2I_1ST_TAG(mb2_info), findee));
}

int
dboot_multiboot2_iterate(struct multiboot2_info_header *mb2_info,
    uint32_t type, int index, void *arg)
{
	struct multiboot_tag *tagp;
	int instance = 0;

	tagp = dboot_multiboot2_find_tag(mb2_info, type);

	while (tagp != NULL) {

		if (index >= 0 && instance == index) {
			if (arg != NULL)
				*(void **)arg = tagp;
			return (instance + 1);
		} else if (index < 0) {
			if (arg != NULL) {
				*(void **)arg = tagp;
				return (instance + 1);
			}
		}

		++instance;

		tagp = dboot_multiboot2_find_tag_impl(MB2_MBI_TAG_ADVANCE(tagp),
		    type);
	}

	return (instance);
}


struct multiboot_tag_mmap *
dboot_multiboot2_get_mmap_tagp(struct multiboot2_info_header *mb2_info)
{
	/*
	 * Initialize global variables associated with information found in
	 * Multiboot 2 information tags.
	 */
	return ((struct multiboot_tag_mmap *)
	    dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_MMAP));
}

char *
dboot_multiboot2_cmdline(struct multiboot2_info_header *mb2_info)
{
	struct multiboot_tag_string *cmdltagp;
	cmdltagp = dboot_multiboot2_find_tag(mb2_info,
	    MULTIBOOT_TAG_TYPE_CMDLINE);

	if (cmdltagp != NULL)
		return (&cmdltagp->string[0]);
	else
		return (NULL);
}


int
dboot_multiboot2_modcount(struct multiboot2_info_header *mb2_info)
{
	return (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    -1, NULL));
}

uint32_t
dboot_multiboot2_modstart(struct multiboot2_info_header *mb2_info, int index)
{
	struct multiboot_tag_module *modtagp = NULL;
	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    index, &modtagp) != 0) {
		return (modtagp->mod_start);
	} else {
		return (0);
	}
}

uint32_t
dboot_multiboot2_modend(struct multiboot2_info_header *mb2_info, int index)
{
	struct multiboot_tag_module *modtagp = NULL;
	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    index, &modtagp) != 0) {
		return (modtagp->mod_end);
	} else {
		return (0);
	}
}

char *
dboot_multiboot2_modcmdline(struct multiboot2_info_header *mb2_info, int index)
{
	struct multiboot_tag_module *modtagp = NULL;
	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    index, &modtagp) != 0) {
		return (&modtagp->cmdline[0]);
	} else {
		return (NULL);
	}
}


int
dboot_multiboot2_basicmeminfo(struct multiboot2_info_header *mb2_info,
    uint32_t *low, uint32_t *high)
{
	struct multiboot_tag_basic_meminfo *mip = 0;
	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_BASIC_MEMINFO,
	    -1, &mip) != 0) {
		*low = mip->mem_lower;
		*high = mip->mem_upper;
		return (1);
	} else
		return (0);
}

int
dboot_multiboot2_mmap_entries(struct multiboot2_info_header *mb2_info,
    struct multiboot_tag_mmap *mb2_mmap_tagp)
{

	if (mb2_mmap_tagp == NULL)
		mb2_mmap_tagp = dboot_multiboot2_get_mmap_tagp(mb2_info);

	if (mb2_mmap_tagp != NULL) {
		/*
		 * This calculation is a bit heinous.  The number of entries
		 * is equal to the size of the tag minus the total size of the
		 * multiboot_tag_mmap structure EXCEPT for the one-entry array
		 * of type multiboot2_memory_map_t.
		 */
		return ((mb2_mmap_tagp->size -
		    sizeof (struct multiboot_tag_mmap) +
		    sizeof (multiboot2_memory_map_t)) /
		    mb2_mmap_tagp->entry_size);
	} else
		return (0);
}

uint64_t
dboot_multiboot2_mmap_get_base(struct multiboot2_info_header *mb2_info,
    struct multiboot_tag_mmap *mb2_mmap_tagp, int index)
{
	multiboot2_memory_map_t *mapentp;

	if (mb2_mmap_tagp == NULL)
		mb2_mmap_tagp = dboot_multiboot2_get_mmap_tagp(mb2_info);

	if (mb2_mmap_tagp != NULL) {
		mapentp = &mb2_mmap_tagp->entries[index];
		return (mapentp->addr);
	} else {
		return (0);
	}
}


uint64_t
dboot_multiboot2_mmap_get_length(struct multiboot2_info_header *mb2_info,
struct multiboot_tag_mmap *mb2_mmap_tagp, int index)
{
	multiboot2_memory_map_t *mapentp;

	if (mb2_mmap_tagp == NULL)
		mb2_mmap_tagp = dboot_multiboot2_get_mmap_tagp(mb2_info);

	if (mb2_mmap_tagp != NULL) {
		mapentp = &mb2_mmap_tagp->entries[index];
		return (mapentp->len);
	} else {
		return (0);
	}
}

uint32_t
dboot_multiboot2_mmap_get_type(struct multiboot2_info_header *mb2_info,
struct multiboot_tag_mmap *mb2_mmap_tagp,
int index)
{
	multiboot2_memory_map_t *mapentp;

	if (mb2_mmap_tagp == NULL)
		mb2_mmap_tagp = dboot_multiboot2_get_mmap_tagp(mb2_info);

	if (mb2_mmap_tagp != NULL) {
		mapentp = &mb2_mmap_tagp->entries[index];
		return (mapentp->type);
	} else {
		return (0);
	}
}

paddr_t
dboot_multiboot2_highest_addr(struct multiboot2_info_header *mb2_info)
{
	/*
	 * The multiboot2 info structure is contiguous, so this
	 * check_higher replaces the checks peppered throughout for all of
	 * multiboot1's multiboot info
	 */
	return ((paddr_t)(uintptr_t)mb2_info + mb2_info->total_size);
}



#ifdef _KERNEL

int
dboot_multiboot2_set_cmdline(struct multiboot2_info_header *mb2_info,
    char *cmdline)
{
	struct multiboot_tag_string *cmdltagp;
	size_t len;
	size_t overhead;

	cmdltagp = dboot_multiboot2_find_tag(mb2_info,
	    MULTIBOOT_TAG_TYPE_CMDLINE);

	if (cmdltagp != NULL) {
		len =  strlen(cmdline) + 1;
		overhead =  2 * sizeof (multiboot_uint32_t);

		if (len >  (cmdltagp->size - overhead)) {

			/* remove old entry and allocate new larger entry */
			(void) dboot_multiboot2_delete_entry(mb2_info, (struct
			    multiboot_tag *)cmdltagp);

			cmdltagp = (struct multiboot_tag_string *)
			    dboot_multiboot2_add_entry(mb2_info, len +
			    overhead);

			if (cmdltagp == NULL)
				return (-1);

			cmdltagp->type = MULTIBOOT_TAG_TYPE_CMDLINE;
		}

		dprintf("set_cmdline, cmdline is  %s size is %x %d\n",
		    cmdline, cmdltagp->size, cmdltagp->size);

		bcopy((void *)cmdline, (void *) cmdltagp->string,
		    strlen(cmdline) + 1);
		return (0);
	} else {
		return (-1);
	}
}


int
dboot_multiboot2_set_modstart(struct multiboot2_info_header *mb2_info,
    int index, uint32_t modstart)
{
	struct multiboot_tag_module *modtagp = NULL;
	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    index, &modtagp) == 0)
		return (-1);

	modtagp->mod_start = modstart;
	return (0);
}


int
dboot_multiboot2_set_modend(struct multiboot2_info_header *mb2_info, int index,
    uint32_t modend)
{
	struct multiboot_tag_module *modtagp = NULL;
	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    index, &modtagp) == 0)
		return (-1);

	modtagp->mod_end = modend;
	return (0);
}


int
dboot_multiboot2_set_modcmdline(struct multiboot2_info_header *mb2_info,
    int index, char *cmdline)
{
	struct multiboot_tag_module *modtagp = NULL;
	size_t len;
	size_t overhead;
	uint32_t save_mode_start;
	uint32_t save_mode_end;


	if (dboot_multiboot2_iterate(mb2_info, MULTIBOOT_TAG_TYPE_MODULE,
	    index, &modtagp) == 0)
		return (-1);

	len =  strlen(cmdline) + 1;
	overhead = sizeof (uint32_t) * 4;

	if (len >  (modtagp->size - overhead)) {

		save_mode_start = modtagp->mod_start;
		save_mode_end = modtagp->mod_end;

		/* remove old entry and allocate new larger entry */
		(void) dboot_multiboot2_delete_entry(mb2_info, (struct
		    multiboot_tag *)modtagp);

		modtagp = (struct multiboot_tag_module *)
		    dboot_multiboot2_add_entry(mb2_info, len + overhead);

		if (modtagp == NULL)
			return (-1);

		modtagp->type = MULTIBOOT_TAG_TYPE_MODULE;
		modtagp->mod_start = save_mode_start;
		modtagp->mod_end = save_mode_end;
	}

	bcopy(cmdline, &modtagp->cmdline[0], len);
	return (0);
}

/*
 * Remove the given entry from mb2_info, by moving up the region starting from
 * the next tag structure (after the entry) to current start address of the
 * entry.
 */
int
dboot_multiboot2_delete_entry(struct multiboot2_info_header *mb2_info,
    struct multiboot_tag *entry)
{
	struct multiboot_tag *dest, *src;
	size_t size;

	dest = entry;
	src = MB2_MBI_TAG_ADVANCE(dest);

	if (src == NULL) {
		/* we are deleting END struct, do nothing. */
		return (-1);
	}

	size = mb2_info->total_size - ((uintptr_t)src - (uintptr_t)mb2_info);

	(void) memmove(dest, src, size);

	mb2_info->total_size -= (uintptr_t)src - (uintptr_t)dest;

	return (0);
}

/*
 * add an entry with the given size, right before the END tag structure
 * returns pointer to where it was inserted. The size may need to be adjusted
 * for alignment of END tag structure.
 */

struct multiboot_tag *
dboot_multiboot2_add_entry(struct multiboot2_info_header *mb2_info, size_t size)
{
	struct multiboot_tag *endtag, *newendtag;
	size_t newsize;

	endtag = dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_END);

	newendtag = (struct multiboot_tag *)
	    RNDUP((uintptr_t)((char *)(endtag) + size), 8);

	newsize = (uintptr_t)newendtag - (uintptr_t)endtag;

	/* assumption is that we have enough space for the extra space */
	(void) memmove(newendtag, endtag, endtag->size);

	endtag->size = newsize;
	mb2_info->total_size += newsize;

	return (endtag);
}

#endif /* _KERNEL */



void
dboot_vesa_info_to_fb_info(struct VbeInfoBlock *vinfo,
    struct ModeInfoBlock *minfo, struct multiboot_tag_framebuffer *fbip)
{
	int i;

	fbip->common.type = MULTIBOOT_TAG_TYPE_FRAMEBUFFER;
	fbip->common.framebuffer_addr = minfo->PhysBasePtr;
	fbip->common.framebuffer_width = minfo->XResolution;
	fbip->common.framebuffer_height = minfo->YResolution;
	fbip->common.framebuffer_bpp = minfo->BitsPerPixel;

	if (VBE_VERSION_MAJOR(vinfo->VbeVersion) < 3)
		fbip->common.framebuffer_pitch = minfo->BytesPerScanLine;
	else
		fbip->common.framebuffer_pitch = minfo->LinBytesPerScanLine;


	if (minfo->MemoryModel == VBE_MM_TEXT) {

		fbip->common.size =
		    sizeof (struct multiboot_tag_framebuffer_common);
		fbip->common.framebuffer_type =
		    MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;
		fbip->common.framebuffer_pitch = (minfo->XResolution << 1);
		fbip->common.framebuffer_bpp = 16; /* char & attribute bytes */

	} else if (minfo->MemoryModel == VBE_MM_PACKED_PIXEL) {

		fbip->common.size =
		    sizeof (struct multiboot_tag_framebuffer_common) +
		    sizeof (multiboot_uint16_t) +
		    (fbip->u1.fb1.framebuffer_palette_num_colors *
		    sizeof (struct multiboot_color));
		fbip->common.framebuffer_type =
		    MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED;
		fbip->u1.fb1.framebuffer_palette_num_colors =
		    MULTIBOOT_MAX_COLORS;
		for (i = 0; i < fbip->u1.fb1.framebuffer_palette_num_colors;
		    i++) {
			fbip->u1.fb1.framebuffer_palette[i] = vga16_palette[i];
		}
	} else if (minfo->MemoryModel == VBE_MM_DIRECT_COLOR) {
		fbip->common.size = sizeof (struct multiboot_tag_framebuffer);
		fbip->common.framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
		if (VBE_VERSION_MAJOR(vinfo->VbeVersion) < 3) {
			fbip->u1.fb2.framebuffer_red_field_position =
			    minfo->RedFieldPosition;
			fbip->u1.fb2.framebuffer_red_mask_size =
			    minfo->RedMaskSize;
			fbip->u1.fb2.framebuffer_green_field_position =
			    minfo->GreenFieldPosition;
			fbip->u1.fb2.framebuffer_green_mask_size =
			    minfo->GreenMaskSize;
			fbip->u1.fb2.framebuffer_blue_field_position =
			    minfo->BlueFieldPosition;
			fbip->u1.fb2.framebuffer_blue_mask_size =
			    minfo->BlueMaskSize;
		} else {
			fbip->u1.fb2.framebuffer_red_field_position =
			    minfo->LinRedFieldPosition;
			fbip->u1.fb2.framebuffer_red_mask_size =
			    minfo->LinRedMaskSize;
			fbip->u1.fb2.framebuffer_green_field_position =
			    minfo->LinGreenFieldPosition;
			fbip->u1.fb2.framebuffer_green_mask_size =
			    minfo->LinGreenMaskSize;
			fbip->u1.fb2.framebuffer_blue_field_position =
			    minfo->LinBlueFieldPosition;
			fbip->u1.fb2.framebuffer_blue_mask_size =
			    minfo->LinBlueMaskSize;
		}
	}
}

void
dboot_fb_info_to_vesa_info(struct multiboot_tag_framebuffer *fbip,
    struct VbeInfoBlock *vinfo, struct ModeInfoBlock *minfo)
{

	minfo->PhysBasePtr = fbip->common.framebuffer_addr;
	minfo->XResolution =  fbip->common.framebuffer_width;
	minfo->YResolution = fbip->common.framebuffer_height;
	minfo->BitsPerPixel = fbip->common.framebuffer_bpp;


	/*
	 * Can not tell here what the video controller supports
	 * so choose a safe version.
	 */
	vinfo->VbeVersion = 0x0200;

	if (VBE_VERSION_MAJOR(vinfo->VbeVersion) < 3)
		minfo->BytesPerScanLine = fbip->common.framebuffer_pitch;
	else
		minfo->LinBytesPerScanLine = fbip->common.framebuffer_pitch;


	if (fbip->common.framebuffer_type ==
	    MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {

		minfo->MemoryModel = VBE_MM_TEXT;
		minfo->XResolution = fbip->common.framebuffer_pitch >> 1;

	} else if (fbip->common.framebuffer_type ==
	    MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {

		minfo->MemoryModel = VBE_MM_PACKED_PIXEL;


	} if (fbip->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
		minfo->MemoryModel = VBE_MM_DIRECT_COLOR;

		if (VBE_VERSION_MAJOR(vinfo->VbeVersion) < 3) {
			minfo->RedFieldPosition =
			    fbip->u1.fb2.framebuffer_red_field_position;

			minfo->RedMaskSize =
			    fbip->u1.fb2.framebuffer_red_mask_size;

			minfo->GreenFieldPosition =
			    fbip->u1.fb2.framebuffer_green_field_position;

			minfo->GreenMaskSize =
			    fbip->u1.fb2.framebuffer_green_mask_size;

			minfo->BlueFieldPosition =
			    fbip->u1.fb2.framebuffer_blue_field_position;

			minfo->BlueMaskSize =
			    fbip->u1.fb2.framebuffer_blue_mask_size;
		} else {

			minfo->LinRedFieldPosition =
			    fbip->u1.fb2.framebuffer_red_field_position;

			minfo->LinRedMaskSize =
			    fbip->u1.fb2.framebuffer_red_mask_size;

			minfo->LinGreenFieldPosition =
			    fbip->u1.fb2.framebuffer_green_field_position;

			minfo->LinGreenMaskSize =
			    fbip->u1.fb2.framebuffer_green_mask_size;

			minfo->LinBlueFieldPosition =
			    fbip->u1.fb2.framebuffer_blue_field_position;

			minfo->LinBlueMaskSize =
			    fbip->u1.fb2.framebuffer_blue_mask_size;
		}
	}
}
