#!/bin/ksh -p
# (note we use "/bin/ksh -p" for Linux/pdksh support in this script)

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

#
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# buildksh93.sh - ast-ksh/ast-open standalone build script for the 
# OpenSolaris ksh93-integration project
#

# ksh93u sources can be downloaded like this from the AT&T site:
# wget --http-user="I accept www.opensource.org/licenses/cpl" --http-passwd="." 'http://www2.research.att.com/~gsf/download/tgz/INIT.2011-02-08.tgz'
# wget --http-user="I accept www.opensource.org/licenses/cpl" --http-passwd="." 'http://www2.research.att.com/~gsf/download/tgz/ast-ksh.2011-02-08.tgz'
# wget --http-user="I accept www.opensource.org/licenses/cpl" --http-passwd="." 'http://www2.research.att.com/~gsf/download/tgz/ast-open.2011-02-08.tgz'

function fatal_error
{
	print -u2 "${progname}: $*"
	exit 1
}

set -o errexit
set -o xtrace

typeset progname="$(basename "${0}")"
typeset buildmode="$1"

if [[ "${buildmode}" == '' ]] ; then
	fatal_error 'buildmode required.'
fi

# Make sure we use the C locale during building to avoid any unintended
# side-effects
export LANG='C'
export LC_ALL="$LANG" LC_MONETARY="$LANG" LC_NUMERIC="$LANG" LC_MESSAGES="$LANG" LC_COLLATE="$LANG" LC_CTYPE="$LANG"
# Make sure the POSIX/XPG6 tools are in front of /usr/bin (/bin is needed for Linux after /usr/bin)
export PATH='/usr/xpg6/bin:/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:/bin:/opt/SUNWspro/bin'

# Make sure the POSIX/XPG6 packages are installed (mandatory for building
# our version of ksh93 correctly).
if [[ "$(uname -s)" == 'SunOS' ]] ; then
	if [[ ! -x '/usr/xpg6/bin/tr' ]] ; then
		fatal_error 'XPG6/4 packages (SUNWxcu6,SUNWxcu4) not installed.'
	fi
fi

