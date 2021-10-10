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
 * Copyright (c) 1993, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "global.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/dklabel.h>
#include <sys/dktp/dadkio.h>
#include <devid.h>
#include <errno.h>

#include "main.h"
#include "analyze.h"
#include "menu.h"
#include "menu_command.h"
#include "menu_defect.h"
#include "menu_partition.h"
#if defined(_FIRMWARE_NEEDS_FDISK)
#include "menu_fdisk.h"
#endif	/* defined(_FIRMWARE_NEEDS_FDISK) */
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

#ifdef	__STDC__
/*
 *	Local prototypes for ANSI C compilers
 */
static int	generic_ck_format(void);
static int	generic_rdwr(int dir, int fd, diskaddr_t blkno, int secnt,
			caddr_t bufaddr, int flags, int *xfercntp);
#else	/* __STDC__ */

static int	generic_ck_format();
static int	generic_rdwr();

#endif	/* __STDC__ */

struct  ctlr_ops genericops = {
	generic_rdwr,
	generic_ck_format,
	0,
	0,
	0,
	0,
	0,
};


/*
 * Check to see if the disk has been formatted.
 * If we are able to read the first track, we conclude that
 * the disk has been formatted.
 */
static int
generic_ck_format()
{
	int	status;

	/*
	 * Try to read the first four blocks.
	 */
	status = generic_rdwr(DIR_READ, cur_file, 0, 4, (caddr_t)cur_buf,
	    F_SILENT, NULL);
	return (!status);
}

/*
 * Read or write the disk.
 * Temporary interface until IOCTL interface finished.
 */
/*ARGSUSED*/
static int
generic_rdwr(dir, fd, blkno, secnt, bufaddr, flags, xfercntp)
	int	dir;
	int	fd;
	diskaddr_t	blkno;
	int	secnt;
	caddr_t	bufaddr;
	int	flags;
	int	*xfercntp;
{

	offset_t	tmpsec, status, tmpblk;
	int		ret;

	tmpsec = (offset_t)secnt * cur_blksz;
	tmpblk = (offset_t)blkno * cur_blksz;

#if defined(_FIRMWARE_NEEDS_FDISK)
	/* Use "p0" file to seek/read the data  */
	(void) open_cur_file(FD_USE_P0_PATH);
#endif
	if (dir == DIR_READ) {
		status = llseek(fd, tmpblk, SEEK_SET);
		if (status != tmpblk) {
			ret = (int)status;
			goto out;
		}

		status = read(fd, bufaddr, (size_t)tmpsec);
		if (status != tmpsec)
			ret = (int)tmpsec;
		else
			ret = 0;
	} else {
		status = llseek(fd, tmpblk, SEEK_SET);
		if (status != tmpblk) {
			ret = (int)status;
			goto out;
		}

		status = write(fd, bufaddr, (size_t)tmpsec);
		if (status != tmpsec)
			ret = (int)tmpsec;
		else
			ret = 0;
	}
out:
#if defined(_FIRMWARE_NEEDS_FDISK)
	/* Restore cur_file with cur_disk->disk_path */
	(void) open_cur_file(FD_USE_CUR_DISK_PATH);
#endif
	return (ret);
}

int
generic_inquiry(int fd)
{
	struct dk_disk_id id;

	if (ioctl(fd, DKIOC_GETDISKID, &id) != 0)
		return (-1);

	switch (id.dkd_dtype) {
	case DKD_ATA_TYPE:
		fmt_print("Model: %s\n",
		    id.disk_id.ata_disk_id.dkd_amodel);
		fmt_print("Revision: %s\n",
		    id.disk_id.ata_disk_id.dkd_afwver);
		fmt_print("Serial: %s\n",
		    id.disk_id.ata_disk_id.dkd_aserial);
		break;

	case DKD_SCSI_TYPE:
		fmt_print("Vendor: %s\n",
		    id.disk_id.scsi_disk_id.dkd_svendor);
		fmt_print("Product: %s\n",
		    id.disk_id.scsi_disk_id.dkd_sproduct);
		fmt_print("Revision: %s\n",
		    id.disk_id.scsi_disk_id.dkd_sfwver);
		fmt_print("Serial: %s\n",
		    id.disk_id.scsi_disk_id.dkd_sserial);
		break;
	}

	return (0);
}

static int
get_disk_info_from_devid(int fd, struct disk_id *diskid)
{
	ddi_devid_t	devid;
	char		*s;
	int		n;
	char		*vid, *pid;
	int		nvid, npid;
	struct dk_cinfo	dkinfo;

	if (devid_get(fd, &devid)) {
		if (option_msg && diag_msg)
			err_print("devid_get failed\n");
		return (-1);
	}

	n = devid_sizeof(devid);
	s = (char *)devid;

	if (ioctl(fd, DKIOCINFO, &dkinfo) == -1) {
		if (option_msg && diag_msg)
			err_print("DKIOCINFO failed\n");
		devid_free(devid);
		return (-1);
	}

	if (dkinfo.dki_ctype != DKC_DIRECT) {
		devid_free(devid);
		return (-1);
	}

	vid = s + 12;
	if (!(pid = strchr(vid, '='))) {
		devid_free(devid);
		return (-1);
	}

	nvid = pid - vid;
	pid += 1;
	npid = n - nvid - 13;

	if (nvid > 9)
		nvid = 9;
	if (npid > 17) {
		pid = pid + npid - 17;
		npid = 17;
	}

	(void) strlcpy(diskid->vendor, vid, nvid);
	(void) strlcpy(diskid->product, pid, npid);

	devid_free(devid);
	return (0);
}

