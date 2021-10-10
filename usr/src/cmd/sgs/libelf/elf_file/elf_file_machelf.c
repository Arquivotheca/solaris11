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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * ELF files can exceed 2GB in size. A standard 32-bit program
 * like 'file' cannot read past 2GB, and will be unable to see
 * the ELF section headers that typically are at the end of the
 * object. The simplest solution to this problem would be to make
 * the 'file' command a 64-bit application. However, as a matter of
 * policy, we do not want to require this. A simple command like
 * 'file' should not carry such a requirement, especially as we
 * support 32-bit only hardware.
 *
 * An alternative solution is to build this code as 32-bit
 * large file aware. The usual way to do this is to define a pair
 * of preprocessor definitions:
 *
 *	_LARGEFILE64_SOURCE
 *		Map standard I/O routines to their largefile aware versions.
 *
 *	_FILE_OFFSET_BITS=64
 *		Map off_t to off64_t
 *
 * The problem with this solution is that libelf is not large file capable,
 * and the libelf header file will prevent compilation if
 * _FILE_OFFSET_BITS is set to 64.
 *
 * So, the solution used in this code is to define _LARGEFILE64_SOURCE
 * to get access to the 64-bit APIs, not to define _FILE_OFFSET_BITS, and to
 * use our own types in place of off_t, and size_t. We read all the file
 * data directly using pread64(), and avoid the use of libelf for anything
 * other than the xlate functionality.
 */
#define	_LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <_libelf.h>
#include <errno.h>
#include <procfs.h>
#include <elfcap.h>
#include <sgs.h>
#include "_elf_file.h"
#include "msg.h"

/*
 * Internal state for a given object. This is information we pass around
 * inside this module, but do not return to the caller. Otherwise it would
 * logically be part of ELF_Info.
 */
typedef struct {
	const char	*eip_file_cmd;	/* argv[0] for file command */
	const char	*eip_file;	/* file being processed */
	Ehdr		eip_ehdr;	/* ELF header */
	Word		eip_shnum;	/* # section headers */
	Word		eip_phnum;	/* # program headers */
	Word		eip_shstrndx;	/* Index of section hdr string table */
	int		eip_fd;		/* fd of file being processed */
	ELF_FILE_OFF_T	eip_offset;	/* Base offset in file for ELF header */
					/*    Non-zero for archive members */
} ELF_InfoPrivate;

static elf_file_t	process_shdr(ELF_Info *, ELF_InfoPrivate *);
static elf_file_t	process_phdr(ELF_Info *, ELF_InfoPrivate *);
static elf_file_t	file_xlatetom(ELF_Info *, Elf_Type, char *);
static elf_file_t	xlatetom_nhdr(ELF_Info *, Nhdr *);
static elf_file_t	get_phdr(ELF_Info *, ELF_InfoPrivate *, Word, Phdr *);
static elf_file_t	get_shdr(ELF_Info *, ELF_InfoPrivate *, Word, Shdr *);


/*
 * file_xlatetom:	translate different headers from file
 * 			representation to memory representaion.
 */
#define	HDRSZ 512
static elf_file_t
file_xlatetom(ELF_Info *ei, Elf_Type type, char *hdr)
{
	Elf_Data src, dst;
	char *hbuf[HDRSZ];

	/* will convert only these types */
	switch (type) {
	case ELF_T_DYN:
	case ELF_T_EHDR:
	case ELF_T_PHDR:
	case ELF_T_SHDR:
	case ELF_T_WORD:
	case ELF_T_CAP:
		break;

	default:
		return (ELF_FILE_BAD);
	}

	src.d_buf = (Elf_Void *)hdr;
	src.d_type = type;
	src.d_version = ei->ei_version;

	dst.d_buf = (Elf_Void *)&hbuf;
	dst.d_version = EV_CURRENT;

	src.d_size = elf_fsize(type, 1, ei->ei_version);
	dst.d_size = elf_fsize(type, 1, EV_CURRENT);
	if (elf_xlatetom(&dst, &src, ei->ei_data) == NULL)
		return (ELF_FILE_BAD);

	(void) memcpy(hdr, &hbuf, dst.d_size);
	return (ELF_FILE_SUCCESS);
}

