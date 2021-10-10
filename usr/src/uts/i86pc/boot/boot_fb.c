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
 * Bit-mapped linear frame buffer boot console.
 * Contains the routines to paint and scroll the screen. It is used by both
 * dboot and the early stage boot console. For simplicity, we alloc a shadow
 * buffer only after the boot memory allocator is in place.
 * To setup the linear frame buffer, we use the information stored inside the
 * fb_info struct (see i86pc/sys/fbinfo.h). VESA or EFI specific code has
 * previously set this information up for us (f.e. see i86pc/boot/boot_vesa.c)
 *
 * The boot_console calls into us when it comes to display characters (see
 * bcons_putchar in boot_console.c). Black background (0) and bright white
 * (0xFF) are hardcoded and assumed thorough the code.
 */

#include <sys/bootconf.h>
#include <sys/fbinfo.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/font.h>
#include <sys/bootinfo.h>
#include <sys/boot_console.h>

#define	SFENCE()	__asm__ volatile("sfence")

/*
 * visual_io-alike struct definitions.
 */
struct boot_fb_blitdata {
	uint16_t	row; 	/* Row to display data at */
	uint16_t	col; 	/* Col to display data at */
	uint16_t	width; 	/* Width of data */
	uint16_t	height; /* Height of data */
	unsigned char   *data; 	/* Data to display */
};

struct boot_fb_copydata {
	uint16_t	s_row; /* Starting row */
	uint16_t	s_col; /* Starting col */
	uint16_t	e_row; /* Ending row */
	uint16_t	e_col; /* Ending col */
	uint16_t	t_row; /* Row to move to */
	uint16_t	t_col; /* Col to move to */
};

static struct boot_fb_copydata	copy_buf;

/* Handy frame buffer global variables. */
static uint8_t			*fb_map;
static uint8_t			depth;
static uint32_t			scanline;
static uint32_t			total_size;
static uint8_t			bytes_per_pixel;
/* Shadow frame buffer. */
static uint8_t			shadow_on;
static uint8_t			*shadow_map;
/* Make the data_buffer as big as the largest font. */
#define	BUF_MAX_FONT_SIZE	12 * 22 * 4
static struct font		boot_fb_font;
static uint8_t			data_buffer[BUF_MAX_FONT_SIZE];
static uint8_t			blank_buffer[BUF_MAX_FONT_SIZE];
static uint8_t			cursor_buffer[BUF_MAX_FONT_SIZE];
/* Offset from 0.0 to center the console. */
static uint16_t			x_offset, y_offset;
/* Coordinates of the last line to clear it during scrolling. */
static fb_info_p_coord_t	last_line;
static uint32_t			last_line_size;

static void boot_fb_blit(struct boot_fb_blitdata *);
static void boot_fb_cursor(boolean_t);

/* Handy macros. */
#define	FB_X_RES	(fb_info.screen_pix.x)
#define	FB_Y_RES	(fb_info.screen_pix.y)

#define	FB_ROWS		(fb_info.screen_char.rows)
#define	FB_COLS		(fb_info.screen_char.cols)

#define	FB_CURS_X	(fb_info.cursor_pix.x)
#define	FB_CURS_Y	(fb_info.cursor_pix.y)

#define	FB_CURS_ROW	(fb_info.cursor_char.rows)
#define	FB_CURS_COL	(fb_info.cursor_char.cols)

#define	CURS_DISPLAY	(B_TRUE)
#define	CURS_BLANK	(B_FALSE)

/*
 * Clear the screen. Black background is assumed.
 */
void
boot_fb_clear_screen()
{
	(void) memset(fb_map, '\0', total_size);
}

/*
 * Mimic the behavior we have in tem to have a smooth transition from the
 * boot console to the coherent console.
 */
static void
boot_fb_set_font(short height, short width)
{
	set_font(&boot_fb_font, (short *)&FB_ROWS, (short *)&FB_COLS, height,
	    width);

	fb_info.font_width = boot_fb_font.width;
	fb_info.font_height = boot_fb_font.height;
}

#define	WHITE_8		(0xFF)	/* direct-mapped white is 0xff */
#define	BLACK_8		(0x00)

static void
bit_to_pix8(uchar_t c)
{
	uint8_t *dest = (uint8_t *)data_buffer;
	generic_bit_to_pix8(&boot_fb_font, dest, c, WHITE_8, BLACK_8);
}

