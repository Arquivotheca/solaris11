/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LOCALEDEF_SEMSTACK_H_
#define	_LOCALEDEF_SEMSTACK_H_

#pragma ident	"%Z%%M%	%I%	%E% SMI"

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
 * OSF/1 1.2
 */
/* @(#)$RCSfile: semstack.h,v $ $Revision: 1.3.2.5 $ */
/* (OSF) $Date: 1992/03/05 17:55:14 $ */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.3  com/cmd/nls/semstack.h, cmdnls, bos320 6/20/91 01:01:06
 */

#include "symtab.h"

typedef struct {
	uint64_t min;
	uint64_t max;
} range_t;


/* valid types for item_type */
typedef enum {
	SK_NONE,
	SK_UINT64,
	SK_INT,
	SK_STR,
	SK_RNG,
	SK_CHR,
	SK_SUBS,
	SK_SYM,
	SK_UNDEF
} item_type_t;

typedef struct {
	item_type_t	type;

	union {			/* type =  */
		uint64_t	uint64_no; /* SK_UINT64 */
		int	int_no;		/*   SK_INT */
		char	*str;	/*   SK_STR, SK_UNDEF */
		range_t	*range;	/*   SK_RNG */
		chr_sym_t	*chr;	/*   SK_CHR */
		_LC_subs_t	*subs;	/*   SK_SUBS */
		symbol_t	*sym;	/*   SK_SYM */
	} value;

} item_t;

/* semstack errors */
#define	SK_OK	0
#define	SK_OVERFLOW 1

#endif /* _LOCALEDEF_SEMSTACK_H_ */
