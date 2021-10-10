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
 *  Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains functions that implement the fdisk menu commands.
 */
#include "global.h"
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/dktp/fdisk.h>
#include <sys/stat.h>
#include <sys/dklabel.h>
#ifdef i386
#include <libfdisk.h>
#endif

#include "main.h"
#include "analyze.h"
#include "menu.h"
#include "menu_command.h"
#include "menu_defect.h"
#include "menu_partition.h"
#include "menu_fdisk.h"
#include "param.h"
#include "misc.h"
#include "label.h"
#include "startup.h"
#include "partition.h"
#include "prompts.h"
#include "checkdev.h"
#include "io.h"
#include "ctlr_scsi.h"
#include "auto_sense.h"

extern	struct menu_item menu_fdisk[];

/*
 * Byte swapping macros for accessing struct ipart
 *	to resolve little endian on Sparc.
 */
#if defined(sparc)
#define	les(val)	((((val)&0xFF)<<8)|(((val)>>8)&0xFF))
#define	lel(val)	(((unsigned)(les((val)&0x0000FFFF))<<16) | \
			(les((unsigned)((val)&0xffff0000)>>16)))

#elif	defined(i386)

#define	les(val)	(val)
#define	lel(val)	(val)

#else	/* defined(sparc) */

#error	No Platform defined

#endif	/* defined(sparc) */


/* Function prototypes */
#ifdef	__STDC__

#if	defined(sparc)

static int getbyte(uchar_t **);
static int getlong(uchar_t **);

#endif	/* defined(sparc) */

static int get_solaris_part(int fd, struct ipart *ipart);
static int update_format_globals(unsigned char systid);

#else	/* __STDC__ */

#if	defined(sparc)

static int getbyte();
static int getlong();

#endif	/* defined(sparc) */

static int get_solaris_part();
static int update_format_globals();


#endif	/* __STDC__ */

#ifdef i386
int  extpart_init(ext_part_t **epp);
void extpart_fini(ext_part_t **epp);
#endif

/*
 * Data structures and functions used in the MBR (Master Boot Record)
 * iteration API.
 *
 * Caller uses mbr_iter_xxx() functions to init, iterate, and destruct
 * state needed to iterate over the elements of an MBR's Partition Table.
 */
typedef struct mbr_iter {
	int		fd;	/* file descriptor of MBR's associated file */
	struct mboot	mboot;	/* MBR, in Little-Endian format */
#ifdef i386
	/* For processing DOS Extended Partitions. */
	ext_part_t	*epp;
#endif
} mbr_iter_t;

/* Value returned on each call to the iteration handler (mbr_hdlr_t). */
typedef enum {
	MBR_ITER_CONT = 0,	/* continue iterating */
	MBR_ITER_DONE,		/* terminate iteration w/o err */
	MBR_ITER_ERR		/* terminate iteration w/ err */
} mbr_iter_status_t;

/* Caller-defined function that is called for each table entry. */
typedef mbr_iter_status_t (*mbr_hdlr_t)(mbr_iter_t *, void *call_state, int idx,
    struct ipart *ip);

/*
 * MBR iteration API:
 *
 * mbr_iter_init: initializes the iterator's state, mbr_iter_t.
 * mbr_iter_fini: deallocates the state
 * mbr_iter:      uses the state to iterate an MBR's Partition Table.
 *                Note that this can be called any number of times
 *                (before calling the _fini function).
 */
static int mbr_iter_init(mbr_iter_t **pp);
static mbr_iter_status_t mbr_iter(mbr_iter_t *p, mbr_hdlr_t h,
    void *caller_state);
static int mbr_iter_fini(mbr_iter_t *p);
#ifdef i386
static ext_part_t *mbr_iter_get_ext_part(mbr_iter_t *p);
#endif


/* State used by an MBR Iterator that returns a Solaris partition's metadata. */
typedef struct mi_sol_part {
	int		found;		/* SolPart found in MBR or ExtPart ? */
	int		found_in_extpart;	/* SolPart in ExtPart ? */
	int		sol_idx;	/* index of SolPart in MBR/ExtPart */
	int		ext_part_found;	/* is Extended Partition found ? */
	/* translated from Little-endian: */
	uint32_t	relsect;	/* start addr of Solaris partition */
	uint32_t	numsect; 	/* number of sectors */
} mi_sol_part_t;

/*
 * State used by an MBR Iterator that looks for closest "neighbor" of
 * a specified Target partition.
 */
typedef struct mi_target_neighbor {
	int		found;		/* is there a Neighbor ? */
	int		target_idx;	/* "find a Neighbor of this part." */
	uint32_t   	target_end;	/* end-addr of Target's partition */
	uint32_t	neighbor_start;	/* start-addr translated from LE */
} mi_target_neighbor_t;


/*
 * Handling the alignment problem of struct ipart.
 */
static void
fill_ipart(char *bootptr, struct ipart *partp)
{
#if defined(sparc)
	/*
	 * Sparc platform:
	 *
	 * Packing short/word for struct ipart to resolve
	 *	little endian on Sparc since it is not
	 *	properly aligned on Sparc.
	 */
	partp->bootid = getbyte((uchar_t **)&bootptr);
	partp->beghead = getbyte((uchar_t **)&bootptr);
	partp->begsect = getbyte((uchar_t **)&bootptr);
	partp->begcyl = getbyte((uchar_t **)&bootptr);
	partp->systid = getbyte((uchar_t **)&bootptr);
	partp->endhead = getbyte((uchar_t **)&bootptr);
	partp->endsect = getbyte((uchar_t **)&bootptr);
	partp->endcyl = getbyte((uchar_t **)&bootptr);
	partp->relsect = getlong((uchar_t **)&bootptr);
	partp->numsect = getlong((uchar_t **)&bootptr);
#elif defined(i386)
	/*
	 * i386 platform:
	 *
	 * The fdisk table does not begin on a 4-byte boundary within
	 * the master boot record; so, we need to recopy its contents
	 * to another data structure to avoid an alignment exception.
	 */
	(void) bcopy(bootptr, partp, sizeof (struct ipart));
#else
#error  No Platform defined
#endif /* defined(sparc) */
}

/*
 * Get a correct byte/short/word routines for Sparc platform.
 */
#if defined(sparc)
static int
getbyte(uchar_t **bp)
{
	int	b;

	b = **bp;
	*bp = *bp + 1;
	return (b);
}

#ifdef DEADCODE
static int
getshort(uchar_t **bp)
{
	int	b;

	b = ((**bp) << 8) | *(*bp + 1);
	*bp += 2;
	return (b);
}
#endif /* DEADCODE */