function print_solaris_builtin_header
{
# Make sure to use \\ instead of \ for continuations
cat <<ENDOFTEXT
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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SOLARIS_KSH_CMDLIST_H
#define	_SOLARIS_KSH_CMDLIST_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * List builtins for Solaris.
 * The list here is partially autogenerated and partially hand-picked
 * based on compatibility with the native Solaris versions of these
 * tools
 */

/*
 * Commands which are 100% compatible with native Solaris versions (/bin is
 * a softlink to ./usr/bin, ksh93 takes care about the lookup)
 */
#define	BINCMDLIST(f)	\\
	{ "/bin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	USRBINCMDLIST(f)	\\
	{ "/usr/bin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	SBINCMDLIST(f)	\\
	{ "/sbin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	SUSRBINCMDLIST(f)	\\
	{ "/usr/sbin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
/* POSIX compatible commands */
#define	XPG6CMDLIST(f)	\\
	{ "/usr/xpg6/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	XPG4CMDLIST(f)	\\
	{ "/usr/xpg4/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#ifdef SHOPT_USR_GNU_BIN_BUILTINS
/* GNU coreutils compatible commands */
#define	GNUCMDLIST(f)	\\
	{ "/usr/gnu/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#else
#define	GNUCMDLIST(f)
#endif
/*
 * Make all ksh93 builtins accessible when /usr/ast/bin was added to
 * /usr/xpg6/bin:/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:/bin:/opt/SUNWspro/bin
 */
#define	ASTCMDLIST(f)	\\
	{ "/usr/ast/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },

/* undo ast_map.h #defines to avoid collision */
#undef basename
#undef dirname
#undef mktemp

/* Generated data, do not edit. */
ASTCMDLIST(basename)
GNUCMDLIST(basename)
XPG4CMDLIST(basename)
ASTCMDLIST(cat)
BINCMDLIST(cat)
ASTCMDLIST(chgrp)
// XPG4CMDLIST(chgrp)
ASTCMDLIST(chmod)
ASTCMDLIST(chown)
// XPG4CMDLIST(chown)
BINCMDLIST(chown)
ASTCMDLIST(cksum)
BINCMDLIST(cksum)
GNUCMDLIST(cksum)
ASTCMDLIST(cmp)
BINCMDLIST(cmp)
ASTCMDLIST(comm)
BINCMDLIST(comm)
GNUCMDLIST(comm)
ASTCMDLIST(cp)
// XPG4CMDLIST(cp)
ASTCMDLIST(cut)
BINCMDLIST(cut)
GNUCMDLIST(cut)
ASTCMDLIST(date)
// XPG4CMDLIST(date)
ASTCMDLIST(dirname)
BINCMDLIST(dirname)
GNUCMDLIST(dirname)
// ASTCMDLIST(egrep)
// XPG4CMDLIST(egrep)
ASTCMDLIST(expr)
GNUCMDLIST(expr)
XPG6CMDLIST(expr)
ASTCMDLIST(fds)
// ASTCMDLIST(fgrep)
// XPG4CMDLIST(fgrep)
ASTCMDLIST(fmt)
ASTCMDLIST(fold)
BINCMDLIST(fold)
GNUCMDLIST(fold)
// ASTCMDLIST(grep)
// XPG4CMDLIST(grep)
ASTCMDLIST(head)
BINCMDLIST(head)
ASTCMDLIST(id)
XPG4CMDLIST(id)
ASTCMDLIST(join)
BINCMDLIST(join)
GNUCMDLIST(join)
ASTCMDLIST(ln)
// XPG4CMDLIST(ln)
ASTCMDLIST(logname)
BINCMDLIST(logname)
GNUCMDLIST(logname)
ASTCMDLIST(md5sum)
ASTCMDLIST(mkdir)
BINCMDLIST(mkdir)
GNUCMDLIST(mkdir)
ASTCMDLIST(mkfifo)
BINCMDLIST(mkfifo)
GNUCMDLIST(mkfifo)
ASTCMDLIST(mktemp)
BINCMDLIST(mktemp)
GNUCMDLIST(mktemp)
ASTCMDLIST(mv)
// XPG4CMDLIST(mv)
ASTCMDLIST(paste)
BINCMDLIST(paste)
GNUCMDLIST(paste)
ASTCMDLIST(pathchk)
BINCMDLIST(pathchk)
GNUCMDLIST(pathchk)
// ASTCMDLIST(readlink)
ASTCMDLIST(rev)
BINCMDLIST(rev)
BINCMDLIST(rm)
ASTCMDLIST(rm)
XPG4CMDLIST(rm)
ASTCMDLIST(rmdir)
BINCMDLIST(rmdir)
GNUCMDLIST(rmdir)
GNUCMDLIST(sleep)
ASTCMDLIST(stty)
// XPG4CMDLIST(stty)
ASTCMDLIST(sum)
BINCMDLIST(sum)
ASTCMDLIST(sync)
BINCMDLIST(sync)
GNUCMDLIST(sync)
SBINCMDLIST(sync)
SUSRBINCMDLIST(sync)
ASTCMDLIST(tail)
BINCMDLIST(tail)
XPG4CMDLIST(tail)
ASTCMDLIST(tee)
BINCMDLIST(tee)
GNUCMDLIST(tee)
ASTCMDLIST(tty)
BINCMDLIST(tty)
GNUCMDLIST(tty)
ASTCMDLIST(uname)
ASTCMDLIST(uniq)
BINCMDLIST(uniq)
GNUCMDLIST(uniq)
ASTCMDLIST(wc)
BINCMDLIST(wc)
GNUCMDLIST(wc)
// ASTCMDLIST(xgrep)
// BINCMDLIST(xgrep)

/* Mandatory for ksh93 test suite and AST scripts */
BINCMDLIST(getconf)

#ifdef	__cplusplus
}
#endif
#endif /* !_SOLARIS_KSH_CMDLIST_H */

ENDOFTEXT
	return 0
}


function print_gnulinux_builtin_header
{
# Make sure to use \\ instead of \ for continuations
cat <<ENDOFTEXT
/***********************************************************************
*								       *
*		This software is part of the ast package	       *
*	   Copyright (c) 1982-2010 AT&T Intellectual Property	       *
*		       and is licensed under the		       *
*		   Common Public License, Version 1.0		       *
*		     by AT&T Intellectual Property		       *
*								       *
*		 A copy of the License is available at  	       *
*	     http://www.opensource.org/licenses/cpl1.0.txt	       *
*	  (with md5 checksum 059e8cd6165cb4c31e351f2b69388fd9)         *
*								       *
*	       Information and Software Systems Research	       *
*			     AT&T Research			       *
*			    Florham Park NJ			       *
*								       *
*	       Roland Mainz <roland.mainz@nrubsig.org>  	       *
*								       *
***********************************************************************/

#ifndef _GNULINUX_KSH_CMDLIST_H
#define        _GNULINUX_KSH_CMDLIST_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * List builtins for Linux.
 * The list here is partially autogenerated and partially hand-picked
 * based on compatibility with the native GNU coreutils versions of
 * these tools
 */

/* GNU coreutils compatible commands.
 * Be careful, some are in /bin while others are in /usr/bin
 */
#define	ASTCMDLIST(f)		{ "/usr/ast/bin/"	#f,	NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	BINCMDLIST(f)		{ "/bin/"		#f,	NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	USRBINCMDLIST(f)	{ "/usr/bin/"		#f,	NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },

/* undo ast_map.h #defines to avoid collision */
#undef basename
#undef dirname
#undef mktemp

/* Generated data, do not edit. */
USRBINCMDLIST(basename)
USRBINCMDLIST(cksum)
USRBINCMDLIST(comm)
USRBINCMDLIST(cut)
USRBINCMDLIST(dirname)
USRBINCMDLIST(expr)
USRBINCMDLIST(fold)
USRBINCMDLIST(join)
USRBINCMDLIST(logname)
BINCMDLIST(mkdir)
USRBINCMDLIST(mkfifo)
BINCMDLIST(mktemp)
USRBINCMDLIST(paste)
USRBINCMDLIST(pathchk)
USRBINCMDLIST(rev)
BINCMDLIST(rmdir)
BINCMDLIST(sleep)
BINCMDLIST(sync)
USRBINCMDLIST(tee)
USRBINCMDLIST(tty)
USRBINCMDLIST(uniq)
USRBINCMDLIST(wc)

/* Mandatory for ksh93 test suite and AST scripts */
USRBINCMDLIST(getconf)

ASTCMDLIST(basename)
ASTCMDLIST(cat)
ASTCMDLIST(chgrp)
ASTCMDLIST(chmod)
ASTCMDLIST(chown)
ASTCMDLIST(cksum)
ASTCMDLIST(cmp)
ASTCMDLIST(comm)
ASTCMDLIST(cp)
ASTCMDLIST(cut)
ASTCMDLIST(date)
ASTCMDLIST(dirname)
// ASTCMDLIST(egrep)
ASTCMDLIST(expr)
ASTCMDLIST(fds)
// ASTCMDLIST(fgrep)
ASTCMDLIST(fmt)
ASTCMDLIST(fold)
// ASTCMDLIST(grep)
ASTCMDLIST(head)
ASTCMDLIST(id)
ASTCMDLIST(join)
ASTCMDLIST(ln)
ASTCMDLIST(logname)
ASTCMDLIST(md5sum)
ASTCMDLIST(mkdir)
ASTCMDLIST(mkfifo)
ASTCMDLIST(mktemp)
ASTCMDLIST(mv)
ASTCMDLIST(paste)
ASTCMDLIST(pathchk)
// ASTCMDLIST(readlink)
ASTCMDLIST(rev)
ASTCMDLIST(rm)
ASTCMDLIST(rmdir)
ASTCMDLIST(stty)
ASTCMDLIST(sum)
ASTCMDLIST(sync)
ASTCMDLIST(tail)
ASTCMDLIST(tee)
ASTCMDLIST(tty)
ASTCMDLIST(uname)
ASTCMDLIST(uniq)
ASTCMDLIST(wc)
// ASTCMDLIST(xgrep)

#ifdef __cplusplus
}
#endif

#endif /* !_GNULINUX_KSH_CMDLIST_H */
ENDOFTEXT
	return 0
}


function build_shell
{
	set -o errexit
	set -o xtrace

	# optdebug.OS.cputype.XXbit.compiler
	case "${buildmode}" in
		*.linux.*)
			gnulinux_builtin_header="${PWD}/tmp_gnulinux_builtin_header.h"
			print_gnulinux_builtin_header >"${gnulinux_builtin_header}"

			# set debug/optimiser flags
			case "${buildmode}" in
				*.opt.*)
					bgcc_optdebug_flags='-O2'
					;;
				*.debug.*)
					bgcc_optdebug_flags='-g -ggdb'
					;;
				*)
					fatal_error "build_shell: Illegal optimiser flag \"${buildmode}\"."
					;;
			esac

			# ksh93+AST config flags
			bast_flags="-DSHOPT_CMDLIB_BLTIN=0 -DSH_CMDLIB_DIR=\\\"/usr/ast/bin\\\" -DSHOPT_CMDLIB_HDR=\\\"${gnulinux_builtin_header}\\\" -DSHOPT_SYSRC -D_map_libc=1"
			
			# gcc flags
			bgcc99='gcc -std=gnu99 -fstrict-aliasing -Wstrict-aliasing'
			bgcc_ccflags="${bon_flags} ${bast_flags} ${bgcc_optdebug_flags}"
			
			case "${buildmode}" in
				# Linux i386
				*.i386.32bit.gcc*)
					HOSTTYPE='linux.i386' CC="${bgcc99} -m32 -fPIC" cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"
					;;
				*.i386.64bit.gcc*)
					HOSTTYPE='linux.i386' CC="${bgcc99} -m64 -fPIC" cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"
					;;
				*)
					fatal_error "build_shell: Illegal Linux type/compiler build mode \"${buildmode}\"."
					;;
			esac
			;;
		*.solaris.*)
			# Notes:
			# 1. Do not remove/modify these flags or their order before either
			# asking the project leads at
			# http://www.opensolaris.org/os/project/ksh93-integration/
			# These flags all have a purpose, even if they look
			# weird/redundant/etc. at the first look.
			#
			# 2. We use -KPIC here since -Kpic is too small on 64bit sparc and
			# on 32bit it's close to the barrier so we use it for both 32bit and
			# 64bit to avoid later suprises when people update libast in the
			# future
			#
			# 3. "-D_map_libc=1" is needed to force map.c to add a "_ast_" prefix to all
			# AST symbol names which may otherwise collide with Solaris/Linux libc
			#
			# 4. "-DSHOPT_SYSRC" enables /etc/ksh.kshrc support (AST default is currently
			# to enable it if /etc/ksh.kshrc or /etc/bash.bashrc are available on the
			# build machine).
			#
			# 5. -D_lib_socket=1 -lsocket -lnsl" was added to make sure ksh93 is compiled
			# with networking support enabled, the current AST build infratructure has
			# problems with detecting networking support in Solaris.
			#
			# 6. "-xc99=%all -D_XOPEN_SOURCE=600 -D__EXTENSIONS__=1" is used to force
			# the compiler into C99 mode. Otherwise ksh93 will be much slower and lacks
			# lots of arithmethic functions.
			#
			# 7. "-D_TS_ERRNO -D_REENTRANT" are flags taken from the default OS/Net
			# build system.
			#
			# 8. "-xpagesize_stack=64K is used on SPARC to enhance the performace
			#
			# 9. -DSHOPT_CMDLIB_BLTIN=0 -DSH_CMDLIB_DIR=\\\"/usr/ast/bin\\\" -DSHOPT_CMDLIB_HDR=\\\"/home/test001/ksh93/ast_ksh_20070322/solaris_cmdlist.h\\\"
			# is used to bind all ksh93 builtins to a "virtual" directory
			# called "/usr/ast/bin/" and to adjust the list of builtins
			# enabled by default to those defined by PSARC 2006/550
			
			solaris_builtin_header="${PWD}/tmp_solaris_builtin_header.h"
			print_solaris_builtin_header >"${solaris_builtin_header}"
			
			# OS/Net build flags
			bon_flags='-D_TS_ERRNO -D_REENTRANT'
			
			# ksh93+AST config flags
			bast_flags="-DSHOPT_CMDLIB_BLTIN=0 -DSH_CMDLIB_DIR=\\\"/usr/ast/bin\\\" -DSHOPT_CMDLIB_HDR=\\\"${solaris_builtin_header}\\\" -DSHOPT_SYSRC -D_map_libc=1"
			
			# set debug/optimiser flags
			case "${buildmode}" in
				*.opt.*)
					bsuncc_optdebug_flags='-xO4 -xalias_level=std'
					bgcc_optdebug_flags='-O2'
					;;
				*.debug.*)
					bsuncc_optdebug_flags='-g -xs'
					bgcc_optdebug_flags='-g'
					;;
				*)
					fatal_error "build_shell: Illegal optimiser flag \"${buildmode}\"."
					;;
			esac
			
			# Sun Studio flags
			bsunc99='/opt/SUNWspro/bin/cc -xc99=%all -D_XOPEN_SOURCE=600 -D__EXTENSIONS__=1'
			bsuncc_app_ccflags_sparc='-xpagesize_stack=64K' # use bsuncc_app_ccflags_sparc only for final executables
			bsuncc_ccflags="${bon_flags} -KPIC ${bsuncc_optdebug_flags} -xspace -Xa -xstrconst -z combreloc -xildoff -xcsi -errtags=yes ${bast_flags} -D_lib_socket=1 -lsocket -lnsl"
			
			# gcc flags
			bgcc99='/usr/sfw/bin/gcc -std=gnu99 -D_XOPEN_SOURCE=600 -D__EXTENSIONS__=1'
			bgcc_warnflags='-Wall -Wextra -Wno-unknown-pragmas -Wno-missing-braces -Wno-sign-compare -Wno-parentheses -Wno-uninitialized -Wno-implicit-function-declaration -Wno-unused -Wno-trigraphs -Wno-char-subscripts -Wno-switch'
			bgcc_ccflags="${bon_flags} ${bgcc_optdebug_flags} ${bgcc_warnflags} ${bast_flags} -D_lib_socket=1 -lsocket -lnsl"
			
			case "${buildmode}" in
				# for -m32/-m64 flags see usr/src/Makefile.master, makefile symbols *_XARCH/co.
				*.i386.32bit.suncc*)  HOSTTYPE='sol11.i386' CC="${bsunc99} -m32"		  cc_sharedlib='-G' CCFLAGS="${bsuncc_ccflags}"  ;;
				*.i386.64bit.suncc*)  HOSTTYPE='sol11.i386' CC="${bsunc99} -m64 -KPIC"  	  cc_sharedlib='-G' CCFLAGS="${bsuncc_ccflags}"  ;;
				*.sparc.32bit.suncc*) HOSTTYPE='sol11.sun4' CC="${bsunc99} -m32"		  cc_sharedlib='-G' CCFLAGS="${bsuncc_ccflags}" bsuncc_app_ccflags="${bsuncc_app_ccflags_sparc}" ;;
				*.sparc.64bit.suncc*) HOSTTYPE='sol11.sun4' CC="${bsunc99} -m64 -dalign -KPIC"    cc_sharedlib='-G' CCFLAGS="${bsuncc_ccflags}" bsuncc_app_ccflags="${bsuncc_app_ccflags_sparc}" ;;
			
				*.i386.32bit.gcc*)  HOSTTYPE='sol11.i386' CC="${bgcc99} -fPIC"  					  cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"  ;;
				*.i386.64bit.gcc*)  HOSTTYPE='sol11.i386' CC="${bgcc99} -m64 -mtune=opteron -Ui386 -U__i386 -fPIC"	  cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"  ;;
				*.sparc.32bit.gcc*) HOSTTYPE='sol11.sun4' CC="${bgcc99} -m32 -mcpu=v8 -fPIC"				  cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"  ;;
				*.sparc.64bit.gcc*) HOSTTYPE='sol11.sun4' CC="${bgcc99} -m64 -mcpu=v9 -fPIC"				  cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"  ;;
				*.s390.32bit.gcc*)  HOSTTYPE='sol11.s390' CC="${bgcc99} -m32	      -fPIC"				  cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"  ;;
				*.s390.64bit.gcc*)  HOSTTYPE='sol11.s390' CC="${bgcc99} -m64	      -fPIC"				  cc_sharedlib='-shared' CCFLAGS="${bgcc_ccflags}"  ;;
			
			*)
				fatal_error "build_shell: Illegal Solaris type/compiler build mode \"${buildmode}\"."
				;;
			esac
			;;
		*)
			fatal_error "Illegal OS build mode \"${buildmode}\"."
			;;
	esac

	# some prechecks
	[[ -z "${CCFLAGS}"    ]] && fatal_error 'build_shell: CCFLAGS is empty.'
	[[ -z "${CC}"	      ]] && fatal_error 'build_shell: CC is empty.'
	[[ -z "${HOSTTYPE}"   ]] && fatal_error 'build_shell: HOSTTYPE is empty.'
	[[ ! -f 'bin/package' ]] && fatal_error 'build_shell: bin/package missing.'
	[[ ! -x 'bin/package' ]] && fatal_error 'build_shell: bin/package not executable.'

	export CCFLAGS CC HOSTTYPE
	
	# build ksh93
	# (SHELL='/bin/ksh' is required to work around buggy /bin/sh implementations)
	SHELL='/bin/ksh' /bin/ksh ./bin/package make PACKAGE_OPTIONS='map-libc' CCFLAGS="${CCFLAGS}" CC="${CC}" HOSTTYPE="${HOSTTYPE}" SHELL='/bin/ksh'
	
	root="${PWD}/arch/${HOSTTYPE}"
	[[ -d "${root}" ]] || fatal_error 'build_shell: directory ${root} not found.'
	log="${root}/lib/package/gen/make.out"
	
	[[ -s "${log}" ]] || fatal_error 'build_shell: no make.out log found.'
	
	if [[ -f "${root}/lib/libast-g.a"       ]] then link_libast='ast-g'	    ; else link_libast='ast'         ; fi
	if [[ -f "${root}/lib/libcmd-g.a"       ]] then link_libcmd='cmd-g'	    ; else link_libcmd='cmd'         ; fi
	if [[ -f "${root}/lib/libcoshell-g.a"   ]] then link_libcoshell='coshell-g' ; else link_libcoshell='coshell' ; fi
	if [[ -f "${root}/lib/libdll-g.a"       ]] then link_libdll='dll-g'	    ; else link_libdll='dll'         ; fi
	if [[ -f "${root}/lib/libsum-g.a"       ]] then link_libsum='sum-g'	    ; else link_libsum='sum'         ; fi
	if [[ -f "${root}/lib/libshell-g.a"     ]] then link_libshell='shell-g'     ; else link_libshell='shell'     ; fi
	
	if [[ "${buildmode}" != *.staticshell* ]] ; then
		# libcmd causes some trouble since there is a squatter in solaris
		# This has been fixed in Solaris 11/B48 but may require adjustments
		# for older Solaris releases
		for lib in 'libast' 'libdll' 'libcmd' 'libcoshell' 'libsum' 'libshell' ; do
			case "${lib}" in
				'libshell')
					base='src/cmd/ksh93/'
					vers=1
					link="-L${root}/lib/ -l${link_libcmd} -l${link_libcoshell} -l${link_libsum} -l${link_libdll} -l${link_libast} -lm"
					;;
				'libdll')
					base="src/lib/${lib}"
					vers=1
					link='-ldl'
					;;
				'libast')
					base="src/lib/${lib}"
					vers=1
					link='-lm'
					;;
				*)
					base="src/lib/${lib}"
					vers=1
					link="-L${root}/lib/ -l${link_libast} -lm"
					;;
			esac
			
			(
				cd "${root}/${base}"
				
				if [[ -f "${lib}-g.a" ]] ; then lib_a="${lib}-g.a" ; else lib_a="${lib}.a" ; fi
				
				if [[ "${buildmode}" == *solaris* ]] ; then
				    ${CC} ${cc_sharedlib} ${CCFLAGS} -Bdirect -Wl,-zallextract -Wl,-zmuldefs -o "${root}/lib/${lib}.so.${vers}" "${lib_a}"  ${link}
				else
				    ${CC} ${cc_sharedlib} ${CCFLAGS} -Wl,--whole-archive -Wl,-zmuldefs "${lib_a}" -Wl,--no-whole-archive -o "${root}/lib/${lib}.so.${vers}" ${link}
				fi
				
				#rm ${lib}.a
				mv "${lib_a}" "disabled_${lib_a}_"
				
				cd "${root}/lib"
				ln -sf "${lib}.so.${vers}" "${lib}.so"
			)
		done
		
		(
			base='src/cmd/ksh93'
			cd "${root}/${base}"
			rm -f \
				"${root}/lib/libshell.a"   "${root}/lib/libshell-g.a" \
				"${root}/lib/libsum.a"     "${root}/lib/libsum-g.a" \
				"${root}/lib/libcoshell.a" "${root}/lib/libcoshell-g.a" \
				"${root}/lib/libdll.a"     "${root}/lib/libdll-g.a" \
				"${root}/lib/libast.a"     "${root}/lib/libast-g.a"
			
			if [[ "${buildmode}" == *solaris* ]] ; then
			    ${CC} ${CCFLAGS} ${bsuncc_app_ccflags} -L${root}/lib/ -Bdirect -o ksh pmain.o -lshell -Bstatic -l${link_libcmd} -Bdynamic -lsum -lcoshell -ldll -last -lm -lmd -lsecdb
			else
			    ${CC} ${CCFLAGS} ${bsuncc_app_ccflags} -L${root}/lib/ -o ksh pmain.o -lshell -lcmd -lcoshell -lsum -ldll -last -lm
			fi
			
			file 'ksh'
			file 'shcomp'
			
			export LD_LIBRARY_PATH="${root}/lib:${LD_LIBRARY_PATH}"
			export LD_LIBRARY_PATH_32="${root}/lib:${LD_LIBRARY_PATH_32}"
			export LD_LIBRARY_PATH_64="${root}/lib:${LD_LIBRARY_PATH_64}"
			ldd 'ksh'
		)
	fi

	return 0
}