#define	WHITE_16	(0xffff)
#define	BLACK_16	(0x0000)

static void
bit_to_pix16(uchar_t c)

{
	uint16_t *destp = (uint16_t *)data_buffer;
	generic_bit_to_pix16(&boot_fb_font, destp, c, WHITE_16, BLACK_16);
}

#define	WHITE_32	(0xffffffff)
#define	BLACK_32	(0x00000000)

static void
bit_to_pix24(uchar_t c)
{
	uint32_t *destp = (uint32_t *)data_buffer;
	generic_bit_to_pix24(&boot_fb_font, destp, c, WHITE_32, BLACK_32);
}

/*
 * Initialize the shadow buffer via the boot memory allocator.
 */
void
boot_fb_shadow_init(bootops_t *bops)
{
	shadow_map = (uint8_t *)bops->bsys_alloc(NULL, NULL, total_size,
	    MMU_PAGESIZE);

	/* Things will be a little slow until the coherent console comes in.. */
	if (shadow_map == NULL)
		return;

	/* Copy the content of the screen to not lose data during scrolling. */
	if (console == CONS_SCREEN_FB)
		(void) memcpy(shadow_map, fb_map, total_size);
	else
		(void) memset(shadow_map, '\0', total_size);

	shadow_on = 1;
}

/*
 * Based on the information contained in fb_info, initialize the framebuffer
 * boot console. We try to mimic the tem operations to have a coherent console.
 */
void
boot_fb_init()
{
	/* Framebuffer pages have been mapped 1:1 by dboot */
	fb_map = (uint8_t *)fb_info.phys_addr;

	depth = fb_info.depth;
	bytes_per_pixel = fb_info.bpp;

	scanline = FB_X_RES * bytes_per_pixel;
	total_size = FB_X_RES * FB_Y_RES * bytes_per_pixel;
	fb_info.used_mem = total_size;

	/*
	 * until we have an easy way to keep track of where the cursor is
	 * during dboot, let's just clean the screen here if we are not
	 * booting with a splash image (console=graphics).
	 */
	if (console == CONS_SCREEN_FB)
		boot_fb_clear_screen();

	boot_fb_set_font(FB_Y_RES, FB_X_RES);

	x_offset = (FB_X_RES - (FB_COLS * boot_fb_font.width)) / 2;
	y_offset = (FB_Y_RES - (FB_ROWS * boot_fb_font.height)) / 2;
	fb_info.start_pix.x = x_offset;
	fb_info.start_pix.y = y_offset;

	FB_CURS_X = x_offset;
	FB_CURS_Y = y_offset;

	(void) memset(blank_buffer, 0x0, sizeof (blank_buffer));

	/*
	 * record information about the "last line" of the screen, to simplify
	 * scrolling.
	 */
	last_line.y = (FB_ROWS - 1) * boot_fb_font.height + y_offset;
	last_line.x = x_offset;
	last_line_size = FB_COLS * boot_fb_font.width * bytes_per_pixel;
}

/*
 * boot_fb_conscopy
 * Copy the area of the screen starting at s_col,s_row and ending at e_col,e_row
 * to the position pointed by t_col,t_row.
 */
static void
boot_fb_conscopy(struct boot_fb_copydata *ma)
{
	uint32_t	s_offset, t_offset;
	uint8_t		*where_fb_dest, *where_fb_src;
	uint8_t		*where_shadow_src, *where_shadow_dest;
	uint32_t	width, height;
	uint32_t	i;

	/* Sanity checks. */
	if (ma->s_col >= FB_X_RES ||
	    ma->s_row >= FB_Y_RES ||
	    ma->e_col >= FB_X_RES ||
	    ma->e_row >= FB_Y_RES ||
	    ma->t_col >= FB_X_RES ||
	    ma->t_row >= FB_Y_RES ||
	    ma->s_col > ma->e_col ||
	    ma->s_row > ma->e_row) {
		return;
	}

	/* Copy from */
	s_offset = (ma->s_col * bytes_per_pixel) + ma->s_row * scanline;
	if (shadow_on)
		where_shadow_src = (uint8_t *)(shadow_map + s_offset);
	else
		where_fb_src = (uint8_t *)fb_map + s_offset;
	/* Copy to */
	t_offset = (ma->t_col * bytes_per_pixel) + ma->t_row * scanline;
	where_fb_dest = (uint8_t *)(fb_map + t_offset);
	if (shadow_on)
		where_shadow_dest = (uint8_t *)(shadow_map + t_offset);
	/* How much */
	width = (ma->e_col - ma->s_col + 1);
	height = (ma->e_row - ma->s_row + 1);

	/* More sanity checks. */
	if (ma->t_row + height > FB_Y_RES || ma->t_col + width > FB_X_RES)
		return;

	width *= bytes_per_pixel;

	for (i = 0; i < height; i++) {
		uint32_t incr = i * scanline;
		uint8_t	*f_dest = where_fb_dest + incr;
		uint8_t	*src;

		if (shadow_on) {
			uint8_t *s_dest = where_shadow_dest + incr;
			src = where_shadow_src + incr;
			(void) memcpy(f_dest, src, width);
			(void) memmove(s_dest, src, width);
		} else {
			src = where_fb_src + incr;
			(void) memmove(f_dest, src, width);
		}
	}
	SFENCE();
}

