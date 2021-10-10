/*
 * Copyright (c) 2002 Damien Miller.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "includes.h"

RCSID("$Id: bsd-getpeereid.c,v 1.1 2002/09/12 00:33:02 djm Exp $");

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if !defined(HAVE_GETPEEREID)

#if defined(SO_PEERCRED)
int
getpeereid(int s, uid_t *euid, gid_t *gid)
{
	struct ucred cred;
	size_t len = sizeof(cred);

	if (getsockopt(s, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0)
		return (-1);
	*euid = cred.uid;
	*gid = cred.gid;

	return (0);
}
#elif defined(HAVE_GETPEERUCRED)
int
getpeereid(int s, uid_t *euid, gid_t *gid)
{
	ucred_t *ucred = NULL;

	if (getpeerucred(s, &ucred) == -1)
		return (-1);
	if ((*euid = ucred_geteuid(ucred)) == (uid_t)-1)
		return (-1);
	if ((*gid = ucred_getrgid(ucred)) == (gid_t)-1)
		return (-1);

	ucred_free(ucred);

	return (0);
}
#else
int
getpeereid(int s, uid_t *euid, gid_t *gid)
{
	*euid = geteuid();
	*gid = getgid();

	return (0);
}
#endif /* defined(SO_PEERCRED) */

#endif /* !defined(HAVE_GETPEEREID) */
