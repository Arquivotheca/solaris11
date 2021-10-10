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
 * Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/* UNIX HEADER */
#include <stdio.h>

/* SIZE HEADERS */
#include "defs.h"

/* ELF HEADERS */
#include "gelf.h"


/* SIZE FUNCTIONS CALLED */
extern void	error();


/* FORMAT STRINGS */

static const char *prusect[3] = {
	"%llx",
	"%llo",
	"%lld"
};

static const char *prusum[3] = {
	" = 0x%llx\n",
	" = 0%llo\n",
	" = %lld\n"
};

static const char *format[3] = {
	"%llx + %llx + %llx = 0x%llx\n",
	"%llo + %llo + %llo = 0%llo\n",
	"%lld + %lld + %lld = %lld\n"
};

static void	process_phdr(Elf *elf, GElf_Half num);

extern int	fflag, Fflag, nflag, numbase, is_archive, oneflag;
extern char	*fname, *archive;

void
process(Elf *elf)
{
	GElf_Xword	size, first, second, third, totsize;
	GElf_Ehdr	ehdr;
	GElf_Shdr	shdr;
	Elf_Scn		*scn;
	size_t		shstrndx, shdrnum, phdrnum, i;
	int		notfirst = 0;

	/*
	 * If there is a program header and the -f flag requesting section
	 * information is not set, then process segments with the process_phdr
	 * function.  Otherwise, process sections.  For the default case, the
	 * first number shall be the size of all sections that are allocatable,
	 * nonwritable and not of type NOBITS; the second number shall be the
	 * size of all sections that are allocatable, writable, and not of type
	 * NOBITS; the third number is the size of all sections that are
	 * writable and not of type NOBITS.  If -f is set, print the size of
	 * each allocatable section, followed by the section name in
	 * parentheses.  If -n is set, print the size of all sections, followed
	 * by the section name in parentheses.
	 */
	if (gelf_getehdr(elf, &ehdr) == 0) {
		error(fname, "invalid file type");
		return;
	}
	if (elf_getphdrnum(elf, &phdrnum) == -1) {
		error(fname, "unable to obtain number of program headers");
		return;
	}
	if (phdrnum && !fflag) {
		process_phdr(elf, phdrnum);
		return;
	}

	if (is_archive) {
		(void) printf("%s[%s]: ", archive, fname);
	} else if (!oneflag && !is_archive) {
		(void) printf("%s: ", fname);
	}

	/*
	 * Determine the number of sections, and the index of the section
	 * header string table.
	 */
	if (elf_getshdrnum(elf, &shdrnum) == -1) {
		error(fname, "unable to obtain number of sections");
		return;
	}
	if (elf_getshstrndx(elf, &shstrndx) == -1) {
		error(fname, "unable to obtain section header string table "
		    "index");
		return;
	}

	scn = NULL;
	size = first = second = third = totsize = 0;

	for (i = 0; i < shdrnum; i++) {
		if ((scn = elf_nextscn(elf, scn)) == NULL) {
			break;
		}
		if (gelf_getshdr(scn, &shdr) == 0) {
			error(fname, "could not get section header");
			break;
		}
		if (Fflag && !fflag) {
			error(fname, "no segment data");
			return;
		} else if (!(shdr.sh_flags & SHF_ALLOC) && fflag && !nflag) {
			continue;
		} else if (!(shdr.sh_flags & SHF_ALLOC) && !nflag) {
			continue;
		} else if ((shdr.sh_flags & SHF_ALLOC) &&
		    !(shdr.sh_flags & SHF_WRITE) &&
		    (shdr.sh_type != SHT_NOBITS) && !fflag && !nflag) {
			first += shdr.sh_size;
		} else if ((shdr.sh_flags & SHF_ALLOC) &&
		    (shdr.sh_flags & SHF_WRITE) &&
		    (shdr.sh_type != SHT_NOBITS) && !fflag && !nflag) {
			second += shdr.sh_size;
		} else if ((shdr.sh_flags & SHF_WRITE) &&
		    (shdr.sh_type == SHT_NOBITS) && !fflag && !nflag) {
			third += shdr.sh_size;
		}

		if (fflag || nflag) {
			char	*name;

			name = elf_strptr(elf, shstrndx, shdr.sh_name);

			size += shdr.sh_size;
			if (notfirst) {
				(void) printf(" + ");
			}
			(void) printf(prusect[numbase], shdr.sh_size);
			(void) printf("(%s)", name);
		}
		notfirst++;
	}
	if ((fflag || nflag) && (shdrnum > 0)) {
		(void) printf(prusum[numbase], size);
	}

	if (!fflag && !nflag) {
		totsize = first + second + third;
		(void) printf(format[numbase], first, second, third, totsize);
	}

	if (Fflag) {
		if (phdrnum) {
			process_phdr(elf, phdrnum);
			return;
		} else {
			error(fname, "no segment data");
			return;
		}
	}
}

