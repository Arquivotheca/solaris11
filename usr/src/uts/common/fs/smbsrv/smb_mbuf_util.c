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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 */

#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_kstat.h>

static kmem_cache_t	*smb_mbc_cache = NULL;

int
smb_mbc_init(void)
{
	if (smb_mbc_cache == NULL) {
		smb_mbc_cache = kmem_cache_create(SMBSRV_KSTAT_MBC_CACHE,
		    sizeof (mbuf_chain_t), 8, NULL, NULL, NULL, NULL, NULL, 0);
	}
	return (0);
}

void
smb_mbc_fini(void)
{
	if (smb_mbc_cache != NULL) {
		kmem_cache_destroy(smb_mbc_cache);
		smb_mbc_cache = NULL;
	}
}

mbuf_chain_t *
smb_mbc_alloc(uint32_t max_bytes)
{
	mbuf_chain_t	*mbc;
	mbuf_t		*m;

	mbc = kmem_cache_alloc(smb_mbc_cache, KM_SLEEP);
	bzero(mbc, sizeof (*mbc));
	mbc->mbc_magic = SMB_MBC_MAGIC;

	if (max_bytes != 0) {
		MGET(m, M_WAIT, MT_DATA);
		m->m_len = 0;
		mbc->chain = m;
		if (max_bytes > MINCLSIZE)
			MCLGET(m, M_WAIT);
	}
	mbc->max_bytes = max_bytes;
	return (mbc);
}

void
smb_mbc_free(mbuf_chain_t *mbc)
{
	SMB_MBC_VALID(mbc);

	m_freem(mbc->chain);
	mbc->chain = NULL;
	mbc->mbc_magic = 0;
	kmem_cache_free(smb_mbc_cache, mbc);
}

/*
 * smb_mbuf_get
 *
 * Allocate mbufs to hold the amount of data specified.
 * A pointer to the head of the mbuf list is returned.
 */
struct mbuf *
smb_mbuf_get(uchar_t *buf, int nbytes)
{
	struct mbuf *mhead = 0;
	struct mbuf *m = 0;
	int count;
	int offset = 0;

	while (nbytes) {
		count = (nbytes > MCLBYTES) ? MCLBYTES : nbytes;
		nbytes -= count;

		if (mhead == 0) {
			MGET(mhead, M_WAIT, MT_DATA);
			m = mhead;
		} else {
			MGET(m->m_next, M_WAIT, MT_DATA);
			m = m->m_next;
		}

		if (count > MLEN) {
			MCLGET(m, M_WAIT);
		}

		m->m_len = count;
		bcopy(buf + offset, m->m_data, count);
		offset += count;
	}
	return (mhead);
}

/*
 * Allocate enough mbufs to accommodate the residual count in a uio.
 */
struct mbuf *
smb_mbuf_allocate(struct uio *uio)
{
	struct iovec *iovp;
	struct mbuf	*mhead = 0;
	struct mbuf	*m = 0;
	int	count, iovs, resid;

	iovp = uio->uio_iov;
	iovs = uio->uio_iovcnt;
	resid = uio->uio_resid;

	while ((resid > 0) && (iovs > 0)) {
		count = (resid > MCLBYTES) ? MCLBYTES : resid;
		resid -= count;

		if (mhead == 0) {
			MGET(mhead, M_WAIT, MT_DATA);
			m = mhead;
		} else {
			MGET(m->m_next, M_WAIT, MT_DATA);
			m = m->m_next;
		}

		if (count > MLEN) {
			MCLGET(m, M_WAIT);
		}

		iovp->iov_base = m->m_data;
		iovp->iov_len = m->m_len = count;
		iovs--;
		iovp++;
	}

	uio->uio_iovcnt -= iovs;
	return (mhead);
}

/*
 * Trim an mbuf chain to nbytes.
 */
