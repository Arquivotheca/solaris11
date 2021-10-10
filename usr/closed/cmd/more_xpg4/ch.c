/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * COPYRIGHT NOTICE
 * 
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 * 
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED 
 */

/*
 * Copyright (c) 1988 Mark Nudleman
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef lint
static char sccsid[] = "@(#)ch.c	5.11 (Berkeley) 6/21/92";
#endif /* not lint */

#if defined(sun)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <libintl.h>
#include <locale.h>
#include <limits.h>
#include <ctype.h>
#include "less.h"

extern int exit_status;
#endif

/*
 * Low level character input from the input file.
 * We use these special purpose routines which optimize moving
 * both forward and backward from the current read pointer.
 */


int file = -1;		/* File descriptor of the input file */

/*
 * Pool of buffers holding the most recently used blocks of the input file.
 */
struct buf {
	struct buf *next, *prev;
	long block;
	int datasize;
	unsigned char data[BUFSIZ*2];	/* double length for escaping */
};
int nbufs;

/*
 * The buffer pool is kept as a doubly-linked circular list, in order from
 * most- to least-recently used.  The circular list is anchored by buf_anchor.
 */
#define	END_OF_CHAIN	((struct buf *)&buf_anchor)
#define	buf_head	buf_anchor.next
#define	buf_tail	buf_anchor.prev

static struct {
	struct buf *next, *prev;
} buf_anchor = { END_OF_CHAIN, END_OF_CHAIN };

extern int ispipe, cbufs, sigs;

int	ch_offset_old;
long	ch_block_old;
/*
 * Current position in file.
 * Stored as a block number and an offset into the block.
 */
static long ch_block;
static int ch_offset;
/*
 * End of input positions
 */
static long eoi_block = -1;
static int eoi_offset = -1;

/* Length of file, needed if input is a pipe. */
static off_t ch_fsize;

/* Number of bytes read, if input is standard input (a pipe). */
static off_t last_piped_pos;

static int	fch_get(void);
static int	buffered(long);

wchar_t		ch_forw_get_w();
wchar_t		ch_back_get_w(off_t *);

/*
 * Get the character pointed to by the read pointer.  ch_get() is a macro
 * which is more efficient to call than fch_get (the function), in the usual
 * case that the block desired is at the head of the chain.
 */
#define	ch_get() \
	((buf_head->block == ch_block && \
	    ch_offset < buf_head->datasize) ? \
	    buf_head->data[ch_offset] : fch_get())

