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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains the functions responsible for opening the output file
 * image, associating the appropriate input elf structures with the new image,
 * and obtaining new elf structures to define the new image.
 */
#include	<stdio.h>
#include	<sys/stat.h>
#include	<fcntl.h>
#include	<link.h>
#include	<errno.h>
#include	<string.h>
#include	<limits.h>
#include	<debug.h>
#include	<unistd.h>
#include	"msg.h"
#include	"_libld.h"

/*
 * Determine a least common multiplier.  Input sections contain an alignment
 * requirement, which elf_update() uses to insure that the section is aligned
 * correctly off of the base of the elf image.  We must also insure that the
 * sections mapping is congruent with this alignment requirement.  For each
 * input section associated with a loadable segment determine whether the
 * segments alignment must be adjusted to compensate for a sections alignment
 * requirements.
 */
Xword
ld_lcm(Xword a, Xword b)
{
	Xword	_r, _a, _b;

	if ((_a = a) == 0)
		return (b);
	if ((_b = b) == 0)
		return (a);

	if (_a > _b)
		_a = b, _b = a;
	while ((_r = _b % _a) != 0)
		_b = _a, _a = _r;
	return ((a / _a) * b);
}

/*
 * Open the output file and insure the correct access modes.
 */
uintptr_t
ld_open_outfile(Ofl_desc *ofl)
{
	mode_t		mode;
	int		flags, err, fd;
	struct stat	status;
	const char	*name = ofl->ofl_name;
	char		rname[PATH_MAX];

	/*
	 * Determine the required file mode from the type of output file we
	 * are creating.
	 */
	mode = (ofl->ofl_flags & (FLG_OF_EXEC | FLG_OF_SHAROBJ)) ? 0777 : 0666;

retry:
	/*
	 * Establish the flags for the output file.  If the file exists and is
	 * a regular file, then the file is first unlinked to ensure that the
	 * new file receives a new inode.  Then, a new output file is created.
	 *
	 * The O_CREAT and O_EXCL flags ensure that the window between a stat()
	 * of the file, and creating the new file, only allows one process to
	 * create the output file.  This model catches parallel build problems,
	 * where two invocations of the same link-edit can result in opening
	 * and updating the same file.  Without the O_EXCL flag, this scenario
	 * has resulted in a spinning link-edit, where the update of the new
	 * output file hash table has been compromised by two processes updating
	 * the same chain entries (update_osym()).
	 */
	flags = O_RDWR | O_CREAT | O_TRUNC | O_EXCL;

	/*
	 * Determine if the output file is a symbolic link.
	 */
	if ((lstat(name, &status) == 0) &&
	    ((status.st_mode & S_IFMT) == S_IFLNK)) {
		int	len;

		errno = 0;

		/*
		 * Resolve all symbolic links in the path.
		 */
		if ((len = resolvepath(name, rname, (PATH_MAX - 1))) != -1) {
			/*
			 * If the file exists, use the resolved path instead of
			 * the original.
			 */
			rname[len] = '\0';
			name = rname;
			errno = 0;

		} else if (errno == ENOENT) {
			/*
			 * The file doesn't exist, so we need to determine the
			 * final destination name.
			 *
			 * resolvepath() sets ENOENT if any path component is a
			 * dangling symlink.  However, success of the outer
			 * lstat() tells us that every path component of 'name'
			 * is present and accessible.  Hence, we know that this
			 * path component is the head of a chain of 1 or more
			 * symlinks, the final one of which is dangling.
			 *
			 * Determining the destination name could be solved by
			 * using repeated calls to readlink(2), but it is
			 * simpler, and more accurate to simply create the
			 * missing file, and then call resolvepath() again.
			 */
			if ((fd = open(name, O_RDWR | O_CREAT, mode)) >= 0) {
				(void) close(fd);
				if ((len = resolvepath(name, rname,
				    (PATH_MAX - 1))) != -1) {
					rname[len] = '\0';
					name = rname;
					errno = 0;
				}
			}
		}
		if (errno) {
			err = errno;
			ld_eprintf(ofl, ERR_FATAL, MSG_INTL(MSG_SYS_OPEN),
			    ofl->ofl_name, strerror(err));
			return (S_ERROR);
		}
	}

	/* Determine whether the output file already exists */
	if (stat(name, &status) == 0) {
		if ((status.st_mode & S_IFMT) != S_IFREG) {
			/*
			 * The file is not a regular file.  Don't delete the
			 * file, and ensure that there's no attempt to delete
			 * the file should the link-edit fail.  This allows
			 * root users to specify /dev/null as the output file
			 * for verification links.
			 */
			ofl->ofl_flags1 |= FLG_OF1_NONREG;
			flags = O_RDWR;
		} else {
			/*
			 * It's a regular file, so unlink it. In standard
			 * Unix fashion, the old file will continue to
			 * exist until its link count drops to 0 and no
			 * process has the file open. In the meantime, we
			 * create a new file (inode) under the same name,
			 * available for new use.
			 *
			 * The advantage of this policy is that creating
			 * a new executable or shared object does not
			 * corrupt existing processes using the old file.
			 * A possible disadvantage is that if the existing
			 * file has a (link_count > 1), the other names will
			 * continue to reference the old inode, thus
			 * breaking the link.
			 *
			 * A subtlety here is that POSIX says we are not
			 * supposed to replace a non-writable file, which
			 * is something that unlink() is happy to do. The
			 * only 100% reliable test against this is to open
			 * the file for non-destructive write access. If the
			 * open succeeds, we are clear to unlink it, and if
			 * not, then the error generated is the error we
			 * need to report.
			 */
			if ((fd = open(name, O_RDWR, mode)) < 0) {
				if ((err = errno) != ENOENT) {
					ld_eprintf(ofl, ERR_FATAL,
					    MSG_INTL(MSG_SYS_OPEN),
					    ofl->ofl_name, strerror(err));
					return (S_ERROR);
				}
			} else
				(void) close(fd);

			if ((unlink(name) == -1) && (errno != ENOENT)) {
				err = errno;
				ld_eprintf(ofl, ERR_FATAL,
				    MSG_INTL(MSG_SYS_UNLINK),
				    ofl->ofl_name, strerror(err));
				return (S_ERROR);
			}
		}
	}

	/*
	 * Open (or create) the output file name (ofl_fd acts as a global
	 * flag to ldexit() signifying whether the output file should be
	 * removed or not on error).
	 */
	if ((ofl->ofl_fd = open(name, flags, mode)) < 0) {
		err = errno;

		/*
		 * If we're expecting to create a new file, and one already
		 * exists, we might have two identical ld() invocations, which
		 * might be the result of a parallel build issue.  Whether this
		 * is the issue or not, we are now back to the same situation
		 * as we were on entering this routine: exclusive access to
		 * the output file (inode) is required, and an existing file is
		 * in the way.
		 *
		 * To recover, we retry this function, and in an attempt to
		 * help diagnose any build problems, a warning is generated.
		 */
		if ((flags & O_CREAT) && (err == EEXIST)) {
			ld_eprintf(ofl, ERR_WARNING,
			    MSG_INTL(MSG_SYS_CREATE), ofl->ofl_name,
			    strerror(err));
			if ((ofl->ofl_flags & FLG_OF_FATAL) == 0)
				goto retry;
		} else {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_SYS_OPEN), ofl->ofl_name,
			    strerror(err));
		}
		return (S_ERROR);
	}

	/*
	 * Having successfully opened the file, retain the resolved name for
	 * use if the file needs to be deleted due to a later error.  Using the
	 * original name runs the risk of deleting a symlink rather than the
	 * file the symlink points to.
	 */
	if ((ofl->ofl_rname = ofl->ofl_name) != name) {
		size_t	len = strlen(name) + 1;
		char	*nname;

		if ((nname = libld_malloc(len)) == 0)
			return (S_ERROR);
		(void) strcpy(nname, name);
		ofl->ofl_rname = nname;
	}

	return (1);
}

