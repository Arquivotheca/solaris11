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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#define	_LARGEFILE64_SOURCE

/* Get definitions for the relocation types supported. */
#define	ELF_TARGET_ALL

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <_libelf.h>
#include <locale.h>
#include <wctype.h>
#include <sys/elf.h>
#include <procfs.h>
#include <sgsrtcid.h>
#include <ar.h>
#include <member.h>
#include <errno.h>
#include "_elf_file.h"
#include "msg.h"


/*
 * Ensure that ELF_FILE_FBUF_MIN is large enough to cover everything
 * we try to identify in this code.
 */
#if ELF_FILE_FBUF_MIN < EI_NIDENT
#error "ELF_FILE_FBUF_MIN is too small (ELF objects)"
#endif
#if ELF_FILE_FBUF_MIN < SARMAG
#error "ELF_FILE_FBUF_MIN is too small (archives)"
#endif
#if ELF_FILE_FBUF_MIN < 16
#error "ELF_FILE_FBUF_MIN is too small (runtime linker configuration files)"
#endif


/*
 * The line and debug section names are used by the strip command.
 * Any changes in the strip implementation need to be reflected here.
 */
static const char *debug_sections[] = {	/* Debug sections in a ELF file */
	MSG_ORIG(MSG_FILE_DBG_DEBUG),	/* .debug */
	MSG_ORIG(MSG_FILE_DBG_STAB),	/* .stab */
	MSG_ORIG(MSG_FILE_DBG_DWARF),	/* .dwarf */
	MSG_ORIG(MSG_FILE_DBG_LINE),	/* .line */
	NULL
};


static const char *
elf_type(ELF_Info *ei)
{
	if (ei->ei_core_type != EC_NOTCORE) {
		/* Print what kind of core is this */
		if (ei->ei_core_type == EC_OLDCORE)
			return (MSG_INTL(MSG_FILE_CORE_PRE26));
		else
			return (MSG_INTL(MSG_FILE_CORE));
	}

	switch (ei->ei_type) {
	case ET_NONE:
		return (MSG_INTL(MSG_FILE_ET_NONE));
	case ET_REL:
		return (MSG_INTL(MSG_FILE_ET_REL));
	case ET_EXEC:
		return (MSG_INTL(MSG_FILE_ET_EXEC));
	case ET_DYN:
		return (MSG_INTL(MSG_FILE_ET_DYN));
	}

	return (MSG_ORIG(MSG_FILE_STR_EMPTY));
}

static void
print_elf_flags(ELF_Info *ei)
{
	Word flags;

	flags = ei->ei_flags;
	switch (ei->ei_machine) {
	case EM_SPARC:
	case EM_SPARC32PLUS:
	case EM_SPARCV9:
		if ((flags & EF_SPARC_EXT_MASK) == 0)
			break;
		if (flags & EF_SPARC_32PLUS)
			(void) printf(MSG_INTL(MSG_FILE_EF_SPARC_32PLUS));
		if (flags & EF_SPARC_SUN_US3) {
			(void) printf(MSG_INTL(MSG_FILE_EF_SPARC_SUN_US3));
		} else if (flags & EF_SPARC_SUN_US1) {
			(void) printf(MSG_INTL(MSG_FILE_EF_SPARC_SUN_US1));
		}
		if (flags & EF_SPARC_HAL_R1)
			(void) printf(MSG_INTL(MSG_FILE_EF_SPARC_HAL_R1));
		break;
	}
}

/*
 * Checks the ident field of the presumeably elf file. If check fails,
 * this is not an elf file.
 */
