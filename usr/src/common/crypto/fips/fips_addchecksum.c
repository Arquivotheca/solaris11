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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include	<ctype.h>
#include	<unistd.h>
#include	<sys/types.h>
#include	<fcntl.h>
#include	<stdio.h>
#include	<libelf.h>
#include	<gelf.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sha1.h>
#include	<sys/elf_SPARC.h>
#include	<fips/fips_checksum.h>


#define	FAIL_EXIT		\
	(void) fprintf(stderr, "failure at line %d\n", __LINE__);	\
	return (-1)

static const char fips_section_name[] = ".SUNW_fips";

static int
add_fips_section(int fd)
{
	GElf_Ehdr	ehdr;
	GElf_Shdr	section;
	Elf		*elf;
	Elf_Scn		*scn = NULL, *shstrtab_scn, *fips_scn = NULL;
	Elf_Data	*shstrtab_data;
	Elf_Data	*sdata;
	unsigned int    old_size, new_size;
	char		*sname, *newbuf;

	/* Obtain the ELF descriptor */
	(void) elf_version(EV_CURRENT);
	if ((elf = elf_begin(fd, ELF_C_RDWR, NULL)) == NULL) {
		FAIL_EXIT;
	}

	if ((gelf_getehdr(elf, &ehdr)) == NULL) {
		FAIL_EXIT;
	} else if ((shstrtab_scn = elf_getscn(elf, ehdr.e_shstrndx)) ==
	    NULL) {
		FAIL_EXIT;
	} else if ((shstrtab_data = elf_getdata(shstrtab_scn, NULL)) == NULL) {
		FAIL_EXIT;
	}

	/* Traverse input file to see if the fips section already exists */
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if ((gelf_getshdr(scn, &section)) == NULL) {
			FAIL_EXIT;
		}
		sname = (char *)shstrtab_data->d_buf + section.sh_name;
		if (strcmp(sname, fips_section_name) == 0) {
			/*
			 * If the fips section already exists, make sure that
			 * the section is large enough.
			 */
			fips_scn = scn;
			if ((sdata = elf_getdata(scn, NULL)) == NULL) {
				FAIL_EXIT;
			}
			if (sdata->d_size < SHA1_DIGEST_LENGTH) {
				newbuf = malloc(SHA1_DIGEST_LENGTH);
				sdata->d_size = SHA1_DIGEST_LENGTH;
				sdata->d_buf = newbuf;
			}
			(void) elf_flagdata(sdata, ELF_C_SET, ELF_F_DIRTY);
			(void) elf_flagscn(scn, ELF_C_SET, ELF_F_DIRTY);
			(void) elf_flagelf(elf, ELF_C_SET, ELF_F_DIRTY);
		}
	}

	/* If the fips section does not exist, allocate the section.  */
	if (fips_scn == NULL) {
		GElf_Shdr	shdr;

		/* add the section name at the end of the .shstrtab section */
		old_size = shstrtab_data->d_size;
		new_size = old_size + strlen(fips_section_name) + 1;
		if ((newbuf = malloc(new_size)) == NULL) {
			FAIL_EXIT;
		}

		(void) memcpy(newbuf, shstrtab_data->d_buf, old_size);
		(void) strlcpy(newbuf + old_size, fips_section_name,
		    new_size - old_size);
		shstrtab_data->d_buf = newbuf;
		shstrtab_data->d_size = new_size;
		shstrtab_data->d_align = 1;
		if ((fips_scn = elf_newscn(elf)) == 0) {
			FAIL_EXIT;
		}

		/* Initialize the fips section */
		if (gelf_getshdr(fips_scn, &shdr) == NULL) {
			FAIL_EXIT;
		}
		/*
		 * sh_name is the starting position of the name
		 * within the shstrtab_data->d_buf buffer
		 */
		shdr.sh_name = old_size;
		shdr.sh_type = SHT_SUNW_SIGNATURE;
		shdr.sh_flags = SHF_EXCLUDE;
		shdr.sh_addr = 0;
		shdr.sh_link = 0;
		shdr.sh_info = 0;
		shdr.sh_size = 0;
		shdr.sh_offset = 0;
		shdr.sh_addralign = 1;

		if (gelf_update_shdr(fips_scn, &shdr) == 0) {
			FAIL_EXIT;
		}

		if ((sdata = elf_newdata(fips_scn)) == NULL) {
			FAIL_EXIT;
		}
		if (sdata->d_size < SHA1_DIGEST_LENGTH) {
			newbuf = malloc(SHA1_DIGEST_LENGTH);
			sdata->d_size = SHA1_DIGEST_LENGTH;
			sdata->d_buf = newbuf;
		}
		(void) elf_flagdata(sdata, ELF_C_SET, ELF_F_DIRTY);
		(void) elf_flagscn(fips_scn, ELF_C_SET, ELF_F_DIRTY);
		(void) elf_flagelf(elf, ELF_C_SET, ELF_F_DIRTY);
	}
	(void) elf_update(elf, ELF_C_WRITE);
	(void) elf_end(elf);
	(void) close(fd);

	return (0);
}