static int
get_disk_ident(int fd, struct disk_id *diskid)
{
	struct dk_disk_id id;

	if (ioctl(fd, DKIOC_GETDISKID, &id) != 0)
		return (-1);

	switch (id.dkd_dtype) {
	case DKD_ATA_TYPE:
		(void) strlcpy(diskid->product,
		    id.disk_id.ata_disk_id.dkd_amodel,
		    sizeof (diskid->product));
		(void) strlcpy(diskid->revision,
		    id.disk_id.ata_disk_id.dkd_afwver,
		    sizeof (diskid->revision));
		break;

	case DKD_SCSI_TYPE:
		(void) strlcpy(diskid->vendor,
		    id.disk_id.scsi_disk_id.dkd_svendor,
		    sizeof (diskid->vendor));
		(void) strlcpy(diskid->product,
		    id.disk_id.scsi_disk_id.dkd_sproduct,
		    sizeof (diskid->product));
		(void) strlcpy(diskid->revision,
		    id.disk_id.scsi_disk_id.dkd_sfwver,
		    sizeof (diskid->revision));
		break;
	}

	return (0);
}

static void
remove_trailing_spaces(char *str)
{
	char *p;

	if (*str == '\0')
		return;

	for (p = str + (strlen(str) - 1); *p == ' ' && p != str; p--)
		*p = '\0';
}

/*
 * Determine disk's ID and capacity.
 */
int
get_disk_id(int fd, struct disk_id *diskid)
{
	struct dk_minfo	minf;

	(void) strlcpy(diskid->vendor, "Unknown", sizeof (diskid->vendor));
	(void) strlcpy(diskid->product, "Unknown", sizeof (diskid->product));
	(void) strlcpy(diskid->revision, "0001", sizeof (diskid->revision));
	diskid->capacity = 0;
	diskid->lbsize = 0;

	if (get_disk_info_from_devid(fd, diskid) != 0) {
		if (get_disk_ident(fd, diskid) != 0)
			scsi_get_disk_id(fd, diskid);
	}

	if (ioctl(fd, DKIOCGMEDIAINFO, &minf) != 0)
		return (-1);

	diskid->capacity = minf.dki_capacity;
	diskid->lbsize = minf.dki_lbsize;

	remove_trailing_spaces(diskid->vendor);
	remove_trailing_spaces(diskid->product);
	remove_trailing_spaces(diskid->revision);

	return (0);
}

void
get_disk_name(char *name, const struct disk_id *diskid)
{
	if (diskid->vendor[0] != '\0') {
		(void) snprintf(name, MAXNAMELEN, "%s-%s-%s",
		    diskid->vendor, diskid->product, diskid->revision);
	} else {
		(void) snprintf(name, MAXNAMELEN, "%s-%s",
		    diskid->product, diskid->revision);
	}
}

/*ARGSUSED6*/
int
raw_rdwr(int dir, int fd, diskaddr_t blkno, int secnt, caddr_t bufaddr,
    int flags, int *xfercntp)
{
	struct dadkio_rwcmd_ext rwcmd;
	int ret = 0;
	int tmpsec;

	bzero((caddr_t)&rwcmd, sizeof (rwcmd));

	tmpsec = secnt * cur_blksz;

	/* Doing raw read */
	rwcmd.cmd = (dir == DIR_READ) ? DADKIO_RWCMD_READ : DADKIO_RWCMD_WRITE;
	rwcmd.blkaddr = blkno;
	rwcmd.buflen  = tmpsec;
	rwcmd.flags   = DADKIO_FLAG_EXT;
	if (flags & F_SILENT)
		rwcmd.flags |= DADKIO_FLAG_SILENT;
	rwcmd.bufaddr = (uint64_t)(uintptr_t)bufaddr;
	rwcmd.status = 0;

	media_error = 0;

#if defined(_FIRMWARE_NEEDS_FDISK)
	if (cur_ctype != NULL &&
	    cur_ctype->ctype_ctype == DKC_PCMCIA_ATA) {
		/*
		 * PCATA requires to use "p0" when calling
		 *	DIOCTL_RWCMD ioctl() to read/write the label
		 */
		(void) open_cur_file(FD_USE_P0_PATH);
		fd = cur_file;
	}
#endif

	if (ioctl(fd, DIOCTL_RWCMD, &rwcmd) == -1) {
		if (errno == EPERM) {
			err_print("Permission denied.\n");
			fullabort();
		}
		err_print("DIOCTL_RWCMD: %s\n", strerror(errno));
		return (-1);
	}

#if defined(_FIRMWARE_NEEDS_FDISK)
	if (cur_ctype != NULL &&
	    cur_ctype->ctype_ctype == DKC_PCMCIA_ATA) {
		/* Restore cur_file with cur_disk->disk_path */
		(void) open_cur_file(FD_USE_CUR_DISK_PATH);
	}
#endif

	switch (rwcmd.status) {
	case  DADKIO_STAT_NOT_READY:
			disk_error = DISK_STAT_NOTREADY;
			ret = DSK_UNAVAILABLE;
			break;
	case  DADKIO_STAT_RESERVED:
			disk_error = DISK_STAT_RESERVED;
			ret = DSK_RESERVED;
			break;
	case  DADKIO_STAT_WRITE_PROTECTED:
			disk_error = DISK_STAT_DATA_PROTECT;
			ret = -1;
			break;
	case DADKIO_STAT_MEDIUM_ERROR:
			media_error = 1;
			ret = -1;
			break;
	}

	return (ret);
}
