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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <libproc.h>

#include <ctype.h>
#include <string.h>
#include <sys/dlpi.h>
#include <sys/ipc.h>
#include <sys/ipc_impl.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/fstyp.h>
#if defined(__i386) || defined(__amd64)
#include <sys/sysi86.h>
#endif /* __i386 */
#include <sys/unistd.h>
#include <sys/file.h>
#include <sys/tiuser.h>
#include <sys/timod.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/termios.h>
#include <sys/termiox.h>
#include <sys/jioctl.h>
#include <sys/filio.h>
#include <fcntl.h>
#include <sys/termio.h>
#include <sys/stermio.h>
#include <sys/ttold.h>
#include <sys/mount.h>
#include <sys/utssys.h>
#include <sys/sysconfig.h>
#include <sys/statvfs.h>
#include <sys/kstat.h>
#include <sys/audio.h>
#include <sys/mixer.h>
#include <sys/cpc_impl.h>
#include <sys/devpoll.h>
#include <sys/strredir.h>
#include <sys/sockio.h>
#include <netinet/ip_mroute.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/ptyvar.h>
#include <sys/des.h>
#include <sys/prnio.h>
#include <sys/dtrace.h>
#include <sys/crypto/ioctladmin.h>
#include <sys/crypto/ioctl.h>
#include <sys/kbio.h>
#include <sys/ptms.h>
#include <sys/aggr.h>
#include <sys/dld.h>
#include <net/simnet.h>
#include <sys/vnic.h>
#include <sys/fs/zfs.h>
#include <inet/kssl/kssl.h>
#include <sys/dkio.h>
#include <sys/cdio.h>
#include <sys/scsi/impl/uscsi.h>
#include <sys/devinfo_impl.h>
#include <sys/dumpadm.h>
#include <sys/mntio.h>
#include <inet/iptun.h>
#include <sys/zcons.h>
#include <sys/usb/clients/hid/hid.h>
#include <sys/pm.h>
#include <sys/soundcard.h>
#include <sys/ioctlname.h>

#include "ramdata.h"
#include "proto.h"

#define	FCNTLMIN	F_DUPFD
#define	FCNTLMAX	F_DUP2FD_CLOEXEC
const char *const FCNTLname[] = {
	"F_DUPFD",
	"F_GETFD",
	"F_SETFD",
	"F_GETFL",
	"F_SETFL",
	"F_O_GETLK",
	"F_SETLK",
	"F_SETLKW",
	"F_CHKFL",
	"F_DUP2FD",
	"F_ALLOCSP",
	"F_FREESP",
	NULL,		/* 12 */
	NULL,		/* 13 */
	"F_GETLK",
	NULL,		/* 15 */
	NULL,		/* 16 */
	NULL,		/* 17 */
	NULL,		/* 18 */
	NULL,		/* 19 */
	NULL,		/* 20 */
	NULL,		/* 21 */
	NULL,		/* 22 */
	"F_GETOWN",
	"F_SETOWN",
	"F_REVOKE",
	"F_HASREMOTELOCKS",
	"F_FREESP64",
	"F_ALLOCSP64",
	NULL,		/* 29 */
	NULL,		/* 30 */
	NULL,		/* 31 */
	NULL,		/* 32 */
	"F_GETLK64",
	"F_SETLK64",
	"F_SETLKW64",
	NULL,		/* 36 */
	NULL,		/* 37 */
	NULL,		/* 38 */
	NULL,		/* 39 */
	"F_SHARE",
	"F_UNSHARE",
	"F_SETLK_NBMAND",
	"F_SHARE_NBMAND",
	"F_SETLK64_NBMAND",
	NULL,		/* 45 */
	"F_BADFD",
	"F_DUPFD_CLOEXEC",
	"F_DUP2FD_CLOEXEC"
};

#define	SYSFSMIN	GETFSIND
#define	SYSFSMAX	GETNFSTYP
const char *const SYSFSname[] = {
	"GETFSIND",
	"GETFSTYP",
	"GETNFSTYP"
};