int
main(int argc, char **argv)
{
	GElf_Ehdr	ehdr;
	GElf_Shdr	section;
	Elf		*elf;
	Elf_Scn		*scn = NULL, *shstrtab_scn;
	Elf_Data	*shstrtab_data, *sdata;
	int		fd;
	unsigned int    size, i;
	char		sha1buf[SHA1_DIGEST_LENGTH];
	char		*sname, *newbuf;

	if (argc != 2) {
		(void) fprintf(stderr, "Usage: %s [filename]\n", argv[0]);
		return (-1);
	}

	/* Open the driver file */
	if ((fd = open(argv[1], O_RDWR)) == -1) {
		goto errorexit;
	}

	/* Read the ELF header */
	size = sizeof (ehdr);
	if (fips_read_file(fd, (char *)(&ehdr), size, 0) < 0) {
		goto errorexit;
	}

	/* check if it is an ELF file */
	for (i = 0; i < SELFMAG; i++) {
		if (ehdr.e_ident[i] != ELFMAG[i]) {
			(void) fprintf(stderr, "%s not an elf file\n", argv[1]);
			goto errorexit;
		}
	}

	if (add_fips_section(fd) < 0) { /* closes fd on success */
		goto errorexit;
	}

	if ((fd = open(argv[1], O_RDWR)) == -1) {
		FAIL_EXIT;
	}

	/* calculate the file checksum */
	if (fips_calc_checksum(fd, sha1buf) < 0) {
		goto errorexit;
	}

	(void) elf_version(EV_CURRENT);
	if ((elf = elf_begin(fd, ELF_C_RDWR, NULL)) == NULL) {
		goto errorexit;
	}

	if ((gelf_getehdr(elf, &ehdr)) == NULL) {
		goto errorexit;
	} else if ((shstrtab_scn = elf_getscn(elf, ehdr.e_shstrndx)) ==
	    NULL) {
		goto errorexit;
	} else if ((shstrtab_data = elf_getdata(shstrtab_scn, NULL)) == NULL) {
		goto errorexit;
	}

	/* Add the checksum to the fips section */
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if ((gelf_getshdr(scn, &section)) == NULL) {
			goto errorexit;
		}

		sname = (char *)shstrtab_data->d_buf + section.sh_name;
		if (strcmp(sname, fips_section_name) == 0) {
			if ((sdata = elf_getdata(scn, NULL)) == NULL) {
				goto errorexit;
			}

			newbuf = sdata->d_buf;
			(void) memcpy(newbuf, sha1buf, SHA1_DIGEST_LENGTH);
			(void) elf_flagdata(sdata, ELF_C_SET, ELF_F_DIRTY);
			(void) elf_flagscn(scn, ELF_C_SET, ELF_F_DIRTY);
			(void) elf_flagelf(elf, ELF_C_SET, ELF_F_DIRTY);
		}
	}
	(void) elf_update(elf, ELF_C_WRITE);
	(void) elf_end(elf);
	(void) close(fd);

	return (0);

errorexit:

	(void) close(fd);

	FAIL_EXIT;
}