void
smb_mbuf_trim(struct mbuf *mhead, int nbytes)
{
	struct mbuf	*m = mhead;

	while (m != 0) {
		if (nbytes <= m->m_len) {
			m->m_len = nbytes;
			if (m->m_next != 0) {
				m_freem(m->m_next);
				m->m_next = 0;
			}
			break;
		}
		nbytes -= m->m_len;
		m = m->m_next;
	}
}

int
MBC_LENGTH(struct mbuf_chain *MBC)
{
	struct mbuf	*m = (MBC)->chain;
	int		used = 0;

	while (m != 0) {
		used += m->m_len;
		m = m->m_next;
	}
	return (used);
}

int
MBC_MAXBYTES(struct mbuf_chain *MBC)
{
	return (MBC->max_bytes);
}

void
MBC_SETUP(struct mbuf_chain *MBC, uint32_t max_bytes)
{
	bzero((MBC), sizeof (struct mbuf_chain));
	(MBC)->max_bytes = max_bytes ? max_bytes : smb_maxbufsize;
}

void
MBC_INIT(struct mbuf_chain *MBC, uint32_t max_bytes)
{
	struct mbuf *m;

	bzero((MBC), sizeof (struct mbuf_chain));

	if (max_bytes != 0) {
		MGET(m, M_WAIT, MT_DATA);
		m->m_len = 0;
		(MBC)->chain = m;
		if (max_bytes > MINCLSIZE)
			MCLGET(m, M_WAIT);
	}
	(MBC)->max_bytes = max_bytes;
}

void
MBC_FLUSH(struct mbuf_chain *MBC)
{
	extern void	m_freem(struct mbuf *);
	struct mbuf	*m;

	while ((m = (MBC)->chain) != 0) {
		(MBC)->chain = m->m_nextpkt;
		m->m_nextpkt = 0;
		m_freem(m);
	}
	MBC_SETUP(MBC, (MBC)->max_bytes);
}

void
MBC_ATTACH_MBUF(struct mbuf_chain *MBC, struct mbuf *MBUF)
{
	if (MBC->chain != 0)
		MBC_FLUSH(MBC);

	(MBC)->chain_offset = 0;
	(MBC)->chain = (MBUF);
}

void
MBC_APPEND_MBUF(struct mbuf_chain *MBC, struct mbuf *MBUF)
{
	struct mbuf	*m;

	if ((MBC)->chain == 0) {
		(MBC)->chain = (MBUF);
	} else {
		m = (MBC)->chain;
		while (m->m_next != 0)
			m = m->m_next;
		m->m_next = (MBUF);
	}
}


void
MBC_ATTACH_BUF(struct mbuf_chain *MBC, unsigned char *BUF, int LEN)
{
	MGET((MBC)->chain, M_WAIT, MT_DATA);
	(MBC)->chain_offset = 0;
	(MBC)->chain->m_flags |= M_EXT;
	(MBC)->chain->m_data = (caddr_t)(BUF);
	(MBC)->chain->m_ext.ext_buf = (caddr_t)(BUF);
	(MBC)->chain->m_len = (LEN);
	(MBC)->chain->m_ext.ext_size = (LEN);
	(MBC)->chain->m_ext.ext_ref = smb_noop;
	(MBC)->max_bytes = (LEN);
}

int
mbc_attach_buffer(mbuf_chain_t *mbc, uint8_t *buffer, size_t len,
    void (*free_fcnt)())
{
	mbuf_t	*m;
	mbuf_t	*tail;

	if ((MBC_LENGTH(mbc) + len) <= mbc->max_bytes) {
		MGET(m, M_WAIT, MT_DATA);
		m->m_flags |= M_EXT;
		m->m_data = (caddr_t)buffer;
		m->m_ext.ext_buf = (caddr_t)buffer;
		m->m_len = len;
		m->m_ext.ext_size = len;
		m->m_ext.ext_ref = free_fcnt;

		if (mbc->chain == NULL) {
			mbc->chain = m;
		} else {
			tail = mbc->chain;
			while (tail->m_next != NULL)
				tail = tail->m_next;

			tail->m_next = m;
		}

		return (0);
	}

	return (ENOSPC);
}