function test_builtin_getconf
{
	(
		print '# testing getconf builtin...'
		set +o errexit
		export PATH='/bin:/usr/bin'
		for lang in ${TEST_LANG} ; do
		    (
		    	printf '## testing LANG=%s\n' "${lang}"
			export LC_ALL="${lang}" LANG="${lang}"
			${SHELL} -c '/usr/bin/getconf -a | 
				     while read i ; do 
				     t="${i%:*}" ; a="$(getconf "$t" 2>/dev/null)" ; 
				     b="$(/usr/bin/getconf "$t" 2>/dev/null)" ; [ "$a" != "$b" ] && print "# |$t|:|$a| != |$b|" ;
				     done'
		    )
		done
		print '# testing getconf done.'
	)
	return 0
}

function test_shell
{
	set -o errexit
	set -o xtrace
	
	ulimit -s 65536 # need larger stack on 64bit SPARC to pass all tests
	
	export SHELL="$(ls -1 $PWD/arch/*/src/cmd/ksh93/ksh)"
	export LD_LIBRARY_PATH="$(ls -1ad $PWD/arch/*/lib):${LD_LIBRARY_PATH}"
	export LD_LIBRARY_PATH_32="$(ls -1ad $PWD/arch/*/lib):${LD_LIBRARY_PATH_32}"
	export LD_LIBRARY_PATH_64="$(ls -1ad $PWD/arch/*/lib):${LD_LIBRARY_PATH_64}"
	printf '## SHELL is |%s|\n' "${SHELL}"
	printf '## LD_LIBRARY_PATH is |%s|\n' "${LD_LIBRARY_PATH}"
	
	[[ ! -f "${SHELL}" ]] && fatal_error "test_shell: |${SHELL}| is not a file."
	[[ ! -x "${SHELL}" ]] && fatal_error "test_shell: |${SHELL}| is not executable."
	
	[[ "${TEST_LANG}" == '' ]] && TEST_LANG='C zh_CN.GB18030 en_US.UTF-8'
	
	${SHELL} -c 'printf "ksh version is %s\n" "${.sh.version}"'
	
	case "${buildmode}" in
		testshell.bcheck*)
			for lang in ${TEST_LANG} ; do
				(
					export LC_ALL="${lang}" LANG="${lang}"
					for i in ./src/cmd/ksh93/tests/*.sh ; do 
						bc_logfile="$(basename "$i").$$.bcheck"
						rm -f "${bc_logfile}"
						/opt/SUNWspro/bin/bcheck -q -access -o "${bc_logfile}" ${SHELL} ./src/cmd/ksh93/tests/shtests -f \
							LD_LIBRARY_PATH_64="$LD_LIBRARY_PATH_64" \
							LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
							LD_LIBRARY_PATH_32="$LD_LIBRARY_PATH_32"\
							LC_ALL="${lang}" LANG="${lang}" \
							VMALLOC_OPTIONS='abort' \
	    						SHCOMP=$PWD/arch/*/bin/shcomp \
							"$i"
						cat "${bc_logfile}"
					done
				)
			done
			;;
		'testshell.builtin.getconf')
			test_builtin_getconf
			;;
		'testshell')
			for lang in ${TEST_LANG} ; do
				(
					export LC_ALL="${lang}" LANG="${lang}"
					for i in ./src/cmd/ksh93/tests/*.sh ; do 
						${SHELL} ./src/cmd/ksh93/tests/shtests -a -f \
							LD_LIBRARY_PATH_64="$LD_LIBRARY_PATH_64" \
							LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
							LD_LIBRARY_PATH_32="$LD_LIBRARY_PATH_32" \
							LC_ALL="${lang}" LANG="${lang}" \
							VMALLOC_OPTIONS='abort' \
	    						SHCOMP=$PWD/arch/*/bin/shcomp \
							"$i"
					done
				)
			done
			test_builtin_getconf
			;;
	esac
	return 0
}

# main
case "${buildmode}" in
	build.*) build_shell ;;
	testshell*)  test_shell  ;;
	*) fatal_error "Illegal build mode \"${buildmode}\"." ;;
esac

exit $?
# EOF.