/*
 * If we are creating a memory model we need to update the present memory image.
 * Use elf_update(ELF_C_NULL) to calculate the offset of each section and their
 * associated data buffers.  From this information determine what padding is
 * required.
 *
 * Two actions are necessary to convert the present disc image into a memory
 * image:
 *
 *  -	Loadable segments must be padded so that the next segment virtual
 *	address and file offset are the same.
 *
 *  -	NOBITS sections must be converted into allocated, null filled sections.
 */
static uintptr_t
pad_outfile(Ofl_desc *ofl)
{
	Aliste		idx1;
	off_t		offset;
	Elf_Scn		*oscn = 0;
	Sg_desc		*sgp;
	Ehdr		*ehdr;

	/*
	 * Update all the elf structures.  This will assign offsets to the
	 * section headers and data buffers as they relate to the new image.
	 */
	if (elf_update(ofl->ofl_welf, ELF_C_NULL) == -1) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_UPDATE),
		    ofl->ofl_name);
		return (S_ERROR);
	}
	if ((ehdr = elf_getehdr(ofl->ofl_welf)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_GETEHDR),
		    ofl->ofl_name);
		return (S_ERROR);
	}

	/*
	 * Initialize the offset by skipping the Elf header and program
	 * headers.
	 */
	offset = ehdr->e_phoff + (ehdr->e_phnum * ehdr->e_phentsize);

	/*
	 * Traverse the segment list looking for loadable segments.
	 */
	for (APLIST_TRAVERSE(ofl->ofl_segs, idx1, sgp)) {
		Phdr	*phdr = &(sgp->sg_phdr);
		Os_desc	*osp;
		Aliste	idx2;

		/*
		 * If we've already processed a loadable segment, the `scn'
		 * variable will be initialized to the last section that was
		 * part of that segment.  Add sufficient padding to this section
		 * to cause the next segments virtual address and file offset to
		 * be the same.
		 */
		if (oscn && (phdr->p_type == PT_LOAD)) {
			Elf_Data *	data;
			size_t 		size;

			size = (size_t)(S_ROUND(offset, phdr->p_align) -
			    offset);

			if ((data = elf_newdata(oscn)) == NULL) {
				ld_eprintf(ofl, ERR_ELF,
				    MSG_INTL(MSG_ELF_NEWDATA), ofl->ofl_name);
				return (S_ERROR);
			}
			if ((data->d_buf = libld_calloc(size, 1)) == 0)
				return (S_ERROR);

			data->d_type = ELF_T_BYTE;
			data->d_size = size;
			data->d_align = 1;
			data->d_version = ofl->ofl_dehdr->e_version;
		}

		/*
		 * Traverse the output sections for this segment calculating the
		 * offset of each section. Retain the final section descriptor
		 * as this will be where any padding buffer will be added.
		 */
		for (APLIST_TRAVERSE(sgp->sg_osdescs, idx2, osp)) {
			Shdr	*shdr = osp->os_shdr;

			offset = (off_t)S_ROUND(offset, shdr->sh_addralign);
			offset += shdr->sh_size;

			/*
			 * If this is a NOBITS output section convert all of
			 * its associated input sections into real, null filled,
			 * data buffers, and change the section to PROGBITS.
			 */
			if (shdr->sh_type == SHT_NOBITS)
				shdr->sh_type = SHT_PROGBITS;
		}

		/*
		 * If this is a loadable segment retain the last output section
		 * descriptor.  This acts both as a flag that a loadable
		 * segment has been seen, and as the segment to which a padding
		 * buffer will be added.
		 */
		if (phdr->p_type == PT_LOAD)
			oscn = osp->os_scn;
	}
	return (1);
}