/*
 * During coherent console, tem sends us a blank line to blit on the screen.
 * We can't afford having a blank line buffer during the very early console
 * boot, so we just "mimic" the behavior here.
 */
static void
bootfb_clear_lastline(fb_p_size_t x, fb_p_size_t y)
{
	uint32_t	offset;
	uint8_t		*where_fb, *where_shadow;
	int		i;

	offset = (x * bytes_per_pixel) + y * scanline;
	where_fb = (uint8_t *)(fb_map + offset);
	if (shadow_on)
		where_shadow = (uint8_t *)(shadow_map + offset);

	for (i = 0; i < boot_fb_font.height; i++) {
		uint8_t	*dest = where_fb + (i * scanline);
		(void) memset(dest, '\0', last_line_size);
		if (shadow_on) {
			dest = where_shadow + (i * scanline);
			(void) memset(dest, '\0', last_line_size);
		}
	}
}

/*
 * We assume here that we always have to scroll "up", and always for the whole
 * screen.
 */
static void
boot_fb_scroll()
{
	static boolean_t	initialized = B_FALSE;

	if (initialized == B_FALSE) {
		copy_buf.s_row = boot_fb_font.height + y_offset;
		copy_buf.e_row = FB_Y_RES - y_offset;
		copy_buf.t_row = y_offset;

		copy_buf.s_col = x_offset;
		copy_buf.e_col = FB_X_RES - x_offset;
		copy_buf.t_col = x_offset;

		initialized = B_TRUE;
	}

	boot_fb_conscopy(&copy_buf);
	bootfb_clear_lastline(last_line.x, last_line.y);
}

/*
 * We call update_row() each time we need to move the cursor down one row.
 * update_row() then keeps the y-pixel and row values in sync and decides when
 * it is necessary to scroll.
 */
static void
update_row()
{
	FB_CURS_ROW++;
	FB_CURS_X = x_offset;
	FB_CURS_COL = 0;

	if (FB_CURS_ROW < FB_ROWS) {
		FB_CURS_Y += boot_fb_font.height;
	} else {
		FB_CURS_ROW--;
		boot_fb_scroll();
	}
}

/*
 * We call update_col() each time we need to move the cursor right one
 * character. update_col then keeps the x-pixel and col values in sync and calls
 * update_row() if we get to the end of the screen.
 */
static void
update_col()
{
	FB_CURS_COL++;
	if (FB_CURS_COL < FB_COLS) {
		FB_CURS_X += boot_fb_font.width;
	} else {
		FB_CURS_COL = 0;
		FB_CURS_X = x_offset;
		update_row();
	}
}

/*
 * boot_console:bcons_putchar ultimately calls in here. We get a character that
 * we have to paint on the screen.
 */
