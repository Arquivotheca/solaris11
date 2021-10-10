/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"
/*
 * process.c    code set conversion on the input
 */
#include "iconv_int.h"

#define	NODATA	-1

static int	getbyte(int, int);
static void	putbyte(unsigned char, int);
static void	putstring(unsigned char *, int, int);
static unsigned char	rewindnodes(int);
static void	clear(int);

/*
 * in the following buffer
 * from begin to readp
 * are the currently held
 * characters being used to
 * from a composite.
 * from readp to end are
 * the next characters
 */
static struct level {
    int  begin;
    int  end;
    int  read_point;
    int  must_clear;
    unsigned char input_chars[KBDBUFSIZE];
    struct cornode *c_node;
} levels[10];

/*
 * Process is a
 * recursive routine which will
 * decend the tree and pass on
 * to th next level in a composite
 * table any finished characters
 * If no data available it will
 * return to previous level
 * will attempt to generate more data.
 */

int
process(struct kbd_tab *t, int fd, int level)
{
	int j;
	unsigned char c;
	int ch;
	struct cornode *n;
	struct cornode *node_tab;


	if (!t) {
		while ((ch = getbyte(fd, level)) != NODATA) {
			c = (unsigned char) (ch & 0x0FF);
			clear(level);
			(void) printf("%c", c);

		}
		return (0);
	}

	if (t->t_flag == KBD_COT) {
		while ((ch = getbyte(fd, level)) != NODATA) {
			c = (unsigned char) (ch & 0x0FF);
			putbyte(c, level + 1);
			clear(level);
			(void) process(t->t_next, fd, level + 1);
		}
		return (0);
	}
	if (t->t_flag & KBD_ONE) {
		while ((ch = getbyte(fd, level)) != NODATA) {
			c = t->t_oneone[ch];
			putbyte(c, level + 1);
			clear(level);
			(void) process(t->t_next, fd, level + 1);
		}
		return (0);
	}
	if (!t->t_nodes) {
		while ((ch = getbyte(fd, level)) != NODATA) {
			c = (unsigned char)(ch & 0x0FF);
			putbyte(c, level + 1);
			clear(level);
			(void) process(t->t_next, fd, level + 1);
		}
		return (0);
	}
	node_tab = t->t_nodep;
	while ((ch = getbyte(fd, level)) != NODATA) {
		c = (unsigned char)(ch & 0x0FF);
		if (!levels[level].c_node) {
			if (t->t_flag & KBD_FULL) {
				if (c > t->t_max || c < t->t_min) {
					putbyte(c, level + 1);
					clear(level);
					(void) process(t->t_next, fd,
					    level + 1);
					continue;
				} else {
					j = (int)(c - t->t_min);
				}
			} else {
				j = 0;
			}
			levels[level].c_node = &node_tab[j];
		}
		for (; ; ) {
			n = levels[level].c_node;
			if (n->c_val == c) {
				/*
				 * Match
				 */
				if (n->c_flag & ND_RESULT) {
					if (n->c_flag & ND_INLINE) {
						putbyte(n->c_child, level + 1);
						clear(level);
						(void) process(t->t_next, fd,
						    level + 1);
						break;
					} else {
						putstring(t->t_textp,
						    n->c_child, level + 1);
						clear(level);
						(void) process(t->t_next, fd,
						    level + 1);
						break;
					}
				}
				/*
				 * decend tree
				 */
				levels[level].c_node = &node_tab[n->c_child];
				break;
			} else {
				/*
				 * not matching
				 */
				if (n->c_flag & ND_LAST) {
					/*
					 * get the first
					 * byte that caused
					 * this tree traversal
					 */
					c = rewindnodes(level);
					/*
					 * give it to the next level
					 * then continue from the second byte
					 * that got us here
					 */
					if (t->t_flag & KBD_ERR) {
						putstring(t->t_textp,
						    t->t_error, level + 1);
					} else {
						putbyte(c, level + 1);
					}
					(void) process(t->t_next, fd,
					    level + 1);
					break;
				} else {
					levels[level].c_node = ++n;
					continue;
				}
			}
		}
	}
	return (0);
}


static int
getbyte(int fd, int level)
{
	int	n;
	int	j;
	char	buf[KBDREADBUF];
	int	end;
	int	begin;
	int	read_point;
	unsigned char	*p;

	end = levels[level].end;
	begin = levels[level].begin;
	read_point = levels[level].read_point;
	p = levels[level].input_chars;

	if (levels[level].must_clear) {
		(void) fprintf(stderr, "%s",
		    gettext("Buffer Overrun. (1)\n"));
		exit(1);
	}
	if (read_point == end) {
		/*
		 * NODATA. If level 0
		 * read a byte or 2.
		 */
		if (level) {
			return (NODATA);
		}
		n = begin - end;
		if (n <= 0) {
			n = KBDBUFSIZE + n;
		}
		if (n > KBDREADBUF) {
			n = KBDREADBUF;
		}
		j = read(fd, buf, n);
		if (!j || j < 0) {
			return (NODATA);
		}

		/*
		 * FILL BUFFER from end
		 */
		for (n = 0; n < j; n++) {
			p[end] = buf[n];
			end = (end + 1) % KBDBUFSIZE;
		}
		levels[level].end = end;

	}

	j = (int)(p[read_point] & 0x0FF);

	/*
	 * increment read point
	 * if i hit begin then must
	 * do a clear opperation
	 * before next read or
	 * will get buffer overran
	 */
	read_point = (read_point + 1) % KBDBUFSIZE;
	levels[level].read_point = read_point;
	if (read_point == begin) {
		levels[level].must_clear = 1;
	}

	return (j);
}

/*
 * putstring uses
 * putbyte
 */
static void
putstring(unsigned char *s, int index, int level)
{
	s += index;

	while (*s) {
		putbyte(*s++, level);
	}
}

/*
 *  Give the byte to the
 * next level
 */

static void
putbyte(unsigned char c, int level)
{
	int	end = levels[level].end;
	unsigned char	*p = levels[level].input_chars;

	p[end] = c;
	levels[level].end = (end + 1) % KBDBUFSIZE;
}

/*
 * Clear the held characters
 * up to the read point.
 * they've been processed
 */
static void
clear(int level)
{
	levels[level].must_clear = 0;
	levels[level].begin = levels[level].read_point;
	levels[level].c_node = NULL;
}

static unsigned char
rewindnodes(int level)
{
	int	begin = levels[level].begin;
	unsigned char	c;

	c = levels[level].input_chars[begin];
	begin = (begin + 1) % KBDBUFSIZE;
	levels[level].read_point = begin;
	levels[level].must_clear = 0;
	levels[level].begin = begin;
	levels[level].c_node = NULL;
	return (c);
}