static int
getlong(uchar_t **bp)
{
	int	b, bh, bl;

	bh = ((**bp) << 8) | *(*bp + 1);
	*bp += 2;
	bl = ((**bp) << 8) | *(*bp + 1);
	*bp += 2;

	b = (bh << 16) | bl;
	return (b);
}
#endif /* defined(sparc) */

#ifdef i386
/*
 * Convert emcpowerN[a-p,p0,p1,p2,p3,p4] to emcpowerNp0 path,
 * this is specific for emc powerpath driver.
 */
static void
get_emcpower_pname(char *name, char *devname)
{
	char	*emcp = "emcpower";
	char	*npt = NULL;
	char	np[MAXNAMELEN];
	int	i = strlen(emcp);

	(void) strcpy(np, devname);
	npt = strstr(np, emcp);
	while ((i < strlen(npt)) && (isdigit(npt[i])))
		i++;
	npt[i] = '\0';
	(void) snprintf(name, MAXNAMELEN, "/dev/rdsk/%sp0", npt);
}
#endif

/*
 * Convert cn[tn]dn to cn[tn]dns2 path
 */
static void
get_sname(char *name)
{
	char		buf[MAXPATHLEN];
	char		*devp = "/dev/dsk";
	char		*rdevp = "/dev/rdsk";
	char		np[MAXNAMELEN];
	char		*npt;

#ifdef i386
	if (emcpower_name(cur_disk->disk_name)) {
		get_emcpower_pname(name, cur_disk->disk_name);
		return;
	}
#endif

	/*
	 * If it is a full path /dev/[r]dsk/cn[tn]dn, use this path
	 */
	(void) strcpy(np, cur_disk->disk_name);
	if (strncmp(rdevp, cur_disk->disk_name, strlen(rdevp)) == 0 ||
	    strncmp(devp, cur_disk->disk_name, strlen(devp)) == 0) {
		/*
		 * Skip if the path is already included with sN
		 */
		if (strchr(np, 's') == strrchr(np, 's')) {
			npt = strrchr(np, 'p');
			/* If pN is found, do not include it */
			if (npt != NULL) {
				*npt = '\0';
			}
			(void) snprintf(buf, sizeof (buf), "%ss2", np);
		} else {
			(void) snprintf(buf, sizeof (buf), "%s", np);
		}
	} else {
		(void) snprintf(buf, sizeof (buf), "/dev/rdsk/%ss2", np);
	}
	(void) strcpy(name, buf);
}

/*
 * Convert cn[tn]dnsn to cn[tn]dnp0 path
 */
void
get_pname(char *name)
{
	char		buf[MAXPATHLEN];
	char		*devp = "/dev/dsk";
	char		*rdevp = "/dev/rdsk";
	char		np[MAXNAMELEN];
	char		*npt;

	/*
	 * If it is a full path /dev/[r]dsk/cn[tn]dnsn, use this path
	 */
	if (cur_disk == NULL) {
		(void) strcpy(np, x86_devname);
	} else {
		(void) strcpy(np, cur_disk->disk_name);
	}

#ifdef i386
	if (emcpower_name(np)) {
		get_emcpower_pname(name, np);
		return;
	}
#endif

	if (strncmp(rdevp, np, strlen(rdevp)) == 0 ||
	    strncmp(devp, np, strlen(devp)) == 0) {
		/*
		 * Skip if the path is already included with pN
		 */
		if (strchr(np, 'p') == NULL) {
			npt = strrchr(np, 's');
			/* If sN is found, do not include it */
			if (isdigit(*++npt)) {
				*--npt = '\0';
			}
			(void) snprintf(buf, sizeof (buf), "%sp0", np);
		} else {
			(void) snprintf(buf, sizeof (buf), "%s", np);
		}
	} else {
		(void) snprintf(buf, sizeof (buf), "/dev/rdsk/%sp0", np);
	}
	(void) strcpy(name, buf);
}

/*
 * Open file descriptor for current disk (cur_file)
 *	with "p0" path or cur_disk->disk_path
 */
void
open_cur_file(int mode)
{
	char	*dkpath;
	char	pbuf[MAXPATHLEN];

	switch (mode) {
		case FD_USE_P0_PATH:
			(void) get_pname(&pbuf[0]);
			dkpath = pbuf;
			break;
		case FD_USE_CUR_DISK_PATH:
			if (cur_disk->fdisk_part.systid == SUNIXOS ||
			    cur_disk->fdisk_part.systid == SUNIXOS2) {
				(void) get_sname(&pbuf[0]);
				dkpath = pbuf;
			} else {
				dkpath = cur_disk->disk_path;
			}
			break;
		default:
			err_print("Error: Invalid mode option for opening "
			    "cur_file\n");
			fullabort();
	}

	/* Close previous cur_file */
	(void) close(cur_file);
	/* Open cur_file with the required path dkpath */
	if ((cur_file = open_disk(dkpath, O_RDWR | O_NDELAY)) < 0) {
		err_print(
		    "Error: can't open selected disk '%s'.\n", dkpath);
		fullabort();
	}
}


/*
 * This routine implements the 'fdisk' command.  It simply runs
 * the fdisk command on the current disk.
 * Use of this is restricted to interactive mode only.
 */
int
c_fdisk()
{

	char		buf[MAXPATHLEN];
	char		pbuf[MAXPATHLEN];
	struct stat	statbuf;

	/*
	 * We must be in interactive mode to use the fdisk command, unless
	 * we force to label this disk.
	 */
	if ((!force_label) &&
	    (option_f != (char *)NULL || isatty(0) != 1 || isatty(1) != 1)) {
		err_print("Fdisk command is for interactive use only!\n");
		return (-1);
	}
	/*
	 * There must be a current disk type and a current disk
	 */
	if (cur_dtype == NULL) {
		err_print("Current Disk Type is not set.\n");
		return (-1);
	}

	/*
	 * Before running the fdisk command, get file status of
	 *	/dev/rdsk/cn[tn]dnp0 path to see if this disk
	 *	supports fixed disk partition table.
	 */
	(void) get_pname(&pbuf[0]);
	if (stat(pbuf, (struct stat *)&statbuf) == -1 ||
	    !S_ISCHR(statbuf.st_mode)) {
		err_print(
		"Disk does not support fixed disk partition table\n");
		return (0);
	}

	/*
	 * Run the fdisk program.
	 */
	if (force_label) {
		/* Put a vtoc label to the disk noninteractively */
		if (force_label_type == L_TYPE_SOLARIS) {
			(void) snprintf(buf, sizeof (buf),
			    "fdisk -B %s \n", pbuf);
		} else {
			/* Will not come here */
			return (-1);
		}
	} else {
		(void) snprintf(buf, sizeof (buf), "fdisk %s\n", pbuf);
	}
	(void) system(buf);

	/*
	 * Open cur_file with "p0" path for accessing the fdisk table
	 */
	(void) open_cur_file(FD_USE_P0_PATH);

	/*
	 * Get solaris partition information in the fdisk partition table
	 */
	if (get_solaris_part(cur_file, &cur_disk->fdisk_part) == -1) {
		err_print("No fdisk solaris partition found\n");
		cur_disk->fdisk_part.numsect = 0;  /* No Solaris */
	}

	/*
	 * Restore cur_file with cur_disk->disk_path
	 */
	(void) open_cur_file(FD_USE_CUR_DISK_PATH);

	return (0);
}