/*
 * xlatetom_nhdr:	There is no routine to convert Note header
 * 			so we convert each field of this header.
 */
static elf_file_t
xlatetom_nhdr(ELF_Info *ei, Nhdr *nhdr)
{
	int ret;

	ret = file_xlatetom(ei, ELF_T_WORD, (char *)&nhdr->n_namesz);
	if (ret != ELF_FILE_SUCCESS)
		return (ret);

	ret = file_xlatetom(ei, ELF_T_WORD, (char *)&nhdr->n_descsz);
	if (ret != ELF_FILE_SUCCESS)
		return (ret);

	return (file_xlatetom(ei, ELF_T_WORD, (char *)&nhdr->n_type));
}

/*
 * elf_file_read:
 *
 * Reads elf header, program headers, and section headers to collect all
 * information needed for 'file' output and stores it in ELF_Info.
 */
elf_file_t
elf_file_read(const char *file_cmd, const char *file, int fd,
    ELF_FILE_OFF_T base_offset, ELF_Info *ei)
{
	ELF_FILE_SIZE_T	size;
	Boolean		ret = TRUE;
	ELF_InfoPrivate	eip;

	(void) memset(ei, 0, sizeof (*ei));

	size = sizeof (eip.eip_ehdr);

	if (pread64(fd, &eip.eip_ehdr, size, base_offset) != size)
		ret = FALSE;

	ei->ei_class = eip.eip_ehdr.e_ident[EI_CLASS];
	ei->ei_data = eip.eip_ehdr.e_ident[EI_DATA];
	ei->ei_version = eip.eip_ehdr.e_ident[EI_VERSION];
	/* do as what libelf:_elf_config() does */
	if (ei->ei_version == 0)
		ei->ei_version = 1;

	if (file_xlatetom(ei, ELF_T_EHDR, (char *)&eip.eip_ehdr) ==
	    ELF_FILE_BAD)
		ret = FALSE;

	eip.eip_file_cmd = (file_cmd != NULL) ?
	    file_cmd : MSG_ORIG(MSG_FILE_STR_FILE);
	if (file == NULL)
		return (ELF_FILE_FATAL);
	eip.eip_file = file;

	/*
	 * Extended section or program indexes in use? If so, special
	 * values in the ELF header redirect us to get the real values
	 * from shdr[0].
	 */
	eip.eip_shnum = eip.eip_ehdr.e_shnum;
	eip.eip_phnum = eip.eip_ehdr.e_phnum;
	eip.eip_shstrndx = eip.eip_ehdr.e_shstrndx;
	if (((eip.eip_shnum == 0) || (eip.eip_phnum == PN_XNUM)) &&
	    (eip.eip_ehdr.e_shoff != 0)) {
		Shdr shdr;

		if (get_shdr(ei, &eip, 0, &shdr) == ELF_FILE_BAD)
			return (ELF_FILE_BAD);
		if (eip.eip_shnum == 0)
			eip.eip_shnum = shdr.sh_size;
		if ((eip.eip_phnum == PN_XNUM) && (shdr.sh_info != 0))
			eip.eip_phnum = shdr.sh_info;
		if (eip.eip_shstrndx == SHN_XINDEX)
			eip.eip_shstrndx = shdr.sh_link;
	}

	eip.eip_fd = fd;
	eip.eip_offset = base_offset;

	ei->ei_type = eip.eip_ehdr.e_type;
	ei->ei_machine = eip.eip_ehdr.e_machine;
	ei->ei_flags = eip.eip_ehdr.e_flags;

	if (ret == FALSE) {
		(void) fprintf(stderr, MSG_INTL(MSG_FILE_BADEHDR),
		    eip.eip_file_cmd, eip.eip_file);
		return (ELF_FILE_BAD);
	}
	if (process_phdr(ei, &eip) == ELF_FILE_BAD)
		return (ELF_FILE_BAD);

	/* We don't need section info for core files */
	if (eip.eip_ehdr.e_type != ET_CORE)
		if (process_shdr(ei, &eip) == ELF_FILE_BAD)
			return (ELF_FILE_BAD);

	return (ELF_FILE_SUCCESS);
}