/*
 * If there are program headers, process any loadable esegments.  In the default
 * case, the first number is the file size of all non-writable segments of type
 * PT_LOAD; the second number is the file size of all writable segments of type
 * PT_LOAD; the third number is the memory size minus the file size of all
 * writable segments of type PT_LOAD.
 * If the -F flag is set, size will print the memory size of each loadable
 * segment, followed by its permission flags.
 * If -n is set, size will print the memory size of all loadable segments and
 * the file size of all non-loadable segments, followed by their permission
 * flags.
 */
static void
process_phdr(Elf *elf, GElf_Half num)
{
	int		i, notfirst = 0;
	GElf_Phdr	p;
	GElf_Xword	memsize, total, First, Second, Third, Totsize;

	memsize = total = 0;
	First = Second = Third = Totsize = 0;

	if (is_archive) {
		(void) printf("%s[%s]: ", archive, fname);
	} else if (!oneflag && !is_archive) {
		(void) printf("%s: ", fname);
	}

	for (i = 0; i < (int)num; i++) {
		if (gelf_getphdr(elf, i, &p) == 0) {
			error(fname, "no segment data");
			return;
		}
		if (!(p.p_flags & PF_W) && (p.p_type == PT_LOAD) && !Fflag) {
			First += p.p_filesz;
		} else if ((p.p_flags & PF_W) && (p.p_type == PT_LOAD) &&
		    !Fflag) {
			Second += p.p_filesz;
			Third += p.p_memsz;
		}
		memsize += p.p_memsz;
		if ((p.p_type == PT_LOAD) && nflag) {
			if (notfirst) {
				(void) printf(" + ");
			}
			(void) printf(prusect[numbase], p.p_memsz);
			total += p.p_memsz;
			notfirst++;
		}
		if ((p.p_type != PT_LOAD) && nflag) {
			if (notfirst) {
				(void) printf(" + ");
			}
			(void) printf(prusect[numbase], p.p_filesz);
			total += p.p_filesz;
			notfirst++;
		}
		if ((p.p_type == PT_LOAD) && Fflag && !nflag) {
			if (notfirst) {
				(void) printf(" + ");
			}
			(void) printf(prusect[numbase], p.p_memsz);
			notfirst++;
		}
		if (Fflag && !nflag && (p.p_type != PT_LOAD)) {
			continue;
		}
		if (Fflag || nflag) {
			switch (p.p_flags) {
			case 0:
				(void) printf("(---)");
				break;
			case PF_X:
				(void) printf("(--x)");
				break;
			case PF_W:
				(void) printf("(-w-)");
				break;
			case PF_W + PF_X:
				(void) printf("(-wx)");
				break;
			case PF_R:
				(void) printf("(r--)");
				break;
			case PF_R + PF_X:
				(void) printf("(r-x)");
				break;
			case PF_R + PF_W:
				(void) printf("(rw-)");
				break;
			case PF_R + PF_W + PF_X:
				(void) printf("(rwx)");
				break;
			default:
				(void) printf("flags(%#x)", p.p_flags);
			}
		}
	}
	if (nflag) {
		(void) printf(prusum[numbase], total);
	}
	if (Fflag && !nflag) {
		(void) printf(prusum[numbase], memsize);
	}
	if (!Fflag && !nflag) {
		Totsize = First + Second + (Third - Second);
		(void) printf(format[numbase], First, Second,
		    Third - Second, Totsize);
	}
}
