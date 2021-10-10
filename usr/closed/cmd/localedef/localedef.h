/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _LOCALEDEF_H
#define	_LOCALEDEF_H

/*
 * Command line options to the C compiler.
 * Don't remove the white space in the beginning of each macro content.
 */
#define	CCFLAGS_COM \
	" -v -K pic -DPIC -G -z defs -z text -z combreloc -D_REENTRANT"

/*
 * Architecture dependent command line options
 */
#define	CCFLAGS_SPARC	" -xO3 -xregs=no%appl"
#define	CCFLAGS_SPARCV9 " -xO3 -m64 -dalign -xregs=no%appl"
#define	CCFLAGS_I386	" -O"
#define	CCFLAGS_AMD64	" -xO3 -m64"

/*
 * Architecture names
 */
#if	defined(__sparc)
#define	ISA32		"sparc"
#define	ISA64		"sparcv9"
#define	CCFLAGS_ISA32	CCFLAGS_SPARC
#define	CCFLAGS_ISA64	CCFLAGS_SPARCV9
#else
#define	ISA32		"i386"
#define	ISA64		"amd64"
#define	CCFLAGS_ISA32	CCFLAGS_I386
#define	CCFLAGS_ISA64	CCFLAGS_AMD64
#endif

#define	CCFLAGS		CCFLAGS_COM CCFLAGS_ISA32
#define	CCFLAGS64	CCFLAGS_COM CCFLAGS_ISA64

/*
 * C compiler name
 */
#define	CCPATH  "cc"

/*
 * to specify libc for linking
 */
#define	LINKC	" -lc"

/*
 * localedef options
 */
#define	ARGSTR	"cc,"

/*
 * Command line to the C compiler
 * <tpath><CCPATH> <ccopts> -h <soname> -o <objname> <filename>
 */
#define	CCCMDLINE	"%s%s %s -h %s -o %s %s"

/*
 * Shared object name
 */
#define	SONAME			"%s.so.%d"
#define	SLASH_SONAME	"/" SONAME

#define	CCPATH_LEN		2
#define	SPC_LEN			1
#define	SONAMEF_LEN		2
#define	OBJNAMEF_LEN	2
#define	SLASH_LEN		1
#define	SOSFX_LEN		14	/* .so.?????????? */

#define	BIT32	0x01
#define	BIT64	0x02

#endif /* _LOCALEDEF_H */
