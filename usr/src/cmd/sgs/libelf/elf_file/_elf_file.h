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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef __ELF_FILE_H
#define	__ELF_FILE_H

#include <_machelf.h>
#include <conv.h>

/*
 * Public interfaces for this code, exported for use by the 'file' utility.
 */
#include <elf_file.h>

/*
 * Data returned by elf_file_read() for use by _elf_file()
 */
#define	BUFSZ 	CONV_CAP_VAL_HW1_BUFSIZE + 1 + \
		CONV_CAP_VAL_HW2_BUFSIZE + 1 + \
		CONV_CAP_VAL_SF1_BUFSIZE
typedef struct ELF_Info {
	boolean_t	ei_dynamic;		/* dynamically linked? */
	boolean_t	ei_stub;		/* stub object? */
	uint_t		ei_core_type;		/* core? what type of core? */
	uint_t		ei_stripped;		/* symtab, debug info */
	uint_t		ei_flags;		/* e_flags */
	uint_t		ei_machine;		/* e_machine */
	uint_t		ei_type;		/* e_type */
	uchar_t		ei_class;		/* e_ident[EI_CLASS] */
	uchar_t		ei_data;		/* e_ident[EI_DATA] */
	uchar_t		ei_version;		/* e_ident[EI_VERSION] */
	char		ei_fname[PRFNSZ];	/* if core, name of process */
	char		ei_cap_str[BUFSZ];	/* capabilities */
} ELF_Info;

/* values for ELF_Info.stripped */
#define	E_DBGINF	0x01
#define	E_SYMTAB	0x02
#define	E_NOSTRIP	0x03

/* values for ELF_Info.core_type; */
#define	EC_NOTCORE	0x0
#define	EC_OLDCORE	0x1
#define	EC_NEWCORE	0x2

/*
 * libelf is not large file capable. To handle large files from 32-bit
 * builds, we define our own 64-bit capable types.
 */
typedef off64_t		ELF_FILE_OFF_T;
typedef uint64_t	ELF_FILE_SIZE_T;


#if defined(_ELF64)

#define	elf_file_read	elf_file_read64

#else

#define	elf_file_read	elf_file_read32

#endif

extern Boolean	elf_file_debug_section(const char *);
elf_file_t	elf_file_read32(const char *, const char *, int,
		    ELF_FILE_OFF_T, ELF_Info *);
elf_file_t	elf_file_read64(const char *, const char *, int,
		    ELF_FILE_OFF_T, ELF_Info *);

#endif /* __ELF_FILE_H */