static elf_file_t
read_elf_data(const char *file_cmd, const char *file, uchar_t *ident,
    int fd, ELF_FILE_OFF_T base_offset, ELF_Info *ei)
{
	elf_file_t	ret;

	if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
	    ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3)
		return (ELF_FILE_NOTELF);

	switch (ident[EI_CLASS]) {
	case ELFCLASS32:
		ret = elf_file_read32(file_cmd, file, fd, base_offset, ei);
		break;
	case ELFCLASS64:
		ret = elf_file_read64(file_cmd, file, fd, base_offset, ei);
		break;
	default:
		(void) fprintf(stderr, MSG_INTL(MSG_FILE_BADCLASS),
		    file_cmd, file, EC_WORD(ident[EI_CLASS]));
		ret = ELF_FILE_BAD;
		break;
	}
	return (ret);
}

/*
 * Output the information for an ELF object
 */
void
elf_output(ELF_Info *ei, Boolean simple)
{
	Conv_inv_buf_t	inv_buf;

	(void) printf(MSG_INTL(MSG_FILE_ELF),
	    conv_ehdr_class(ei->ei_class, CONV_FMT_ALT_DUMP, &inv_buf),
	    conv_ehdr_data(ei->ei_data, CONV_FMT_ALT_FILE, &inv_buf),
	    elf_type(ei),
	    conv_ehdr_mach(ei->ei_machine, CONV_FMT_ALT_FILE, &inv_buf),
	    EC_WORD(ei->ei_version));

	if (simple)
		return;

	/* Print Flags */
	print_elf_flags(ei);

	/* If it is a core, identify the program that dumped it */
	if (ei->ei_core_type != EC_NOTCORE) {
		(void) printf(MSG_INTL(MSG_FILE_COREFROM), ei->ei_fname);
		return;
	}

	/* Print Capabilities */
	if (ei->ei_cap_str[0] != '\0')
		(void) printf(MSG_ORIG(MSG_FILE_FMT_CAP), ei->ei_cap_str);

	if ((ei->ei_type != ET_EXEC) && (ei->ei_type != ET_DYN))
		return;

	/* Print if it is a stub object */
	if (ei->ei_stub)
		(void) printf(MSG_INTL(MSG_FILE_STUB));

	/* Print if it is dynamically linked */
	if (ei->ei_dynamic)
		(void) printf(MSG_INTL(MSG_FILE_LINKDYNAMIC));
	else
		(void) printf(MSG_INTL(MSG_FILE_LINKSTATIC));

	/* Print if it is stripped */
	if (ei->ei_stripped & E_SYMTAB) {
		(void) printf(MSG_INTL(MSG_FILE_NOTSTRIPPED));
		if (!(ei->ei_stripped & E_DBGINF))
			(void) printf(MSG_INTL(MSG_FILE_NODEBUGINFO));
	} else {
		(void) printf(MSG_INTL(MSG_FILE_STRIPPED));
	}
}

/*
 * entry:
 *	file - File to examine
 *	fd - File descriptor, open for read on file.
 *	ident - Buffer containing initial bytes from file. Caller
 *		is responsible for ensuring that this buffer contains
 *		at least EI_NIDENT (16) bytes.
 */
static elf_file_t
elf_check(const char *file_cmd, const char *file, int fd, uchar_t *ident)
{
	ELF_Info	ei;
	elf_file_t	ret;

	/*
	 * Read the e_ident, and other ELF information from the file.
	 * Return quietly if not elf.
	 */
	ret = read_elf_data(file_cmd, file, ident, fd, 0, &ei);

	switch (ret) {
	case ELF_FILE_SUCCESS:
		elf_output(&ei, 0);
		break;
	case ELF_FILE_BAD:
		/*
		 * A bad ELF object is a problem for our code, but does not
		 * represent an error to the file utility. We simply need to
		 * report it accurately.
		 */
		(void) printf(MSG_INTL(MSG_FILE_BADELF));
		ret = ELF_FILE_SUCCESS;
		break;
	}
	return (ret);
}

/*
 * Return TRUE if the two archive members can be considered to be of the same
 * type. This includes basic ELF attributes (elfclass, machine type, etc), but
 * not more, superficial things such as capabilities, or debug/strip status.
 */
