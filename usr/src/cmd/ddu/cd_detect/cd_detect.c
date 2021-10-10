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
 * List CD/DVD device.
 * Get CD/DVD media information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libddudev.h"

#define	RQBUF_SIZE		32
#define	SCMD_TIMEOUT		30
#define	MMC_INQUIRY		0x12
#define	MMC_GET_CONFIG		0x46
#define	MMC_READ_TOC		0x43
#define	MMC_READ_DISC_INFO	0x51
#define	MMC_GET_MODE_SENSE	0x5a

static struct uscsi_cmd		cmd;
static char			cdb[16];

/* cd_detect usage */
void
usage()
{
	FPRINTF(stderr, "Usage: cd_detect [-l][-M (device path)]\n");
	FPRINTF(stderr, "       -l: ");
	FPRINTF(stderr, "list cd devices\n");
	FPRINTF(stderr, "       -d {devfs path}: ");
	FPRINTF(stderr, "list cd devices and media information\n");
	exit(1);
}

/*
 * Detect device is ready
 *
 * fd: device handle
 *
 * Return:
 * >=0: Device is ready
 * <0: Device is not ready
 */
int
ready_dev(int fd)
{
	char	rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	cmd.uscsi_cdblen = 6;

	return (scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE));
}

/*
 * Check whether media in device
 *
 * fd: device handle
 *
 * Return:
 * 0: No media in device
 * 1: Media in device
 * 2: Media in device but not ready
 */
int
check_no_media(int fd)
{
	int		ret;
	int		len;
	char		rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	cmd.uscsi_cdblen = 6;

	/* Test device is ready */
	ret = scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE);

	/* Device has media */
	if (ret >= 0) {
		return (1);
	}

	len = 32 - cmd.uscsi_rqresid;
	/*
	 * Device not ready
	 *
	 * If rqbuf[2](SENSE)=2, rqbuf[12](ASC)=0x3a:
	 * MEDIUM NOT PRESENT
	 * rqbuf[13](ASCQ)=0: MEDIUM NOT PRESENT
	 * rqbuf[13](ASCQ)=1: MEDIUM NOT PRESENT - TRAY CLOSED
	 * rqbuf[13](ASCQ)=2: MEDIUM NOT PRESENT - TRAY OPEN
	 */
	if ((cmd.uscsi_status == 2) && (len >= 14)) {
		if (((rq_buf[2] & 0x0f) == 2) && (rq_buf[12] == 0x3a)) {
			if ((rq_buf[13] == 0) || (rq_buf[13] == 1) ||
			    (rq_buf[13] == 2)) {
				return (0);
			}
		}
	}

	/* Device has media, but not ready */
	return (2);
}

/*
 * Get information about the disc device capabilities
 *
 * fd: device handle
 * feature: mmc feature number
 * buf: store capabilities
 * len: buf length
 *
 * Return:
 * 0: Get device capabilities
 * 1: Fail to get device capabilities
 */
int
get_disc_config(int fd, unsigned short feature, uint8_t *buf, uint16_t len)
{
	char		rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_READ|USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	cmd.uscsi_cdblen = 0xa;
	cmd.uscsi_bufaddr = (char *)buf;
	cmd.uscsi_buflen = len;

	/* Set get config command */
	cmd.uscsi_cdb[0] = MMC_GET_CONFIG;
	/* At least one feature descriptor */
	cmd.uscsi_cdb[1] = 0x2;
	/* Set Starting Feature Number in CDB */
	cmd.uscsi_cdb[2] = (feature >> 8) & 0xff;
	cmd.uscsi_cdb[3] = feature & 0xff;
	cmd.uscsi_cdb[7] = (len >> 8) & 0xff;
	cmd.uscsi_cdb[8] = len & 0xff;

	return (scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE));
}

/*
 * Read media TOC information
 *
 * fd: device handle
 * trackno: Track number should be read
 * buf: Will store TOC information
 * len: buffer length
 *
 * Return:
 * >=0: Read TOC
 * <0: Fail to read TOC
 */
int
read_toc(int fd, uint8_t trackno, uint8_t *buf, uint16_t len)
{
	char		rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_READ|USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	cmd.uscsi_cdblen = 0xa;
	cmd.uscsi_bufaddr = (char *)buf;
	cmd.uscsi_buflen = len;

	/* Set read TOC command */
	cmd.uscsi_cdb[0] = MMC_READ_TOC;
	/* Track number will be read */
	cmd.uscsi_cdb[6] = trackno;
	cmd.uscsi_cdb[7] = (len >> 8) & 0xff;
	cmd.uscsi_cdb[8] = len & 0xff;

	return (scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE));
}

