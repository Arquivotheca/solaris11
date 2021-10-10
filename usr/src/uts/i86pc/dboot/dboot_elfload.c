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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <sys/types.h>
#include <sys/inttypes.h>
#include <sys/systm.h>
#include <sys/elf.h>
#include <sys/elf_notes.h>

#include <util/memcpy.h>

#include "dboot_xboot.h"
#include "dboot_elfload.h"
#include "dboot_printf.h"

static caddr_t elf_file = 0;

#define	PGETBYTES(offset)	((void *)(elf_file + (offset)))

static void *
getehdr(void)
{
	uchar_t *ident;
	void *hdr = NULL;

	ident = PGETBYTES(0);
	if (ident == NULL)
		dboot_panic("Cannot read kernel ELF header");

	if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
	    ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3)
		dboot_panic("not an ELF file!");

	if (ident[EI_CLASS] == ELFCLASS32)
		hdr = PGETBYTES(0);
	else if (ident[EI_CLASS] == ELFCLASS64)
		hdr = PGETBYTES(0);
	else
		dboot_panic("Unknown ELF class");

	return (hdr);
}


/*
 * parse the elf file for program information
 */
int
dboot_elfload64(uintptr_t file_image)
{
	Elf64_Ehdr *eh;
	Elf64_Phdr *phdr;
	Elf64_Shdr *shdr;
	caddr_t allphdrs, sechdrs;
	int i;
	void *src[2];
	void *src_va[2];
	size_t src_sz[2] = { 0, 0 };
	paddr_t dst;
	paddr_t next_addr;
	void *bss_addr;
	size_t bss_sz = 0;
	int idx;

	elf_file = (caddr_t)file_image;

	allphdrs = NULL;

	eh = getehdr();
	if (eh == NULL)
		dboot_panic("getehdr() failed");

	if (eh->e_type != ET_EXEC)
		dboot_panic("not ET_EXEC, e_type = 0x%x", eh->e_type);

	if (eh->e_phnum == 0 || eh->e_phoff == 0)
		dboot_panic("no program headers");

	/*
	 * Get the program headers.
	 */
	allphdrs = PGETBYTES(eh->e_phoff);
	if (allphdrs == NULL)
		dboot_panic("Failed to get program headers e_phnum = %d",
		    eh->e_phnum);

	/*
	 * Get the section headers.
	 */
	sechdrs = PGETBYTES(eh->e_shoff);
	if (sechdrs == NULL)
		dboot_panic("Failed to get section headers e_shnum = %d",
		    eh->e_shnum);

	/*
	 * Next look for interesting program headers.
	 */
	for (i = 0; i < eh->e_phnum; i++) {
		/*LINTED [ELF program header alignment]*/
		phdr = (Elf64_Phdr *)(allphdrs + eh->e_phentsize * i);

		/*
		 * Dynamically-linked executable.
		 * Complain.
		 */
		if (phdr->p_type == PT_INTERP) {
			dboot_printf("warning: PT_INTERP section\n");
			continue;
		}

		/*
		 * at this point we only care about PT_LOAD segments
		 */
		if (phdr->p_type != PT_LOAD)
			continue;

		if (phdr->p_flags == (PF_R | PF_W) && phdr->p_vaddr == 0) {
			dboot_printf("warning: krtld reloc info?\n");
			continue;
		}

		/*
		 * If memory size is zero just ignore this header.
		 */
		if (phdr->p_memsz == 0)
			continue;

		/*
		 * If load address 1:1 then ignore this header.
		 */
		if (phdr->p_paddr == phdr->p_vaddr) {
			if (prom_debug)
				dboot_printf("Skipping PT_LOAD segment for "
				    "paddr = 0x%lx\n", (ulong_t)phdr->p_paddr);
			continue;
		}

		/*
		 * copy the data to kernel area
		 */
		if (phdr->p_paddr != FOUR_MEG && phdr->p_paddr != 2 * FOUR_MEG)
			dboot_panic("Bad paddr for kernel nucleus segment");
		/*
		 * Sanity-check the size of the segment.  The .text and .data
		 * are both loaded at LOWER addresses than the area in which
		 * dboot (and the ELF headers) are located.  No overlap is
		 * permitted between the data segment (starting at 8M) and the
		 * dboot load area (12M).
		 */
		if (phdr->p_filesz > FOUR_MEG ||
		    (phdr->p_paddr + phdr->p_filesz) >= (3 * FOUR_MEG)) {
			dboot_panic("unix ELF section too large to load!");
		}

		idx = (phdr->p_paddr == FOUR_MEG) ? 0 : 1;
		src[idx] = (void *)(uintptr_t)PGETBYTES(phdr->p_offset);
		src_sz[idx] = (size_t)phdr->p_filesz;
		src_va[idx] = (void *)(uintptr_t)phdr->p_vaddr;

		next_addr = phdr->p_paddr + phdr->p_filesz;
	}


	/*
	 * Next look for bss
	 */
	for (i = 0; i < eh->e_shnum; i++) {
		shdr = (Elf64_Shdr *)(sechdrs + eh->e_shentsize * i);

		/* zero out bss */
		if (shdr->sh_type == SHT_NOBITS) {
			bss_addr = (void *)(uintptr_t)next_addr;
			bss_sz = shdr->sh_size;

			/*
			 * Sanity-check BSS size.  BSS follows the .data
			 * segment in the 8M-12M region.  If BSS would
			 * overwrite the dboot load area (12M), panic.
			 */
			if (((uintptr_t)bss_addr + bss_sz) >= (3 * FOUR_MEG))
				dboot_panic("bss too large!");
			break;
		}
	}

	/*
	 * Now that we're done with the elf headers, copy kernel .text and
	 * .data into place (since they might overwrite the elf headers
	 * loaded before 0xc00000).
	 */

	for (i = 0; i < 2; i++) {
		if (src_sz[i] == 0)
			continue;
		dst = (i == 0) ? FOUR_MEG : (2 * FOUR_MEG);
		if (prom_debug)
			dboot_printf("copying 0x%lx bytes from 0x%lx "
			    "to physaddr 0x%lx (va=0x%lx)\n",
			    (ulong_t)src_sz[i], (ulong_t)src[i],
			    (ulong_t)dst, (ulong_t)src_va[i]);
		(void) memcpy((void *)(uintptr_t)dst, src[i],
		    src_sz[i]);
	}

	/*
	 * Finally, zero-out the BSS
	 */
	if (bss_sz > 0) {
		if (prom_debug)
			dboot_printf("zeroing bss @ 0x%p size 0x%lx\n",
			    bss_addr, (ulong_t)bss_sz);
		(void) memset(bss_addr, 0, bss_sz);
	}

	/*
	 * Ignore the intepreter (or should we die if there is one??)
	 */
	return (0);
}
