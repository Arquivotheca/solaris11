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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <ar.h>
#include <stdlib.h>
#include "libelf.h"
#include "decl.h"
#include "member.h"

int
elf_end(Elf * elf)
{
	Elf_Scn *	s;
	Dnode *	d;
	Elf_Void *		trail = 0;
	int			rc;

	if (elf == 0)
		return (0);

	ELFWLOCK(elf)
	if (--elf->ed_activ != 0) {
		rc = elf->ed_activ;
		ELFUNLOCK(elf)
		return (rc);
	}

#ifndef __lock_lint
	while (elf->ed_activ == 0) {
		for (s = elf->ed_hdscn; s != 0; s = s->s_next) {
			if (s->s_myflags & SF_ALLOC) {
				if (trail != 0)
					free(trail);
				trail = (Elf_Void *)s;
			}

			if ((s->s_myflags & SF_READY) == 0)
				continue;
			for (d = s->s_hdnode; d != 0; ) {
				register Dnode	*t;

				if (d->db_buf != 0)
					free(d->db_buf);
				if ((t = d->db_raw) != 0) {
					if (t->db_buf != 0)
						free(t->db_buf);
					if (t->db_myflags & DBF_ALLOC)
						free(t);
				}
				t = d->db_next;
				if (d->db_myflags & DBF_ALLOC)
					free(d);
				d = t;
			}
		}
		if (trail != 0) {
			free(trail);
			trail = 0;
		}

		{
			register Memlist	*l;
			register Memident	*i;

			for (l = elf->ed_memlist; l; l = (Memlist *)trail) {
				trail = (Elf_Void *)l->m_next;
				for (i = (Memident *)(l + 1); i < l->m_free;
				    i++)
					free(i->m_member);
				free(l);
			}
		}
		if (elf->ed_myflags & EDF_EHALLOC)
			free(elf->ed_ehdr);
		if (elf->ed_myflags & EDF_PHALLOC)
			free(elf->ed_phdr);
		if (elf->ed_myflags & EDF_SHALLOC)
			free(elf->ed_shdr);
		if (elf->ed_myflags & EDF_RAWALLOC)
			free(elf->ed_raw);
		if (elf->ed_myflags & EDF_ASALLOC)
			free(elf->ed_arsym);
		if (elf->ed_myflags & EDF_ASTRALLOC)
			free(elf->ed_arstr);

		/*
		 * Don't release the image until the last reference dies.
		 * If the image was introduced via elf_memory() then
		 * we don't release it at all, it's not ours to release.
		 */

		if (elf->ed_parent == 0) {
			if (elf->ed_vm != 0)
				free(elf->ed_vm);
			else if ((elf->ed_myflags & EDF_MEMORY) == 0)
				_elf_unmap(elf->ed_image, elf->ed_imagesz);
		}
		trail = (Elf_Void *)elf;
		elf = elf->ed_parent;
		ELFUNLOCK(trail)
		free(trail);
		if (elf == 0)
			break;
		/*
		 * If parent is inactive we close
		 * it too, so we need to lock it too.
		 */
		ELFWLOCK(elf)
		--elf->ed_activ;
	}

	if (elf) {
		ELFUNLOCK(elf)
	}
#else
	/*
	 * This sill stuff is here to make warlock happy
	 * durring it's lock checking.  The problem is that it
	 * just can't track the multiple dynamic paths through
	 * the above loop so we just give it a simple one it can
	 * look at.
	 */
	_elf_unmap(elf->ed_image, elf->ed_imagesz);
	ELFUNLOCK(elf)
#endif

	return (0);
}