/*
 * Read disc information
 *
 * fd: device handle
 * buf: to store disc information
 * len: buffer length
 *
 * Return:
 * >=0: Read disc information
 * <0: Fail to read disc information
 */
int
read_disc_info(int fd, uint8_t *buf, uint16_t len)
{
	char		rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_READ|USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	cmd.uscsi_cdblen = 0xa;
	cmd.uscsi_bufaddr = (char *)buf;
	cmd.uscsi_buflen = len;
	/* Set read disc info command */
	cmd.uscsi_cdb[0] = MMC_READ_DISC_INFO;
	cmd.uscsi_cdb[7] = (len >> 8) & 0xff;
	cmd.uscsi_cdb[8] = len & 0xff;

	return (scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE));
}

/*
 * Get device mode sense information
 *
 * fd: device handle
 * page_code: mode sense page code
 * buf: to store mode sense information
 * len: buf length
 *
 * Return:
 * >=0: Read mode sense information
 * <0: Fail to read mode sense information
 */
int
get_mode_sense(int fd, uint8_t page_code, uint8_t *buf, uint16_t len)
{
	char		rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_READ|USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	cmd.uscsi_cdblen = 0xa;
	cmd.uscsi_bufaddr = (char *)buf;
	cmd.uscsi_buflen = len;
	/* Set mode sense command */
	cmd.uscsi_cdb[0] = MMC_GET_MODE_SENSE;
	/* Don't return any block descriptors */
	cmd.uscsi_cdb[1] = 0x08;
	/* Set mode sense page code */
	cmd.uscsi_cdb[2] = page_code;
	cmd.uscsi_cdb[7] = (len >> 8) & 0xff;
	cmd.uscsi_cdb[8] = len & 0xff;

	return (scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE));
}

/*
 * Inquiry device information
 *
 * fd: device handle
 * buf: to store device information
 * len: buf length
 *
 * Return:
 * >=0: Get device information
 * <0: Fail to get device information
 */
