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

#ifndef	_SYS_CTYPE_H
#define	_SYS_CTYPE_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	ISDIGIT(_c) \
	((_c) >= '0' && (_c) <= '9')

#define	ISXDIGIT(_c) \
	(ISDIGIT(_c) || \
	((_c) >= 'a' && (_c) <= 'f') || \
	((_c) >= 'A' && (_c) <= 'F'))

#define	ISLOWER(_c) \
	((_c) >= 'a' && (_c) <= 'z')

#define	ISUPPER(_c) \
	((_c) >= 'A' && (_c) <= 'Z')

#define	ISALPHA(_c) \
	(ISUPPER(_c) || \
	ISLOWER(_c))

#define	ISALNUM(_c) \
	(ISALPHA(_c) || \
	ISDIGIT(_c))

#define	ISSPACE(_c) \
	((_c) == ' ' || \
	(_c) == '\t' || \
	(_c) == '\r' || \
	(_c) == '\n' || \
	(_c) == '\f')

#define	isdigit(c) (ISDIGIT(c))
#define	isxdigit(c) (ISXDIGIT(c))
#define	islower(c) (ISLOWER(c))
#define	isupper(c) (ISUPPER(c))
#define	isalpha(c) (ISALPHA(c))
#define	isalnum(c) (ISALNUM(c))
#define	isspace(c) (ISSPACE(c))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CTYPE_H */
