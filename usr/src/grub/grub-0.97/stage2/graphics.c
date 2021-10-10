/* graphics.c - graphics mede support for GRUB */
/* Implemented as a terminal type by Jeremy Katz <katzj@redhat.com> based
 * on a patch by Paulo C�sar Pereira de Andrade <pcpa@conectiva.com.br>
 */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2001,2002  Red Hat, Inc.
 *  Portions copyright (C) 2000  Conectiva, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifdef SUPPORT_GRAPHICS

#include <term.h>
#include <shared.h>
#include <graphics.h>

#ifdef	OVERLAY_LOGO
#include <logo.xbm>
#endif	/* OVERLAY_LOGO */

int saved_videomode;
unsigned char *font8x16;

int graphics_inited = 0;
int grounds_set;

#define	PALETTE_REDSHIFT	16
#define	PALETTE_GREENSHIFT	8
#define	PALETTE_COLORMASK	63

#define	PALETTE_NCOLORS		16

#define	PALETTE_RED(entry)	((entry) >> PALETTE_REDSHIFT)
#define	PALETTE_GREEN(entry)	(((entry) >> PALETTE_GREENSHIFT) & \
				    PALETTE_COLORMASK)
#define	PALETTE_BLUE(entry)	((entry) & PALETTE_COLORMASK)

static char splashimage[64];
static int splash_palette[PALETTE_NCOLORS];

#define	HPIXELS		640
#define	VPIXELS		480
#define	HPIXELSPERBYTE	8

#define	ROWBYTES	(HPIXELS / HPIXELSPERBYTE)
#define	SCREENBYTES	(ROWBYTES * VPIXELS)

#define VSHADOW VSHADOW1
unsigned char VSHADOW1[SCREENBYTES];
unsigned char VSHADOW2[SCREENBYTES];
unsigned char VSHADOW4[SCREENBYTES];
unsigned char VSHADOW8[SCREENBYTES];

static unsigned char *s1 = (unsigned char*)VSHADOW1;
static unsigned char *s2 = (unsigned char*)VSHADOW2;
static unsigned char *s4 = (unsigned char*)VSHADOW4;
static unsigned char *s8 = (unsigned char*)VSHADOW8;

/* constants to define the viewable area */
const int x0 = 0;
const int x1 = ROWBYTES;
const int y0 = 0;
const int y1 = 30;

/* text buffer has to be kept around so that we can write things as we
 * scroll and the like */
unsigned short text[ROWBYTES * 30];

/* why do these have to be kept here? */
int foreground = (63 << 16) | (63 << 8) | (63), background = 0, border = 0;


/* current position */
static int fontx = 0;
static int fonty = 0;

/* global state so that we don't try to recursively scroll or cursor */
static int no_scroll = 0;

/* color state */
static int graphics_standard_color = A_NORMAL;
static int graphics_normal_color = A_NORMAL;
static int graphics_highlight_color = A_REVERSE;
static int graphics_current_color = A_NORMAL;
static color_state graphics_color_state = COLOR_STATE_STANDARD;


/* graphics local functions */
static void graphics_setxy(int col, int row);
static void graphics_scroll(void);
static int read_image(char *);

#ifdef	OVERLAY_LOGO
static void draw_xbmlogo(void);
#endif	/* OVERLAY_LOGO */

/* FIXME: where do these really belong? */
static inline void outb(unsigned short port, unsigned char val)
{
    __asm __volatile ("outb %0,%1"::"a" (val), "d" (port));
}

static void MapMask(int value) {
    outb(0x3c4, 2);
    outb(0x3c5, value);
}

/* bit mask register */
static void BitMask(int value) {
    outb(0x3ce, 8);
    outb(0x3cf, value);
}


/* Set the splash image */
void graphics_set_splash(char *splashfile) {
    grub_strcpy(splashimage, splashfile);
}

/* Get the current splash image */
char *graphics_get_splash(void) {
    return splashimage;
}

/* Initialize a vga16 graphics display with the palette based off of
 * the image in splashimage.  If the image doesn't exist, leave graphics
 * mode.  */