/*
 * Create an output section.  The first instance of an input section triggers
 * the creation of a new output section.
 */
static uintptr_t
create_outsec(Ofl_desc *ofl, Sg_desc *sgp, Os_desc *osp, Word ptype, int shidx,
    Boolean fixalign)
{
	Elf_Scn	*scn;
	Shdr	*shdr;

	/*
	 * Get a section descriptor for the section.
	 */
	if ((scn = elf_newscn(ofl->ofl_welf)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_NEWSCN),
		    ofl->ofl_name);
		return (S_ERROR);
	}
	osp->os_scn = scn;

	/*
	 * Get a new section header table entry and copy the pertinent
	 * information from the in-core descriptor.
	 */
	if ((shdr = elf_getshdr(scn)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_GETSHDR),
		    ofl->ofl_name);
		return (S_ERROR);
	}
	*shdr = *(osp->os_shdr);
	osp->os_shdr = shdr;

	/*
	 * If this is the first section within a loadable segment, and the
	 * alignment needs to be updated, record this section.
	 */
	if ((fixalign == TRUE) && (ptype == PT_LOAD) && (shidx == 1))
		sgp->sg_fscn = scn;

	/*
	 * If not building a relocatable object, remove any of the
	 * following flags, as they have been acted upon and are not
	 * meaningful in the output:
	 *	SHF_ORDERED, SHF_LINK_ORDER, SHF_GROUP
	 * For relocatable objects, we allow them to propagate to
	 * the output object to be handled by the next linker that
	 * sees them.
	 */
	if ((ofl->ofl_flags & FLG_OF_RELOBJ) == 0)
		osp->os_shdr->sh_flags &= ~(ALL_SHF_ORDER|SHF_GROUP);

	/*
	 * If this is a TLS section, save it so that the PT_TLS program header
	 * information can be established after the output image has been
	 * initially created.  At this point, all TLS input sections are ordered
	 * as they will appear in the output image.
	 */
	if ((ofl->ofl_flags & FLG_OF_TLSPHDR) &&
	    (osp->os_shdr->sh_flags & SHF_TLS) &&
	    (aplist_append(&ofl->ofl_ostlsseg, osp,
	    AL_CNT_OFL_OSTLSSEG) == NULL))
		return (S_ERROR);

	return (0);
}