/*
 * Read MBR on the disk
 * if the Solaris partition has changed,
 *	reread the vtoc
 */
#ifdef DEADCODE
static void
update_cur_parts()
{

	int i;
	register struct partition_info *parts;

	for (i = 0; i < NDKMAP; i++) {
#if defined(_SUNOS_VTOC_16)
		if (cur_parts->vtoc.v_part[i].p_tag &&
		    cur_parts->vtoc.v_part[i].p_tag != V_ALTSCTR) {
			cur_parts->vtoc.v_part[i].p_start = 0;
			cur_parts->vtoc.v_part[i].p_size = 0;

#endif
			cur_parts->pinfo_map[i].dkl_nblk = 0;
			cur_parts->pinfo_map[i].dkl_cylno = 0;
			cur_parts->vtoc.v_part[i].p_tag =
			    default_vtoc_map[i].p_tag;
			cur_parts->vtoc.v_part[i].p_flag =
			    default_vtoc_map[i].p_flag;
#if defined(_SUNOS_VTOC_16)
		}
#endif
	}
	cur_parts->pinfo_map[C_PARTITION].dkl_nblk = ncyl * spc();

#if defined(_SUNOS_VTOC_16)
	/*
	 * Adjust for the boot partitions
	 */
	cur_parts->pinfo_map[I_PARTITION].dkl_nblk = spc();
	cur_parts->pinfo_map[I_PARTITION].dkl_cylno = 0;
	cur_parts->vtoc.v_part[C_PARTITION].p_start =
	    cur_parts->pinfo_map[C_PARTITION].dkl_cylno * nhead * nsect;
	cur_parts->vtoc.v_part[C_PARTITION].p_size =
	    cur_parts->pinfo_map[C_PARTITION].dkl_nblk;

	cur_parts->vtoc.v_part[I_PARTITION].p_start =
	    cur_parts->pinfo_map[I_PARTITION].dkl_cylno;
	cur_parts->vtoc.v_part[I_PARTITION].p_size =
	    cur_parts->pinfo_map[I_PARTITION].dkl_nblk;

#endif	/* defined(_SUNOS_VTOC_16) */
	parts = cur_dtype->dtype_plist;
	cur_dtype->dtype_ncyl = ncyl;
	cur_dtype->dtype_plist = cur_parts;
	parts->pinfo_name = cur_parts->pinfo_name;
	cur_disk->disk_parts = cur_parts;
	cur_ctype->ctype_dlist = cur_dtype;

}
#endif /* DEADCODE */

/*
 * mbr_iter_init
 *
 * Initializes the state so that one or more calls to mbr_iter can
 * iterate over the elements of the Partition Table in the MBR residing
 * on the cur_disk device.
 *
 * Use get_pname to open() the "p0" (raw device) that cur_disk references.  Read
 * that device's MBR and copy it into the iterator's state.  mbr_iter_fini must
 * be called to deallocate this _init function's resources.
 *
 * Inputs:
 *  cur_disk	- name of device that MBR is expected to reside on
 *  cur_blksz	- number of bytes in Read of MBR
 *
 * Outputs:
 *  mbr_iter_t	- allocated and initialized struct that must be passed to
 *                other mbr_iter_xxx functions
 *
 * Return value:
 *  0		- if no error
 *  non-zero	- if a fatal error
 *			- EINVAL is returned if an MBR is not found.
 */
static int
mbr_iter_init(mbr_iter_t **iterpp)
{
	mbr_iter_t	*iterp;
	int		status;
	struct stat	statbuf;
	char		*mbr;
	char		buf[MAXPATHLEN];

	assert(iterpp != NULL);

	/* Compute "p0" name of the cur_disk device. */
	(void) get_pname(buf);
	if ((stat(buf, &statbuf) == -1) || !S_ISCHR(statbuf.st_mode)) {
		return (EBADF);
	}

	/* Allocate memory for iterator's state. */
	iterp = calloc(1, sizeof (mbr_iter_t));
	if (iterp == NULL) {
		*iterpp = NULL;
		return (ENOMEM);
	}

	/* Open the raw file (which contains the MBR). */
	if ((iterp->fd = open(buf, O_RDONLY)) < 0) {
		free(iterp);
		return (EBADF);
	}

	/*
	 * Allocate transient memory to hold the MBR that will be read.  Note
	 * that cur_blksz and the bytelength of the MBR are not necessarily
	 * the same.
	 */
	mbr = malloc(cur_blksz);
	if (mbr == NULL) {
		free(iterp);
		return (ENOMEM);
	}

	/* Read MBR into buffer. */
	status = read(iterp->fd, mbr, cur_blksz);
	if (status != cur_blksz) {
		free(iterp);
		free(mbr);
		return (EIO);
	}

	/* Copy the MBR into the iterator's state. */
	(void) memcpy(&iterp->mboot, mbr, sizeof (struct mboot));

	/* Deallocate the transient IO buffer. */
	free(mbr);

	/* Confirm that data actually contains an MBR. */
	if (les(iterp->mboot.signature) != MBB_MAGIC)  {
		free(iterp);
		return (EINVAL);
	}

#ifdef i386
	/*
	 * Init support for DOS Extended Partitions (eg, a Solaris partition
	 * can reside within a (Primary) DOS Extended Partition).
	 */
	(void) extpart_init(&iterp->epp);
#endif
	/*
	 * Return address of iterator's state.  Caller passes this address
	 * to all mbr_iter_xxx functions.
	 */
	*iterpp = iterp;

	return (0);
}

/*
 * mbr_iter
 *
 * Commences an iteration of the Partition Table represented by the
 * mbr_iter_t argument.  For each Table element, the caller-specified
 * handler is called, passing both the caller's state and the
 * iterator's state.
 *
 * Note that this function can be called more than once (before calling
 * mbr_iter_fini).
 */
static mbr_iter_status_t
mbr_iter(mbr_iter_t *iterp, mbr_hdlr_t hdlr, void *caller_state)
{
	struct ipart		ip;
	int			i;
	mbr_iter_status_t	rv;

	assert(iterp != NULL);
	assert(hdlr != NULL);

	for (i = 0; i < FD_NUMPART; i++) {
		int	ipc;
		/*
		 * The MBR's partition table is not properly aligned; copy
		 * current element into a naturally-aligned struct.
		 */
		ipc = i * sizeof (struct ipart);
		(void) fill_ipart(&iterp->mboot.parts[ipc], &ip);

		/* call caller's handler */
		rv = (*hdlr)(iterp, caller_state, i, &ip);

		if (rv != MBR_ITER_CONT) {
			/* then do not continue iterating. */
			return (rv);
		}
	}

	return (MBR_ITER_DONE);
}

