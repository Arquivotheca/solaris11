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

/*
 * boot_vesa.c
 * Extracts information from the framebuffer_info that's passed as part of
 * the xboot_info structure
 */
#include <sys/multiboot2.h>
#include <sys/fbinfo.h>
#include <sys/types.h>
#include <sys/vbe.h>
#include <sys/bootinfo.h>

#define	FB_FAIL_NULLPTR		(1)
#define	FB_FAIL_NO_RGB		(2)
#define	FB_FAIL_BADVAL		(3)
#define	FB_FAIL_TEXTMODE	(4)

uint16_t		fb_fail_reason = 0;

/*
 * Populates the fb_info structure with the VESA information contained in the
 * xboot_info. Returns -1 on failure, so that the console initialization
 * code has a chance to revert to VGA TEXT MODE.
 */
int
xbi_fb_info_init(struct xboot_info *xbi)
{
	struct multiboot_tag_framebuffer *fbip;

	if (xbi->bi_framebuffer_info == 0) {
		fb_fail_reason = FB_FAIL_NULLPTR;
		return (-1);
	}

	fbip = (struct multiboot_tag_framebuffer *)
	    (uintptr_t)xbi->bi_framebuffer_info;

	if (fbip->common.framebuffer_type ==
	    MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
		fb_fail_reason = FB_FAIL_TEXTMODE;
		return (-1);
	} else if (fbip->common.framebuffer_type !=
	    MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
		/* XXX - Need to handle Indexed-color mode */
		fb_fail_reason = FB_FAIL_NO_RGB;
		return (-1);
	}

	/*
	 * Start populating the fb_info structure (size, physical address of
	 * of the framebuffer and depth)
	 */
	fb_info.screen_pix.x = fbip->common.framebuffer_width;
	fb_info.screen_pix.y = fbip->common.framebuffer_height;
	fb_info.phys_addr = fbip->common.framebuffer_addr;
	fb_info.depth = fbip->common.framebuffer_bpp;

	/*
	 * Best-effort attempt to catch some (volountary or unvolountary)
	 * maliciousness.
	 */
	if (fb_info.screen_pix.x == 0 || fb_info.screen_pix.y == 0) {
		fb_fail_reason = FB_FAIL_BADVAL;
		return (-1);
	}

	fb_info.bpp = fb_info.depth / 8;
	if (fb_info.depth % 8 != 0)
		fb_info.bpp++;

	fb_info.phys_mem = fb_info.screen_pix.x * fb_info.screen_pix.y *
	    fb_info.bpp;

	fb_info.rgb.red.size = fbip->u1.fb2.framebuffer_red_mask_size;
	fb_info.rgb.red.shift = fbip->u1.fb2.framebuffer_red_field_position;
	fb_info.rgb.green.size = fbip->u1.fb2.framebuffer_green_mask_size;
	fb_info.rgb.green.shift = fbip->u1.fb2.framebuffer_green_field_position;
	fb_info.rgb.blue.size = fbip->u1.fb2.framebuffer_blue_mask_size;
	fb_info.rgb.blue.shift = fbip->u1.fb2.framebuffer_blue_field_position;

	return (0);
}
