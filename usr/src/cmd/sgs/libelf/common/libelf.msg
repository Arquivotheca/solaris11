#
# Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#


@ _START_

# Message file for cmd/sgs/libelf.

@ MSG_ID_LIBELF


# Null Error

@ EINF_NULLERROR	"Elf error 0"


# BUG	bug	Internal error

@ EBUG_COOKSTAT		"Internal error: cook status"
@ EBUG_COOKTYPE		"Internal error: cook data type"
@ EBUG_SCNLIST		"Internal error: broken section list"
@ EBUG_THRDKEY		"Internal error: thr_keycreate() failed. \
			 Unable to allocate error buffer"
@ EBUG_THRDSET		"Internal error: thr_setspecific failed. \
			 Unable to create TSD for error message."


# FMT	fmt	Format error

@ EFMT_ARFMAG		"Format error: archive fmag"
@ EFMT_ARHDRSZ		"Format error: archive header truncated"
@ EFMT_ARMEMSZ		"Format error: archive member truncated"
@ EFMT_ARSYM		"Format error: archive symtab truncated"
@ EFMT_ARSYMSTR		"Format error: archive symtab strings"
@ EFMT_ARSYMSZ		"Format error: archive symtab size"
@ EFMT_DATA		"Format error: data region truncated"
@ EFMT_EHDRSZ		"Format error: ehdr size"
@ EFMT_ENTSZ		"Format error: shdr entry size"
@ EFMT_PHDRSZ		"Format error: phdr entry size"
@ EFMT_PHTAB		"Format error: phdr table truncated"
@ EFMT_SCNSZ		"Format error: section sh_size too small for data"
@ EFMT_SHDRSZ		"Format error: shdr entry size"
@ EFMT_SHTAB		"Format error: shdr table truncated"
@ EFMT_VER2		"Format error: ehdr EI_VERSION/e_version mismatch"
@ EFMT_VM		"Format error: reference outside file"
@ EFMT_FBIG_CLASS32	"Format error: file size exceeds 4GB 32-bit limit"
@ EFMT_FBIG_LARGEFILE	"Format error: file size exceeds 2GB limit of 32-bit \
			 linker. 64-bit linker is recommended."

@ EFMT_ARSTRNM		"Format error: archive strtab name"
@ EFMT_ARUNKNM		"Format error: archive special name unknown"

# IO	io	I/O error

@ EIO_FBIG		"I/O error: file too big for memory"
@ EIO_FSZ		"I/O error: get file size"
@ EIO_READ		"I/O error: raw read"
@ EIO_SEEK		"I/O error: seek"
@ EIO_SYNC		"I/O error: output msync"
@ EIO_VM		"I/O error: region read"
@ EIO_WRITE		"I/O error: output write"


# MEM	mem	Memory error

@ EMEM_ARMEM		"Memory allocation error: archive member header"
@ EMEM_ARSTR		"Memory allocation error: archive long file name \
			 strings"
@ EMEM_ARSYM		"Memory allocation error: archive symtab"
@ EMEM_DATA		"Memory allocation error: section data"
@ EMEM_DNODE		"Memory allocation error: data node"
@ EMEM_EHDR		"Memory allocation error: ehdr"
@ EMEM_ELF		"Memory allocation error: elf descriptor"
@ EMEM_OUT		"Memory allocation error: output file space"
@ EMEM_PHDR		"Memory allocation error: phdr table"
@ EMEM_SCN		"Memory allocation error: scn descriptors"
@ EMEM_SHDR		"Memory allocation error: shdr table"
@ EMEM_SNODE		"Memory allocation error: section node"
@ EMEM_VM		"Memory allocation error: vm stats"
@ EMEM_ERRMSG		"Memory allocation error: during errmsg generation"


# REQ	req	Request error

@ EREQ_AR		"Request error: not archive"
@ EREQ_ARMEMFD		"Request error: archive/member fildes mismatch"
@ EREQ_ARRDWR		"Request error: archive/member begin() for writing"
@ EREQ_BEGIN		"Request error: begin() ELF_C_..."
@ EREQ_CLASS		"Request error: class file/memory mismatch"
@ EREQ_CNTLCMD		"Request error: cntl() ELF_C_..."
@ EREQ_CNTLWRT		"Request error: cntl() ELF_C_FD... on output file"
@ EREQ_DATA		"Request error: data/scn mismatch"
@ EREQ_ENCODE		"Request error: unknown data encoding"
@ EREQ_FLAG		"Request error: flag() ELF_C_..."
@ EREQ_NDX		"Request error: no section at index"
@ EREQ_NOFD		"Request error: file descriptor disabled"
@ EREQ_RAND		"Request error: offset out of range"
@ EREQ_RDWR		"Request error: begin() ELF_C_... mismatch with parent"
@ EREQ_SCNNULL		"Request error: can't manipulate null section"
@ EREQ_STROFF		"Request error: string offset out of range"
@ EREQ_STRSCN		"Request error: no string table"
@ EREQ_UPDATE		"Request error: update() ELF_C_..."
@ EREQ_UPDWRT		"Request error: update() for write on read-only file"
@ EREQ_VER		"Request error: unknown version"
@ EREQ_NOWRIMAGE	"Request error: update(WRIMAGE) not performed on parent"