static int
fch_get(void)
{
	extern int bs_mode;
	register struct buf *bp;
	register int n, ch;
	off_t pos;
	unsigned char buf[BUFSIZ];

	/* look for a buffer holding the desired block. */
	for (bp = buf_head;  bp != END_OF_CHAIN;  bp = bp->next)
		if (bp->block == ch_block) {
			if (ch_offset >= bp->datasize)
				/*
				 * Need more data in this buffer.
				 */
				goto read_more;
			/*
			 * On a pipe, we don't sort the buffers LRU
			 * because this can cause gaps in the buffers.
			 * For example, suppose we've got 12 1K buffers,
			 * and a 15K input stream.  If we read the first 12K
			 * sequentially, then jump to line 1, then jump to
			 * the end, the buffers have blocks 0,4,5,6,..,14.
			 * If we then jump to line 1 again and try to
			 * read sequentially, we're out of luck when we
			 * get to block 1 (we'd get the "pipe error" below).
			 * To avoid this, we only sort buffers on a pipe
			 * when we actually READ the data, not when we
			 * find it already buffered.
			 */
			if (ispipe)
				return(bp->data[ch_offset]);
			goto found;
		}
	/*
	 * Block is not in a buffer.  Take the least recently used buffer
	 * and read the desired block into it.  If the LRU buffer has data
	 * in it, and input is a pipe, then try to allocate a new buffer first.
	 */
	if (ispipe && buf_tail->block != (long)(-1))
		(void)ch_addbuf(1);
	bp = buf_tail;
	bp->block = ch_block;
	bp->datasize = 0;

read_more:
	if ((ch_block == eoi_block) && (ch_offset == eoi_offset))
		return(EOI);

	pos = ((off_t) ch_block * BUFSIZ) + bp->datasize;
	if (ispipe) {
		/*
		 * The data requested should be immediately after
		 * the last data read from the pipe.
		 */
		if (pos != last_piped_pos) {
#if defined(sun)
			error(gettext("pipe error"));
			exit_status = 2;
#else
			error(MSGSTR(PIPE_ERR, "pipe error"));
#endif
			quit();
		}
	} else
		(void)lseek(file, pos, SEEK_SET);

	/*
	 * Read the block.
	 * If we read less than a full block, we just return the
	 * partial block and pick up the rest next time.
	 */
#if defined(sun)
	n = read(file, buf, BUFSIZ - bp->datasize);
#else
	n = iread(file, buf, BUFSIZ - bp->datasize);
	if (n == READ_INTR)
		return (EOI);
#endif
	if (n < 0) {
#if defined(sun)
		error(gettext("read error"));
		exit_status = 2;
#else
		error("read error");
#endif
		quit();
	}
	(void) memcpy(&bp->data[bp->datasize], &buf[0], n);

	bp->datasize += n;
	if (ispipe)
		last_piped_pos += n;
	

	/*
	 * Remember end-of-input, can't put a marker in our data
	 * because we are using all 8 bits...
	 */
	if (n == 0) {
		ch_fsize = pos;
		eoi_block = ch_block;
		eoi_offset = bp->datasize;
	}

found:
	if (buf_head != bp) {
		/*
		 * Move the buffer to the head of the buffer chain.
		 * This orders the buffer chain, most- to least-recently used.
		 */
		bp->next->prev = bp->prev;
		bp->prev->next = bp->next;

		bp->next = buf_head;
		bp->prev = END_OF_CHAIN;
		buf_head->prev = bp;
		buf_head = bp;
	}

	if (ch_offset >= bp->datasize)
		/*
		 * After all that, we still don't have enough data.
		 * Go back and try again.
		 */
		goto read_more;

	return(bp->data[ch_offset]);
}

/*
 * Determine if a specific block is currently in one of the buffers.
 */
static int
buffered(long block)
{
	register struct buf *bp;

	for (bp = buf_head; bp != END_OF_CHAIN; bp = bp->next)
		if (bp->block == block)
			return(1);
	return(0);
}

/*
 * Seek to a specified position in the file.
 * Return 0 if successful, non-zero if can't seek there.
 */
int
ch_seek(register off_t pos)
{
	long new_block;

	new_block = pos / BUFSIZ;
	if (!ispipe || pos == last_piped_pos || buffered(new_block)) {
		/*
		 * Set read pointer.
		 */
		ch_block = new_block;
		ch_offset = pos % BUFSIZ;
		return(0);
	}
	return(1);
}

/*
 * Seek to the end of the file.
 */
int
ch_end_seek(void)
{
	if (!ispipe)
		return(ch_seek(ch_length()));

	/*
	 * Do it the slow way: read till end of data.
	 */
	while (ch_forw_get() != EOI)
		if (sigs)
			return(1);
	return(0);
}

/*
 * Seek to the beginning of the file, or as close to it as we can get.
 * We may not be able to seek there if input is a pipe and the
 * beginning of the pipe is no longer buffered.
 */
int
ch_beg_seek(void)
{
	register struct buf *bp, *firstbp;

	/*
	 * Try a plain ch_seek first.
	 */
	if (ch_seek((off_t)0) == 0)
		return(0);

	/*
	 * Can't get to position 0.
	 * Look thru the buffers for the one closest to position 0.
	 */
	firstbp = bp = buf_head;
	if (bp == END_OF_CHAIN)
		return(1);
	while ((bp = bp->next) != END_OF_CHAIN)
		if (bp->block < firstbp->block)
			firstbp = bp;
	ch_block = firstbp->block;
	ch_offset = 0;
	return(0);
}