#define	SCONFMIN	_CONFIG_NGROUPS
#define	SCONFMAX	_CONFIG_EPHID_MAX
const char *const SCONFname[] = {
	"_CONFIG_NGROUPS",		/*  2 */
	"_CONFIG_CHILD_MAX",		/*  3 */
	"_CONFIG_OPEN_FILES",		/*  4 */
	"_CONFIG_POSIX_VER",		/*  5 */
	"_CONFIG_PAGESIZE",		/*  6 */
	"_CONFIG_CLK_TCK",		/*  7 */
	"_CONFIG_XOPEN_VER",		/*  8 */
	"_CONFIG_HRESCLK_TCK",		/*  9 */
	"_CONFIG_PROF_TCK",		/* 10 */
	"_CONFIG_NPROC_CONF",		/* 11 */
	"_CONFIG_NPROC_ONLN",		/* 12 */
	"_CONFIG_AIO_LISTIO_MAX",	/* 13 */
	"_CONFIG_AIO_MAX",		/* 14 */
	"_CONFIG_AIO_PRIO_DELTA_MAX",	/* 15 */
	"_CONFIG_DELAYTIMER_MAX",	/* 16 */
	"_CONFIG_MQ_OPEN_MAX",		/* 17 */
	"_CONFIG_MQ_PRIO_MAX",		/* 18 */
	"_CONFIG_RTSIG_MAX",		/* 19 */
	"_CONFIG_SEM_NSEMS_MAX",	/* 20 */
	"_CONFIG_SEM_VALUE_MAX",	/* 21 */
	"_CONFIG_SIGQUEUE_MAX",		/* 22 */
	"_CONFIG_SIGRT_MIN",		/* 23 */
	"_CONFIG_SIGRT_MAX",		/* 24 */
	"_CONFIG_TIMER_MAX",		/* 25 */
	"_CONFIG_PHYS_PAGES",		/* 26 */
	"_CONFIG_AVPHYS_PAGES",		/* 27 */
	"_CONFIG_COHERENCY",		/* 28 */
	"_CONFIG_SPLIT_CACHE",		/* 29 */
	"_CONFIG_ICACHESZ",		/* 30 */
	"_CONFIG_DCACHESZ",		/* 31 */
	"_CONFIG_ICACHELINESZ",		/* 32 */
	"_CONFIG_DCACHELINESZ",		/* 33 */
	"_CONFIG_ICACHEBLKSZ",		/* 34 */
	"_CONFIG_DCACHEBLKSZ",		/* 35 */
	"_CONFIG_DCACHETBLKSZ",		/* 36 */
	"_CONFIG_ICACHE_ASSOC",		/* 37 */
	"_CONFIG_DCACHE_ASSOC",		/* 38 */
	NULL,				/* 39 */
	NULL,				/* 40 */
	NULL,				/* 41 */
	"_CONFIG_MAXPID",		/* 42 */
	"_CONFIG_STACK_PROT",		/* 43 */
	"_CONFIG_NPROC_MAX",		/* 44 */
	"_CONFIG_CPUID_MAX",		/* 45 */
	"_CONFIG_SYMLOOP_MAX",		/* 46 */
	"_CONFIG_EPHID_MAX",		/* 47 */
};

