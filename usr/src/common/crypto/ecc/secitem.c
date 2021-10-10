/* BEGIN CSTYLED */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Netscape security libraries.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1994-2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * Sun elects to use this software under the MPL license.
 */

/*
 * Support routines for SECItem data structure.
 *
 * $Id: secitem.c,v 1.14 2006/05/22 22:24:34 wtchang%redhat.com Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#ifdef _KERNEL
#include <sys/kmem.h>
#else
#include <string.h>
#include <strings.h>
#include <assert.h>
#endif
#include "ec.h"
#include "ecl-curve.h"
#include "ecc_impl.h"

void SECITEM_FreeItem(SECItem *, PRBool);

SECItem *
SECITEM_AllocItem(PRArenaPool *arena, SECItem *item, unsigned int len,
    int kmflag)
{
    SECItem *result = NULL;
    void *mark = NULL;

    if (arena != NULL) {
	mark = PORT_ArenaMark(arena);
    }

    if (item == NULL) {
	if (arena != NULL) {
	    result = PORT_ArenaZAlloc(arena, sizeof(SECItem), kmflag);
	} else {
	    result = PORT_ZAlloc(sizeof(SECItem), kmflag);
	}
	if (result == NULL) {
	    goto loser;
	}
    } else {
	PORT_Assert(item->data == NULL);
	result = item;
    }

    result->len = len;
    if (len) {
	if (arena != NULL) {
	    result->data = PORT_ArenaAlloc(arena, len, kmflag);
	} else {
	    result->data = PORT_Alloc(len, kmflag);
	}
	if (result->data == NULL) {
	    goto loser;
	}
    } else {
	result->data = NULL;
    }

    if (mark) {
	PORT_ArenaUnmark(arena, mark);
    }
    return(result);

loser:
    if ( arena != NULL ) {
	if (mark) {
	    PORT_ArenaRelease(arena, mark);
	}
    }
    SECITEM_FreeItem(result, (item == NULL) ? PR_TRUE : PR_FALSE);
    /*
     * If item is not NULL, the above also sets item->data and
     * item->len to 0.
     */
    return(NULL);
}

SECStatus
SECITEM_CopyItem(PRArenaPool *arena, SECItem *to, const SECItem *from,
   int kmflag)
{
    to->type = from->type;
    if (from->data && from->len) {
	if ( arena ) {
	    to->data = (unsigned char*) PORT_ArenaAlloc(arena, from->len,
		kmflag);
	} else {
	    to->data = (unsigned char*) PORT_Alloc(from->len, kmflag);
	}
	
	if (!to->data) {
	    return SECFailure;
	}
	PORT_Memcpy(to->data, from->data, from->len);
	to->len = from->len;
    } else {
	to->data = 0;
	to->len = 0;
    }
    return SECSuccess;
}

void
SECITEM_FreeItem(SECItem *zap, PRBool freeit)
{
    if (zap) {
#ifdef _KERNEL
	kmem_free(zap->data, zap->len);
#else
	free(zap->data);
#endif
	zap->data = 0;
	zap->len = 0;
	if (freeit) {
#ifdef _KERNEL
	    kmem_free(zap, sizeof (SECItem));
#else
	    free(zap);
#endif
	}
    }
}
/* END CSTYLED */

void
SECITEM_ZfreeItem(SECItem *zap, PRBool freeit)
{
	if (zap) {
		PORT_ZFree(zap->data, zap->len);
		zap->data = 0;
		zap->len = 0;
		if (freeit) {
			PORT_ZFree(zap, sizeof (SECItem));
		}
	}
}