int
inq_dev(int fd, uint8_t *buf, uint16_t len)
{
	char		rq_buf[RQBUF_SIZE];

	(void) memset(&cmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(cdb, 0, 16);
	cmd.uscsi_cdb = cdb;

	cmd.uscsi_flags = USCSI_READ|USCSI_SILENT;
	cmd.uscsi_timeout = SCMD_TIMEOUT;
	/* Set INQUIRY command */
	cmd.uscsi_cdb[0] = MMC_INQUIRY;
	cmd.uscsi_cdb[3] = (len >> 8) & 0xff;
	cmd.uscsi_cdb[4] = len & 0xff;
	cmd.uscsi_cdblen = 6;
	cmd.uscsi_bufaddr = (char *)buf;
	cmd.uscsi_buflen = len;

	return (scsi_cmd(fd, &cmd, rq_buf, RQBUF_SIZE));
}

/*
 * Print CD/DVD-ROM/RW device information
 *
 * dev_path: CD/DVD-ROM/RW device path under /dev/rdsk
 *
 * output (printed):
 * device path: production information: RW capability
 *
 * Return:
 * 0: Get and print device information
 * 1: Fail to open or inquiry device
 */
int
prt_dev_info(const char *dev_path)
{
	int	fd;
	int	ret;
	uint16_t	len;
	uint8_t 	*buf;
	uint8_t 	*status;

	fd = open(dev_path, O_RDONLY|O_NDELAY);

	if (fd < 0) {
		FPRINTF(stderr, "can not open deivce %s\n", dev_path);
		return (1);
	}

	buf = (uint8_t *)malloc(256);

	if (buf == NULL) {
		perror(
		    "fail to allocate memory space for inquiry information");
		(void) close(fd);
		return (1);
	}

	/* Inquiry device to get production information */
	ret = inq_dev(fd, buf, 96);

	if (ret < 0) {
		FPRINTF(stderr, "fail to inquiry device\n");
		free(buf);
		(void) close(fd);
		return (1);
	}

	PRINTF("%s | %.8s %.16s %.4s | CD Reader",
	    dev_path, &buf[8], &buf[16], &buf[32]);

	/* Get mode sense to check device write ability */
	ret = get_mode_sense(fd, 0x2a, buf, 254);

	/* Fail to get mode sense, assume only read ability */
	if (ret < 0) {
		PRINTF(" ");
		free(buf);
		(void) close(fd);
		return (0);
	}

	len = buf[6];
	len = (len << 8) | buf[7];
	len = len + 8;
	status = &buf[len + 2];
	/* To check write ability */
	if (status[1] & 0x01) {
		PRINTF("/Writer");
	}

	PRINTF(" ");

	free(buf);
	(void) close(fd);
	return (0);
}

/*
 * Print media or device profile number
 *
 * num: profile number
 *
 * Return:
 * 0: Print profile number
 * 1: Fail to parse the profile number, and print "unknown"
 */
int
prt_profile_num(uint16_t num)
{
	int ret;

	ret = 0;

	switch (num) {
		case 0x8:
			PRINTF("CD-ROM");
			break;
		case 0x9:
			PRINTF("CD-R");
			break;
		case 0xa:
			PRINTF("CD-RW");
			break;
		case 0x10:
			PRINTF("DVD-ROM");
			break;
		case 0x11:
			PRINTF("DVD-R Sequential recording");
			break;
		case 0x12:
			PRINTF("DVD-RAM");
			break;
		case 0x13:
			PRINTF("DVD-RW Restricted Overwrite");
			break;
		case 0x14:
			PRINTF("DVD-RW Sequential recording");
			break;
		case 0x15:
			PRINTF("DVD-R Dual Layer Sequential recording");
			break;
		case 0x16:
			PRINTF("DVD-R Dual Layer Jump recording");
			break;
		case 0x17:
			PRINTF("DVD-RW Dual Layer");
			break;
		case 0x18:
			PRINTF("DVD-Download disc recording");
			break;
		case 0x1a:
			PRINTF("DVD+RW");
			break;
		case 0x1b:
			PRINTF("DVD+R");
			break;
		case 0x2b:
			PRINTF("DVD+/-RW");
			break;
		case 0x40:
			PRINTF("BD-ROM");
			break;
		case 0x41:
			PRINTF("BD-R Sequential Recording Mode (SRM)");
			break;
		case 0x42:
			PRINTF("BD-R Random Recording Mode (RRM)");
			break;
		case 0x43:
			PRINTF("BD-RE");
			break;
		case 0x50:
			PRINTF("HD_DVD-ROM");
			break;
		case 0x51:
			PRINTF("HD_DVD-R");
			break;
		case 0x52:
			PRINTF("HD_DVD-RAM");
			break;
		case 0x53:
			PRINTF("HD_DVD-RW");
			break;
		case 0x58:
			PRINTF("HD_DVD-R Dual Layer");
			break;
		case 0x5a:
			PRINTF("HD_DVD-RW Dual Layer");
			break;
		default:
			PRINTF("unknown");
			ret = -1;
	}

	return (ret);
}

/*
 * Get device main path(/dev/rdsk/\*s2), open it and print
 * device information.
 * This is called indirectly via di_devlink_walk.
 *
 * devlink: device link handle
 * arg: device devfs path
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk device tree
 * DI_WALK_TERMINATE: get main device link, stop walk
 */
int
check_cdlink(di_devlink_t devlink, void* arg)
{
	const char *link_path;
	int	ret;

	link_path = di_devlink_path(devlink);

	if (strncmp(link_path, "/dev/rdsk/", 10) == 0) {
		if (strcmp("s2", strrchr(link_path, 's')) == 0) {
			ret = prt_dev_info(link_path);
			if (ret) {
				*(int *)arg = 0;
			} else {
				*(int *)arg = 1;
			}

			return (DI_WALK_TERMINATE);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Check CD/DVD-ROM/RW device and print device information
 * This is called indirectly via di_walk_minor.
 *
 * node: device node handle
 * minor: device minor handle
 * arg: NULL
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk device tree
 */
int
check_cddev(di_node_t node, di_minor_t minor, void *arg)
{
	char *minor_path;
	char *str;
	int	bCD;

	minor_path = di_devfs_minor_path(minor);
	if (minor_path) {
		bCD = 0;
		(void) di_devlink_walk(arg, NULL, minor_path,
		    DI_PRIMARY_LINK, &bCD, check_cdlink);
		if (bCD) {
			str = di_devfs_path(node);
			PRINTF("|%s\n", str);
			di_devfs_path_free(str);
		}
		di_devfs_path_free(minor_path);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Check and print media information
 *
 * dev_path: CD/DVD-ROM/RW device path
 *
 * output (printed):
 * device type:
 * media type:
 * media TOC information:
 *
 * Return:
 * 0: Get device and media information
 * 1: Fail to get device or media information
 */
int
check_media(char *dev_path)
{
	int	fd;
	int	ret;
	int	retry;
	uint8_t	buf[16];
	uint8_t 	*data;
	uint8_t 	*p;
	uint16_t	len;
	uint16_t	dev_type, media_type;
	uint32_t	addr;

	fd = open(dev_path, O_RDONLY|O_NDELAY);

	if (fd < 0) {
		FPRINTF(stderr, "can not open deivce %s\n", dev_path);
		return (1);
	}

	/* Get device config information to get device type */
	ret = get_disc_config(fd, 0, buf, 16);

	if (ret < 0) {
		buf[12] = 0xff;
		buf[13] = 0xff;
	}

	dev_type = ((((unsigned short)buf[12]) << 8) | buf[13]);

	PRINTF("Device Type: ");

	/* Print device profile number */
	(void) prt_profile_num(dev_type);
	PRINTF("\n");

	/* Detect media in device */
	ret = check_no_media(fd);

	if (!ret) {
		PRINTF("No media in device.\n");
		(void) close(fd);
		return (1);
	}

	/*
	 * If device has media, but not ready
	 * Wait device is ready
	 */
	if (ret == 2) {
		for (retry = 0; retry < 5; retry++) {
			ret = ready_dev(fd);

			if (ret >= 0) {
				break;
			}
		}
	} else {
		ret = 0;
	}

	if (ret) {
		PRINTF("Device not ready.\n");
		(void) close(fd);
		return (1);
	}

	/* Get device config to get media type */
	ret = get_disc_config(fd, 0, buf, 16);
	PRINTF("Media Type: ");

	/* If fail to get media type, set type is 0xffff(unknown) */
	if (ret < 0) {
		buf[6] = 0xff;
		buf[7] = 0xff;
	}

	media_type = ((((unsigned short)buf[6]) << 8) | buf[7]);

	/* Print media type profile number */
	ret = prt_profile_num(media_type);
	PRINTF("\n");

	ret = read_disc_info(fd, buf, 4);
	if (ret < 0) {
		/* If can not read disc info, assume can read disc TOC */
		buf[2] = 0x3;
	}

	/* Read media TOC information */
	if ((buf[2] & 0x3)) {
		ret = read_toc(fd, 0, buf, 4);

		if (ret < 0) {
			FPRINTF(stderr,
			    "fail to read device %s TOC information\n",
			    dev_path);
			(void) close(fd);
			return (1);
		}

		len = (((uint16_t)buf[0]) << 8 | buf[1]);
		len = len + 2;

		data = (uint8_t *)malloc(len);

		if (data == NULL) {
			perror("fail to allocate memory to store "
			    "TOC information");
			(void) close(fd);
			return (1);
		}

		ret = read_toc(fd, 0, data, len);

		if (ret < 0) {
			free(data);
			(void) close(fd);
			return (1);
		}

		PRINTF("Track Information:\n");
		p = &data[4];

		while (p < (data + len)) {
			if (p[2] != 0xaa) {
				PRINTF(" %-3d      |", p[2]);
			} else {
				PRINTF("Leadout   |");
			}

			if (p[1] & 0x4) {
				PRINTF("Data   |");
			} else {
				PRINTF("Audio  |");
			}

			addr = ((((uint32_t)p[4]) << 24) |
			    (((uint32_t)p[5]) << 16) |
			    (((uint32_t)p[6]) << 8) | p[7]);
			PRINTF("%du\n", addr);

			p = p + 8;
		}

		free(data);
	} else {
		PRINTF("Media is blank\n");
	}

	(void) close(fd);
	return (0);
}

int
main(int argc, char **argv)
{
	int	c, ret;
	di_node_t	root_node;
	di_devlink_handle_t	hDevLink;

	ret = 0;

	if (argc < 2) {
		usage();
	}

	while ((c = getopt(argc, argv, "lM:")) != EOF) {
		switch (c) {
		case 'l':
			root_node = di_init("/", DINFOSUBTREE|DINFOMINOR|
			    DINFOPROP|DINFOLYR);
			if (root_node == DI_NODE_NIL) {
				perror("di_init() failed");
				return (1);
			}
			hDevLink = di_devlink_init(NULL, 0);
			if (hDevLink == NULL) {
				perror("di_devlink_init() failed");
				di_fini(root_node);
				return (1);
			}
			/*
			 * Walk device tree, to get CD device based on
			 * keyword "ddi_block:cdrom"
			 */
			(void) di_walk_minor(root_node, DDI_NT_CD,
			    0, hDevLink, check_cddev);
			(void) di_devlink_fini(&hDevLink);
			di_fini(root_node);
			break;
		case 'M':
			ret = check_media(optarg);
			break;
		default:
			usage();
		}
	}
	return (ret);
}