int
MBC_SHADOW_CHAIN(struct mbuf_chain *SUBMBC, struct mbuf_chain *MBC,
    int OFF, int LEN)
{
	if (((OFF) + (LEN)) > (MBC)->max_bytes)
		return (EMSGSIZE);

	*(SUBMBC) = *(MBC);
	(SUBMBC)->chain_offset = (OFF);
	(SUBMBC)->max_bytes = (OFF) + (LEN);
	(SUBMBC)->shadow_of = (MBC);
	return (0);
}

int
mbc_moveout(mbuf_chain_t *mbc, caddr_t buf, int buflen, int *tlen)
{
	int	rc = 0;
	int	len = 0;

	if ((mbc != NULL) && (mbc->chain != NULL)) {
		mbuf_t	*m;

		m = mbc->chain;
		while (m) {
			if ((len + m->m_len) <= buflen) {
				bcopy(m->m_data, buf, m->m_len);
				buf += m->m_len;
				len += m->m_len;
				m = m->m_next;
				continue;
			}
			rc = EMSGSIZE;
			break;
		}
		m_freem(mbc->chain);
		mbc->chain = NULL;
		mbc->flags = 0;
	}
	*tlen = len;
	return (rc);
}

size_t
mbc_space(mbuf_chain_t *mbc)
{
	return ((size_t)(mbc->max_bytes - mbc->chain_offset));
}

/*
 * mbc_len
 *
 * Returns the numbers of bytes of data still available.
 */
size_t
mbc_len(mbuf_chain_t *mbc)
{
	mbuf_t	*m;
	size_t	len;

	len = 0;
	m = mbc->chain;
	while (m != NULL) {
		len += m->m_len;
		m = m->m_next;
	}

	if (mbc->chain_offset <= len)
		len -= mbc->chain_offset;
	else
		len = 0;

	return (len);
}

int
mbc_duplicate(mbuf_chain_t *mbc, mbuf_chain_t *dup)
{
	*dup = *mbc;
	dup->shadow_of = mbc;
	return (0);
}

int
mbc_sorecv(mbuf_chain_t *mbc, ksocket_t so)
{
	uint8_t	*pbuf;
	size_t	len;
	size_t	recvd;
	int	rc;

	ASSERT(so != NULL);

	len = mbc_space(mbc);
	ASSERT(len != 0);

	pbuf = smb_mem_alloc(len);
	if (pbuf == NULL)
		return (ENOMEM);

	rc = mbc_attach_buffer(mbc, pbuf, len, smb_mem_free);
	if (rc == 0) {
		rc = ksocket_recv(so, pbuf, len, MSG_WAITALL, &recvd, CRED());
		if ((rc == 0) && (len != recvd))
			rc = -1;
	}

	return (rc);
}

void
mbc_set_offset(mbuf_chain_t *mbc, int32_t offset)
{
	mbc->chain_offset = offset;
}

/*
 * Free a single mbuf structure.  Calls m->m_ext.ext_ref() to free any
 * associated external buffers if present (indicated by m->m_flags & M_EXT)
 */
struct mbuf *
m_free(struct mbuf *m)
{
	struct mbuf *n;

	MFREE(m, n);
	return (n);
}

/*
 * Free a list of mbufs.  Each mbuf in the list is freed similarly to m_free.
 */
void
m_freem(struct mbuf *m)
{
	struct mbuf *n;

	if (m == NULL)
		return;
	/*
	 * Lint doesn't like the m = n assignment at the close of the loop
	 * but it is correct.  MFREE assigns n = (m)->m_next so the loop
	 * is effectively assigning m = (m)->m_next then exiting when
	 * m == NULL
	 */
	do {
		MFREE(m, n);
	} while ((m = n) != 0);
}

/*
 * Mbuffer utility routines.
 */

void /*ARGSUSED*/
mclref(caddr_t p, int size, int adj) /* size, adj are unused */
{
	MEM_FREE("mbuf", p);
}