/*
 * Return the length of the file, if known.
 */
off_t
ch_length(void)
{
	if (ispipe)
		return(ch_fsize);
	return((off_t)(lseek(file, (off_t)0, SEEK_END)));
}

/*
 * Return the current position in the file.
 */
off_t
ch_tell(void)
{
	return((off_t) ch_block * BUFSIZ + ch_offset);
}

/*
 * Return the current position in the file.
 */
off_t
ch_tell_old(void)
{
	return((off_t) ch_block_old * BUFSIZ + ch_offset_old);
}

/*
 * Get the current char and post-increment the read pointer.
 */
int
ch_forw_get(void)
{
	register int c;

	c = ch_get();
	if (c != EOI && ++ch_offset >= BUFSIZ) {
		ch_offset = 0;
		++ch_block;
	}
	return(c);
}

/*
 * Get the current wchar_t and post-increment the read pointer.
 */
wchar_t
ch_forw_get_w(void)
{
	int	c;
	unsigned char 	multi[MB_LEN_MAX + 1];
	int	len;
	int	fix;
	wchar_t	wc;

	ch_offset_old = ch_offset;
	ch_block_old = ch_block;

	if ((c = ch_forw_get()) == EOI)
		return(EOI);

	if (isascii(c))
		return(c);

	multi[0] = (unsigned char)c;
	for (len = 1; len < (unsigned int)MB_CUR_MAX; len++) {
		if ((c = ch_forw_get()) == EOI)
			break;
		if (c == '\n')
			break;
		multi[len] = (unsigned char)c;
	}

	if ((fix = mbtowc(&wc, (char *)multi, len)) <= 0) {
		fix = 1;
		wc = multi[0];
	}
	ch_block = ch_block_old;
	if ((ch_offset = ch_offset_old + fix) >= BUFSIZ) {
		ch_offset = ch_offset_old + fix - BUFSIZ;
		ch_block++;
	}
	return(wc);
}

/*
 * Pre-decrement the read pointer and get the new current char.
 */
int
ch_back_get(void)
{
	if (--ch_offset < 0) {
		if (ch_block <= 0 || (ispipe && !buffered(ch_block-1))) {
			ch_offset = 0;
			return(EOI);
		}
		ch_offset = BUFSIZ - 1;
		ch_block--;
	}
	return(ch_get());
}

/*
 * Pre-decrement the read pointer and get the new current char.
 */