static Boolean
ar_elf_compat(ELF_Info *ei1, ELF_Info *ei2)
{
	Word	scr1, scr2;

	/* Everything other than machine must match exactly */
	if ((ei1->ei_class != ei2->ei_class) ||
	    (ei1->ei_data != ei2->ei_data) ||
	    (ei1->ei_type != ei2->ei_type) ||
	    (ei1->ei_core_type != ei2->ei_core_type) ||
	    (ei1->ei_version != ei2->ei_version))
		return (FALSE);

	/*
	 * There are some machines that we consider equivalent. Map the
	 * machine types to these before doing the comparison.
	 */
	switch (ei1->ei_machine) {
	case EM_SPARC32PLUS:
		scr1 = EM_SPARC;
		break;
	case EM_486:
		scr1 = EM_386;
		break;
	default:
		scr1 = ei1->ei_machine;
	}
	switch (ei2->ei_machine) {
	case EM_SPARC32PLUS:
		scr2 = EM_SPARC;
		break;
	case EM_486:
		scr2 = EM_386;
		break;
	default:
		scr2 = ei2->ei_machine;
	}
	return (scr1 == scr2);
}

/*
 * Read the initial special archive members (symbol table, string table),
 * and position offset to the first non-special member.
 */
static elf_file_t
ar_read_special(const char *file_cmd, const char *file, int fd,
    ELF_FILE_OFF_T *off, elf_file_ar_t style, const char **strtab,
    Word *strtab_len)
{
	struct ar_hdr	hdr;
	Word		i;
	char		*_strtab;

	*strtab_len = 0;
	*off = SARMAG;
	while (pread64(fd, &hdr, sizeof (hdr), *off) == sizeof (hdr)) {
		/* If this is a symbol table, display its type */
		if ((strncmp(MSG_ORIG(MSG_FILE_AR_SYM),
		    hdr.ar_name, MSG_FILE_AR_SYM_SIZE) == 0) ||
		    (strncmp(MSG_ORIG(MSG_FILE_AR_SYM64),
		    hdr.ar_name, MSG_FILE_AR_SYM64_SIZE) == 0)) {
			(void) printf(MSG_INTL(MSG_FILE_AR_SYMTBL),
			    EC_WORD((hdr.ar_name[1] == ' ') ? 32 : 64));
		} else if ((strncmp(MSG_ORIG(MSG_FILE_AR_STRTAB),
		    hdr.ar_name, MSG_FILE_AR_STRTAB_SIZE) == 0)) {
			/*
			 * String table: If we are providing detailed
			 * per-member output, then we need the name
			 * strings. Otherwise, quietly skip it.
			 */
			if (style == ELF_FILE_AR_DETAIL) {
				*strtab_len =  _elf_number(&hdr.ar_size[0],
				    &hdr.ar_size[ARSZ(ar_size)], 10);
				if (*strtab_len != 0) {
					_strtab = malloc(*strtab_len);
					if (_strtab == NULL) {
						int err = errno;

						(void) fprintf(stderr,
						    MSG_INTL(MSG_FILE_BADALLOC),
						    file_cmd, strerror(err));
						return (ELF_FILE_FATAL);
					}
				}
				if (pread64(fd, _strtab, *strtab_len,
				    *off + sizeof (hdr)) != *strtab_len) {
					(void) fprintf(stderr,
					    MSG_INTL(MSG_FILE_BADARSTRTAB),
					    file_cmd, file);
					return (ELF_FILE_BAD);
				}
				_strtab[*strtab_len - 1] = '\0';
				for (i = 0; i < *strtab_len; i++)
					if (_strtab[i] == '/')
						_strtab[i] = '\0';
				*strtab = _strtab;
			}
		} else {
			/* We don't recognize this member as special */
			return (ELF_FILE_SUCCESS);
		}

		/* Move on to next member */
		*off += sizeof (hdr) + _elf_number(hdr.ar_size,
		    &hdr.ar_size[sizeof (hdr.ar_size)], 10);
		if (*off & 0x1)
			(*off)++;
	}

	return (ELF_FILE_SUCCESS);
}