/*
 * Special processing to realign empty sections that might exist at the
 * beginning of a segment.  Although this is a generic section update, NOBITS
 * sections have been the trigger for this alignment manipulation.
 *
 * NOBITS sections may have large alignment requirements to provide for copy
 * relocation data.  NOBITS sections may also be assigned to their own segment
 * using a mapfile.  TLS sections, .tdata and .tbss, are collected before .bss,
 * and thus might lead any bss segment.  If the .tdata section has no content,
 * the sections existence can disrupt the required offset and alignment of the
 * .bss section.  This can result in invalid symbol offsets being computed for
 * .bss symbols.
 *
 * If a section of size is found, and the preceding sections within the same
 * segment have no size, then realign these preceding sections to the alignment
 * of the section that has size.  This ensures that libelf, which is not aware
 * of segments or virtual addresses, will assign the same offset to the initial
 * sections without size as the first section that has size.  This in turn
 * ensures that the assignment of virtual addresses to symbols within the
 * section that has size will start at the virtual address assigned to the
 * segment.
 */
static void
realign_sections(Sg_desc *sgp, Aliste idx, Xword align)
{
	Aliste	idx1;
	Os_desc	*osp;

	/*
	 * Traverse the output sections for this segment.
	 */
	for (APLIST_TRAVERSE(sgp->sg_osdescs, idx1, osp)) {
		Aliste	idx2;
		Is_desc	*isp;
		int	os_isdescs_idx;

		/*
		 * Only process sections that precede the supplied index.
		 */
		if (idx1 == idx)
			return;

		/*
		 * Obtain the first input section for this section, and align
		 * its data with the supplied alignment.
		 */
#pragma error_messages(off, E_END_OF_LOOP_CODE_NOT_REACHED)
		OS_ISDESCS_TRAVERSE(os_isdescs_idx, osp, idx2, isp) {
			isp->is_indata->d_align = align;

			/*
			 * Having found the first input section, we're done.
			 * Use a "goto" to break out of the two "for ()" loops
			 * defined by the OS_ISDESCS_TRAVERSE macro.  The
			 * error_messages pragma suppresses a compiler warning
			 * regarding not reaching the end of the "foo ()" loop.
			 */
			goto break_os_isdescs_traverse;
		}
#pragma error_messages(on, E_END_OF_LOOP_CODE_NOT_REACHED)
break_os_isdescs_traverse:
		;
	}
}

/*
 * Create the elf structures that allow the input data to be associated with the
 * new image:
 *
 *  -	define the new elf image using elf_begin(),
 *
 *  -	obtain an elf header for the image,
 *
 *  -	traverse the input segments and create a program header array to define
 *	the required segments,
 *
 *  -	traverse the output sections for each segment assigning a new section
 *	descriptor and section header for each,
 *
 *  -	traverse the input sections associated with each output section and
 *	assign a new data descriptor to each (each output section becomes a
 *	linked list of input data buffers).
 */
