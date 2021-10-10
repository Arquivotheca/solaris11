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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/panic.h>
#include <sys/ctype.h>
#include <sys/fbinfo.h>
#include <sys/bootinfo.h>
#include <sys/boot_console.h>
#if defined(__xpv)
#include <sys/hypervisor.h>
#endif /* __xpv */

#include <dboot/dboot_printf.h>

#include "boot_serial.h"
#include "boot_vga.h"

#if defined(_BOOT)
#include <dboot/dboot_asm.h>
#include <dboot/dboot_xboot.h>
extern unsigned long strtoul(const char *str, char **nptr, int base);
#else /* _BOOT */
#include <sys/sunddi.h>
#include <sys/bootconf.h>
#if defined(__xpv)
#include <sys/evtchn_impl.h>
#endif /* __xpv */
static char *defcons_buf;
static char *defcons_cur;
#endif /* _BOOT */

#if defined(__xpv)
extern void bcons_init_xen(char *);
extern void bcons_putchar_xen(int);
extern int bcons_getchar_xen(void);
extern int bcons_ischar_xen(void);
#endif /* __xpv */

static int cons_color = CONS_COLOR;
int dual_console;
int console;

static int serial_ischar(void);
static int serial_getchar(void);
static void serial_putchar(int);
static void serial_adjust_prop(void);

static int fb_info_init(struct xboot_info *);
extern int xbi_fb_info_init(struct xboot_info *);
extern void boot_fb_init();
extern void boot_fb_putchar(uint8_t);

static char *boot_line = NULL;

fb_info_t fb_info;
int restore_vga_text;
int restore_vesa_mode;
uint16_t saved_vbe_mode = 0;

/*
 * Defined inside i86pc/dboot/dboot_printf.c for dboot and inside
 * i86pc/os/machdep.c for genunix.
 */
extern uint8_t	force_screen_output;

#define	CMDPARAM_SEPARATOR(c) ((c) == ',' || (c) == '\'' || \
	(c) == '"' || ISSPACE(c))

#if !defined(_BOOT)
/* Set if the console or mode are expressed in the boot line */
static int console_set, console_mode_set;
#endif

int	boot_fb_type = 0;

/* Clear the screen and initialize VIDEO, XPOS and YPOS. */
void
clear_screen(void)
{
#if 0
	/*
	 * XXX should set vga mode so we don't depend on the
	 * state left by the boot loader.  Note that we have to
	 * enable the cursor before clearing the screen since
	 * the cursor position is dependant upon the cursor
	 * skew, which is initialized by vga_cursor_display()
	 */
	vga_cursor_display();
	vga_clear(cons_color);
	vga_setpos(0, 0);
#endif
}

/* Put the character C on the screen. */
static void
screen_putchar(int c)
{
	int row, col;

	vga_getpos(&row, &col);
	switch (c) {
	case '\t':
		col += 8 - (col % 8);
		if (col == VGA_TEXT_COLS)
			col = 79;
		vga_setpos(row, col);
		break;

	case '\r':
		vga_setpos(row, 0);
		break;

	case '\b':
		if (col > 0)
			vga_setpos(row, col - 1);
		break;

	case '\n':
		if (row < VGA_TEXT_ROWS - 1)
			vga_setpos(row + 1, col);
		else
			vga_scroll(cons_color);
		break;

	default:
		vga_drawc(c, cons_color);
		if (col < VGA_TEXT_COLS -1)
			vga_setpos(row, col + 1);
		else if (row < VGA_TEXT_ROWS - 1)
			vga_setpos(row + 1, 0);
		else {
			vga_setpos(row, 0);
			vga_scroll(cons_color);
		}
		break;
	}
}

static int port;