#define	PATHCONFMIN	_PC_LINK_MAX
#define	PATHCONFMAX	_PC_XATTR_EXISTS
const char *const PATHCONFname[] = {
	"_PC_LINK_MAX",			/*  1 */
	"_PC_MAX_CANON",		/*  2 */
	"_PC_MAX_INPUT",		/*  3 */
	"_PC_NAME_MAX",			/*  4 */
	"_PC_PATH_MAX",			/*  5 */
	"_PC_PIPE_BUF",			/*  6 */
	"_PC_NO_TRUNC",			/*  7 */
	"_PC_VDISABLE",			/*  8 */
	"_PC_CHOWN_RESTRICTED",		/*  9 */
	"_PC_ASYNC_IO",			/* 10 */
	"_PC_PRIO_IO",			/* 11 */
	"_PC_SYNC_IO",			/* 12 */
	"_PC_ALLOC_SIZE_MIN",		/* 13 */
	"_PC_REC_INCR_XFER_SIZE",	/* 14 */
	"_PC_REC_MAX_XFER_SIZE",	/* 15 */
	"_PC_REC_MIN_XFER_SIZE",	/* 16 */
	"_PC_REC_XFER_ALIGN",		/* 17 */
	"_PC_SYMLINK_MAX",		/* 18 */
	"_PC_2_SYMLINKS",		/* 19 */
	"_PC_ACL_ENABLED",		/* 20 */
	"_PC_MIN_HOLE_SIZE",		/* 21 */
	"_PC_CASE_BEHAVIOR",		/* 22 */
	"_PC_SATTR_ENABLED",		/* 23 */
	"_PC_SATTR_EXISTS",		/* 24 */
	"_PC_ACCESS_FILTERING",		/* 25 */
	"_PC_TIMESTAMP_RESOLUTION",	/* 26 */
	NULL,				/* 27 */
	NULL,				/* 28 */
	NULL,				/* 29 */
	NULL,				/* 30 */
	NULL,				/* 31 */
	NULL,				/* 32 */
	NULL,				/* 33 */
	NULL,				/* 34 */
	NULL,				/* 35 */
	NULL,				/* 36 */
	NULL,				/* 37 */
	NULL,				/* 38 */
	NULL,				/* 39 */
	NULL,				/* 40 */
	NULL,				/* 41 */
	NULL,				/* 42 */
	NULL,				/* 43 */
	NULL,				/* 44 */
	NULL,				/* 45 */
	NULL,				/* 46 */
	NULL,				/* 47 */
	NULL,				/* 48 */
	NULL,				/* 49 */
	NULL,				/* 50 */
	NULL,				/* 51 */
	NULL,				/* 52 */
	NULL,				/* 53 */
	NULL,				/* 54 */
	NULL,				/* 55 */
	NULL,				/* 56 */
	NULL,				/* 57 */
	NULL,				/* 58 */
	NULL,				/* 59 */
	NULL,				/* 60 */
	NULL,				/* 61 */
	NULL,				/* 62 */
	NULL,				/* 63 */
	NULL,				/* 64 */
	NULL,				/* 65 */
	NULL,				/* 66 */
	"_PC_FILESIZEBITS",		/* 67 */
	NULL,				/* 68 */
	NULL,				/* 69 */
	NULL,				/* 70 */
	NULL,				/* 71 */
	NULL,				/* 72 */
	NULL,				/* 73 */
	NULL,				/* 74 */
	NULL,				/* 75 */
	NULL,				/* 76 */
	NULL,				/* 77 */
	NULL,				/* 78 */
	NULL,				/* 79 */
	NULL,				/* 80 */
	NULL,				/* 81 */
	NULL,				/* 82 */
	NULL,				/* 83 */
	NULL,				/* 84 */
	NULL,				/* 85 */
	NULL,				/* 86 */
	NULL,				/* 87 */
	NULL,				/* 88 */
	NULL,				/* 89 */
	NULL,				/* 90 */
	NULL,				/* 91 */
	NULL,				/* 92 */
	NULL,				/* 93 */
	NULL,				/* 94 */
	NULL,				/* 95 */
	NULL,				/* 96 */
	NULL,				/* 97 */
	NULL,				/* 98 */
	NULL,				/* 99 */
	"_PC_XATTR_ENABLED",		/* 100 */
	"_PC_XATTR_EXISTS",		/* 101, _PC_LAST */
};

void
ioctl_ioccom(char *buf, size_t size, uint_t code, int nbytes, int x, int y)
{
	const char *inoutstr;

	if (code & IOC_VOID)
		inoutstr = "";
	else if ((code & IOC_INOUT) == IOC_INOUT)
		inoutstr = "WR";
	else
		inoutstr = code & IOC_IN ? "W" : "R";

	if (isascii(x) && isprint(x))
		(void) snprintf(buf, size, "_IO%sN('%c', %d, %d)", inoutstr,
		    x, y, nbytes);
	else
		(void) snprintf(buf, size, "_IO%sN(0x%x, %d, %d)", inoutstr,
		    x, y, nbytes);
}


const char *
ioctlstr(private_t *pri, uint_t code)
{
	const char *str = ioctlname(code);

	/*
	 * Developers hide ascii ioctl names in the ioctl subcode; for example
	 * 0x445210 should be printed 'D'<<16|'R'<<8|10.  We allow for all
	 * three high order bytes (called hi, mid and lo) to contain ascii
	 * characters.
	 */
	if (str == NULL) {
		int c_hi = code >> 24;
		int c_mid = (code >> 16) & 0xff;
		int c_mid_nm = (code >> 16);
		int c_lo = (code >> 8) & 0xff;
		int c_lo_nm = code >> 8;

		if (isascii(c_lo) && isprint(c_lo) &&
		    isascii(c_mid) && isprint(c_mid) &&
		    isascii(c_hi) && isprint(c_hi))
			(void) sprintf(pri->code_buf,
			    "(('%c'<<24)|('%c'<<16)|('%c'<<8)|%d)",
			    c_hi, c_mid, c_lo, code & 0xff);
		else if (isascii(c_lo) && isprint(c_lo) &&
		    isascii(c_mid_nm) && isprint(c_mid_nm))
			(void) sprintf(pri->code_buf,
			    "(('%c'<<16)|('%c'<<8)|%d)", c_mid, c_lo,
			    code & 0xff);
		else if (isascii(c_lo_nm) && isprint(c_lo_nm))
			(void) sprintf(pri->code_buf, "(('%c'<<8)|%d)",
			    c_lo_nm, code & 0xff);
		else if (code & (IOC_VOID|IOC_INOUT))
			ioctl_ioccom(pri->code_buf, sizeof (pri->code_buf),
			    code, c_mid, c_lo, code & 0xff);
		else
			(void) sprintf(pri->code_buf, "0x%.4X", code);
		str = (const char *)pri->code_buf;
	}

	return (str);
}