int graphics_init()
{
    int image_read, index, color;

    if (!graphics_inited) {
        saved_videomode = set_videomode(0x12);
    }

    font8x16 = (unsigned char*)graphics_get_font();

    image_read = read_image(splashimage);

    /*
     * Set VGA palette color 0 to be the system background color, 15 to be the
     * system foreground color, and 17 to be the system border color.
     *
     * If the splashimage was read successfully, program the palette with
     * its new colors; if not, set them to the background color.
     */

   graphics_set_palette(0, PALETTE_RED(background),
	    PALETTE_GREEN(background), PALETTE_BLUE(background));

    for (index = 1; index < 15; index++) {
	color = (image_read ? splash_palette[index] : background);
	graphics_set_palette(index, PALETTE_RED(color),
	    PALETTE_GREEN(color), PALETTE_BLUE(color));
    }

   graphics_set_palette(15, PALETTE_RED(foreground),
	    PALETTE_GREEN(foreground), PALETTE_BLUE(foreground));

    graphics_set_palette(0x11, PALETTE_RED(border), PALETTE_GREEN(border),
	PALETTE_BLUE(border));

#ifdef	OVERLAY_LOGO
    draw_xbmlogo();
#endif	/* OVERLAY_LOGO */

    graphics_inited = 1;

    /* make sure that the highlight color is set correctly */
    graphics_highlight_color = ((graphics_normal_color >> 4) | 
				((graphics_normal_color & 0xf) << 4));

    return 1;
}

/* Leave graphics mode */
void graphics_end(void)
{
    if (graphics_inited) {
        set_videomode(saved_videomode);
        graphics_inited = 0;
    }
}

/* Print ch on the screen.  Handle any needed scrolling or the like */
void graphics_putchar(int ch) {
    ch &= 0xff;

    graphics_cursor(0);

    if (ch == '\n') {
        if (fonty + 1 < y1)
            graphics_setxy(fontx, fonty + 1);
        else
            graphics_scroll();
        graphics_cursor(1);
        return;
    } else if (ch == '\r') {
        graphics_setxy(x0, fonty);
        graphics_cursor(1);
        return;
    }

    graphics_cursor(0);

    text[fonty * ROWBYTES + fontx] = ch;
    text[fonty * ROWBYTES + fontx] &= 0x00ff;
    if (graphics_current_color & 0xf0)
        text[fonty * ROWBYTES + fontx] |= 0x100;

    graphics_cursor(0);

    if ((fontx + 1) >= x1) {
        graphics_setxy(x0, fonty);
        if (fonty + 1 < y1)
            graphics_setxy(x0, fonty + 1);
        else
            graphics_scroll();
    } else {
        graphics_setxy(fontx + 1, fonty);
    }

    graphics_cursor(1);
}

/* get the current location of the cursor */
int graphics_getxy(void) {
    return (fontx << 8) | fonty;
}

void graphics_gotoxy(int x, int y) {
    graphics_cursor(0);

    graphics_setxy(x, y);

    graphics_cursor(1);
}

void graphics_cls(void) {
    int i;
    unsigned char *mem;

    graphics_cursor(0);
    graphics_gotoxy(x0, y0);

    mem = (unsigned char*)VIDEOMEM;

    for (i = 0; i < ROWBYTES * 30; i++)
        text[i] = ' ';
    graphics_cursor(1);

    BitMask(0xff);

    /* plane 1 */
    MapMask(1);
    grub_memcpy(mem, s1, SCREENBYTES);

    /* plane 2 */
    MapMask(2);
    grub_memcpy(mem, s2, SCREENBYTES);

    /* plane 3 */
    MapMask(4);
    grub_memcpy(mem, s4, SCREENBYTES);

    /* plane 4 */
    MapMask(8);
    grub_memcpy(mem, s8, SCREENBYTES);

    MapMask(15);
}

void graphics_setcolorstate (color_state state) {
    switch (state) {
    case COLOR_STATE_STANDARD:
        graphics_current_color = graphics_standard_color;
        break;
    case COLOR_STATE_NORMAL:
        graphics_current_color = graphics_normal_color;
        break;
    case COLOR_STATE_HIGHLIGHT:
        graphics_current_color = graphics_highlight_color;
        break;
    default:
        graphics_current_color = graphics_standard_color;
        break;
    }

    graphics_color_state = state;
}

void graphics_setcolor (int normal_color, int highlight_color) {
    graphics_normal_color = normal_color;
    graphics_highlight_color = highlight_color;

    graphics_setcolorstate (graphics_color_state);
}

int graphics_setcursor (int on) {
    /* FIXME: we don't have a cursor in graphics */
    return 1;
}