/*
 * get_phdr:	reads program header of specified index.
 *
 * entry:
 *	ei - Information block for file being analyzed
 *	eip - Private information block for file being analyzed
 *	inx - Index of header to read
 *	phdr - Address of program header variable to read the
 *		file data into.
 */
static elf_file_t
get_phdr(ELF_Info * ei, ELF_InfoPrivate *eip, Word inx, Phdr *phdr)
{
	ELF_FILE_OFF_T	off = 0;
	ELF_FILE_SIZE_T	size;

	if (inx >= eip->eip_phnum)
		return (ELF_FILE_BAD);

	size = sizeof (Phdr);
	off = eip->eip_offset +
	    (ELF_FILE_OFF_T)eip->eip_ehdr.e_phoff + (inx * size);
	if (pread64(eip->eip_fd, phdr, size, off) != size)
		return (ELF_FILE_BAD);

	return (file_xlatetom(ei, ELF_T_PHDR, (char *)phdr));
}

/*
 * get_shdr:	reads section header of specified index.
 *
 * entry:
 *	ei - Information block for file being analyzed
 *	eip - Private information block for file being analyzed
 *	inx - Index of header to read
 *	shdr - Address of section header variable to read the
 *		file data into.
 */
static elf_file_t
get_shdr(ELF_Info *ei, ELF_InfoPrivate *eip, Word inx, Shdr *shdr)
{
	ELF_FILE_OFF_T	off = 0;
	ELF_FILE_SIZE_T	size;

	/*
	 * Prevent access to non-existent section headers.
	 *
	 * A value of 0 for e_shoff means that there is no section header
	 * array in the file. A value of 0 for e_shndx does not necessarily
	 * mean this - there can still be a 1-element section header array
	 * to support extended section or program header indexes that
	 * exceed the 16-bit fields used in the ELF header to represent them.
	 */
	if ((eip->eip_ehdr.e_shoff == 0) ||
	    ((inx > 0) && (inx >= eip->eip_shnum)))
		return (ELF_FILE_BAD);

	size = sizeof (*shdr);
	off = eip->eip_offset +
	    (ELF_FILE_OFF_T)eip->eip_ehdr.e_shoff + (inx * size);

	if (pread64(eip->eip_fd, shdr, size, off) != size)
		return (ELF_FILE_BAD);

	return (file_xlatetom(ei, ELF_T_SHDR, (char *)shdr));
}

/*
 * process_phdr:	Read Program Headers and see if it is a core
 *			file of either new or (pre-restructured /proc)
 * 			type, read the name of the file that dumped this
 *			core, else see if this is a dynamically linked.
 */