const char *
fcntlname(int code)
{
	const char *str = NULL;

	if (code >= FCNTLMIN && code <= FCNTLMAX)
		str = FCNTLname[code-FCNTLMIN];
	return (str);
}

const char *
sfsname(int code)
{
	const char *str = NULL;

	if (code >= SYSFSMIN && code <= SYSFSMAX)
		str = SYSFSname[code-SYSFSMIN];
	return (str);
}

/* ARGSUSED */
const char *
si86name(int code)
{
	const char *str = NULL;

#if defined(__i386) || defined(__amd64)
	switch (code) {
	case SI86SWPI:		str = "SI86SWPI";	break;
	case SI86SYM:		str = "SI86SYM";	break;
	case SI86CONF:		str = "SI86CONF";	break;
	case SI86BOOT:		str = "SI86BOOT";	break;
	case SI86AUTO:		str = "SI86AUTO";	break;
	case SI86EDT:		str = "SI86EDT";	break;
	case SI86SWAP:		str = "SI86SWAP";	break;
	case SI86FPHW:		str = "SI86FPHW";	break;
	case SI86FPSTART:	str = "SI86FPSTART";	break;
	case GRNON:		str = "GRNON";		break;
	case GRNFLASH:		str = "GRNFLASH";	break;
	case STIME:		str = "STIME";		break;
	case SETNAME:		str = "SETNAME";	break;
	case RNVR:		str = "RNVR";		break;
	case WNVR:		str = "WNVR";		break;
	case RTODC:		str = "RTODC";		break;
	case CHKSER:		str = "CHKSER";		break;
	case SI86NVPRT:		str = "SI86NVPRT";	break;
	case SANUPD:		str = "SANUPD";		break;
	case SI86KSTR:		str = "SI86KSTR";	break;
	case SI86MEM:		str = "SI86MEM";	break;
	case SI86TODEMON:	str = "SI86TODEMON";	break;
	case SI86CCDEMON:	str = "SI86CCDEMON";	break;
	case SI86CACHE:		str = "SI86CACHE";	break;
	case SI86DELMEM:	str = "SI86DELMEM";	break;
	case SI86ADDMEM:	str = "SI86ADDMEM";	break;
/* 71 through 74 reserved for VPIX */
	case SI86V86: 		str = "SI86V86";	break;
	case SI86SLTIME:	str = "SI86SLTIME";	break;
	case SI86DSCR:		str = "SI86DSCR";	break;
	case RDUBLK:		str = "RDUBLK";		break;
/* NFA entry point */
	case SI86NFA:		str = "SI86NFA";	break;
	case SI86VM86:		str = "SI86VM86";	break;
	case SI86VMENABLE:	str = "SI86VMENABLE";	break;
	case SI86LIMUSER:	str = "SI86LIMUSER";	break;
	case SI86RDID: 		str = "SI86RDID";	break;
	case SI86RDBOOT:	str = "SI86RDBOOT";	break;
/* Merged Product defines */
	case SI86SHFIL:		str = "SI86SHFIL";	break;
	case SI86PCHRGN:	str = "SI86PCHRGN";	break;
	case SI86BADVISE:	str = "SI86BADVISE";	break;
	case SI86SHRGN:		str = "SI86SHRGN";	break;
	case SI86CHIDT:		str = "SI86CHIDT";	break;
	case SI86EMULRDA: 	str = "SI86EMULRDA";	break;
/* RTC commands */
	case WTODC:		str = "WTODC";		break;
	case SGMTL:		str = "SGMTL";		break;
	case GGMTL:		str = "GGMTL";		break;
	case RTCSYNC:		str = "RTCSYNC";	break;
	}
#endif /* __i386 */

	return (str);
}

const char *
utscode(int code)
{
	const char *str = NULL;

	switch (code) {
	case UTS_UNAME:		str = "UNAME";	break;
	case UTS_USTAT:		str = "USTAT";	break;
	case UTS_FUSERS:	str = "FUSERS";	break;
	}

	return (str);
}