static void
serial_init(void)
{
	switch (console) {
	case CONS_TTYA:
		port = 0x3f8;
		break;
	case CONS_TTYB:
		port = 0x2f8;
		break;
	case CONS_TTYC:
		port = 0x3e8;
		break;
	case CONS_TTYD:
		port = 0x2e8;
		break;
	}

	outb(port + ISR, 0x20);
	if (inb(port + ISR) & 0x20) {
		/*
		 * 82510 chip is present
		 */
		outb(port + DAT+7, 0x04);	/* clear status */
		outb(port + ISR, 0x40);  /* set to bank 2 */
		outb(port + MCR, 0x08);  /* IMD */
		outb(port + DAT, 0x21);  /* FMD */
		outb(port + ISR, 0x00);  /* set to bank 0 */
	} else {
		/*
		 * set the UART in FIFO mode if it has FIFO buffers.
		 * use 16550 fifo reset sequence specified in NS
		 * application note. disable fifos until chip is
		 * initialized.
		 */
		outb(port + FIFOR, 0x00);		/* clear */
		outb(port + FIFOR, FIFO_ON);		/* enable */
		outb(port + FIFOR, FIFO_ON|FIFORXFLSH);  /* reset */
		outb(port + FIFOR,
		    FIFO_ON|FIFODMA|FIFOTXFLSH|FIFORXFLSH|0x80);
		if ((inb(port + ISR) & 0xc0) != 0xc0) {
			/*
			 * no fifo buffers so disable fifos.
			 * this is true for 8250's
			 */
			outb(port + FIFOR, 0x00);
		}
	}

	/* disable interrupts */
	outb(port + ICR, 0);


	/* adjust setting based on tty properties */
	serial_adjust_prop();

#if defined(_BOOT)
	/*
	 * Do a full reset to match console behavior.
	 * 0x1B + c - reset everything
	 */
	serial_putchar(0x1B);
	serial_putchar('c');
#endif
}

/* Advance str pointer past white space */
#define	EAT_WHITE_SPACE(str)	{			\
	while ((*str != '\0') && ISSPACE(*str))		\
		str++;					\
}

/*
 * boot_line is set when we call here.  Search it for the argument name,
 * and if found, return a pointer to it.
 */
static char *
find_boot_line_prop(const char *name)
{
	char *ptr;
	char *ret = NULL;
	char end_char;
	size_t len;

	if (boot_line == NULL)
		return (NULL);

	len = strlen(name);

	/*
	 * We have two nested loops here: the outer loop discards all options
	 * except -B, and the inner loop parses the -B options looking for
	 * the one we're interested in.
	 */
	for (ptr = boot_line; *ptr != '\0'; ptr++) {
		EAT_WHITE_SPACE(ptr);

		if (*ptr == '-') {
			ptr++;
			while ((*ptr != '\0') && (*ptr != 'B') &&
			    !ISSPACE(*ptr))
				ptr++;
			if (*ptr == '\0')
				goto out;
			else if (*ptr != 'B')
				continue;
		} else {
			while ((*ptr != '\0') && !ISSPACE(*ptr))
				ptr++;
			if (*ptr == '\0')
				goto out;
			continue;
		}

		do {
			ptr++;
			EAT_WHITE_SPACE(ptr);

			if ((strncmp(ptr, name, len) == 0) &&
			    (ptr[len] == '=')) {
				ptr += len + 1;
				if ((*ptr == '\'') || (*ptr == '"')) {
					ret = ptr + 1;
					end_char = *ptr;
					ptr++;
				} else {
					ret = ptr;
					end_char = ',';
				}
				goto consume_property;
			}

			/*
			 * We have a property, and it's not the one we're
			 * interested in.  Skip the property name.  A name
			 * can end with '=', a comma, or white space.
			 */
			while ((*ptr != '\0') && (*ptr != '=') &&
			    (*ptr != ',') && (!ISSPACE(*ptr)))
				ptr++;

			/*
			 * We only want to go through the rest of the inner
			 * loop if we have a comma.  If we have a property
			 * name without a value, either continue or break.
			 */
			if (*ptr == '\0')
				goto out;
			else if (*ptr == ',')
				continue;
			else if (ISSPACE(*ptr))
				break;
			ptr++;

			/*
			 * Is the property quoted?
			 */
			if ((*ptr == '\'') || (*ptr == '"')) {
				end_char = *ptr;
				ptr++;
			} else {
				/*
				 * Not quoted, so the string ends at a comma
				 * or at white space.  Deal with white space
				 * later.
				 */
				end_char = ',';
			}

			/*
			 * Now, we can ignore any characters until we find
			 * end_char.
			 */
consume_property:
			for (; (*ptr != '\0') && (*ptr != end_char); ptr++) {
				if ((end_char == ',') && ISSPACE(*ptr))
					break;
			}
			if (*ptr && (*ptr != ',') && !ISSPACE(*ptr))
				ptr++;
		} while (*ptr == ',');
	}
out:
	return (ret);
}