#ifdef	OVERLAY_LOGO
static void draw_xbmlogo(void)
{
    unsigned char mask;
    unsigned xbm_index = 0, xbm_incr;
    unsigned screenx, logox, logoy, fb_offset, fb_index;

    /* 
     * Place the logo such that the right hand side will be four pixels from
     * the right hand edge of the screen and the bottom will be two pixels
     * from the bottom edge.
     */
    fb_offset = ((VPIXELS - 1) - logo_height - 2) * ROWBYTES;
    xbm_incr = (logo_width / 8) + 1;

    for (logoy = 0; logoy < logo_height; logoy++) {
	for (logox = 0, screenx = (HPIXELS - 1) - logo_width - 4;
	  logox < logo_width; logox++, screenx++) {
	    mask = 0x80 >> (screenx & 7);
	    fb_index = fb_offset + (screenx >> 3);

	    /*
	     * If a bit is clear in the bitmap, draw it onto the
	     * framebuffer in the default foreground color.
	     */
	    if ((logo_bits[xbm_index + (logox >> 3)] &
		(1 << (logox & 7))) == 0) {
		    /* system default foreground color */
		    s1[fb_index] |= mask;
		    s2[fb_index] |= mask;
		    s4[fb_index] |= mask;
		    s8[fb_index] |= mask;
	    }
	}

	xbm_index += xbm_incr;
	fb_offset += ROWBYTES;
    }
}
#endif	/* OVERLAY_LOGO */

/*
 * Read in the splashscreen image and set the palette up appropriately.
 * 
 * Format of splashscreen is an XPM (can be gzipped) with up to 15 colors and
 * is assumed to be of the proper screen dimensions.
 */