uintptr_t
ld_create_outfile(Ofl_desc *ofl)
{
	Sg_desc		*sgp;
	Os_desc		*osp;
	Is_desc		*isp;
	Elf_Data	*tlsdata = 0;
	Aliste		idx1;
	ofl_flag_t	flags = ofl->ofl_flags;
	ofl_flag_t	flags1 = ofl->ofl_flags1;
	size_t		ndx;
	Elf_Cmd		cmd;
	Boolean		fixalign = FALSE;
	int		fd, nseg = 0, shidx, dataidx, ptloadidx = 0;

	DBG_CALL(Dbg_basic_create(ofl->ofl_lml));

	/*
	 * If DF_1_NOHDR or FLG_OF1_VADDR were set,
	 * we need to do alignment adjustment.
	 */
	if ((flags1 & FLG_OF1_VADDR) ||
	    (ofl->ofl_dtflags_1 & DF_1_NOHDR)) {
		fixalign = TRUE;
	}

	if (flags1 & FLG_OF1_MEMORY) {
		fd = 0;
		cmd = ELF_C_IMAGE;
	} else {
		fd = ofl->ofl_fd;
		cmd = ELF_C_WRITE;
	}

	/*
	 * If there are any ordered sections, handle them here.
	 */
	if ((ofl->ofl_ordered != NULL) &&
	    (ld_sort_ordered(ofl) == S_ERROR))
		return (S_ERROR);

	/*
	 * Tell the access library about our new temporary file.
	 */
	if ((ofl->ofl_welf = elf_begin(fd, cmd, 0)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_BEGIN),
		    ofl->ofl_name);
		return (S_ERROR);
	}

	/*
	 * Obtain a new Elf header.
	 */
	if ((ofl->ofl_nehdr = elf_newehdr(ofl->ofl_welf)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_NEWEHDR),
		    ofl->ofl_name);
		return (S_ERROR);
	}
	ofl->ofl_nehdr->e_machine = ofl->ofl_dehdr->e_machine;

	DBG_CALL(Dbg_util_nl(ofl->ofl_lml, DBG_NL_STD));
	for (APLIST_TRAVERSE(ofl->ofl_segs, idx1, sgp)) {
		int	frst = 0, checkalign;
		Phdr	*phdr = &(sgp->sg_phdr);
		Word	ptype = phdr->p_type;
		Aliste	idx2;
		size_t	secsize, segsize;

		/*
		 * Count the number of segments that will go in the program
		 * header table. If a segment is empty, ignore it.
		 */
		if (!(flags & FLG_OF_RELOBJ)) {
			/*
			 * If the program header type belongs to the os range,
			 * the resulting object is ELFOSABI_SOLARIS.
			 */
			if ((ptype >= PT_LOOS) && (ptype <= PT_HIOS))
				ofl->ofl_flags |= FLG_OF_OSABI;

			if (ptype == PT_PHDR) {
				/*
				 * If we are generating an interp section (and
				 * thus an associated PT_INTERP program header
				 * entry) also generate a PT_PHDR program header
				 * entry.  This allows the kernel to generate
				 * the appropriate aux vector entries to pass to
				 * the interpreter (refer to exec/elf/elf.c).
				 * Note that if an image was generated with an
				 * interp section, but no associated PT_PHDR
				 * program header entry, the kernel will simply
				 * pass the interpreter an open file descriptor
				 * when the image is executed).
				 */
				if (ofl->ofl_osinterp)
					nseg++;
			} else if (ptype == PT_INTERP) {
				if (ofl->ofl_osinterp)
					nseg++;
			} else if (ptype == PT_DYNAMIC) {
				if (flags & FLG_OF_DYNAMIC)
					nseg++;
			} else if (ptype == PT_TLS) {
				if (flags & FLG_OF_TLSPHDR)
					nseg++;
			} else if (ptype == PT_SUNW_UNWIND) {
				if (ofl->ofl_unwindhdr)
					nseg++;
			} else if (ptype == PT_SUNWDTRACE) {
				if (ofl->ofl_dtracesym)
					nseg++;
			} else if (ptype == PT_SUNWCAP) {
				if (ofl->ofl_oscap)
					nseg++;
			} else if (ptype == PT_SUNWSTACK) {
				if ((sgp->sg_flags & FLG_SG_DISABLED) == 0)
					nseg++;
			} else if (sgp->sg_flags & FLG_SG_EMPTY) {
					nseg++;
			} else if (sgp->sg_osdescs != NULL) {
				if ((sgp->sg_flags & FLG_SG_PHREQ) == 0) {
					/*
					 * If this is a segment for which
					 * we are not making a program header,
					 * don't increment nseg
					 */
					ptype = (sgp->sg_phdr).p_type = PT_NULL;
				} else if (ptype != PT_NULL)
					nseg++;
			}
		}

		/*
		 * Establish any processing unique to the first loadable
		 * segment.
		 */
		if ((ptype == PT_LOAD) && (ptloadidx == 0)) {
			ptloadidx++;

			/*
			 * If the first loadable segment is not supposed to
			 * include the ELF or program headers,  alignments
			 * of the following segments need to be fixed,
			 * plus a .dynamic FLAGS1 setting is required.
			 */
			if (ofl->ofl_dtflags_1 & DF_1_NOHDR)
				fixalign = TRUE;
		}

		shidx = 0;
		checkalign = 0;
		secsize = segsize = 0;

		for (APLIST_TRAVERSE(sgp->sg_osdescs, idx2, osp)) {
			Aliste	idx3;
			int	os_isdescs_idx;
			Xword	lalign = 0;

			dataidx = 0;

			OS_ISDESCS_TRAVERSE(os_isdescs_idx, osp, idx3, isp) {
				Elf_Data	*data;
				Ifl_desc	*ifl = isp->is_file;

				/*
				 * An input section in the list that has
				 * been previously marked to be discarded
				 * should be completely ignored.
				 */
				if (isp->is_flags & FLG_IS_DISCARD)
					continue;

				/*
				 * At this point we know whether a section has
				 * been referenced.  If it hasn't, and the whole
				 * file hasn't been referenced (which would have
				 * been caught in ignore_section_processing()),
				 * give a diagnostic (-D unused,detail) or
				 * discard the section if -zignore is in effect.
				 */
				if (ifl &&
				    (((ifl->ifl_flags & FLG_IF_FILEREF) == 0) ||
				    ((ptype == PT_LOAD) &&
				    ((isp->is_flags & FLG_IS_SECTREF) == 0) &&
				    (isp->is_shdr->sh_size > 0)))) {
					Lm_list	*lml = ofl->ofl_lml;

					if (ifl->ifl_flags & FLG_IF_IGNORE) {
						isp->is_flags |= FLG_IS_DISCARD;
						DBG_CALL(Dbg_unused_sec(lml,
						    isp));
						continue;
					} else {
						DBG_CALL(Dbg_unused_sec(lml,
						    isp));
					}
				}

				/*
				 * If this section provides no data, and isn't
				 * referenced, then it can be discarded as well.
				 * Note, if this is the first input section
				 * associated to an output section, let it
				 * through, there may be a legitimate reason why
				 * the user wants a null section.  Discarding
				 * additional sections is intended to remove the
				 * empty clutter the compilers have a habit of
				 * creating.  Don't provide an unused diagnostic
				 * as these sections aren't typically the users
				 * creation.
				 */
				if (ifl && dataidx &&
				    ((isp->is_flags & FLG_IS_SECTREF) == 0) &&
				    (isp->is_shdr->sh_size == 0)) {
					isp->is_flags |= FLG_IS_DISCARD;
					continue;
				}

				/*
				 * The first input section triggers the creation
				 * of the associated output section.
				 */
				if (osp->os_scn == NULL) {
					shidx++;

					if (create_outsec(ofl, sgp, osp, ptype,
					    shidx, fixalign) == S_ERROR)
						return (S_ERROR);
				}

				dataidx++;

				/*
				 * Create a new output data buffer for each
				 * input data buffer, thus linking the new
				 * buffers to the new elf output structures.
				 * Simply make the new data buffers point to
				 * the old data.
				 */
				if ((data = elf_newdata(osp->os_scn)) == NULL) {
					ld_eprintf(ofl, ERR_ELF,
					    MSG_INTL(MSG_ELF_NEWDATA),
					    ofl->ofl_name);
					return (S_ERROR);
				}
				*data = *(isp->is_indata);
				isp->is_indata = data;

				if ((fixalign == TRUE) && (ptype == PT_LOAD) &&
				    (shidx == 1) && (dataidx == 1))
					data->d_align = sgp->sg_align;

				/*
				 * Save the first TLS data buffer, as this is
				 * the start of the TLS segment. Realign this
				 * buffer based on the alignment requirements
				 * of all the TLS input sections.
				 */
				if ((flags & FLG_OF_TLSPHDR) &&
				    (isp->is_shdr->sh_flags & SHF_TLS)) {
					if (tlsdata == 0)
						tlsdata = data;
					tlsdata->d_align =
					    ld_lcm(tlsdata->d_align,
					    isp->is_shdr->sh_addralign);
				}

#if	defined(_ELF64) && defined(_ILP32)
				/*
				 * 4106312, the 32-bit ELF64 version of ld
				 * needs to be able to create large .bss
				 * sections.  The d_size member of Elf_Data
				 * only allows 32-bits in _ILP32, so we build
				 * multiple data-items that each fit into 32-
				 * bits.  libelf (4106398) can summ these up
				 * into a 64-bit quantity.  This only works
				 * for NOBITS sections which don't have any
				 * real data to maintain and don't require
				 * large file support.
				 */
				if (isp->is_shdr->sh_type == SHT_NOBITS) {
					Xword sz = isp->is_shdr->sh_size;

					while (sz >> 32) {
						data->d_size = SIZE_MAX;
						sz -= (Xword)SIZE_MAX;

						data = elf_newdata(osp->os_scn);
						if (data == NULL)
							return (S_ERROR);
					}
					data->d_size = (size_t)sz;
				}
#endif

				/*
				 * If this segment requires rounding realign the
				 * first data buffer associated with the first
				 * section.
				 */
				if ((frst++ == 0) &&
				    (sgp->sg_flags & FLG_SG_ROUND)) {
					Xword    align;

					if (data->d_align)
						align = (Xword)
						    S_ROUND(data->d_align,
						    sgp->sg_round);
					else
						align = sgp->sg_round;

					data->d_align = (size_t)align;
				}

				/*
				 * Save the largest alignment for any non-zero
				 * sized section, and keep track of the total
				 * section size.  Note, the section size need
				 * not be accurate in terms of alignment
				 * padding, as this size value is only used as
				 * a trigger for calling realign_sections().
				 */
				if (data->d_size) {
					if (data->d_align > lalign)
						lalign = data->d_align;
					secsize += data->d_size;
				}
			}

			/*
			 * If this output section has size, and there are
			 * preceding sections within this segment that have not
			 * contributed any size, realign the preceding sections.
			 */
			if (!(flags & FLG_OF_RELOBJ) && secsize && idx2 &&
			    (checkalign == 0)) {
				checkalign = 1;

				if (lalign && (segsize == 0))
					realign_sections(sgp, idx2, lalign);
			}

			/*
			 * Keep track of the total segment size.  Note, the
			 * segment size need not be accurate in terms of
			 * alignment padding as this size value is only used
			 * as a trigger for calling realign_sections().
			 */
			segsize += secsize;

			/*
			 * Clear the szoutrels counter so that it can be used
			 * again in the building of relocs.  See machrel.c.
			 */
			osp->os_szoutrels = 0;
		}
	}

	/*
	 * Did we use ELF features from the osabi range? If so,
	 * update the ELF header osabi fields. If this doesn't happen,
	 * those fields remain 0, reflecting a generic System V ELF ABI.
	 */
	if (ofl->ofl_flags & FLG_OF_OSABI) {
		ofl->ofl_nehdr->e_ident[EI_OSABI] = ELFOSABI_SOLARIS;
		ofl->ofl_nehdr->e_ident[EI_ABIVERSION] = EAV_SUNW_CURRENT;
	}

	/*
	 * Build an empty PHDR.
	 */
	if (nseg) {
		if ((ofl->ofl_phdr = elf_newphdr(ofl->ofl_welf,
		    nseg)) == NULL) {
			ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_NEWPHDR),
			    ofl->ofl_name);
			return (S_ERROR);
		}
	}

	/*
	 * If we need to generate a memory model, pad the image.
	 */
	if (flags1 & FLG_OF1_MEMORY) {
		if (pad_outfile(ofl) == S_ERROR)
			return (S_ERROR);
	}

	/*
	 * After all the basic input file processing, all data pointers are
	 * referencing two types of memory:
	 *
	 *  -	allocated memory, ie. elf structures, internal link editor
	 *	structures, and any new sections that have been created.
	 *
	 *  -	original input file mmap'ed memory, ie. the actual data
	 *	sections of the input file images.
	 *
	 * Up until now, the only memory modifications have been carried out on
	 * the allocated memory.  Before carrying out any relocations, write the
	 * new output file image and reassign any necessary data pointers to the
	 * output files memory image.  This insures that any relocation
	 * modifications are made to the output file image and not to the input
	 * file image, thus preventing the creation of dirty pages and reducing
	 * the overall swap space requirement.
	 *
	 * Write out the elf structure so as to create the new file image.
	 */
	if ((ofl->ofl_size = (size_t)elf_update(ofl->ofl_welf,
	    ELF_C_WRIMAGE)) == (size_t)-1) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_UPDATE),
		    ofl->ofl_name);
		return (S_ERROR);
	}

	/*
	 * Initialize the true `ofl' information with the memory images address
	 * and size.  This will be used to write() out the image once any
	 * relocation processing has been completed.  We also use this image
	 * information to setup a new Elf descriptor, which is used to obtain
	 * all the necessary elf pointers within the new output image.
	 */
	if ((ofl->ofl_elf = elf_begin(0, ELF_C_IMAGE,
	    ofl->ofl_welf)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_BEGIN),
		    ofl->ofl_name);
		return (S_ERROR);
	}
	if ((ofl->ofl_nehdr = elf_getehdr(ofl->ofl_elf)) == NULL) {
		ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_GETEHDR),
		    ofl->ofl_name);
		return (S_ERROR);
	}
	if (!(flags & FLG_OF_RELOBJ))
		if ((ofl->ofl_phdr = elf_getphdr(ofl->ofl_elf)) == NULL) {
			ld_eprintf(ofl, ERR_ELF, MSG_INTL(MSG_ELF_GETPHDR),
			    ofl->ofl_name);
			return (S_ERROR);
		}

	/*
	 * Reinitialize the section descriptors, section headers and obtain new
	 * output data buffer pointers (these will be used to perform any
	 * relocations).
	 */
	ndx = 0;
	for (APLIST_TRAVERSE(ofl->ofl_segs, idx1, sgp)) {
		Phdr	*_phdr = &(sgp->sg_phdr);
		Os_desc	*osp;
		Aliste	idx2;
		Boolean	recorded = FALSE;

		for (APLIST_TRAVERSE(sgp->sg_osdescs, idx2, osp)) {
			/*
			 * Make sure that an output section was originally
			 * created.  Input sections that had been marked as
			 * discarded may have made an output section
			 * unnecessary.  Remove this alist entry so that
			 * future output section descriptor processing doesn't
			 * have to compensate for this empty section.
			 */
			if (osp->os_scn == NULL) {
				aplist_delete(sgp->sg_osdescs, &idx2);
				continue;
			}
			if ((osp->os_scn =
			    elf_getscn(ofl->ofl_elf, ++ndx)) == NULL) {
				ld_eprintf(ofl, ERR_ELF,
				    MSG_INTL(MSG_ELF_GETSCN), ofl->ofl_name,
				    ndx);
				return (S_ERROR);
			}
			if ((osp->os_shdr =
			    elf_getshdr(osp->os_scn)) == NULL) {
				ld_eprintf(ofl, ERR_ELF,
				    MSG_INTL(MSG_ELF_GETSHDR), ofl->ofl_name);
				return (S_ERROR);
			}
			if ((fixalign == TRUE) && sgp->sg_fscn &&
			    (recorded == FALSE)) {
				size_t	fndx;
				Elf_Scn *scn;

				scn = sgp->sg_fscn;
				if ((fndx = elf_ndxscn(scn)) == SHN_UNDEF) {
					ld_eprintf(ofl, ERR_ELF,
					    MSG_INTL(MSG_ELF_NDXSCN),
					    ofl->ofl_name);
					return (S_ERROR);
				}
				if (ndx == fndx) {
					sgp->sg_fscn = osp->os_scn;
					recorded = TRUE;
				}
			}

			if ((osp->os_outdata =
			    elf_getdata(osp->os_scn, NULL)) == NULL) {
				ld_eprintf(ofl, ERR_ELF,
				    MSG_INTL(MSG_ELF_GETDATA), ofl->ofl_name);
				return (S_ERROR);
			}

			/*
			 * If this section is part of a loadable segment insure
			 * that the segments alignment is appropriate.
			 */
			if (_phdr->p_type == PT_LOAD) {
				_phdr->p_align = ld_lcm(_phdr->p_align,
				    osp->os_shdr->sh_addralign);
			}
		}
	}
	return (1);
}