#define	MATCHES(p, pat)	\
	(strncmp(p, pat, strlen(pat)) == 0 ? (p += strlen(pat), 1) : 0)

#define	SKIP(p, c)				\
	while (*(p) != 0 && *p != (c))		\
		++(p);				\
	if (*(p) == (c))			\
		++(p);

/*
 * find a tty mode property either from cmdline or from boot properties
 */
static char *
get_mode_value(char *name)
{
	/*
	 * when specified on boot line it looks like "name" "="....
	 */
	if (boot_line != NULL) {
		return (find_boot_line_prop(name));
	}

#if defined(_BOOT)
	return (NULL);
#else
	/*
	 * if we're running in the full kernel we check the bootenv.rc settings
	 */
	{
		static char propval[20];

		propval[0] = 0;
		if (do_bsys_getproplen(NULL, name) <= 0)
			return (NULL);
		(void) do_bsys_getprop(NULL, name, propval);
		return (propval);
	}
#endif
}

/*
 * adjust serial port based on properties
 * These come either from the cmdline or from boot properties.
 */
static void
serial_adjust_prop(void)
{
	char propname[20];
	char *propval;
	char *p;
	ulong_t baud;
	uchar_t lcr = 0;
	uchar_t mcr = DTR | RTS;


	(void) strcpy(propname, "ttyX-mode");
	propname[3] = 'a' + console - CONS_TTYA;

	propval = get_mode_value(propname);
	if (propval == NULL)
		propval = "9600,8,n,1,-";
#if !defined(_BOOT)
	else
		console_mode_set = 1;
#endif

	/* property is of the form: "9600,8,n,1,-" */
	p = propval;
	if (MATCHES(p, "110,"))
		baud = ASY110;
	else if (MATCHES(p, "150,"))
		baud = ASY150;
	else if (MATCHES(p, "300,"))
		baud = ASY300;
	else if (MATCHES(p, "600,"))
		baud = ASY600;
	else if (MATCHES(p, "1200,"))
		baud = ASY1200;
	else if (MATCHES(p, "2400,"))
		baud = ASY2400;
	else if (MATCHES(p, "4800,"))
		baud = ASY4800;
	else if (MATCHES(p, "19200,"))
		baud = ASY19200;
	else if (MATCHES(p, "38400,"))
		baud = ASY38400;
	else if (MATCHES(p, "57600,"))
		baud = ASY57600;
	else if (MATCHES(p, "115200,"))
		baud = ASY115200;
	else {
		baud = ASY9600;
		SKIP(p, ',');
	}
	outb(port + LCR, DLAB);
	outb(port + DAT + DLL, baud & 0xff);
	outb(port + DAT + DLH, (baud >> 8) & 0xff);

	switch (*p) {
	case '5':
		lcr |= BITS5;
		++p;
		break;
	case '6':
		lcr |= BITS6;
		++p;
		break;
	case '7':
		lcr |= BITS7;
		++p;
		break;
	case '8':
		++p;
	default:
		lcr |= BITS8;
		break;
	}

	SKIP(p, ',');

	switch (*p) {
	case 'n':
		lcr |= PARITY_NONE;
		++p;
		break;
	case 'o':
		lcr |= PARITY_ODD;
		++p;
		break;
	case 'e':
		++p;
	default:
		lcr |= PARITY_EVEN;
		break;
	}


	SKIP(p, ',');

	switch (*p) {
	case '1':
		/* STOP1 is 0 */
		++p;
		break;
	default:
		lcr |= STOP2;
		break;
	}
	/* set parity bits */
	outb(port + LCR, lcr);

	(void) strcpy(propname, "ttyX-rts-dtr-off");
	propname[3] = 'a' + console - CONS_TTYA;

	propval = get_mode_value(propname);
	if (propval == NULL)
		propval = "false";
	if (propval[0] != 'f' && propval[0] != 'F')
		mcr = 0;
	/* set modem control bits */
	outb(port + MCR, mcr | OUT2);
}