static elf_file_t
process_phdr(ELF_Info *ei, ELF_InfoPrivate *eip)
{
	Word inx;

	Nhdr	_nhdr, *nhdr;	/* note header just read */
	Phdr	phdr;

	ELF_FILE_SIZE_T	nsz, nmsz, dsz;
	ELF_FILE_OFF_T	offset;
	Word	class;
	int	ntype;
	char	*psinfo, *fname;

	nsz = sizeof (Nhdr);
	nhdr = &_nhdr;
	class = eip->eip_ehdr.e_ident[EI_CLASS];
	for (inx = 0; inx < eip->eip_phnum; inx++) {
		if (get_phdr(ei, eip, inx, &phdr) == ELF_FILE_BAD)
			return (ELF_FILE_BAD);

		/* read the note if it is a core */
		if (phdr.p_type == PT_NOTE &&
		    eip->eip_ehdr.e_type == ET_CORE) {
			/*
			 * If the next segment is also a note, use it instead.
			 */
			if (get_phdr(ei, eip, inx+1, &phdr) == ELF_FILE_BAD)
				return (ELF_FILE_BAD);
			if (phdr.p_type != PT_NOTE) {
				/* read the first phdr back */
				if (get_phdr(ei, eip, inx, &phdr) ==
				    ELF_FILE_BAD)
					return (ELF_FILE_BAD);
			}
			offset = eip->eip_offset + phdr.p_offset;
			if (pread64(eip->eip_fd, (void *)nhdr, nsz, offset)
			    != nsz)
				return (ELF_FILE_BAD);

			/* Translate the ELF note header */
			if (xlatetom_nhdr(ei, nhdr) == ELF_FILE_BAD)
				return (ELF_FILE_BAD);

			ntype = nhdr->n_type;
			nmsz = nhdr->n_namesz;
			dsz = nhdr->n_descsz;

			offset += nsz + ((nmsz + 0x03) & ~0x3);
			if ((psinfo = malloc(dsz)) == NULL) {
				int err = errno;
				(void) fprintf(stderr,
				    MSG_INTL(MSG_FILE_BADALLOC),
				    eip->eip_file_cmd, strerror(err));
				return (ELF_FILE_FATAL);
			}
			if (pread64(eip->eip_fd, psinfo, dsz, offset) != dsz)
				return (ELF_FILE_BAD);
			/*
			 * We want to print the string contained
			 * in psinfo->pr_fname[], where 'psinfo'
			 * is either an old NT_PRPSINFO structure
			 * or a new NT_PSINFO structure.
			 *
			 * Old core files have only type NT_PRPSINFO.
			 * New core files have type NT_PSINFO.
			 *
			 * These structures are also different by
			 * virtue of being contained in a core file
			 * of either 32-bit or 64-bit type.
			 *
			 * To further complicate matters, we ourself
			 * might be compiled either 32-bit or 64-bit.
			 *
			 * For these reason, we just *know* the offsets of
			 * pr_fname[] into the four different structures
			 * here, regardless of how we are compiled.
			 */
			if (class == ELFCLASS32) {
				/* 32-bit core file, 32-bit structures */
				if (ntype == NT_PSINFO)
					fname = psinfo + 88;
				else	/* old: NT_PRPSINFO */
					fname = psinfo + 84;
			} else if (class == ELFCLASS64) {
				/* 64-bit core file, 64-bit structures */
				if (ntype == NT_PSINFO)
					fname = psinfo + 136;
				else	/* old: NT_PRPSINFO */
					fname = psinfo + 120;
			}
			ei->ei_core_type = (ntype == NT_PRPSINFO)?
			    EC_OLDCORE : EC_NEWCORE;
			(void) memcpy(ei->ei_fname, fname, strlen(fname));
			free(psinfo);
		}
		if (phdr.p_type == PT_DYNAMIC) {
			ei->ei_dynamic = B_TRUE;
		}
	}
	return (ELF_FILE_SUCCESS);
}

/*
 * process_shdr:
 * -	Read Section Headers to attempt to get HW/SW capabilities
 *	by looking at the SUNW_cap section and set string in Elf_Info.
 * -	Look for symbol tables and debug information sections. Set the
 *	"stripped" field in Elf_Info with corresponding flags.
 * -	Look for a dynamic section, and if DF_1_STUB is present, set
 *	ei->ei_stub.
 */