/*
 * mbr_iter_fini
 *
 * Deallocates iterator state created by mbr_iter_init.
 */
static int
mbr_iter_fini(mbr_iter_t *iterp)
{
#ifdef i386
	extpart_fini(&iterp->epp);
#endif
	if (iterp->fd >= 0) {
		(void) close(iterp->fd);
		iterp->fd = -1;
	}

	free(iterp);
	return (0);
}

#ifdef i386
/*
 * mbr_iter_get_ext_part
 *
 * Returns the address of the ext_part_t structure, which is used to process a
 * DOS Extended Partition.
 */
static ext_part_t *
mbr_iter_get_ext_part(mbr_iter_t *iterp)
{
	return (iterp->epp);
}
#endif

static int
get_solaris_part(int fd, struct ipart *ipart)
{
	int		i;
	struct ipart	ip;
	struct ipart	selected;
	int		status;
	char		*mbr;
	char		*bootptr;
	ushort_t	found = 0;
#ifdef i386
	uint32_t	relsec, numsec;
	int		pno, rval, ext_part_found = 0;
	ext_part_t	*epp;
#endif

	(void) lseek(fd, 0, 0);

	/*
	 * We may get mbr of different size, but the first 512 bytes
	 * are valid information.
	 */
	mbr = malloc(cur_blksz);
	if (mbr == NULL) {
		err_print("No memory available.\n");
		return (-1);
	}
	status = read(fd, mbr, cur_blksz);

	if (status != cur_blksz) {
		err_print("Bad read of fdisk partition. Status = %x\n", status);
		err_print("Cannot read fdisk partition information.\n");
		free(mbr);
		return (-1);
	}

	(void) memcpy(&boot_sec, mbr, sizeof (struct mboot));
	free(mbr);

#ifdef i386
	(void) extpart_init(&epp);
#endif
	for (i = 0; i < FD_NUMPART; i++) {
		int	ipc;

		ipc = i * sizeof (struct ipart);

		/* Handling the alignment problem of struct ipart */
		bootptr = &boot_sec.parts[ipc];
		(void) fill_ipart(bootptr, &ip);

#ifdef i386
		if (fdisk_is_dos_extended(ip.systid) && (ext_part_found == 0)) {
			/* We support only one extended partition per disk */
			ext_part_found = 1;
			rval = fdisk_get_solaris_part(epp, &pno, &relsec,
			    &numsec);
			if (rval == FDISK_SUCCESS) {
				/*
				 * Found a solaris partition inside the
				 * extended partition. Update the statistics.
				 */
				if (nhead != 0 && nsect != 0) {
					pcyl = numsec / (nhead * nsect);
					xstart = relsec / (nhead * nsect);
					ncyl = pcyl - acyl;
				}
				solaris_offset = relsec;
				found = 2;
				ip.bootid = 0;
				ip.beghead = ip.begsect = ip.begcyl = 0xff;
				ip.endhead = ip.endsect = ip.endcyl = 0xff;
				ip.systid = SUNIXOS2;
				ip.relsect = relsec;
				ip.numsect = numsec;
				ipart->bootid = ip.bootid;
				status = bcmp(&ip, ipart,
				    sizeof (struct ipart));
				bcopy(&ip, ipart, sizeof (struct ipart));
			}
			continue;
		}
#endif

		/*
		 * we are interested in Solaris and EFI partition types
		 */
#ifdef i386
		if ((ip.systid == SUNIXOS &&
		    (fdisk_is_linux_swap(epp, lel(ip.relsect), NULL) != 0)) ||
		    ip.systid == SUNIXOS2 ||
		    ip.systid == EFI_PMBR) {
#else
		if (ip.systid == SUNIXOS ||
		    ip.systid == SUNIXOS2 ||
		    ip.systid == EFI_PMBR) {
#endif
			/*
			 * use the last active solaris partition id found
			 * (there should only be 1 active partition id)
			 * if there are no active solaris partition id
			 * then use the first inactive solaris partition id
			 */
			if (found == 0 || ip.bootid == ACTIVE) {

				found = 1;
				bcopy(&ip, &selected, sizeof (struct ipart));

				/*
				 * there be only 1 active partition
				 */
				if (ip.bootid == ACTIVE)
					break;
			}
		}
	}

#ifdef i386
	extpart_fini(&epp);
#endif

	if (!found) {
		err_print("Solaris fdisk partition not found\n");
		return (-1);
	} else if (found == 1) {
		/*
		 * Found a primary solaris partition.
		 * compare the previous and current Solaris partition.
		 * Include use bootid in determination of Solaris partition
		 * changes
		 */
		status = bcmp(&selected, ipart, sizeof (struct ipart));
		bcopy(&selected, ipart, sizeof (struct ipart));

		/*
		 * if the disk has an EFI label, nhead and nsect may
		 * be zero.  This test protects us from FPE's, and
		 * format still seems to work fine
		 */
		if (nhead != 0 && nsect != 0) {
			pcyl = lel(selected.numsect) / (nhead * nsect);
			xstart = lel(selected.relsect) / (nhead * nsect);
			ncyl = pcyl - acyl;
		}
#ifdef DEBUG
		else {
			err_print("Critical geometry values are zero:\n"
			    "\tnhead = %d; nsect = %d\n", nhead, nsect);
		}
#endif /* DEBUG */

		solaris_offset = (uint_t)lel(selected.relsect);

	}

	/*
	 * if the disk partitioning has changed
	 * update format global variables
	 */
	if (status) {
		status = update_format_globals(ipart->systid);
		if (status != 0) {
			err_print("update_format_globals failed.\n");
			return (-1);
		}
	}
	return (0);
}

static int
update_format_globals(unsigned char systid)
{
	char	*dkpath = NULL;
	char	pbuf[MAXPATHLEN];
	struct disk_type	*dptr = NULL;

	/*
	 * Handle Solaris partition
	 */
	if (systid == SUNIXOS || systid == SUNIXOS2) {
		struct dk_label 	search_label;
		struct disk_type	*dp = NULL;
		struct partition_info	*part = NULL;
		struct partition_info	*pt = NULL;
		int status = 0;
		int i = 0;

		(void) get_sname(&pbuf[0]);
		dkpath = pbuf;

		/* Close previous cur_file */
		(void) close(cur_file);

		/* Open cur_file with the required path dkpath */
		if ((cur_file = open_disk(dkpath, O_RDWR | O_NDELAY)) < 0) {
			err_print(
			    "Error: can't open selected disk '%s'.\n", dkpath);
			return (-1);
		}

		status = read_label(cur_file, &search_label);

		if (status == -1) {
			err_print("Cannot read label information.\n");
			return (-1);
		}

		if (delete_disk_type(cur_disk->disk_type) != 0) {
			err_print("delete_disk_type failed "
			    "when systid is SUNIXOS. \n");
		}

		/*
		 * Allocate a new disk type for the SCSI controller.
		 */
		dptr = (struct disk_type *)zalloc(sizeof (struct disk_type));

		/*
		 * Link the disk into the list of disks
		 */
		dp = cur_ctlr->ctlr_ctype->ctype_dlist;
		if (dp == NULL) {
			cur_ctlr->ctlr_ctype->ctype_dlist = dptr;
		} else {
			while (dp->dtype_next != NULL) {
				dp = dp->dtype_next;
			}
			dp->dtype_next = dptr;
		}
		dptr->dtype_next = NULL;

		/*
		 * Allocate and initialize the disk name.
		 */
		dptr->dtype_asciilabel = alloc_string(
		    search_label.dkl_asciilabel);

		/*
		 * Initialize disk geometry info
		 */
		dptr->dtype_pcyl = search_label.dkl_pcyl;
		dptr->dtype_ncyl = search_label.dkl_ncyl;
		dptr->dtype_acyl = search_label.dkl_acyl;
		dptr->dtype_nhead = search_label.dkl_nhead;
		dptr->dtype_nsect = search_label.dkl_nsect;
		dptr->dtype_rpm = search_label.dkl_rpm;

		/*
		 * Allocate partition info
		 */
		part = (struct partition_info *)
		    zalloc(sizeof (struct partition_info));

		/*
		 * Link the partition into the list of partition
		 */
		pt = dptr->dtype_plist;
		if (pt == NULL) {
			dptr->dtype_plist = part;
		} else {

			while (pt->pinfo_next != NULL) {
				pt = pt->pinfo_next;
			}
			pt->pinfo_next = part;
		}
		part->pinfo_next = NULL;

		/*
		 * Set up the partition name
		 */
		part->pinfo_name = alloc_string("default");

		/*
		 * Fill in the partition info from the label
		 */
		for (i = 0; i < NDKMAP; i++) {

#if defined(_SUNOS_VTOC_8)
			part->pinfo_map[i] = search_label.dkl_map[i];

#elif defined(_SUNOS_VTOC_16)
			part->pinfo_map[i].dkl_cylno =
			    search_label.dkl_vtoc.v_part[i].p_start /
			    ((blkaddr32_t)(dptr->dtype_nhead *
			    dptr->dtype_nsect - apc));
			part->pinfo_map[i].dkl_nblk =
			    search_label.dkl_vtoc.v_part[i].p_size;
#else
#error No VTOC format defined.
#endif	/* defined(_SUNOS_VTOC_8) */
		}

		/*
		 * Use the VTOC if valid, or install a default
		 */
		if (search_label.dkl_vtoc.v_version == V_VERSION) {
			(void) memcpy(cur_disk->v_volume,
			    search_label.dkl_vtoc.v_volume,
			    LEN_DKL_VVOL);
			part->vtoc = search_label.dkl_vtoc;
		} else {
			(void) memset(cur_disk->v_volume, 0, LEN_DKL_VVOL);
			set_vtoc_defaults(part);
		}

		/*
		 * Intialize format global variable
		 */
		pcyl = search_label.dkl_pcyl;
		ncyl = search_label.dkl_ncyl;
		acyl = search_label.dkl_acyl;
		nhead = search_label.dkl_nhead;
		nsect = search_label.dkl_nsect;

		cur_label = L_TYPE_SOLARIS;
		cur_disk->label_type = L_TYPE_SOLARIS;
		cur_disk->disk_type = dptr;
		cur_disk->disk_parts = dptr->dtype_plist;
		cur_dtype = dptr;
		cur_parts = dptr->dtype_plist;

		return (0);

	} else {
		struct efi_info	efinfo;

		(void) get_pname(&pbuf[0]);
		dkpath = pbuf;

		/* Close previous cur_file */
		(void) close(cur_file);

		/* Open cur_file with the required path dkpath */
		if ((cur_file = open_disk(dkpath, O_RDWR | O_NDELAY)) < 0) {
			err_print(
			    "Error: can't open selected disk '%s'.\n", dkpath);
			return (-1);
		}

		/*
		 * Since EFI is a full disk partition, so if EFI partition
		 * is changed, there must be from other partition to EFI.
		 * EFI to EFI is impossible. According to this background,
		 * get EFI partition info directly
		 */
		dptr = auto_efi_sense(cur_file, &efinfo);
		if (dptr == NULL) {
			err_print("auto_efi_sense failed.\n");
			return (-1);
		}

		if (delete_disk_type(cur_disk->disk_type) != 0) {
			err_print("delete_disk_type failed when "
			    "systid is EFI_PMBR. \n");
		}

		/*
		 * Intialize format global variable
		 */
		cur_label = L_TYPE_EFI;
		cur_disk->label_type = L_TYPE_EFI;
		cur_disk->disk_type = dptr;
		cur_disk->disk_parts = dptr->dtype_plist;
		cur_dtype = dptr;
		cur_parts = dptr->dtype_plist;
		cur_parts->etoc = efinfo.e_parts;

		ncyl = pcyl = nsect = psect = acyl = phead = 0;

		return (0);
	}
}

/*
 * mbr_hdlr_find_sol_part
 *
 * MBR Iterator that finds a Solaris partition (if any).
 *
 * This iterator looks for the Solaris partition in an MBR or in
 * a DOS Extended Partition (if present).  When iteration is
 * complete, the state in mi_sol_part_t contains these values:
 *
 *  .found		- whether a Solaris partition was found (in MBR
 *                         or ExtPart)
 *  .found_in_extpart	- whether a Solaris partition was found in ExtPart
 *  .sol_idx		- index of Solaris partition
 *  .relsect		- starting addr of Solaris partition
 *  .numsect		- length of Solaris partition (in sectors)
 */
/*ARGSUSED*/
static mbr_iter_status_t
mbr_hdlr_find_sol_part(mbr_iter_t *iterp, void *call_statep, int index,
    struct ipart *ip)
{
	mi_sol_part_t	*solp = (mi_sol_part_t *)call_statep;
#ifdef i386
	ext_part_t	*epp;
#endif

#ifdef i386
	epp = mbr_iter_get_ext_part(iterp);

	if (ip->systid == SUNIXOS2 || ip->systid == EFI_PMBR ||
	    (ip->systid == SUNIXOS &&
	    (fdisk_is_linux_swap(epp, lel(ip->relsect), NULL) != 0))) {
#else
	if (ip->systid == SUNIXOS || ip->systid == SUNIXOS2 ||
	    ip->systid == EFI_PMBR) {
#endif
		solp->found = 1;

		/*
		 * Index into MBR's Partition Table.  Values range from
		 * 0-3, which map onto devices "p1" through "p4".
		 */
		solp->sol_idx = index;

		solp->relsect = lel(ip->relsect);
		solp->numsect = lel(ip->numsect);
#ifdef DEBUG
		fmt_print("Found Solaris partition in MBR[%d]\n", index);
#endif
		return (MBR_ITER_DONE);
	}

#ifdef i386
	if (fdisk_is_dos_extended(ip->systid) && (solp->ext_part_found == 0)) {
		int		rval;
		int		part_num;
		uint32_t	relsect;
		uint32_t	numsect;

		/* Solaris supports only one Extended Partition per disk */
		solp->ext_part_found = 1;

		/* Is there a Solaris Partition in Extended Partition ? */
		epp = mbr_iter_get_ext_part(iterp);
		rval = fdisk_get_solaris_part(epp, &part_num, &relsect,
		    &numsect);
		if (rval == FDISK_SUCCESS) {
			solp->found = 1;
			solp->found_in_extpart = 1;

			/*
			 * The first partition in an Extended Partition is "p5",
			 * which is used as a suffix in the device-path to the
			 * partition.  The second partition is "p6", and so on.
			 */
			solp->sol_idx = part_num;
			solp->relsect = relsect;
			solp->numsect = numsect;
#ifdef DEBUG
			fmt_print("Found Solaris partition in ExtPart p%d  "
			    "start: %u  len: %u\n", part_num, solp->relsect,
			    solp->numsect);
#endif
			return (MBR_ITER_DONE);
		}
#ifdef DEBUG
		if (rval != FDISK_SUCCESS) {
			fmt_print("ExtPart does not have a Solaris partition.");
		}
#endif
	}
#endif
	return (MBR_ITER_CONT);
}

/*
 * mbr_hdlr_find_neighbor_part
 *
 * MBR Iterator that finds (if any) the "neighbor" of a specified
 * Target partition.  The Neighbor of the Target partition is the
 * partition whose start-addr is closest to the Target partition's
 * end-addr.
 *
 * After iteration, mi_target_neighbor_t contains the results:
 *
 *  .found		- a Boolean determining whether a Neighbor was found
 *  .neighbor_start	- start-addr of Neighbor (translated from Little-endian)
 */
/*ARGSUSED*/
static mbr_iter_status_t
mbr_hdlr_find_neighbor_part(mbr_iter_t *iterp, void *call_statep, int index,
    struct ipart *ip)
{
	uint32_t		start_addr;
	mi_target_neighbor_t	*neighborp;

	neighborp = (mi_target_neighbor_t *)call_statep;

	/* The Neighbor cannot be the same as the Target. */
	if (index == neighborp->target_idx) {
		return (MBR_ITER_CONT);
	}

	start_addr = lel(ip->relsect);

	/* Check if current partition can be a Neighbor. */
	if (start_addr > neighborp->target_end && start_addr <
	    neighborp->neighbor_start) {
		neighborp->found = 1;
		neighborp->neighbor_start = start_addr;

		return (MBR_ITER_DONE);
	}

	return (MBR_ITER_CONT);
}

/*
 * get_solaris_part_neighbor
 *
 * Search for a Solaris partition and a "neighbor" of the Solaris partition.
 *
 * Search for a Solaris partition in both the MBR and the first DOS Extended
 * Partition (if any) that is found.  If found, find the Neighbor of that
 * partition, where the Neighbor is defined as the partition whose start-addr
 * is closest to the Solaris Partition's end-addr.
 *
 * Note that when a Solaris partition is found in an Extended Partition, the
 * end-address of the Extended Partition can represent the Neighbor's
 * starting address (as the addr-range of each partition within an Extended
 * Partition must not overlap and must fall within the ExtPart's addr-range).
 *
 * Results:
 *   *is_sol_part		- whether a Solaris partition is present
 *   *is_neighbor_part		- whether a Neighbor partition is present
 *   *sol_part_start		- start-addr of Solaris partition
 *   *neighbor_part_start	- start-addr of Neighbor partition
 */
int
get_solaris_part_neighbor(int *is_sol_part, int *is_neighbor_part,
    uint32_t *sol_part_start, uint32_t *neighbor_part_start)
{
	mbr_iter_t		*iter_fwk_state;
	mi_sol_part_t		sol_part_state;
	mi_target_neighbor_t	neighbor_state;

	/* Assume that a Solaris and Neighbor partition will not be found. */
	*is_sol_part = 0;
	*is_neighbor_part = 0;

	/* Init iteration framework. */
	if (mbr_iter_init(&iter_fwk_state) != 0) {
		return (EINVAL);
	}

	/*
	 * Init iterator that will search for a Solaris partition.
	 */
	bzero(&sol_part_state, sizeof (mi_sol_part_t));
	(void) mbr_iter(iter_fwk_state, mbr_hdlr_find_sol_part,
	    (void *)&sol_part_state);

#ifdef DEBUG
	fmt_print("Solaris partition: idx: %d  start: %u  end: %u  len: %u\n",
	    sol_part_state.sol_idx, sol_part_state.relsect,
	    sol_part_state.relsect + sol_part_state.numsect - 1,
	    sol_part_state.numsect);
#endif

	*is_sol_part = sol_part_state.found;
	if (sol_part_state.found == 0) {
		/* then a Solaris partition was not found. */
		return (0);
	}

	/* starting addr of Solaris partition */
	*sol_part_start = sol_part_state.relsect;


	/*
	 * Search for the Neighbor partition.  Use a different algorithm based
	 * on whether Solaris partition was found in the MBR or the Extended
	 * Partition.
	 */
#ifdef i386
	if (sol_part_state.found_in_extpart != 0) {
		ext_part_t	*epp;
		uint32_t	last_free_sec;
		uint32_t	begsec;

		/* The Solaris partition was found in the Extended Partition. */

		/* Obtain state needed to iterate an Extended Partition. */
		epp = mbr_iter_get_ext_part(iter_fwk_state);

		/*
		 * Compute addr of first sector that free-space must be
		 * contiguous to, which is the address of last sector
		 * in Solaris partition.  It is not "last_addr + 1".
		 */
		begsec = sol_part_state.relsect + sol_part_state.numsect - 1;

		/* Find last free sector (starting at begsec) */
		last_free_sec = fdisk_ext_find_last_free_sec(epp, begsec);

		*is_neighbor_part = 1;
		*neighbor_part_start = last_free_sec;
	}
#endif
	if (sol_part_state.found_in_extpart == 0) {
		/*
		 * Init iterator that will search for (any) Neighbor of the
		 * Solaris partition within the MBR.
		 */
		neighbor_state.neighbor_start = UINT32_MAX;
		neighbor_state.found = 0;  /* "a neighbor has not been found" */
		neighbor_state.target_idx = sol_part_state.sol_idx;
		/* end-addr of Solaris partition: */
		neighbor_state.target_end = sol_part_state.relsect +
		    sol_part_state.numsect - 1;
		(void) mbr_iter(iter_fwk_state, mbr_hdlr_find_neighbor_part,
		    (void *)&neighbor_state);

		*is_neighbor_part = neighbor_state.found;

		if (neighbor_state.found != 0) {
			*neighbor_part_start = neighbor_state.neighbor_start;
		}
	}

	/* Destroy iteration framework. */
	(void) mbr_iter_fini(iter_fwk_state);

	return (0);
}

/*
 * characterize_solaris_part
 *
 * Compute whether the current disk, cur_disk, contains an MBR
 * and whether the MBR references a Solaris partition.
 */
int
characterize_solaris_part(int *mbr_found, int *sol_part_found)
{
	mbr_iter_t	*iter_fwk_state;
	mi_sol_part_t	sol_part_state;
	int		rv;

	*sol_part_found = 0;
	*mbr_found = 0;

	/*
	 * As part of initializing framework, confirm if disk contains
	 * an MBR.
	 */
	rv = mbr_iter_init(&iter_fwk_state);
	if (rv != 0) {
		return (rv);
	} else {
		*mbr_found = 1;
	}

	/* Init iterator that searches for a Solaris partition. */
	bzero(&sol_part_state, sizeof (mi_sol_part_t));
	(void) mbr_iter(iter_fwk_state, mbr_hdlr_find_sol_part,
	    (void *)&sol_part_state);

	*sol_part_found = sol_part_state.found;

	(void) mbr_iter_fini(iter_fwk_state);

	return (0);
}


int
copy_solaris_part(struct ipart *ipart)
{

	int		status, i, fd;
	struct mboot	mboot;
	char		*mbr;
	struct ipart	ip;
	struct ipart	selected;
	char		buf[MAXPATHLEN];
	char		*bootptr;
	struct stat	statbuf;
	ushort_t	found = 0;
#ifdef i386
	uint32_t	relsec, numsec;
	int		pno, rval, ext_part_found = 0;
	ext_part_t	*epp;
#endif

	(void) get_pname(&buf[0]);
	if (stat(buf, &statbuf) == -1 ||
	    !S_ISCHR(statbuf.st_mode) ||
	    ((cur_label == L_TYPE_EFI) &&
	    (cur_disk->disk_flags & DSK_LABEL_DIRTY))) {
		/*
		 * Make sure to reset solaris_offset to zero if it is
		 *	previously set by a selected disk that
		 *	supports the fdisk table.
		 */
		solaris_offset = 0;
		/*
		 * Return if this disk does not support fdisk table or
		 * if it uses an EFI label but has not yet been labelled.
		 * If the EFI label has not been written then the open
		 * on the partition will fail.
		 */
		return (0);
	}

	if ((fd = open(buf, O_RDONLY)) < 0) {
		err_print("Error: can't open disk '%s'.\n", buf);
		return (-1);
	}

	/*
	 * We may get mbr of different size, but the first 512 bytes
	 * are valid information.
	 */
	mbr = malloc(cur_blksz);
	if (mbr == NULL) {
		err_print("No memory available.\n");
		return (-1);
	}
	status = read(fd, mbr, cur_blksz);

	if (status != cur_blksz) {
		err_print("Bad read of fdisk partition.\n");
		(void) close(fd);
		free(mbr);
		return (-1);
	}

	(void) memcpy(&mboot, mbr, sizeof (struct mboot));

#ifdef i386
	(void) extpart_init(&epp);
#endif
	for (i = 0; i < FD_NUMPART; i++) {
		int	ipc;

		ipc = i * sizeof (struct ipart);

		/* Handling the alignment problem of struct ipart */
		bootptr = &mboot.parts[ipc];
		(void) fill_ipart(bootptr, &ip);

#ifdef i386
		if (fdisk_is_dos_extended(ip.systid) && (ext_part_found == 0)) {
			/* We support only one extended partition per disk */
			ext_part_found = 1;
			rval = fdisk_get_solaris_part(epp, &pno, &relsec,
			    &numsec);
			if (rval == FDISK_SUCCESS) {
				/*
				 * Found a solaris partition inside the
				 * extended partition. Update the statistics.
				 */
				if (nhead != 0 && nsect != 0) {
					pcyl = numsec / (nhead * nsect);
					ncyl = pcyl - acyl;
				}
				solaris_offset = relsec;
				ip.bootid = 0;
				ip.beghead = ip.begsect = ip.begcyl = 0xff;
				ip.endhead = ip.endsect = ip.endcyl = 0xff;
				ip.systid = SUNIXOS2;
				ip.relsect = relsec;
				ip.numsect = numsec;
				bcopy(&ip, ipart, sizeof (struct ipart));
			}
			continue;
		}
#endif


#ifdef i386
		if ((ip.systid == SUNIXOS &&
		    (fdisk_is_linux_swap(epp, lel(ip.relsect), NULL) != 0)) ||
		    ip.systid == SUNIXOS2 ||
		    ip.systid == EFI_PMBR) {
#else
		if (ip.systid == SUNIXOS ||
		    ip.systid == SUNIXOS2 ||
		    ip.systid == EFI_PMBR) {
#endif
			if (found == 0 || ip.bootid == ACTIVE) {
				/*
				 * use the last active solaris partition found
				 * (there should only be 1 active partition id)
				 * if there are no active solaris partition id
				 * then use the first inactive solaris partition
				 */
				found = 1;
				bcopy(&ip, &selected, sizeof (struct ipart));

				/*
				 * there be only 1 active partition
				 */
				if (ip.bootid == ACTIVE)
					break;
			}
		}
	}

	if (found == 1) {
		solaris_offset = lel(selected.relsect);
		bcopy(&selected, ipart, sizeof (struct ipart));

		/*
		 * if the disk has an EFI label, we typically won't
		 * have values for nhead and nsect.  format seems to
		 * work without them, and we need to protect ourselves
		 * from FPE's
		 */
		if (nhead != 0 && nsect != 0) {
			pcyl = lel(selected.numsect) / (nhead * nsect);
			ncyl = pcyl - acyl;
		}
#ifdef DEBUG
		else {
			err_print("Critical geometry values are zero:\n"
			    "\tnhead = %d; nsect = %d\n", nhead, nsect);
		}
#endif /* DEBUG */

	}

#ifdef i386
	extpart_fini(&epp);
#endif

	(void) close(fd);
	free(mbr);
	return (0);
}

#if defined(_FIRMWARE_NEEDS_FDISK)
int
auto_solaris_part(struct dk_label *label)
{

	int		status, i, fd;
	struct mboot	mboot;
	char		*mbr;
	struct ipart	ip;
	struct ipart	selected;
	char		*bootptr;
	char		pbuf[MAXPATHLEN];
	ushort_t	found = 0;
#ifdef i386
	uint32_t	relsec, numsec;
	int		pno, rval, ext_part_found = 0;
	ext_part_t	*epp;
#endif

	(void) get_pname(&pbuf[0]);
	if ((fd = open_disk(pbuf, O_RDONLY)) < 0) {
		err_print("Error: can't open selected disk '%s'.\n", pbuf);
		return (-1);
	}

	/*
	 * We may get mbr of different size, but the first 512 bytes
	 * are valid information.
	 */
	mbr = malloc(cur_blksz);
	if (mbr == NULL) {
		err_print("No memory available.\n");
		return (-1);
	}
	status = read(fd, mbr, cur_blksz);

	if (status != cur_blksz) {
		err_print("Bad read of fdisk partition.\n");
		free(mbr);
		return (-1);
	}

	(void) memcpy(&mboot, mbr, sizeof (struct mboot));

#ifdef i386
	(void) extpart_init(&epp);
#endif
	for (i = 0; i < FD_NUMPART; i++) {
		int	ipc;

		ipc = i * sizeof (struct ipart);

		/* Handling the alignment problem of struct ipart */
		bootptr = &mboot.parts[ipc];
		(void) fill_ipart(bootptr, &ip);

#ifdef i386
		if (fdisk_is_dos_extended(ip.systid) && (ext_part_found == 0)) {
			/* We support only one extended partition per disk */
			ext_part_found = 1;
			rval = fdisk_get_solaris_part(epp, &pno, &relsec,
			    &numsec);
			if (rval == FDISK_SUCCESS) {
				/*
				 * Found a solaris partition inside the
				 * extended partition. Update the statistics.
				 */
				if ((label->dkl_nhead != 0) &&
				    (label->dkl_nsect != 0)) {
					label->dkl_pcyl =
					    numsec / (label->dkl_nhead *
					    label->dkl_nsect);
					label->dkl_ncyl = label->dkl_pcyl -
					    label->dkl_acyl;
				}
				solaris_offset = relsec;
			}
			continue;
		}
#endif

		/*
		 * if the disk has an EFI label, the nhead and nsect fields
		 * the label may be zero.  This protects us from FPE's, and
		 * format still seems to work happily
		 */


#ifdef i386
		if ((ip.systid == SUNIXOS &&
		    (fdisk_is_linux_swap(epp, lel(ip.relsect), NULL) != 0)) ||
		    ip.systid == SUNIXOS2 ||
		    ip.systid == EFI_PMBR) {
#else
		if (ip.systid == SUNIXOS ||
		    ip.systid == SUNIXOS2 ||
		    ip.systid == EFI_PMBR) {
#endif
			if (found == 0 || ip.bootid == ACTIVE) {
				/*
				 * use the last active solaris partition found
				 * (there should only be 1 active partition id)
				 * if there are no active solaris partition id
				 * then use the first inactive solaris partition
				 */
				found = 1;
				bcopy(&ip, &selected, sizeof (struct ipart));

				/*
				 * there be only 1 active partition
				 */
				if (ip.bootid == ACTIVE)
					break;
			}
		}
	}

	if (found == 1) {
		if ((label->dkl_nhead != 0) && (label->dkl_nsect != 0)) {
			label->dkl_pcyl = lel(selected.numsect) /
			    (label->dkl_nhead * label->dkl_nsect);
			label->dkl_ncyl = label->dkl_pcyl -
			    label->dkl_acyl;
		}
#ifdef DEBUG
		else {
			err_print("Critical label fields aren't "
			    "non-zero:\n"
			    "\tlabel->dkl_nhead = %d; "
			    "label->dkl_nsect = "
			    "%d\n", label->dkl_nhead,
			    label->dkl_nsect);
		}
#endif /* DEBUG */

		solaris_offset = lel(selected.relsect);
	}

#ifdef i386
	extpart_fini(&epp);
#endif
	(void) close(fd);
	free(mbr);
	return (0);
}
#endif	/* defined(_FIRMWARE_NEEDS_FDISK) */


int
good_fdisk()
{
	char		buf[MAXPATHLEN];
	struct stat	statbuf;

	(void) get_pname(&buf[0]);
	if (stat(buf, &statbuf) == -1 ||
	    !S_ISCHR(statbuf.st_mode) ||
	    cur_label == L_TYPE_EFI) {
		/*
		 * Return if this disk does not support fdisk table or
		 * if the disk is labeled with EFI.
		 */
		return (1);
	}

	if (lel(cur_disk->fdisk_part.numsect) > 0) {
		return (1);
	} else {
		err_print("WARNING - ");
		err_print("This disk may be in use by an application "
		    "that has\n\t  modified the fdisk table. Ensure "
		    "that this disk is\n\t  not currently in use "
		    "before proceeding to use fdisk.\n");
		return (0);
	}
}

#ifdef i386
int
extpart_init(ext_part_t **epp)
{
	int		rval, lf_op_flag = 0;
	char		p0_path[MAXPATHLEN];

	get_pname(&p0_path[0]);
	lf_op_flag |= FDISK_READ_DISK;
	if ((rval = libfdisk_init(epp, p0_path, NULL, lf_op_flag)) !=
	    FDISK_SUCCESS) {
		switch (rval) {
			/*
			 * FDISK_EBADLOGDRIVE, FDISK_ENOLOGDRIVE
			 * and FDISK_EBADMAGIC can be considered
			 * as soft errors and hence we do not exit.
			 */
			case FDISK_EBADLOGDRIVE:
				break;
			case FDISK_ENOLOGDRIVE:
				break;
			case FDISK_EBADMAGIC:
				break;
			case FDISK_ENOVGEOM:
				err_print("Could not get virtual geometry for"
				    " this device\n");
				fullabort();
				break;
			case FDISK_ENOPGEOM:
				err_print("Could not get physical geometry for"
				    " this device\n");
				fullabort();
				break;
			case FDISK_ENOLGEOM:
				err_print("Could not get label geometry for "
				    " this device\n");
				fullabort();
				break;
			default:
				err_print("Failed to initialise libfdisk.\n");
				fullabort();
				break;
		}
	}
	return (0);
}

void
extpart_fini(ext_part_t **epp)
{
	libfdisk_fini(epp);
}
#endif