static int
fb_info_init(struct xboot_info *xbi)
{
	char *cols_str;
	char *rows_str;
#if !defined(_BOOT)
	unsigned long val;
#endif
	if (xbi->bi_framebuffer_info != 0) {
		if (xbi_fb_info_init(xbi) == -1) {
			boot_fb_type = BOOT_FB_NONE;
			return (-1);
		}
		if (xbi->bi_vesa_mode != XBI_VBE_MODE_INVALID) {
			/*
			 * Keep track of the picked VESA mode to reset
			 * it at fastreboot time.
			 */
			saved_vbe_mode = xbi->bi_vesa_mode;
			boot_fb_type = BOOT_FB_VESA;
		} else {
			boot_fb_type = BOOT_FB_GENERIC;
		}
	} else {
		/* Text mode */
		boot_fb_type = BOOT_FB_NONE;
		return (-1);
	}

	fb_info.screen_char.cols = 0;
	fb_info.screen_char.rows = 0;

	cols_str = find_boot_line_prop("screen-#columns");
	if (cols_str != NULL) {
#if !defined(_BOOT)
		if (ddi_strtoul(cols_str, NULL, 0, &val) == 0) {
			fb_info.screen_char.cols = val;
		} else
			fb_info.screen_char.cols = 0;
#else
		fb_info.screen_char.cols = strtoul(cols_str, 0, 0);
#endif
	}

	if (fb_info.screen_char.cols == 0)
		fb_info.screen_char.cols = 80;

	rows_str = find_boot_line_prop("screen-#rows");
	if (rows_str != NULL) {
#if !defined(_BOOT)
		if (ddi_strtoul(rows_str, NULL, 0, &val) == 0) {
			fb_info.screen_char.rows = val;
		} else
			fb_info.screen_char.rows = 0;
#else
		fb_info.screen_char.rows = strtoul(rows_str, 0, 0);
#endif
	}

	if (fb_info.screen_char.rows == 0)
		fb_info.screen_char.rows = 34;

	/*
	 * Since we emulate set_font behavior in the boot_console we need to
	 * keep track of the original request of columns & rows in case
	 * no font fits and we pick the default one with a smaller border size.
	 * We then pass the requested value to tem, so that the set_font
	 * behavior will be identical to the emulated one.
	 * Note that requested_char will always be equal to screen_char if a
	 * font + default border fits.
	 */
	fb_info.requested_char.rows = fb_info.screen_char.rows;
	fb_info.requested_char.cols = fb_info.screen_char.cols;

	fb_info.cursor_char.cols = 0;
	fb_info.cursor_char.rows = 0;

	return (0);
}


/*
 * A structure to map console names to values.
 */
typedef struct {
	char *name;
	int value;
} console_value_t;

console_value_t console_devices[] = {
	{ "ttya", CONS_TTYA },
	{ "ttyb", CONS_TTYB },
	{ "ttyc", CONS_TTYC },
	{ "ttyd", CONS_TTYD },
	{ "force-text", CONS_SCREEN_VGATEXT },
	{ "graphics", CONS_SCREEN_GRAPHICS },
	{ "text", CONS_SCREEN_FB},
#if defined(__xpv)
	{ "hypervisor", CONS_HYPERVISOR },
#endif
#if !defined(_BOOT)
	{ "usb-serial", CONS_USBSER },
#endif
	{ "", CONS_INVALID }
};