static elf_file_t
process_shdr(ELF_Info *ei, ELF_InfoPrivate *eip)
{
	Word 		eltn, mac;
	Word 		i, j;
	ELF_FILE_OFF_T	elt_off;
	ELF_FILE_SIZE_T	eltsize;
	const char	*section_name;
	char		*section_name_alloc;
	Shdr		shdr;
	Word		sh_size;


	mac = eip->eip_ehdr.e_machine;

	/* if there are no sections, return success anyway */
	if (eip->eip_ehdr.e_shoff == 0 && eip->eip_shnum == 0)
		return (ELF_FILE_SUCCESS);

	/* read section names from String Section */
	if (get_shdr(ei, eip, eip->eip_shstrndx, &shdr) == ELF_FILE_BAD)
		return (ELF_FILE_BAD);

	/*
	 * Read section string table, guarding against missing, or
	 * corrupt strings.
	 */
	sh_size = shdr.sh_size;
	if (sh_size == 0) {
		section_name = MSG_ORIG(MSG_FILE_STR_EMPTY);
		section_name_alloc = NULL;
	} else {
		if ((section_name = section_name_alloc = malloc(sh_size)) ==
		    NULL)
			return (ELF_FILE_FATAL);
		if (pread64(eip->eip_fd, section_name_alloc, shdr.sh_size,
		    eip->eip_offset + shdr.sh_offset) != shdr.sh_size)
			goto  ret_file_bad;
		section_name_alloc[sh_size - 1] = '\0';
	}


	/* read all the sections and process them */
	for (i = 0; i < eip->eip_shnum; i++) {
		const char *str;

		if (get_shdr(ei, eip, i, &shdr) == ELF_FILE_BAD)
			goto ret_file_bad;

		if (shdr.sh_type == SHT_NULL)
			continue;

		if (shdr.sh_type == SHT_DYNAMIC) {
			Dyn	dyn;

			if (shdr.sh_size == 0 || shdr.sh_entsize == 0) {
				(void) fprintf(stderr,
				    MSG_INTL(MSG_FILE_DYN_ZERO),
				    eip->eip_file_cmd, eip->eip_file);
				goto ret_file_bad;
			}
			eltsize = sizeof (Dyn);
			elt_off = eip->eip_offset + shdr.sh_offset;
			eltn = (shdr.sh_size / shdr.sh_entsize);
			for (j = 0; j < eltn; j++) {
				/*
				 * read dyn and xlate the values
				 */
				if ((pread64(eip->eip_fd, &dyn, eltsize,
				    elt_off) != eltsize) ||
				    (file_xlatetom(ei, ELF_T_DYN, (char *)&dyn)
				    != ELF_FILE_SUCCESS)) {
					(void) fprintf(stderr,
					    MSG_INTL(MSG_FILE_DYN_CNTREAD),
					    eip->eip_file_cmd, eip->eip_file);
					goto ret_file_bad;
				}

				if ((dyn.d_tag == DT_FLAGS_1) &&
				    (dyn.d_un.d_val & DF_1_STUB))
					ei->ei_stub = B_TRUE;

				elt_off += eltsize;
			}

			continue;
		}

		if (shdr.sh_type == SHT_SUNW_cap) {
			Cap	Chdr;
			char	*capstr = ei->ei_cap_str;
			size_t	tsize = sizeof (ei->ei_cap_str);

			if (shdr.sh_size == 0 || shdr.sh_entsize == 0) {
				(void) fprintf(stderr,
				    MSG_INTL(MSG_FILE_CAP_ZERO),
				    eip->eip_file_cmd, eip->eip_file);
				goto ret_file_bad;
			}
			eltsize = sizeof (Cap);
			elt_off = eip->eip_offset + shdr.sh_offset;
			eltn = (shdr.sh_size / shdr.sh_entsize);
			for (j = 0; j < eltn; j++) {
				size_t	csize;

				/*
				 * read cap and xlate the values
				 */
				if ((pread64(eip->eip_fd, &Chdr, eltsize,
				    elt_off) != eltsize) ||
				    (file_xlatetom(ei, ELF_T_CAP,
				    (char *)&Chdr) != ELF_FILE_SUCCESS)) {
					(void) fprintf(stderr,
					    MSG_INTL(MSG_FILE_CAP_CNTREAD),
					    eip->eip_file_cmd, eip->eip_file);
					goto ret_file_bad;
				}

				/*
				 * file(1) only displays any objects software
				 * or hardware capabilities. These occur before
				 * the first CA_SUNW_NULL. Presently, any
				 * platform or machine capabilities are not
				 * displayed. These capabilities can be of an
				 * arbitrary length, making the sizing of an
				 * output buffer difficult. In addition, these
				 * capabilities are typically used to select
				 * symbol capabilities, and don't lend
				 * themselves very well for definiting object
				 * capabilities.
				 */
				if (Chdr.c_tag == CA_SUNW_NULL)
					break;

				if ((Chdr.c_tag != CA_SUNW_HW_1) &&
				    (Chdr.c_tag != CA_SUNW_HW_2) &&
				    (Chdr.c_tag != CA_SUNW_SF_1))
					continue;

				if (capstr != ei->ei_cap_str) {
					/*
					 * Add a space to any previous string
					 * in preparation for adding any new
					 * strings.
					 */
					*capstr++ = ' ';
					*capstr = '\0';
					tsize--;
				}
				(void) elfcap_tag_to_str(ELFCAP_STYLE_UC,
				    Chdr.c_tag, Chdr.c_un.c_val, capstr, tsize,
				    ELFCAP_FMT_SNGSPACE, mac);

				elt_off += eltsize;

				csize = strlen(capstr);
				capstr += csize;
				tsize -= csize;
			}
		}

		/*
		 * Definition time:
		 *	- "not stripped" means that an executable file
		 *	contains a Symbol Table (.symtab)
		 *	- "stripped" means that an executable file
		 *	does not contain a Symbol Table.
		 * When strip -l or strip -x is run, it strips the
		 * debugging information (.line section name (strip -l),
		 * .line, .debug*, .stabs*, .dwarf* section names
		 * and SHT_SUNW_DEBUGSTR and SHT_SUNW_DEBUG
		 * section types (strip -x), however the Symbol
		 * Table will still be present.
		 * Therefore, if
		 *	- No Symbol Table present, then report
		 *		"stripped"
		 *	- Symbol Table present with debugging
		 *	information (line number or debug section names,
		 *	or SHT_SUNW_DEBUGSTR or SHT_SUNW_DEBUG section
		 *	types) then report:
		 *		"not stripped"
		 *	- Symbol Table present with no debugging
		 *	information (line number or debug section names,
		 *	or SHT_SUNW_DEBUGSTR or SHT_SUNW_DEBUG section
		 *	types) then report:
		 *		"not stripped, no debugging information
		 *		available"
		 */
		if ((ei->ei_stripped & E_NOSTRIP) == E_NOSTRIP)
			continue;

		if (!(ei->ei_stripped & E_SYMTAB) &&
		    (shdr.sh_type == SHT_SYMTAB)) {
			ei->ei_stripped |= E_SYMTAB;
			continue;
		}

		str = (shdr.sh_name > sh_size) ? MSG_ORIG(MSG_FILE_STR_EMPTY) :
		    &section_name[shdr.sh_name];

		if (!(ei->ei_stripped & E_DBGINF) &&
		    ((shdr.sh_type == SHT_SUNW_DEBUG) ||
		    (shdr.sh_type == SHT_SUNW_DEBUGSTR) ||
		    (elf_file_debug_section(str)))) {
			ei->ei_stripped |= E_DBGINF;
		}
	}
	if (section_name_alloc != NULL)
		free(section_name_alloc);

	return (ELF_FILE_SUCCESS);

ret_file_bad:
	/*
	 * Exit on ELF_FILE_BAD without leaking the section_name memory.
	 */
	if (section_name_alloc != NULL)
		free(section_name_alloc);

	return (ELF_FILE_BAD);
}