static const char *
ar_member_name(struct ar_hdr *hdr, const char *strtab, Word strtab_len)
{
	const char	*name;
	Word		off;
	Word		i;

	if (hdr->ar_name[0] == '/') {
		off = _elf_number(&hdr->ar_name[1],
		    &hdr->ar_name[ARSZ(ar_name)], 10);
		if (off >= strtab_len) {
			hdr->ar_name[ARSZ(ar_name) - 1] = '\0';
			name = hdr->ar_name;
		} else {
			name = strtab + off;
		}
	} else {
		for (i = 1; i < ARSZ(ar_name) - 1; i++)
			if (hdr->ar_name[i] == '/') {
				i++;
				break;
			}
		hdr->ar_name[i - 1] = '\0';
		name = hdr->ar_name;
	}

	return (name);
}


/*
 * Output a new line for a per-member ELF_FILE_AR_DETAIL line. The rules
 * for determining the separator string between the colon and the following
 * text mirror those of the prf() macro in the 'file' code.
 */
static void
detail_prf(const char *file, size_t file_len, const char *member, int is_xpg4)
{
	const char *sep;

	if (is_xpg4)
		sep = MSG_ORIG(MSG_FILE_STR_SP);
	else if ((file_len + strlen(member) + 2) > 6)
		sep = MSG_ORIG(MSG_FILE_STR_TAB);
	else
		sep = MSG_ORIG(MSG_FILE_STR_TABTAB);

	(void) printf(MSG_ORIG(MSG_FILE_FMT_DETAIL), file, member, sep);

}

