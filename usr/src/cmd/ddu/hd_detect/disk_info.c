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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Get disk information,
 * include media capacity, vendor and deviceid information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <stropts.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/byteorder.h>
#include <sys/scsi/generic/commands.h>
#include <sys/scsi/impl/commands.h>

#include "libddudev.h"
#include "disk_info.h"

/*
 * Use scsi command to get device capacity
 *
 * fd: device handle
 *
 * Return:
 * Return device capacity, or return 0 on failure
 */
uint64_t
get_scsi_capacity(int fd)
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	int			status;
	char			rq_buf[RQBUF_SIZE];
	struct scsi_capacity	cap;
	struct scsi_capacity_16	cap_16;
	uint64_t		size;
	uint32_t		lba_size;

	(void) memset((char *)&cap, 0, sizeof (struct scsi_capacity));
	(void) memset((char *)&cap_16, 0, sizeof (struct scsi_capacity_16));
	(void) memset((char *)&ucmd, 0, sizeof (struct uscsi_cmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));

	cdb.scc_cmd = SCMD_READ_CAPACITY;
	ucmd.uscsi_cdb = (caddr_t)&cdb;
	ucmd.uscsi_cdblen = CDB_GROUP1;
	ucmd.uscsi_bufaddr = (caddr_t)&cap;
	ucmd.uscsi_buflen = sizeof (struct scsi_capacity);

	ucmd.uscsi_flags = USCSI_ISOLATE | USCSI_SILENT | USCSI_READ;

	status = scsi_cmd(fd, &ucmd, rq_buf, RQBUF_SIZE);

	if (cap.capacity == 0xffffffff) {
		(void) memset((char *)&ucmd, 0, sizeof (ucmd));
		(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));

		ucmd.uscsi_cdb = (caddr_t)&cdb;
		ucmd.uscsi_cdblen = CDB_GROUP4;
		ucmd.uscsi_bufaddr = (caddr_t)&cap_16;
		ucmd.uscsi_buflen = sizeof (struct scsi_capacity_16);

		ucmd.uscsi_flags = USCSI_ISOLATE | USCSI_SILENT | USCSI_READ;

		cdb.scc_cmd = SCMD_SVC_ACTION_IN_G4;
		cdb.cdb_opaque[1] = SSVC_ACTION_READ_CAPACITY_G4;
		cdb.cdb_opaque[10] =
		    (uchar_t)((ucmd.uscsi_buflen & 0xff000000) >> 24);
		cdb.cdb_opaque[11] =
		    (uchar_t)((ucmd.uscsi_buflen & 0x00ff0000) >> 16);
		cdb.cdb_opaque[12] =
		    (uchar_t)((ucmd.uscsi_buflen & 0x0000ff00) >> 8);
		cdb.cdb_opaque[13] =
		    (uchar_t)(ucmd.uscsi_buflen & 0x000000ff);

		status = scsi_cmd(fd, &ucmd, rq_buf, RQBUF_SIZE);
	}

	if (status >= 0) {
		if (cap.capacity == 0xffffffff) {
			size = BE_64(cap_16.sc_capacity);
			lba_size = BE_32(cap_16.sc_lbasize);
		} else {
			size = BE_32(cap.capacity);
			lba_size = BE_32(cap.lbasize);
		}
		size++;
		size = size * lba_size;
	} else {
		FPRINTF(stderr, "fail to get disk capacity\n");
		size = 0;
	}

	return (size);
}

/*
 * Print disk information
 *
 * output (printed):
 * driver:
 * Geom:
 * Capacity:
 */
int
prt_disk_info(char *dpath)
{
	int		fd;
	int		ret;
	struct dk_cinfo	dkinfo;
	struct vtoc	dkvtoc;
	ushort_t	sectorsz;
	uint64_t	size;
	int		l_efi;

	fd = open(dpath, O_RDWR | O_NDELAY);

	if (fd < 0) {
		perror(dpath);
		return (1);
	}

	/* get disk info, to get device driver name */
	ret = ioctl(fd, DKIOCINFO, &dkinfo);

	if (ret >= 0) {
		PRINTF("driver: %s\n", dkinfo.dki_dname);
	} else {
		perror("fail to get disk info");
		return (1);
	}

	sectorsz = 0;
	l_efi = 0;

	/*
	 * Get disk sector size from disk VTOC or EXTVTOC
	 * If disk is efi label, need not get sector size
	 */
	ret = ioctl(fd, DKIOCGVTOC, &dkvtoc);

	/* according vtoc get sector size */
	if (ret >= 0) {
		sectorsz = dkvtoc.v_sectorsz;
	} else {
		/* disk is efi label */
		if (errno == ENOTSUP) {
			l_efi = 1;
		} else {
			struct extvtoc	edkvtoc;

			/* use extend vtoc to get sector size */
			ret = ioctl(fd, DKIOCGEXTVTOC, &edkvtoc);

			if (ret >= 0) {
				sectorsz = edkvtoc.v_sectorsz;
			} else {
				/*
				 * Error seen here,
				 * assume default sector size is 512
				 * and get disk geom to caculate disk capacity
				 */
				perror("fail to get disk extend vtoc");
			}
		}
	}

	if (l_efi) {
		/* for efi label, use scsi command to get capacity */
		size = get_scsi_capacity(fd);
		size = size /1000000;
	} else {
		struct dk_geom	dkgeom;

		/*
		 * If fail to get sector size, the default sector size is 512
		 */
		if (sectorsz == 0) {
			sectorsz = 512;
		}

		/*
		 * Get disk geom information to caculate disk size
		 *
		 * If fail to get disk geom information, try to get
		 * disk size by scsi command
		 */
		ret = ioctl(fd, DKIOCG_PHYGEOM, &dkgeom);

		if (ret < 0) {
			ret = ioctl(fd, DKIOCGGEOM, &dkgeom);
		}

		if (ret >= 0) {
			size = (uint64_t)(dkgeom.dkg_ncyl * dkgeom.dkg_nhead);
			size = size * dkgeom.dkg_nsect;
			size = size / 1000 * sectorsz;
			size = size /1000;
			PRINTF("Geom: <yl %d, head %d, nsect %d>\n",
			    dkgeom.dkg_ncyl, dkgeom.dkg_nhead,
			    dkgeom.dkg_nsect);
		} else {
			/*
			 * Error seen here, get disk capacity by SCSI command
			 */
			perror("fail to get disk geom information");
			size = get_scsi_capacity(fd);
			size = size /1000000;
		}
	}

	/*
	 * If fail to get disk size, assume it is removable media
	 * try to get disk size by MEDIAINFO
	 */
	if (size == 0) {
		struct dk_minfo	minf;

		ret = ioctl(fd, DKIOCGMEDIAINFO, &minf);

		if (ret >= 0) {
			size = minf.dki_capacity * minf.dki_lbsize / 512;
		} else {
			perror("fail to get disk media information");
		}
	}

	(void) close(fd);

	if (size > 1000) {
		PRINTF("Capacity: %lluG\n", size / 1000);
	} else {
		if (size) {
			PRINTF("Capacity: %lluM\n", size);
		} else {
			FPRINTF(stderr, "fail to get disk capacity\n");
			return (1);
		}
	}

	return (0);
}