void
bcons_init(struct xboot_info *xbi)
{
	console_value_t *consolep;
	size_t len, cons_len;
	char *cons_str;
#if !defined(_BOOT)
#if !defined(DEBUG)
	extern int post_fastreboot;
#endif
	extern int restore_vga_text;
#endif

	boot_line = (char *)(uintptr_t)xbi->bi_cmdline;

	if (strstr(boot_line, "dual_console") != 0) {
		console = CONS_TTYA;
		serial_init();
		dual_console = 1;
	}

	console = CONS_INVALID;

#if defined(__xpv)
	bcons_init_xen(boot_line);
#endif /* __xpv */

	cons_str = find_boot_line_prop("console");
	if (cons_str == NULL)
		cons_str = find_boot_line_prop("output-device");

	/*
	 * Go through the console_devices array trying to match the string
	 * we were given.  The string on the command line must end with
	 * a comma or white space.
	 */
	if (cons_str != NULL) {
		cons_len = strlen(cons_str);
		consolep = console_devices;
		for (; consolep->name[0] != '\0'; consolep++) {
			len = strlen(consolep->name);
			if ((len <= cons_len) && ((cons_str[len] == '\0') ||
			    CMDPARAM_SEPARATOR(cons_str[len])) &&
			    (strncmp(cons_str, consolep->name, len) == 0)) {
				console = consolep->value;
				break;
			}
		}
	}


#if defined(__xpv)
	/*
	 * domU's always use the hypervisor regardless of what
	 * the console variable may be set to.
	 */
	console = CONS_HYPERVISOR;
#endif /* __xpv */

	/*
	 * If no console device specified, default to text.
	 * Remember what was specified for second phase.
	 */
	if (console == CONS_INVALID)
		console = CONS_SCREEN_FB;
#if !defined(_BOOT)
	else
		console_set = 1;
#endif

	switch (console) {
	case CONS_TTYA:
	case CONS_TTYB:
	case CONS_TTYC:
	case CONS_TTYD:
		serial_init();
		break;

	case CONS_HYPERVISOR:
		break;

#if !defined(_BOOT)
	case CONS_USBSER:
		/*
		 * We can't do anything with USB until we have memory
		 * management.
		 */
		break;
#endif
	case CONS_SCREEN_GRAPHICS:
	case CONS_SCREEN_FB:
		if (fb_info_init(xbi) == 0) {
			boot_fb_init();
			kb_init();
#if !defined(_BOOT)
#if !defined(DEBUG)
			/*
			 * On DEBUG kernels, always enforce a reset of the
			 * VESA mode. On non-DEBUG do it only if absolutely
			 * necessary, which means, coming from fastreboot.
			 */
			if (post_fastreboot)
#endif
				if (boot_fb_type == BOOT_FB_VESA)
					restore_vesa_mode = 1;
#endif
			break;
		}
		/*
		 * If the bitmap framebuffer initialization fails, revert
		 * to text mode.
		 */
		console = CONS_SCREEN_VGATEXT;

	/*FALLTHRU*/
	case CONS_SCREEN_VGATEXT:
	default:
		/*
		 * Either the user has selected 'force-text', has mispelled
		 * the console name or we failed getting the VESA information.
		 * In any case a working console is expected, so forcing a reset
		 * to VGA TEXT mode is the way to go.
		 * The reset will happen in mlsetup.c/fix_console_mode(). This
		 * happens before kmdb kicks in (if we booted -k/-d), so we only
		 * end up clearing potential debug lines from dboot/early boot
		 * stages, which is acceptable.
		 *
		 * Since we always restore the VGA TEXT mode, we don't have to
		 * explicitly check for post_fastreboot. Please remember that
		 * fastreboot *needs* it to guarantee a sane console, if
		 * planning to change this behavior.
		 */
		restore_vga_text = 1;
#if defined(_BOOT)
		clear_screen();	/* clears the grub or xen screen */
#endif /* _BOOT */
		kb_init();
		break;
	}
	boot_line = NULL;
}

#if !defined(_BOOT)
/*
 * 2nd part of console initialization.
 * In the kernel (ie. fakebop), this can be used only to switch to
 * using a serial port instead of screen based on the contents
 * of the bootenv.rc file.
 */
/*ARGSUSED*/
void
bcons_init2(char *inputdev, char *outputdev, char *consoledev)
{
	int cons = CONS_INVALID;
	char *devnames[] = { consoledev, outputdev, inputdev, NULL };
	console_value_t *consolep;
	int i;

	if (console != CONS_USBSER && console != CONS_SCREEN_GRAPHICS) {
		if (console_set) {
			/*
			 * If the console was set on the command line,
			 * but the ttyX-mode was not, we only need to
			 * check bootenv.rc for that setting.
			 */
			if ((!console_mode_set) &&
			    (console == CONS_TTYA || console == CONS_TTYB ||
			    console == CONS_TTYC || console == CONS_TTYD))
				serial_init();
			return;
		}

		for (i = 0; devnames[i] != NULL; i++) {
			consolep = console_devices;
			for (; consolep->name[0] != '\0'; consolep++) {
				if (strcmp(devnames[i], consolep->name) == 0) {
					cons = consolep->value;
				}
			}
			if (cons != CONS_INVALID)
				break;
		}


		if ((cons == CONS_INVALID) || (cons == console)) {
			/*
			 * we're sticking with whatever the current setting is
			 */
			return;
		}

		console = cons;
		if (cons == CONS_TTYA || cons == CONS_TTYB ||
		    cons == CONS_TTYC || cons == CONS_TTYD) {
			serial_init();
			return;
		}
	} else {
		/*
		 * USB serial and GRAPHICS console
		 * we just collect data into a buffer
		 */
		extern void *defcons_init(size_t);
		defcons_buf = defcons_cur = defcons_init(MMU_PAGESIZE);
	}
}