wchar_t
ch_back_get_w(off_t *fpos)
{

#define	ISSINGLE(c)	(((c) == ' ') || ((c) == '\n') || ((c) == '\t'))
#define TMPBUFSIZ	512

	off_t	new_pos;
	off_t	old_pos;
	off_t	pos;
	int	c;

	struct Tmp_buf {
		wchar_t	wc;
		off_t	pos;
	};
	struct Tmp_buf	*p, *q;
	
	static struct Tmp_buf	*tmp_buf = NULL;
	static size_t	tmpbufsize = TMPBUFSIZ;
	static off_t	idx = 0;
	
	new_pos = old_pos = ch_tell();
	if (*fpos == old_pos) {
		c = ch_back_get();	/* get one previous char */
		if (ISSINGLE(c)) {
			/* space or CR or tab */
			*fpos = ch_tell();
			return ((wchar_t) c);
		} else if (c == EOI) {
			/* the beginning of the buffer */
			*fpos = ch_tell();
			return (EOI);
		}

		/* search a fixed single byte character */
		for (;;) {
			c = ch_back_get();	/* get one previous char */
			if (ISSINGLE(c)) {
				/* space or CR or tab */
				*fpos = ch_tell();
				break;
			}
			if (c == EOI) {
				/* the beginning of the buffer */
				*fpos = (off_t)0;
				break;
			}
		}

		if (tmp_buf == (struct Tmp_buf *) NULL) {
			/* initialize tmp_buf */
			/* At this time, tmpbufsize is TMPBUFSIZ(512) */
			tmp_buf = (struct Tmp_buf *)malloc(
				sizeof(struct Tmp_buf) * tmpbufsize);
			if (tmp_buf == NULL) {
				error(gettext("cannot allocate memory"));
				exit_status = 2;
				quit();
			}
		}
		p = tmp_buf;	/* the beginning of the buffer */
		q = tmp_buf + (tmpbufsize - 1);
						/* the end of the buffer */

		idx = 0;
		for (; (pos = ch_tell()) < old_pos;) {
			new_pos = pos;
			if (p > q) {
				/* tmp_buf overflowed */
				tmpbufsize = tmpbufsize + TMPBUFSIZ;
				tmp_buf = (struct Tmp_buf *)realloc((void *)tmp_buf,
					(sizeof(struct Tmp_buf) * tmpbufsize));
				if (tmp_buf == NULL) {
					error(gettext("cannot allocate memory"));
					exit_status = 2;
					quit();
				}
				p = tmp_buf + idx;
				q = tmp_buf + (tmpbufsize - 1);
			}
			p->wc = ch_forw_get_w();
			p->pos = pos;
			p++;
			idx++;
		}
		p--;
		idx -= 2;
		ch_seek(new_pos);
		return (p->wc);
	} else {
		p = tmp_buf + idx;
		idx--;
		ch_seek(p->pos);
		return (p->wc);
	}
}

/*
 * Allocate buffers.
 * Caller wants us to have a total of at least want_nbufs buffers.
 * keep==1 means keep the data in the current buffers;
 * otherwise discard the old data.
 */
void
ch_init(int want_nbufs, int keep)
{
	register struct buf *bp;
	char message[80];

	cbufs = nbufs;
	if (nbufs < want_nbufs && ch_addbuf(want_nbufs - nbufs)) {
		/*
		 * Cannot allocate enough buffers.
		 * If we don't have ANY, then quit.
		 * Otherwise, just report the error and return.
		 */
#if defined(sun)
		(void)sprintf(message, gettext("cannot allocate %d buffers"),
		    want_nbufs - nbufs);
		exit_status = 2;
#else
		(void)sprintf(message, MSGSTR(NOBUFF, "cannot allocate %d buffers"),
		    want_nbufs - nbufs);
#endif
		error(message);
		if (nbufs == 0)
			quit();
		return;
	}

	if (keep)
		return;

	/*
	 * We don't want to keep the old data,
	 * so initialize all the buffers now.
	 */
	for (bp = buf_head;  bp != END_OF_CHAIN;  bp = bp->next)
		bp->block = (long)(-1);
	last_piped_pos = (off_t)0;
	ch_fsize = NULL_POSITION;
	eoi_block = -1;
	eoi_offset = -1;
	(void)ch_seek((off_t)0);
}

/*
 * Allocate some new buffers.
 * The buffers are added to the tail of the buffer chain.
 */
int
ch_addbuf(int nnew)
{
	register struct buf *bp;
	register struct buf *newbufs;

	/*
	 * We don't have enough buffers.  
	 * Allocate some new ones.
	 */
	newbufs = (struct buf *)calloc((u_int)nnew, sizeof(struct buf));
	if (newbufs == NULL)
		return(1);

	/*
	 * Initialize the new buffers and link them together.
	 * Link them all onto the tail of the buffer list.
	 */
	nbufs += nnew;
	cbufs = nbufs;
	for (bp = &newbufs[0];  bp < &newbufs[nnew];  bp++) {
		bp->next = bp + 1;
		bp->prev = bp - 1;
		bp->block = (long)(-1);
	}
	newbufs[nnew-1].next = END_OF_CHAIN;
	newbufs[0].prev = buf_tail;
	buf_tail->next = &newbufs[0];
	buf_tail = &newbufs[nnew-1];
	return(0);
}