static int read_image(char *s)
{
    char buf[32], pal[16], tail[128];
    unsigned char c, base, mask;
    unsigned i, len, idx, colors, x, y, width, height;
    char *cp;

    if (!grub_open(s))
        return 0;

    /* read XPM header - must match memcmp string PRECISELY. */
    if (!grub_read((char*)&buf, 10) || grub_memcmp(buf, "/* XPM */\n", 10)) {
	errnum = ERR_NOTXPM;
        grub_close();
        return 0;
    }
    
    /* skip characters until we reach an initial '"' */
    while (grub_read(&c, 1)) {
        if (c == '"')
            break;
    }

    /* skip whitespace */
    while (grub_read(&c, 1) && (c == ' ' || c == '\t'))
	;

    /*
     * Format here should be four integers:
     *
     *     Width Height NumberOfColors CharactersPerPixel
     */
    i = 0;
    width = c - '0';
    while (grub_read(&c, 1)) {
        if (c >= '0' && c <= '9')
            width = width * 10 + c - '0';
        else
            break;
    }

    /* skip whitespace to advance to next digit */
    while (grub_read(&c, 1) && (c == ' ' || c == '\t'))
	;

    height = c - '0';
    while (grub_read(&c, 1)) {
        if (c >= '0' && c <= '9')
            height = height * 10 + c - '0';
        else
            break;
    }

    /* skip whitespace to advance to next digit */
    while (grub_read(&c, 1) && (c == ' ' || c == '\t')) ;

    colors = c - '0';
    while (grub_read(&c, 1)) {
        if (c >= '0' && c <= '9')
            colors = colors * 10 + c - '0';
        else
            break;
    }

    /* eat rest of line - assumes chars per pixel is one */
    while (grub_read(&c, 1) && c != '"')
        ;

    /*
     * Parse the XPM palette - the format is:
     *
     *    identifier colorspace #RRGGBB
     *
     * The identifier is simply a single character; the colorspace identifier
     * is skipped as it's assumed to be "c" denoting RGB color.
     *
     * The six digits after the "#" are assumed to be a six digit RGB color
     * identifier as defined in X11's rgb.txt file.
     */
    for (i = 0, idx = 1; i < colors; i++) {
        len = 0;

        while (grub_read(&c, 1) && c != '"')
            ;

        grub_read(&c, 1);       /* char */
        base = c;
        grub_read(buf, 4);      /* \t c # */

        while (grub_read(&c, 1) && c != '"') {
            if (len < sizeof(buf))
                buf[len++] = c;
        }

	/*
	 * The RGB hex digits should be six characters in length.
	 *
	 * If the color field contains anything other than six
	 * characters, such as "None" to denote a transparent color,
	 * ignore it.
	 */
        if (len == 6) {
            int r = ((hex(buf[0]) << 4) | hex(buf[1])) >> 2;
            int g = ((hex(buf[2]) << 4) | hex(buf[3])) >> 2;
            int b = ((hex(buf[4]) << 4) | hex(buf[5])) >> 2;

	    if (idx > 14) {
		errnum = ERR_TOOMANYCOLORS;
		grub_close();
		return 0;
	    }

            pal[idx] = base;
	    splash_palette[idx++] = 
		((r & PALETTE_COLORMASK) << PALETTE_REDSHIFT) |
		((g & PALETTE_COLORMASK) << PALETTE_GREENSHIFT) |
		(b & PALETTE_COLORMASK);
        }
    }

    colors = idx - 1;	/* actual number of colors used in XPM image */
    x = y = len = 0;

    /* clear (zero out) all four planes of the framebuffer */
    for (i = 0; i < SCREENBYTES; i++)
        s1[i] = s2[i] = s4[i] = s8[i] = 0;

    /* parse the XPM data */
    while (y < height) {
	/* exit on EOF, otherwise skip characters until an initial '"' */
        while (1) {
            if (!grub_read(&c, 1)) {
		errnum = ERR_CORRUPTXPM;
                grub_close();
                return 0;
            }
            if (c == '"')
                break;
        }

	/* read characters until we hit an EOF or a terminating '"' */
        while (grub_read(&c, 1) && c != '"') {
	    int pixel = 0;

	    /*
	     * Look up the specified pixel color in the palette; the
	     * pixel will not be drawn if its color cannot be found or
	     * if no colors were specified in the XPM image itself.
	     */
            for (i = 1; i <= colors; i++)
                if (pal[i] == c) {
                    pixel = i;
                    break;
                }

	    /*
	     * A bit is set in each of the "planes" of the frame buffer to
	     * denote a pixel drawn in each color of the palette.
	     *
	     * The planes are a binary representation of the palette, so a
	     * pixel in color "1" of the palette would be denoted by setting a
	     * bit in plane "s1"; a pixel in color "15" of the palette would
	     * set the same bit in each of the four planes.
	     *
	     * Pixels are represented by set bits in a byte, in the order
	     * left-to-right (e.g. pixel 0 is 0x80, pixel 7 is 1.)
	     */
	    if (pixel != 0) {
		mask = 0x80 >> (x & 7);

		if (pixel & 1)
		    s1[len + (x >> 3)] |= mask;
		if (pixel & 2)
		    s2[len + (x >> 3)] |= mask;
		if (pixel & 4)
		    s4[len + (x >> 3)] |= mask;
		if (pixel & 8)
		    s8[len + (x >> 3)] |= mask;
	    }

	    /*
	     * Increment "x"; if we hit pixel HPIXELS, wrap to the start of the
	     * next horizontal line if we haven't yet reached the bottom of
	     * the screen.
	     */
            if (++x >= HPIXELS) {
                x = 0;

                if (y++ < VPIXELS)
                    len += ROWBYTES;
		else
		    break;
            }
        }

    }
        /* check for foreground and background keywords in a trailing comment */
	grounds_set = 0;
	for (i = 0; i++; i < 128)
		tail[i] = 0;
	(void)grub_read((char*)&tail, -1);
	if (cp = strstr(tail, "foreground")) {
		int r, g, b;

		cp += 11;

		if (grub_strlen(cp) >= 6) {
                	r = ((hex(cp[0]) << 4) | hex(cp[1])) >> 2;
                	g = ((hex(cp[2]) << 4) | hex(cp[3])) >> 2;
                	b = ((hex(cp[4]) << 4) | hex(cp[5])) >> 2;

	        	foreground =
			    ((r & PALETTE_COLORMASK) << PALETTE_REDSHIFT) |
			    ((g & PALETTE_COLORMASK) << PALETTE_GREENSHIFT) |
			    (b & PALETTE_COLORMASK);

			grounds_set++;
		}
	}
	if (cp = strstr(tail, "background")) {
		int r, g, b;

		cp += 11;

		if (grub_strlen(cp) >= 6) {
                	r = ((hex(cp[0]) << 4) | hex(cp[1])) >> 2;
                	g = ((hex(cp[2]) << 4) | hex(cp[3])) >> 2;
                	b = ((hex(cp[4]) << 4) | hex(cp[5])) >> 2;

	        	background =
			    ((r & PALETTE_COLORMASK) << PALETTE_REDSHIFT) |
			    ((g & PALETTE_COLORMASK) << PALETTE_GREENSHIFT) |
			    (b & PALETTE_COLORMASK);

			grounds_set++;
		}
	}
	if (grounds_set == 1)	/* setting only one does not count */
		grounds_set = 0;

    grub_close();

    return 1;
}