const char *
rctlsyscode(int code)
{
	const char *str = NULL;
	switch (code) {
	case 0:		str = "GETRCTL";	break;
	case 1:		str = "SETRCTL";	break;
	case 2:		str = "RCTLSYS_LST";	break;
	case 3:		str = "RCTLSYS_CTL";	break;
	case 4:		str = "RCTLSYS_SETPROJ";	break;
	default:	str = "UNKNOWN";	break;
	}
	return (str);
}

const char *
rctl_local_action(private_t *pri, uint_t val)
{
	uint_t action = val & (~RCTL_LOCAL_ACTION_MASK);

	char *s = pri->code_buf;

	*s = '\0';

	if (action & RCTL_LOCAL_NOACTION) {
		action ^= RCTL_LOCAL_NOACTION;
		(void) strlcat(s, "|RCTL_LOCAL_NOACTION",
		    sizeof (pri->code_buf));
	}
	if (action & RCTL_LOCAL_SIGNAL) {
		action ^= RCTL_LOCAL_SIGNAL;
		(void) strlcat(s, "|RCTL_LOCAL_SIGNAL",
		    sizeof (pri->code_buf));
	}
	if (action & RCTL_LOCAL_DENY) {
		action ^= RCTL_LOCAL_DENY;
		(void) strlcat(s, "|RCTL_LOCAL_DENY",
		    sizeof (pri->code_buf));
	}

	if ((action & (~RCTL_LOCAL_ACTION_MASK)) != 0)
		return (NULL);
	else if (*s != '\0')
		return (s+1);
	else
		return (NULL);
}


const char *
rctl_local_flags(private_t *pri, uint_t val)
{
	uint_t pval = val & RCTL_LOCAL_ACTION_MASK;
	char *s = pri->code_buf;

	*s = '\0';

	if (pval & RCTL_LOCAL_MAXIMAL) {
		pval ^= RCTL_LOCAL_MAXIMAL;
		(void) strlcat(s, "|RCTL_LOCAL_MAXIMAL",
		    sizeof (pri->code_buf));
	}

	if ((pval & RCTL_LOCAL_ACTION_MASK) != 0)
		return (NULL);
	else if (*s != '\0')
		return (s+1);
	else
		return (NULL);
}


const char *
sconfname(int code)
{
	const char *str = NULL;

	if (code >= SCONFMIN && code <= SCONFMAX)
		str = SCONFname[code-SCONFMIN];
	return (str);
}

const char *
pathconfname(int code)
{
	const char *str = NULL;

	if (code >= PATHCONFMIN && code <= PATHCONFMAX)
		str = PATHCONFname[code-PATHCONFMIN];
	return (str);
}

#define	ALL_O_FLAGS \
	(O_NDELAY|O_APPEND|O_SYNC|O_DSYNC|O_NONBLOCK|O_CREAT|O_TRUNC\
	|O_EXCL|O_NOCTTY|O_LARGEFILE|O_RSYNC|O_XATTR|O_NOFOLLOW|O_NOLINKS\
	|O_CLOEXEC|O_DIRECTORY|O_TTY_INIT|FXATTRDIROPEN)