static elf_file_t
ar_elf_check(const char *file_cmd, const char *file, int is_xpg4,
    elf_file_ar_t style, int fd, const char *armag_buf)
{
	/* Content we expect to see in an archive */
	enum {
		ARS_EMPTY,	/* Archive has no data members */
		ARS_NOELF,	/* Archive contains no ELF members */
		ARS_ELF,	/* All ELF members of single type */
		ARS_MIXED_ELF,	/* ELF members of dissimilar type */
		ARS_MIXED	/* ELF and non-ELF members */
	} ar_summary = ARS_EMPTY;

	uchar_t		ident[EI_NIDENT];
	ELF_Info	*ei, ei_save, ei_next;
	struct ar_hdr	hdr;
	ELF_FILE_OFF_T	off;
	elf_file_t	ret;
	const char	*strtab;
	Word		strtab_len;
	size_t		file_len;

	/* If not an archive, then return quietly */
	if (strncmp(ARMAG, armag_buf, SARMAG) != 0)
		return (ELF_FILE_NOTELF);

	(void) printf(MSG_INTL(MSG_FILE_AR));

	/*
	 * Read the special archive members at the head of the archive,
	 * and find the offset to the first regular member.
	 */
	ret = ar_read_special(file_cmd, file, fd, &off, style, &strtab,
	    &strtab_len);
	if (ret != ELF_FILE_SUCCESS)
		return (ret);

	switch (style) {
	case ELF_FILE_AR_BASIC:
		/* If only providing basic information, we're done */
		return (ELF_FILE_SUCCESS);
	case ELF_FILE_AR_DETAIL:
		file_len = strlen(file);
		break;
	}

	/*
	 * Loop through the archive members.
	 *
	 * -	For ELF_FILE_AR_DETAIL, issue a line for each member
	 *	describing it in ELF terms.
	 *
	 * -	For ELF_FILE_AR_SUMMARY, build up a summary description
	 *	for the contents. If we determine that the archive
	 *	contains ARS_MIXED content, then we can stop at that point,
	 *	as reading more members can never get us out of that state.
	 *
	 *	The information for the first ELF member is placed into
	 *	ei_save for possible use producing output. Any following
	 *	ELF member is read into ei_next, and then compared to ei_save
	 *	to determine if they have compatible basic attributes.
	 */
	ei = &ei_save;
	while (pread64(fd, &hdr, sizeof (hdr), off) == sizeof (hdr)) {
		off += sizeof (hdr);

		if ((pread64(fd, ident, EI_NIDENT, off) == EI_NIDENT) &&
		    (read_elf_data(file_cmd, file, ident, fd, off, ei)
		    == ELF_FILE_SUCCESS)) {
			if (style == ELF_FILE_AR_DETAIL) {
				detail_prf(file, file_len, ar_member_name(&hdr,
				    strtab, strtab_len), is_xpg4);
				elf_output(&ei_save, 0);
				goto loop_continue;
			}

			/* ELF_FILE_AR_SUMMARY */
			switch (ar_summary) {
			case ARS_EMPTY:
				/*
				 * Retain the current information for
				 * comparison to following members, and mark
				 * it as having ELF content.
				 */
				ei = &ei_next;
				ar_summary = ARS_ELF;
				break;
			case ARS_NOELF:
				ar_summary = ARS_MIXED;
				goto loop_break;
			case ARS_ELF:
				/*
				 * Are the objects compatible? If so,
				 * stay at current setting. Otherwise, move
				 * to ARS_MIXED_ELF.
				 */
				if (!ar_elf_compat(&ei_save, &ei_next))
					ar_summary = ARS_MIXED_ELF;
			}
		} else {	/* Not an ELF member */
			if (style == ELF_FILE_AR_DETAIL) {
				detail_prf(file, file_len, ar_member_name(&hdr,
				    strtab, strtab_len), is_xpg4);
				(void) printf(MSG_INTL(MSG_FILE_AR_NONELF));
				goto loop_continue;
			}

			/* ELF_FILE_AR_SUMMARY */
			switch (ar_summary) {
			case ARS_EMPTY:
				ar_summary = ARS_NOELF;
				break;
			case ARS_ELF:
			case ARS_MIXED_ELF:
				ar_summary = ARS_MIXED;
				goto loop_break;
			}
		}

		/* Move on to next member */
	loop_continue:
		off += _elf_number(hdr.ar_size,
		    &hdr.ar_size[sizeof (hdr.ar_size)], 10);
		if (off & 0x1)
			off++;
	}
loop_break:

	if (style == ELF_FILE_AR_DETAIL) {
		if (strtab_len > 0)
			free((void *) strtab);
		return (ELF_FILE_SUCCESS);
	}

	/* Provide a content summary */
	switch (ar_summary) {
	case ARS_EMPTY:
		(void) printf(MSG_INTL(MSG_FILE_AR_EMPTY));
		break;
	case ARS_NOELF:
		(void) printf(MSG_ORIG(MSG_FILE_STR_COMMASP));
		(void) printf(MSG_INTL(MSG_FILE_AR_NONELF));
		break;
	case ARS_ELF:
		/*
		 * All members are ELF and have compatible basic attributes.
		 * Display the attributes as we would for a standalone object.
		 */
		(void) printf(MSG_ORIG(MSG_FILE_STR_COMMASP));
		elf_output(&ei_save, 1);
		break;
	case ARS_MIXED_ELF:
		/*
		 * All members are ELF, but the basic attributes are
		 * incompatible. Provide the user with that basic information,
		 * but do not go further. A tool like elfdump is more
		 * appropriate for going deeper.
		 */
		(void) printf(MSG_INTL(MSG_FILE_AR_MIXELF));
		break;
	case ARS_MIXED:
		(void) printf(MSG_INTL(MSG_FILE_AR_MIX));
		break;
	}

	return (ELF_FILE_SUCCESS);
}

/*
 * is_rtld_config - If file is a runtime linker config file, prints
 * the description and returns True (1). Otherwise, silently returns
 * False (0).
 */
