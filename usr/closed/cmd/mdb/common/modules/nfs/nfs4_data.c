/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.
 * All rights reserved.  Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"


#include "nfs_mdb.h"
#include "nfs4_mdb.h"


void
nfs_mutex_print(const kmutex_t *lock)
{
	uintptr_t p = *(uintptr_t *)lock;

#ifndef _LP64
	uintptr_t q = *(1 + (uintptr_t *)lock);
#endif

	if (p == 0) {
		mdb_printf("mutex not held");
		return;
	}
	if (!(p & 0x6)) {
		/* adaptive */
		mdb_printf("adaptive - owner %p %s ",
				p & ~7L,
				p & 1 ? "has waiters." : "");
		return;
	}
	if ((p & 0xff) == 0xff) {
		/* spin mutex */
#ifdef _LP64
		mdb_printf("spin - lock(%x)/oldspl(%x)/minspl(%x)",
				(p >> 8) & 0xff,
				p >> 48,
				(p >> 32) & 0xffff);
#else
		mdb_printf("spin - lock(%x)/oldspl(%x)/minspl(%x)",
				(p >> 8) & 0xff,
				q >> 16,
				q & 0xffff);
#endif
		return;
	}
	mdb_printf("mutex dead.");
}


void
nfs_rwlock_print(const krwlock_t *lock)
{
	uintptr_t p = *(uintptr_t *)lock;
	mdb_printf("owner %p %s %s %s",
			p & ~7L,
			p & 1 ? "has_waiters" : "",
			p & 2 ? "write_wanted" : "",
			p & 4 ? "write_locked" : "");
}

void
nfs_bprint(uint_t len, uchar_t *addr)
{
	int i;

	for (i = 0; i < len; i++) {
		mdb_printf("%02x", addr[i]);
	}
	mdb_printf(" ");
}

int
nfs4_changeid4_print(changeid4 ci)
{
	if (nfs4_mdb_opt & NFS_MDB_OPT_SOLARIS_SRV)
		mdb_printf("%Y:%d ns",
			(time_t)*(uint32_t *)&ci, *(1 + (uint32_t *)&ci));

	mdb_printf(" %#llx", ci);
	return (0);
}

/*ARGSUSED*/
int
nfs4_print_cb_client4(cb_client4 cc, int notused)
{
	char buf[32];

	if (mdb_readstr(buf, sizeof (buf),
		(uintptr_t)cc.cb_location.r_netid) == -1) {
		mdb_warn("couldn't readstr r_netid at %p\n",
				cc.cb_location.r_netid);
		buf[0] = 0;
	}

	mdb_printf("(prog: %x over %s/", cc.cb_program, buf);

	if (mdb_readstr(buf, sizeof (buf),
		(uintptr_t)cc.cb_location.r_addr) == -1) {
		mdb_warn("couldn't readstr r_netid at %p\n",
				cc.cb_location.r_addr);
		buf[0] = 0;
	}

	mdb_printf("%s)", buf);

	return (0);
}


/*ARGSUSED*/
int
nfs4_print_verifier4(verifier4 vf, int notused)
{
	/*
	 * Note here that since verifier4 is defined as char [8]
	 * instead of uint64_t, we must not say &vf
	 */
	mdb_printf("%#llx", *(uint64_t *)(uintptr_t)vf);
	return (0);
}


int
nfs4_print_stateid4(stateid4 st4, int solarisserver)
{
	const char *state_type[3] = { "OPENID", "LOCKID", "DELEGID" };
	if (solarisserver) {
		stateid_t st = *(stateid_t *)&st4;
		mdb_printf("(seq: %d, boot: %Y, type: %s, ident: %#x, pid: %d)",
				st.bits.chgseq, st.bits.boottime,
		    st.bits.type < 3 ? state_type[st.bits.type] : "undefined",
				st.bits.ident, st.bits.pid);
	}
	else
		mdb_printf("%#llx%llx",
				*(uint64_t *)(void *)&st4,
				*(1 + (uint64_t *)(void *)&st4));
	return (0);
}


int
nfs_read_print_hex(uintptr_t p, int n)
{
	int status;
	char *s = mdb_alloc(n, UM_SLEEP);

	if (mdb_vread(s, n, p) == -1) {
		mdb_warn("couldn't read %d bytes at %p\n", n, p);
		return (-1);
	}
	status = nfs_print_hex(s, n);
	mdb_free(s, n);
	return (status);
}

int
nfs_print_hex(char *s, int n)
{
	int i, j;
	char *buf;
	char *hexdigit = "0123456789abcdef";

	buf = mdb_alloc(1+2*n, UM_SLEEP);
	for (i = j = 0; i < n; i++) {
		buf[j++] = hexdigit[(s[i]&0xf0)>>4];
		buf[j++] = hexdigit[(s[i]&0x0f)];
	}
	buf[j] = 0;
	mdb_printf("%s", buf);
	mdb_free(buf, 1+2*n);
	return (0);
}