/* Convert a character which is a hex digit to the appropriate integer */
int hex(int v)
{
    if (v >= 'A' && v <= 'F')
        return (v - 'A' + 10);
    if (v >= 'a' && v <= 'f')
        return (v - 'a' + 10);
    return (v - '0');
}


/* move the graphics cursor location to col, row */
static void graphics_setxy(int col, int row) {
    if (col >= x0 && col < x1) {
        fontx = col;
        cursorX = col << 3;
    }
    if (row >= y0 && row < y1) {
        fonty = row;
        cursorY = row << 4;
    }
}

/* scroll the screen */
static void graphics_scroll() {
    int i, j;

    /* we don't want to scroll recursively... that would be bad */
    if (no_scroll)
        return;
    no_scroll = 1;

    /* move everything up a line */
    for (j = y0 + 1; j < y1; j++) {
        graphics_gotoxy(x0, j - 1);
        for (i = x0; i < x1; i++) {
            graphics_putchar(text[j * ROWBYTES + i]);
        }
    }

    /* last line should be blank */
    graphics_gotoxy(x0, y1 - 1);
    for (i = x0; i < x1; i++)
        graphics_putchar(' ');
    graphics_setxy(x0, y1 - 1);

    no_scroll = 0;
}

void graphics_cursor(int set) {
    unsigned char *pat, *mem, *ptr, chr[16 << 2];
    int i, ch, invert, offset;

    if (set && no_scroll)
        return;

    offset = cursorY * ROWBYTES + fontx;
    ch = text[fonty * ROWBYTES + fontx] & 0xff;
    invert = (text[fonty * ROWBYTES + fontx] & 0xff00) != 0;
    pat = font8x16 + (ch << 4);

    mem = (unsigned char*)VIDEOMEM + offset;

    if (!set) {
        for (i = 0; i < 16; i++) {
            unsigned char mask = pat[i];

            if (!invert) {
                chr[i     ] = ((unsigned char*)VSHADOW1)[offset];
                chr[16 + i] = ((unsigned char*)VSHADOW2)[offset];
                chr[32 + i] = ((unsigned char*)VSHADOW4)[offset];
                chr[48 + i] = ((unsigned char*)VSHADOW8)[offset];

                /* FIXME: if (shade) */
                if (1) {
                    if (ch == DISP_VERT || ch == DISP_LL ||
                        ch == DISP_UR || ch == DISP_LR) {
                        unsigned char pmask = ~(pat[i] >> 1);

                        chr[i     ] &= pmask;
                        chr[16 + i] &= pmask;
                        chr[32 + i] &= pmask;
                        chr[48 + i] &= pmask;
                    }
                    if (i > 0 && ch != DISP_VERT) {
                        unsigned char pmask = ~(pat[i - 1] >> 1);

                        chr[i     ] &= pmask;
                        chr[16 + i] &= pmask;
                        chr[32 + i] &= pmask;
                        chr[48 + i] &= pmask;
                        if (ch == DISP_HORIZ || ch == DISP_UR || ch == DISP_LR) {
                            pmask = ~pat[i - 1];

                            chr[i     ] &= pmask;
                            chr[16 + i] &= pmask;
                            chr[32 + i] &= pmask;
                            chr[48 + i] &= pmask;
                        }
                    }
                }
                chr[i     ] |= mask;
                chr[16 + i] |= mask;
                chr[32 + i] |= mask;
                chr[48 + i] |= mask;

                offset += ROWBYTES;
            }
            else {
                chr[i     ] = mask;
                chr[16 + i] = mask;
                chr[32 + i] = mask;
                chr[48 + i] = mask;
            }
        }
    }
    else {
        MapMask(15);
        ptr = mem;
        for (i = 0; i < 16; i++, ptr += ROWBYTES) {
            cursorBuf[i] = pat[i];
            *ptr = ~pat[i];
        }
        return;
    }

    offset = 0;
    for (i = 1; i < 16; i <<= 1, offset += 16) {
        int j;

        MapMask(i);
        ptr = mem;
        for (j = 0; j < 16; j++, ptr += ROWBYTES)
            *ptr = chr[j + offset];
    }

    MapMask(15);
}

#endif /* SUPPORT_GRAPHICS */