@ EREQ_DSZ		"Request error: destination too small"
@ EREQ_TYPE		"Request error: unknown ELF_T_..."

#  SEQ	seq	Sequence error

@ ESEQ_EHDR		"Sequence error: ehdr not created"
@ ESEQ_VER		"Sequence error: version not set"


#
# Strings for use by _elf_file() and its support code, found in the
# elf_file subdirectory. These all start with the MSG_FILE_ prefix.
#
@ MSG_FILE_AR			"current ar archive"
@ MSG_FILE_AR_EMPTY		", empty"
@ MSG_FILE_AR_MIX		", mixed ELF and non-ELF content"
@ MSG_FILE_AR_MIXELF		", mixed ELF content"
@ MSG_FILE_AR_NONELF		"non-ELF content"
@ MSG_FILE_AR_SYMTBL		", %d-bit symbol table"
@ MSG_FILE_CAP_CNTREAD		"%s: %s: can't read ELF capabilities data - \
				 ELF capabilities ignored\n"
@ MSG_FILE_CAP_ZERO		"%s: %s zero size or zero entry ELF section - \
				 ELF capabilities ignored\n"
@ MSG_FILE_CORE			"core file"
@ MSG_FILE_COREFROM		", from '%s'"
@ MSG_FILE_CORE_PRE26		"pre-2.6 core file"
@ MSG_FILE_DYN_CNTREAD		"%s: %s: can't read ELF dynamic section - \
				 ignored\n"
@ MSG_FILE_DYN_ZERO		"%s: %s zero size or zero entry ELF \
				 dynamic section - ignored\n"
@ MSG_FILE_EF_SPARC_32PLUS	", V8+ Required"
@ MSG_FILE_EF_SPARC_HAL_R1	", HaL R1 Extensions Required"
@ MSG_FILE_EF_SPARC_SUN_US1	", UltraSPARC1 Extensions Required"
@ MSG_FILE_EF_SPARC_SUN_US3	", UltraSPARC3 Extensions Required"
@ MSG_FILE_ELF			"ELF %s %s %s %s Version %d"
@ MSG_FILE_ET_DYN		"dynamic lib"
@ MSG_FILE_ET_EXEC		"executable"
@ MSG_FILE_ET_NONE		"unknown type"
@ MSG_FILE_ET_REL		"relocatable"
@ MSG_FILE_BADALLOC		"%s: malloc failed: %s\n"
@ MSG_FILE_BADAR		"invalid archive"
@ MSG_FILE_BADCLASS		"%s: %s: unknown ELFCLASS: %d\n"
@ MSG_FILE_BADEHDR		"%s: %s: can't read ELF header\n"
@ MSG_FILE_BADELF		"invalid ELF object"
@ MSG_FILE_BADARSTRTAB		"%s: %s: can't read archive string table\n"
@ MSG_FILE_LINKDYNAMIC		", dynamically linked"
@ MSG_FILE_LINKSTATIC		", statically linked"
@ MSG_FILE_NOTSTRIPPED		", not stripped"
@ MSG_FILE_NODEBUGINFO		", no debugging information available"
@ MSG_FILE_RLC			"Runtime Linking Configuration %s %s %s"
@ MSG_FILE_STRIPPED		", stripped"
@ MSG_FILE_STUB			", stub"


@ _END_


# The following strings represent reserved names.  Reference to these strings
# is via the MSG_ORIG() macro, and thus translations are required.

@ MSG_SUNW_OST_SGS	"SUNW_OST_SGS"


# System error format

@ MSG_FMT_ERR		"%s: %s"

#
# Strings for use by _elf_file() and its support code, found in the
# libelf/elf_file subdirectory. These all start with the MSG_FILE_ prefix.
#
@ MSG_FILE_AR_STRTAB	"//              "
@ MSG_FILE_AR_SYM	"/               "
@ MSG_FILE_AR_SYM64	"/SYM64/         "
@ MSG_FILE_DBG_DEBUG	".debug"
@ MSG_FILE_DBG_DWARF	".stab"
@ MSG_FILE_DBG_STAB	".dwarf"
@ MSG_FILE_DBG_LINE	".line"
@ MSG_FILE_FMT_CAP	" [%s]"
@ MSG_FILE_FMT_DETAIL	"\n%s(%s):%s"
@ MSG_FILE_STR_COMMASP	", "
@ MSG_FILE_STR_EMPTY	""
@ MSG_FILE_STR_FILE	"file"
@ MSG_FILE_STR_SP	" "
@ MSG_FILE_STR_TAB	"\t"
@ MSG_FILE_STR_TABTAB	"\t\t"