const char *
openarg(private_t *pri, int arg)
{
	char *str = pri->code_buf;

	if ((arg & ~(O_ACCMODE | ALL_O_FLAGS)) != 0)
		return (NULL);

	switch (arg & O_ACCMODE) {
	default:
		return (NULL);
	case O_RDONLY:
		(void) strcpy(str, "O_RDONLY");
		break;
	case O_WRONLY:
		(void) strcpy(str, "O_WRONLY");
		break;
	case O_RDWR:
		(void) strcpy(str, "O_RDWR");
		break;
	case O_SEARCH:
		(void) strcpy(str, "O_SEARCH");
		break;
	case O_EXEC:
		(void) strcpy(str, "O_EXEC");
		break;
	}

	if (arg & O_NDELAY)
		(void) strlcat(str, "|O_NDELAY", sizeof (pri->code_buf));
	if (arg & O_APPEND)
		(void) strlcat(str, "|O_APPEND", sizeof (pri->code_buf));
	if (arg & O_SYNC)
		(void) strlcat(str, "|O_SYNC", sizeof (pri->code_buf));
	if (arg & O_DSYNC)
		(void) strlcat(str, "|O_DSYNC", sizeof (pri->code_buf));
	if (arg & O_NONBLOCK)
		(void) strlcat(str, "|O_NONBLOCK", sizeof (pri->code_buf));
	if (arg & O_CREAT)
		(void) strlcat(str, "|O_CREAT", sizeof (pri->code_buf));
	if (arg & O_TRUNC)
		(void) strlcat(str, "|O_TRUNC", sizeof (pri->code_buf));
	if (arg & O_EXCL)
		(void) strlcat(str, "|O_EXCL", sizeof (pri->code_buf));
	if (arg & O_NOCTTY)
		(void) strlcat(str, "|O_NOCTTY", sizeof (pri->code_buf));
	if (arg & O_LARGEFILE)
		(void) strlcat(str, "|O_LARGEFILE", sizeof (pri->code_buf));
	if (arg & O_RSYNC)
		(void) strlcat(str, "|O_RSYNC", sizeof (pri->code_buf));
	if (arg & O_XATTR)
		(void) strlcat(str, "|O_XATTR", sizeof (pri->code_buf));
	if (arg & O_NOFOLLOW)
		(void) strlcat(str, "|O_NOFOLLOW", sizeof (pri->code_buf));
	if (arg & O_NOLINKS)
		(void) strlcat(str, "|O_NOLINKS", sizeof (pri->code_buf));
	if (arg & O_CLOEXEC)
		(void) strlcat(str, "|O_CLOEXEC", sizeof (pri->code_buf));
	if (arg & O_DIRECTORY)
		(void) strlcat(str, "|O_DIRECTORY", sizeof (pri->code_buf));
#if (O_TTY_INIT != 0)
	if (arg & O_TTY_INIT)
		(void) strlcat(str, "|O_TTY_INIT", sizeof (pri->code_buf));
#endif
	if (arg & FXATTRDIROPEN)
		(void) strlcat(str, "|FXATTRDIROPEN", sizeof (pri->code_buf));

	return ((const char *)str);
}

const char *
whencearg(int arg)
{
	const char *str = NULL;

	switch (arg) {
	case SEEK_SET:	str = "SEEK_SET";	break;
	case SEEK_CUR:	str = "SEEK_CUR";	break;
	case SEEK_END:	str = "SEEK_END";	break;
	case SEEK_DATA:	str = "SEEK_DATA";	break;
	case SEEK_HOLE:	str = "SEEK_HOLE";	break;
	}

	return (str);
}

#define	IPC_FLAGS	(IPC_ALLOC|IPC_CREAT|IPC_EXCL|IPC_NOWAIT)

char *
ipcflags(private_t *pri, int arg)
{
	char *str = pri->code_buf;

	if (arg & 0777)
		(void) sprintf(str, "0%.3o", arg&0777);
	else
		*str = '\0';

	if (arg & IPC_ALLOC)
		(void) strcat(str, "|IPC_ALLOC");
	if (arg & IPC_CREAT)
		(void) strcat(str, "|IPC_CREAT");
	if (arg & IPC_EXCL)
		(void) strcat(str, "|IPC_EXCL");
	if (arg & IPC_NOWAIT)
		(void) strcat(str, "|IPC_NOWAIT");

	return (str);
}

const char *
msgflags(private_t *pri, int arg)
{
	char *str;

	if (arg == 0 || (arg & ~(IPC_FLAGS|MSG_NOERROR|0777)) != 0)
		return ((char *)NULL);

	str = ipcflags(pri, arg);

	if (arg & MSG_NOERROR)
		(void) strcat(str, "|MSG_NOERROR");

	if (*str == '|')
		str++;
	return ((const char *)str);
}

const char *
semflags(private_t *pri, int arg)
{
	char *str;

	if (arg == 0 || (arg & ~(IPC_FLAGS|SEM_UNDO|0777)) != 0)
		return ((char *)NULL);

	str = ipcflags(pri, arg);

	if (arg & SEM_UNDO)
		(void) strcat(str, "|SEM_UNDO");

	if (*str == '|')
		str++;
	return ((const char *)str);
}

const char *
shmflags(private_t *pri, int arg)
{
	char *str;

	if (arg == 0 || (arg & ~(IPC_FLAGS|SHM_RDONLY|SHM_RND|0777)) != 0)
		return ((char *)NULL);

	str = ipcflags(pri, arg);

	if (arg & SHM_RDONLY)
		(void) strcat(str, "|SHM_RDONLY");
	if (arg & SHM_RND)
		(void) strcat(str, "|SHM_RND");

	if (*str == '|')
		str++;
	return ((const char *)str);
}

