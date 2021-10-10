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
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 * Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Global include file for all sgs SPARC machine dependent macros, constants
 * and declarations.
 */

#ifndef	_MACHDEP_SPARC_H
#define	_MACHDEP_SPARC_H

#include <link.h>
#include <sys/machelf.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Elf header information.
 */
#define	M_MACH_32		EM_SPARC
#define	M_MACH_64		EM_SPARCV9

#ifdef _ELF64
#define	M_MACH			EM_SPARCV9
#define	M_CLASS			ELFCLASS64
#else
#define	M_MACH			EM_SPARC
#define	M_CLASS			ELFCLASS32
#endif
#define	M_MACHPLUS		EM_SPARC32PLUS
#define	M_DATA			ELFDATA2MSB
#define	M_FLAGSPLUS		EF_SPARC_32PLUS

/*
 * Page boundary Macros: truncate to previous page boundary and round to
 * next page boundary (refer to generic macros in ../sgs.h also).
 */
#define	M_PTRUNC(X)	((X) & ~(syspagsz - 1))
#define	M_PROUND(X)	(((X) + syspagsz - 1) & ~(syspagsz - 1))

/*
 * Segment boundary macros: truncate to previous segment boundary and round
 * to next page boundary.
 */
#ifndef	M_SEGSIZE
#define	M_SEGSIZE	ELF_SPARC_MAXPGSZ
#endif
#define	M_STRUNC(X)	((X) & ~(M_SEGSIZE - 1))
#define	M_SROUND(X)	(((X) + M_SEGSIZE - 1) & ~(M_SEGSIZE - 1))

/*
 * Relocation type macro.
 */
#define	M_RELOC		Rela

/*
 * TLS static segments must be rounded to the following requirements,
 * due to libthread stack allocation.
 */
#if	defined(_ELF64)
#define	M_TLSSTATALIGN	0x10
#else
#define	M_TLSSTATALIGN	0x08
#endif

/*
 * Instruction encodings.
 */
#define	M_SAVESP64	0x9de3bfc0	/* save %sp, -64, %sp */
#define	M_CALL		0x40000000
#define	M_JMPL		0x81c06000	/* jmpl %g1 + simm13, %g0 */
#define	M_SETHIG0	0x01000000	/* sethi %hi(val), %g0 */
#define	M_SETHIG1	0x03000000	/* sethi %hi(val), %g1 */
#define	M_STO7G1IM	0xde206000	/* st	 %o7,[%g1 + %lo(val)] */
#define	M_SUBFPSPG1	0x8227800e	/* sub	%fp,%sp,%g1 */
#define	M_NOP		0x01000000	/* sethi 0, %o0 (nop) */
#define	M_BA_A		0x30800000	/* ba,a */
#define	M_BA_A_PT	0x30480000	/* ba,a %icc, <dst> */
#define	M_MOVO7TOG1	0x8210000f	/* mov %o7, %g1 */
#define	M_MOVO7TOG5	0x8a10000f	/* mov %o7, %g5 */
#define	M_MOVI7TOG1	0x8210001f	/* mov %i7, %g1 */
#define	M_BA_A_XCC	0x30680000	/* ba,a %xcc */
#define	M_JMPL_G5G0	0x81c16000	/* jmpl %g5 + 0, %g0 */
#define	M_XNOR_G5G1	0x82396000	/* xnor	%g5, 0, %g1 */


#define	M_BIND_ADJ	4		/* adjustment for end of */
					/*	elf_rtbndr() address */

/* transition flags for got sizing */
#define	M_GOT_LARGE	(Sword)(-M_GOT_MAXSMALL - 1)
#define	M_GOT_SMALL	(Sword)(-M_GOT_MAXSMALL - 2)
#define	M_GOT_MIXED	(Sword)(-M_GOT_MAXSMALL - 3)

/*
 * Other machine dependent entities
 */
#ifdef _ELF64
#define	M_SEGM_ALIGN	ELF_SPARCV9_MAXPGSZ
/*
 * Put default 64-bit programs above 4 gigabytes to help insure correctness, so
 * that any 64-bit programs that truncate pointers will fault now instead of
 * corrupting itself and dying mysteriously.  64-bit programs can also be
 * restricted to a 32-bit address space (SF1_SUNW_ADDR32), and these programs
 * provide an alternative origin.
 */
#define	M_SEGM_ORIGIN	(Addr)0x100000000ULL	/* default 1st segment origin */
#define	M_SEGM_AORIGIN	(Addr)0x100000ULL	/* alternative 1st segment */
						/*    origin */
#else
#define	M_SEGM_ALIGN	ELF_SPARC_MAXPGSZ
#define	M_SEGM_ORIGIN	(Addr)0x10000		/* default 1st segment origin */
#define	M_SEGM_AORIGIN	M_SEGM_ORIGIN		/* alternative 1st segment */
						/*    origin */
#endif

/*
 * Make common relocation information transparent to the common code
 */
#define	M_REL_DT_TYPE	DT_RELA		/* .dynamic entry */
#define	M_REL_DT_SIZE	DT_RELASZ	/* .dynamic entry */
#define	M_REL_DT_ENT	DT_RELAENT	/* .dynamic entry */
#define	M_REL_DT_COUNT	DT_RELACOUNT	/* .dynamic entry */
#define	M_REL_SHT_TYPE	SHT_RELA	/* section header type */
#define	M_REL_ELF_TYPE	ELF_T_RELA	/* data buffer type */