static void
defcons_putchar(int c)
{
	if (defcons_buf != NULL &&
	    defcons_cur + 1 - defcons_buf < MMU_PAGESIZE) {
		*defcons_cur++ = c;
		*defcons_cur = 0;
	}
}
#endif	/* _BOOT */

static void
serial_putchar(int c)
{
	int checks = 10000;

	while (((inb(port + LSR) & XHRE) == 0) && checks--)
		;
	outb(port + DAT, (char)c);
}

static int
serial_getchar(void)
{
	uchar_t lsr;

	while (serial_ischar() == 0)
		;

	lsr = inb(port + LSR);
	if (lsr & (SERIAL_BREAK | SERIAL_FRAME |
	    SERIAL_PARITY | SERIAL_OVERRUN)) {
		if (lsr & SERIAL_OVERRUN) {
			return (inb(port + DAT));
		} else {
			/* Toss the garbage */
			(void) inb(port + DAT);
			return (0);
		}
	}
	return (inb(port + DAT));
}

static int
serial_ischar(void)
{
	return (inb(port + LSR) & RCA);
}

static void
_doputchar(int c)
{
	switch (console) {
	case CONS_TTYA:
	case CONS_TTYB:
	case CONS_TTYC:
	case CONS_TTYD:
		serial_putchar(c);
		return;
	case CONS_SCREEN_VGATEXT:
		if (dual_console)
			serial_putchar(c);
		screen_putchar(c);
		return;
	case CONS_SCREEN_FB:
		if (dual_console)
			serial_putchar(c);
		boot_fb_putchar(c);
		return;
	case CONS_SCREEN_GRAPHICS:
		if (dual_console)
			serial_putchar(c);
		if (force_screen_output) {
			boot_fb_putchar(c);
			return;
		}
	/*FALLTHRU*/
#if !defined(_BOOT)
	case CONS_USBSER:
		defcons_putchar(c);
#endif /* _BOOT */
		return;
	}
}

void
bcons_putchar(int c)
{

#if !defined(__xpv)
	static int bhcharpos = 0;
	if (c == '\t') {
		do {
			_doputchar(' ');
		} while (++bhcharpos % 8);
		return;
	} else  if (c == '\n' || c == '\r') {
		bhcharpos = 0;
		if (console != CONS_SCREEN_FB)
			_doputchar('\r');
		_doputchar(c);
		return;
	} else if (c == '\b') {
		if (bhcharpos)
			bhcharpos--;
		_doputchar(c);
		return;
	}

	bhcharpos++;
	_doputchar(c);

#else
	bcons_putchar_xen(c);
#endif /* !__xpv */

}

/*
 * kernel character input functions
 */
int
bcons_getchar(void)
{
#if !defined(__xpv)

	switch (console) {
	case CONS_TTYA:
	case CONS_TTYB:
	case CONS_TTYC:
	case CONS_TTYD:
		return (serial_getchar());
	default:
		return (kb_getchar());
	}
#else
	return (bcons_getchar_xen());
#endif /* !__xpv */
}

#if !defined(_BOOT)

int
bcons_ischar(void)
{

#if !defined(__xpv)

	switch (console) {
	case CONS_TTYA:
	case CONS_TTYB:
	case CONS_TTYC:
	case CONS_TTYD:
		return (serial_ischar());
	default:
		return (kb_ischar());
	}
#else
	return (bcons_ischar_xen());

#endif /* !__xpv */
}

#endif /* _BOOT */