#define	MSGCMDMIN	0
#define	MSGCMDMAX	IPC_STAT64
const char *const MSGCMDname[MSGCMDMAX+1] = {
	NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL,
	"IPC_RMID",	/* 10 */
	"IPC_SET",	/* 11 */
	"IPC_STAT",	/* 12 */
	"IPC_SET64",	/* 13 */
	"IPC_STAT64",	/* 14 */
};

#define	SEMCMDMIN	0
#define	SEMCMDMAX	IPC_STAT64
const char *const SEMCMDname[SEMCMDMAX+1] = {
	NULL,		/* 0 */
	NULL,		/* 1 */
	NULL,		/* 2 */
	"GETNCNT",	/* 3 */
	"GETPID",	/* 4 */
	"GETVAL",	/* 5 */
	"GETALL",	/* 6 */
	"GETZCNT",	/* 7 */
	"SETVAL",	/* 8 */
	"SETALL",	/* 9 */
	"IPC_RMID",	/* 10 */
	"IPC_SET",	/* 11 */
	"IPC_STAT",	/* 12 */
	"IPC_SET64",	/* 13 */
	"IPC_STAT64",	/* 14 */
};

#define	SHMCMDMIN	0
#define	SHMCMDMAX	IPC_XSTAT64
const char *const SHMCMDname[SHMCMDMAX+1] = {
	NULL,		/* 0 */
	NULL,		/* 1 */
	NULL,		/* 2 */
	"SHM_LOCK",	/* 3 */
	"SHM_UNLOCK",	/* 4 */
	NULL, NULL, NULL, NULL, NULL,			/* 5 NULLs */
	"IPC_RMID",	/* 10 */
	"IPC_SET",	/* 11 */
	"IPC_STAT",	/* 12 */
	"IPC_SET64",	/* 13 */
	"IPC_STAT64",	/* 14 */
	"IPC_XSTAT64",	/* 15 */
};

const char *
msgcmd(int arg)
{
	const char *str = NULL;

	if (arg >= MSGCMDMIN && arg <= MSGCMDMAX)
		str = MSGCMDname[arg-MSGCMDMIN];
	return (str);
}

const char *
semcmd(int arg)
{
	const char *str = NULL;

	if (arg >= SEMCMDMIN && arg <= SEMCMDMAX)
		str = SEMCMDname[arg-SEMCMDMIN];
	return (str);
}

const char *
shmcmd(int arg)
{
	const char *str = NULL;

	if (arg >= SHMCMDMIN && arg <= SHMCMDMAX)
		str = SHMCMDname[arg-SHMCMDMIN];
	return (str);
}

const char *
strrdopt(int arg)	/* streams read option (I_SRDOPT I_GRDOPT) */
{
	const char *str = NULL;

	switch (arg) {
	case RNORM:	str = "RNORM";		break;
	case RMSGD:	str = "RMSGD";		break;
	case RMSGN:	str = "RMSGN";		break;
	}

	return (str);
}

/* bit map of streams events (I_SETSIG & I_GETSIG) */
const char *
strevents(private_t *pri, int arg)
{
	char *str = pri->code_buf;

	if (arg & ~(S_INPUT|S_HIPRI|S_OUTPUT|S_MSG|S_ERROR|S_HANGUP))
		return ((char *)NULL);

	*str = '\0';
	if (arg & S_INPUT)
		(void) strcat(str, "|S_INPUT");
	if (arg & S_HIPRI)
		(void) strcat(str, "|S_HIPRI");
	if (arg & S_OUTPUT)
		(void) strcat(str, "|S_OUTPUT");
	if (arg & S_MSG)
		(void) strcat(str, "|S_MSG");
	if (arg & S_ERROR)
		(void) strcat(str, "|S_ERROR");
	if (arg & S_HANGUP)
		(void) strcat(str, "|S_HANGUP");

	return ((const char *)(str+1));
}

const char *
tiocflush(private_t *pri, int arg)	/* bit map passsed by TIOCFLUSH */
{
	char *str = pri->code_buf;

	if (arg & ~(FREAD|FWRITE))
		return ((char *)NULL);

	*str = '\0';
	if (arg & FREAD)
		(void) strcat(str, "|FREAD");
	if (arg & FWRITE)
		(void) strcat(str, "|FWRITE");

	return ((const char *)(str+1));
}