/*
 * Make common relocation types transparent to the common code
 */
#define	M_R_NONE	R_SPARC_NONE
#define	M_R_GLOB_DAT	R_SPARC_GLOB_DAT
#define	M_R_COPY	R_SPARC_COPY
#define	M_R_RELATIVE	R_SPARC_RELATIVE
#define	M_R_JMP_SLOT	R_SPARC_JMP_SLOT
#define	M_R_REGISTER	R_SPARC_REGISTER
#define	M_R_FPTR	R_SPARC_NONE
#define	M_R_NUM		R_SPARC_NUM

#ifdef	_ELF64
#define	M_R_ARRAYADDR	R_SPARC_64
#define	M_R_DTPMOD	R_SPARC_TLS_DTPMOD64
#define	M_R_DTPOFF	R_SPARC_TLS_DTPOFF64
#define	M_R_TPOFF	R_SPARC_TLS_TPOFF64
#else	/* _ELF32 */
#define	M_R_ARRAYADDR	R_SPARC_32
#define	M_R_DTPMOD	R_SPARC_TLS_DTPMOD32
#define	M_R_DTPOFF	R_SPARC_TLS_DTPOFF32
#define	M_R_TPOFF	R_SPARC_TLS_TPOFF32
#endif	/* _ELF64 */


/*
 * Make register symbols transparent to common code
 */
#define	M_DT_REGISTER	DT_SPARC_REGISTER

/*
 * Make plt section information transparent to the common code.
 */
#define	M_PLT_SHF_FLAGS	(SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR)

/*
 * Make default data segment and stack flags transparent to the common code.
 */
#define	M_DATASEG_PERM	(PF_R | PF_W | PF_X)
#ifdef _ELF64
#define	M_STACK_PERM	(PF_R | PF_W)
#else
#define	M_STACK_PERM	(PF_R | PF_W | PF_X)
#endif

/*
 * Define a set of identifies for special sections.  These allow the sections
 * to be ordered within the output file image.  These values should be
 * maintained consistently, where appropriate, in each platform specific header
 * file.
 *
 *  -	null identifies that this section does not need to be added to the
 *	output image (ie. shared object sections or sections we're going to
 *	recreate (sym tables, string tables, relocations, etc.)).
 *
 *  -	any user defined section will be first in the associated segment.
 *
 *  -	interp and capabilities sections are next, as these are accessed
 *	immediately the first page of the image is mapped.
 *
 *  -	objects that do not provide an interp normally have a read-only
 *	.dynamic section that comes next (in this case, there is no need to
 *	update a DT_DEBUG entry at runtime).
 *
 *  -	the syminfo, hash, dynsym, dynstr and rel's are grouped together as
 *	these will all be accessed together by ld.so.1 to perform relocations.
 *
 *  -	the got, dynamic, and plt are grouped together as these may also be
 *	accessed first by ld.so.1 to perform relocations, fill in DT_DEBUG
 *	(executables only), and .plt[0].
 *
 *  -	unknown sections (stabs, comments, etc.) go at the end.
 *
 * Note that .tlsbss/.bss are given the largest identifiers.  This insures that
 * if any unknown sections become associated to the same segment as the .bss,
 * the .bss sections are always the last section in the segment.
 */
#define	M_ID_NULL	0x00
#define	M_ID_USER	0x01

#define	M_ID_INTERP	0x02			/* SHF_ALLOC */
#define	M_ID_CAP	0x03
#define	M_ID_CAPINFO	0x04
#define	M_ID_CAPCHAIN	0x05

#define	M_ID_DYNAMIC	0x06			/* if no .interp, then no */
						/*    DT_DEBUG is required */
#define	M_ID_UNWINDHDR	0x07
#define	M_ID_UNWIND	0x08

#define	M_ID_SYMINFO	0x09
#define	M_ID_HASH	0x0a
#define	M_ID_LDYNSYM	0x0b			/* always right before DYNSYM */
#define	M_ID_DYNSYM	0x0c
#define	M_ID_DYNSTR	0x0d
#define	M_ID_VERSION	0x0e
#define	M_ID_DYNSORT	0x0f
#define	M_ID_REL	0x10
#define	M_ID_ARRAY	0x11
#define	M_ID_TEXT	0x12			/* SHF_ALLOC + SHF_EXECINSTR */
#define	M_ID_DATA	0x20

/*	M_ID_USER	0x01			dual entry - listed above */
#define	M_ID_GOTDATA	0x02			/* SHF_ALLOC + SHF_WRITE */
#define	M_ID_GOT	0x03
#define	M_ID_PLT	0x04
/*	M_ID_DYNAMIC	0x06			dual entry - listed above */
/*	M_ID_UNWIND	0x08			dual entry - listed above */

#define	M_ID_UNKNOWN	0xfc			/* just before TLS */

#define	M_ID_TLS	0xfd			/* just before bss */
#define	M_ID_TLSBSS	0xfe
#define	M_ID_BSS	0xff

#define	M_ID_SYMTAB_NDX	0x02			/* ! SHF_ALLOC */
#define	M_ID_SYMTAB	0x03
#define	M_ID_STRTAB	0x04
#define	M_ID_DYNSYM_NDX	0x05
#define	M_ID_NOTE	0x06


#ifdef	__cplusplus
}
#endif

#endif /* _MACHDEP_SPARC_H */