static elf_file_t
is_rtld_config(const char *fbuf)
{
	Conv_inv_buf_t	inv_buf;
	Rtc_id		*id;

	if (RTC_ID_TEST(fbuf)) {
		id = (Rtc_id *) fbuf;
		(void) printf(MSG_INTL(MSG_FILE_RLC),
		    conv_ehdr_class(id->id_class, CONV_FMT_ALT_DUMP, &inv_buf),
		    conv_ehdr_data(id->id_data, CONV_FMT_ALT_FILE, &inv_buf),
		    conv_ehdr_mach(id->id_machine, CONV_FMT_ALT_FILE,
		    &inv_buf));

		return (ELF_FILE_SUCCESS);
	}

	return (ELF_FILE_NOTELF);
}

/*
 * Return TRUE if str gives the name of a debug section.
 */
Boolean
elf_file_debug_section(const char *str)
{
	Word i;

	/*
	 * Only need to compare the strlen(str_list[i]) bytes.
	 * That way .stab will match on .stab* sections, and
	 * .debug will match on .debug* sections.
	 */
	for (i = 0; debug_sections[i] != NULL; i++) {
		if (strncmp(debug_sections[i], str,
		    strlen(debug_sections[i])) == 0) {
			return (TRUE);
		}
	}
	return (FALSE);
}

/*
 * elf_file - applies default position-sensitive tests to identifiy ELF
 *	objects or runtime linker configuration files. This function is
 *	private and undocumented, and only for use by the 'file' command.
 *
 * entry:
 *	file_cmd - String identifying file command (argv[0]).
 *	file - File path of file being inspected
 *	is_xpg4 - True (1) if called by the XPG4 version of 'file',
 *		False (0) otherwise.
 *	style - Specifies how archive content is displayed.
 *	fd - File descriptor for file, open for reading.
 *	fbuf - Buffer containing initial bytes from the head of
 *		the file, or NULL.
 *	fbuf_size - The number of bytes of data found in fbuf.
 *
 * exit:
 *	The current file pointer for fd is not moved.
 *
 *	Data is read from fd, but no writes are done.
 *
 *	Output is written to stdout, and possibly stderr, on behalf
 *	of the file utility.
 *
 *	Returns ELF_FILE_FATAL on error which should result in
 *	error (non-zero) exit status for the file utility.
 *	Returns ELF_FILE_NOTELF if no matching file type found.
 *	Returns ELF_FILE_SUCCESS if matching file type found.
 */

elf_file_t
_elf_file(const char *file_cmd, const char *file, int is_xpg4,
    elf_file_ar_t style, int fd, const char *fbuf, size_t fbuf_size)
{
	union {
		char	obj[EI_NIDENT];
		char	rtc[sizeof (Rtc_id)];
		char	ar[SARMAG];
	} scr;
	elf_file_t	ret;

	/* ELF object? */
	if (fbuf_size < sizeof (scr.obj)) {
		if (pread64(fd, scr.obj, sizeof (scr.obj), 0) !=
		    sizeof (scr.obj))
			return (ELF_FILE_NOTELF);
		fbuf = scr.obj;
	}
	ret = elf_check(file_cmd, file, fd, (uchar_t *)fbuf);
	if (ret != ELF_FILE_NOTELF)
		return (ret);

	/* Archive? */
	if (fbuf_size < sizeof (scr.ar)) {
		if (pread64(fd, scr.ar, sizeof (scr.ar), 0) != sizeof (scr.ar))
			return (ELF_FILE_NOTELF);
		fbuf = scr.ar;
	}
	ret = ar_elf_check(file_cmd, file, is_xpg4, style, fd, fbuf);
	if (ret == ELF_FILE_BAD) {
		(void) printf(MSG_INTL(MSG_FILE_BADAR));
		ret = ELF_FILE_SUCCESS;
	}
	if (ret != ELF_FILE_NOTELF)
		return (ret);

	/*
	 * Runtime linker (ld.so.1) configuration file?
	 */
	if (fbuf_size < sizeof (scr.rtc)) {
		if (pread64(fd, scr.rtc, sizeof (scr.rtc), 0) !=
		    sizeof (scr.rtc))
			return (ELF_FILE_NOTELF);
		fbuf = scr.rtc;
	}
	return (is_rtld_config(fbuf));
}