const char *
strflush(int arg)	/* streams flush option (I_FLUSH) */
{
	const char *str = NULL;

	switch (arg) {
	case FLUSHR:	str = "FLUSHR";		break;
	case FLUSHW:	str = "FLUSHW";		break;
	case FLUSHRW:	str = "FLUSHRW";	break;
	}

	return (str);
}

#define	ALL_MOUNT_FLAGS	(MS_RDONLY|MS_FSS|MS_DATA|MS_NOSUID|MS_REMOUNT| \
	MS_NOTRUNC|MS_OVERLAY|MS_OPTIONSTR|MS_GLOBAL|MS_FORCE|MS_NOMNTTAB)

const char *
mountflags(private_t *pri, int arg)	/* bit map of mount syscall flags */
{
	char *str = pri->code_buf;
	size_t used = 0;

	if (arg & ~ALL_MOUNT_FLAGS)
		return ((char *)NULL);

	*str = '\0';
	if (arg & MS_RDONLY)
		used = strlcat(str, "|MS_RDONLY", sizeof (pri->code_buf));
	if (arg & MS_FSS)
		used = strlcat(str, "|MS_FSS", sizeof (pri->code_buf));
	if (arg & MS_DATA)
		used = strlcat(str, "|MS_DATA", sizeof (pri->code_buf));
	if (arg & MS_NOSUID)
		used = strlcat(str, "|MS_NOSUID", sizeof (pri->code_buf));
	if (arg & MS_REMOUNT)
		used = strlcat(str, "|MS_REMOUNT", sizeof (pri->code_buf));
	if (arg & MS_NOTRUNC)
		used = strlcat(str, "|MS_NOTRUNC", sizeof (pri->code_buf));
	if (arg & MS_OVERLAY)
		used = strlcat(str, "|MS_OVERLAY", sizeof (pri->code_buf));
	if (arg & MS_OPTIONSTR)
		used = strlcat(str, "|MS_OPTIONSTR", sizeof (pri->code_buf));
	if (arg & MS_GLOBAL)
		used = strlcat(str, "|MS_GLOBAL", sizeof (pri->code_buf));
	if (arg & MS_FORCE)
		used = strlcat(str, "|MS_FORCE", sizeof (pri->code_buf));
	if (arg & MS_NOMNTTAB)
		used = strlcat(str, "|MS_NOMNTTAB", sizeof (pri->code_buf));

	if (used == 0 || used >= sizeof (pri->code_buf))
		return ((char *)NULL);			/* use prt_hex() */

	return ((const char *)(str+1));
}

const char *
svfsflags(private_t *pri, ulong_t arg)	/* bit map of statvfs syscall flags */
{
	char *str = pri->code_buf;

	if (arg & ~(ST_RDONLY|ST_NOSUID|ST_NOTRUNC)) {
		(void) sprintf(str, "0x%lx", arg);
		return (str);
	}
	*str = '\0';
	if (arg & ST_RDONLY)
		(void) strcat(str, "|ST_RDONLY");
	if (arg & ST_NOSUID)
		(void) strcat(str, "|ST_NOSUID");
	if (arg & ST_NOTRUNC)
		(void) strcat(str, "|ST_NOTRUNC");
	if (*str == '\0')
		(void) strcat(str, "|0");
	return ((const char *)(str+1));
}

const char *
fuiname(int arg)	/* fusers() input argument */
{
	const char *str = NULL;

	switch (arg) {
	case F_FILE_ONLY:	str = "F_FILE_ONLY";		break;
	case F_CONTAINED:	str = "F_CONTAINED";		break;
	}

	return (str);
}

const char *
fuflags(private_t *pri, int arg)	/* fusers() output flags */
{
	char *str = pri->code_buf;

	if (arg & ~(F_CDIR|F_RDIR|F_TEXT|F_MAP|F_OPEN|F_TRACE|F_TTY)) {
		(void) sprintf(str, "0x%x", arg);
		return (str);
	}
	*str = '\0';
	if (arg & F_CDIR)
		(void) strcat(str, "|F_CDIR");
	if (arg & F_RDIR)
		(void) strcat(str, "|F_RDIR");
	if (arg & F_TEXT)
		(void) strcat(str, "|F_TEXT");
	if (arg & F_MAP)
		(void) strcat(str, "|F_MAP");
	if (arg & F_OPEN)
		(void) strcat(str, "|F_OPEN");
	if (arg & F_TRACE)
		(void) strcat(str, "|F_TRACE");
	if (arg & F_TTY)
		(void) strcat(str, "|F_TTY");
	if (*str == '\0')
		(void) strcat(str, "|0");
	return ((const char *)(str+1));
}
