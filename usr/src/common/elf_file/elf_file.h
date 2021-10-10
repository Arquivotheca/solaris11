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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ELF_FILE_H
#define	_ELF_FILE_H

/*
 * The code used by the 'file' command to identify linking-related files
 * is maintained under sgs, and is made available to 'file' as a private
 * interface in libelf named _elf_file(). That code handles:
 *
 * -	ELF objects
 * -	core files
 * -	archives
 * -	crle config files
 */

/*
 * return status for _elf_file() and supporting code internal to libelf.
 *
 * ELF_FILE_BAD is an internal state, and is never returned by _elf_file().
 * _elf_file() converts it to ELF_FILE_SUCCESS before passing it to the caller.
 */
typedef enum {
	ELF_FILE_SUCCESS =	0,	/* File successfully identified */
	ELF_FILE_BAD =		1,	/* File identified, but corrupt */
					/*	(internal --- not returned) */
	ELF_FILE_FATAL =	2,	/* File identified, but hit a fatal */
					/*	resource error. Must exit */
	ELF_FILE_NOTELF =	3	/* Not a file type we identify */
} elf_file_t;

/*
 * Type of ar argument to _elf_file(). Determines how archives are
 * described:
 *	Basic	Identify archive and symbol table type
 *	Summary	To basic, add a summary of archive contents
 *	Detail	To basic, add one line per archive member describing contents
 */
typedef enum {
	ELF_FILE_AR_BASIC =	0,
	ELF_FILE_AR_SUMMARY =	1,
	ELF_FILE_AR_DETAIL =	2
} elf_file_ar_t;

/*
 * If the caller to _elf_file() wants to supply an fbuf buffer, the
 * buffer must be at least ELF_FILE_FBUF_MIN bytes in size in order to
 * cover all of the initial byte sequences that _elf_file() will examine.
 *
 * -	ELF ident array (EI_NIDENT == 16 bytes)
 * -	AR magic string (SARMAG == 8 bytes)
 * -	Runtime Linker Configuration (sizeof(Rtc_id) == 16 bytes)
 *
 * A shorter buffer is ignored.
 */
#define	ELF_FILE_FBUF_MIN	16

elf_file_t	_elf_file(const char *, const char *, int, elf_file_ar_t,
		    int, const char *, size_t);

#endif /* _ELF_FILE_H */