void
boot_fb_putchar(uint8_t c)
{
	struct boot_fb_blitdata	display_buf;
	boolean_t		erase = B_FALSE;

	boot_fb_cursor(CURS_BLANK);

	if (c == '\r') {
		FB_CURS_X = x_offset;
		FB_CURS_COL = 0;
		boot_fb_cursor(CURS_DISPLAY);
		return;
	}
	if (c == '\n') {
		update_row();
		boot_fb_cursor(CURS_DISPLAY);
		return;
	}

	if (c == '\t') {
		boot_fb_cursor(CURS_DISPLAY);
		return;
	}

	if (c == '\b') {
		if (FB_CURS_COL > 0) {
			FB_CURS_COL--;
			FB_CURS_X -= boot_fb_font.width;
		}
		c = ' ';
		erase = B_TRUE;
	}

	if (depth == 8)
		bit_to_pix8(c);
	if (depth == 15 || depth == 16)
		bit_to_pix16(c);
	if (depth == 24 || depth == 32)
		bit_to_pix24(c);

	display_buf.col = FB_CURS_X;
	display_buf.row = FB_CURS_Y;
	display_buf.width = boot_fb_font.width;
	display_buf.height = boot_fb_font.height;
	display_buf.data = data_buffer;

	boot_fb_blit(&display_buf);
	if (!erase)
		update_col();

	boot_fb_cursor(CURS_DISPLAY);
}

/*
 * Preparing the cursor is a two step process. First we copy whatever is
 * on the screen in a backup buffer (blankbuf), that we will later use to
 * restore the content of the screen when the cursor moves. After that we
 * xor the content of the backup buffer (0x00 becames 0xFF and vice versa)
 * to display the cursor and eventually achieve the highlight effect.
 */
static void
boot_fb_prepare_cursor(struct boot_fb_blitdata *blt, uint8_t *blankbuf)
{
	uint32_t	offset;
	uint8_t		*where;
	uint32_t	w_size = blt->width * bytes_per_pixel;
	uint32_t	copy_size;
	int		i;

	/* Sanitize copy boundaries. */
	if (blt->col >= FB_X_RES ||
	    blt->row >= FB_Y_RES ||
	    blt->col + blt->width > FB_X_RES ||
	    blt->row + blt->height > FB_Y_RES) {
		return;
	}

	/* Save data under the cursor to later blank it correctly. */
	offset = (blt->col * bytes_per_pixel) + blt->row * scanline;
	if (shadow_on)
		where = (uint8_t *)shadow_map + offset;
	else
		where = (uint8_t *)fb_map + offset;

	for (i = 0; i < blt->height; i++) {
		uint8_t	*src = where + (i * scanline);
		uint8_t	*dest = blankbuf + (i * w_size);

		(void) memcpy(dest, src, w_size);
	}

	/* Done with saving, prepare the cursor buffer to display on screen. */
	copy_size = blt->height * w_size;

	for (i = 0; i < copy_size; i++) {
		if (blankbuf[i] == 0xFF)
			blt->data[i] = 0x0;
		else
			blt->data[i] = 0xFF;
	}
}

static void
boot_fb_cursor(boolean_t display)
{
	struct boot_fb_blitdata	cursor;

	cursor.col = FB_CURS_X;
	cursor.row = FB_CURS_Y;
	cursor.width = boot_fb_font.width;
	cursor.height = boot_fb_font.height;

	if (display) {
		cursor.data = cursor_buffer;
		boot_fb_prepare_cursor(&cursor, blank_buffer);
	} else {
		/* blank_buffer has been filled by boot_fb_prepare_cursor() */
		cursor.data = blank_buffer;
	}

	boot_fb_blit(&cursor);
}

/*
 * boot_fb_blit paints the contents of da->data at the screen location specified
 * by da->col,da->row.
 */
static void
boot_fb_blit(struct boot_fb_blitdata *da)
{
	uint32_t	offset;
	uint8_t		*data = da->data;
	uint8_t		*where_fb, *where_shadow;
	uint32_t	w_size = da->width * bytes_per_pixel;
	int		i;

	/* sanity checks */
	if (da->col >= FB_X_RES ||
	    da->row >= FB_Y_RES ||
	    da->col + da->width > FB_X_RES ||
	    da->row + da->height > FB_Y_RES) {
		return;
	}

	offset = (da->col * bytes_per_pixel) + da->row * scanline;
	where_fb = (uint8_t *)(fb_map + offset);
	if (shadow_on)
		where_shadow = (uint8_t *)(shadow_map + offset);

	for (i = 0; i < da->height; i++) {
		uint8_t	*dest = where_fb + (i * scanline);
		uint8_t	*src = data + (i * w_size);

		(void) memcpy(dest, src, w_size);
		if (shadow_on) {
			dest = where_shadow + (i * scanline);
			(void) memcpy(dest, src, w_size);
		}
	}
	SFENCE();
}
