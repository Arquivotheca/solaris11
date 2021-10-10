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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#ifndef	_SYS_LOFI_H
#define	_SYS_LOFI_H

#include <sys/types.h>
#include <sys/time.h>
#include <sys/taskq.h>
#include <sys/vtoc.h>
#include <sys/dkio.h>
#include <sys/vnode.h>
#include <sys/list.h>
#include <sys/crypto/api.h>
#include <sys/zone.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * /dev names:
 *	/dev/lofictl	- master control device
 *	/dev/lofi	- block devices, named by minor number
 *	/dev/rlofi	- character devices, named by minor number
 */
#define	LOFI_DRIVER_NAME	"lofi"
#define	LOFI_CTL_NODE		"ctl"
#define	LOFI_CTL_NAME		LOFI_DRIVER_NAME LOFI_CTL_NODE
#define	LOFI_BLOCK_NAME		LOFI_DRIVER_NAME
#define	LOFI_CHAR_NAME		"r" LOFI_DRIVER_NAME

#define	SEGHDR		1
#define	COMPRESSED	1
#define	UNCOMPRESSED	0
#define	MAXALGLEN	36

/*
 *
 * Use is:
 *	ld = open("/dev/lofictl", O_RDWR | O_EXCL);
 *
 * lofi must be opened exclusively. Access is controlled by permissions on
 * the device, which is 644 by default. Write-access is required for ioctls
 * that change state, but only read-access is required for the ioctls that
 * return information. Basically, only root can add and remove files, but
 * non-root can look at the current lists.
 *
 * ioctl usage:
 *
 * kernel ioctls
 *
 *	strcpy(li.li_filename, "somefilename");
 *	ioctl(ld, LOFI_MAP_FILE, &li);
 *	newminor = li.li_minor;
 *
 *	strcpy(li.li_filename, "somefilename");
 *	ioctl(ld, LOFI_UNMAP_FILE, &li);
 *
 *	strcpy(li.li_filename, "somefilename");
 *	li.li_minor = minor_number;
 *	ioctl(ld, LOFI_MAP_FILE_MINOR, &li);
 *
 *	li.li_minor = minor_number;
 *	ioctl(ld, LOFI_UNMAP_FILE_MINOR, &li);
 *
 *	li.li_minor = minor_number;
 *	ioctl(ld, LOFI_GET_FILENAME, &li);
 *	filename = li.li_filename;
 *	encrypted = li.li_crypto_enabled;
 *
 *	strcpy(li.li_filename, "somefilename");
 *	ioctl(ld, LOFI_GET_MINOR, &li);
 *	minor = li.li_minor;
 *
 *	li.li_minor = 0;
 *	ioctl(ld, LOFI_GET_MAXMINOR, &li);
 *	maxminor = li.li_minor;
 *
 *	strcpy(li.li_filename, "somefilename");
 *	li.li_minor = 0;
 *	ioctl(ld, LOFI_CHECK_COMPRESSED, &li);
 *
 * If the 'li_force' flag is set for any of the LOFI_UNMAP_* commands, then if
 * the device is busy, the underlying vnode will be closed, and any subsequent
 * operations will fail.  It will behave as if the device had been forcibly
 * removed, so the DKIOCSTATE ioctl will return DKIO_DEV_GONE.  When the device
 * is last closed, it will be torn down.
 *
 * If the 'li_cleanup' flag is set for any of the LOFI_UNMAP_* commands, then
 * if the device is busy, it is marked for removal at the next time it is
 * no longer held open by anybody.  When the device is last closed, it will be
 * torn down.
 *
 * Oh, and last but not least: these ioctls are totally private and only
 * for use by lofiadm(1M).
 *
 */

typedef enum	iv_method {
	IVM_NONE,	/* no iv needed, iv is null */
	IVM_ENC_BLKNO	/* iv is logical block no. encrypted */
} iv_method_t;

struct lofi_ioctl {
	uint32_t 	li_minor;
	boolean_t	li_force;
	boolean_t	li_cleanup;
	char	li_filename[MAXPATHLEN];

	/* the following fields are required for compression support */
	char	li_algorithm[MAXALGLEN];

	/* the following fields are required for encryption support */
	boolean_t	li_crypto_enabled;
	crypto_mech_name_t	li_cipher;	/* for data */
	uint32_t	li_key_len;		/* for data */
	char		li_key[56];	/* for data: max 448-bit Blowfish key */
	crypto_mech_name_t	li_iv_cipher;	/* for iv derivation */
	uint32_t	li_iv_len;		/* for iv derivation */
	iv_method_t	li_iv_type;		/* for iv derivation */
	uint_t		li_lbs;
	uint_t		li_pbs;
};

#define	LOFI_IOC_BASE		(('L' << 16) | ('F' << 8))

#define	LOFI_MAP_FILE		(LOFI_IOC_BASE | 0x01)
#define	LOFI_MAP_FILE_MINOR	(LOFI_IOC_BASE | 0x02)
#define	LOFI_UNMAP_FILE		(LOFI_IOC_BASE | 0x03)
#define	LOFI_UNMAP_FILE_MINOR	(LOFI_IOC_BASE | 0x04)
#define	LOFI_GET_FILENAME	(LOFI_IOC_BASE | 0x05)
#define	LOFI_GET_MINOR		(LOFI_IOC_BASE | 0x06)
#define	LOFI_GET_MAXMINOR	(LOFI_IOC_BASE | 0x07)
#define	LOFI_CHECK_COMPRESSED	(LOFI_IOC_BASE | 0x08)
#define	LOFI_SET_BLOCKSIZE	(LOFI_IOC_BASE | 0x09)

/*
 * file types that might be usable with lofi, maybe. Only regular
 * files are documented though.
 */
#define	S_ISLOFIABLE(mode) \
	(S_ISREG(mode) || S_ISBLK(mode) || S_ISCHR(mode))

#if defined(_KERNEL)


/*
 * Cache decompressed data segments for the compressed lofi images.
 *
 * To avoid that we have to decompress data of a compressed
 * segment multiple times when accessing parts of the segment's
 * data we cache the uncompressed data, using a simple linked list.
 */
struct lofi_comp_cache {
	list_node_t	lc_list;		/* linked list */
	uchar_t		*lc_data;		/* decompressed segment data */
	uint64_t	lc_index;		/* segment index */
};

#define	V_ISLOFIABLE(vtype) \
	((vtype == VREG) || (vtype == VBLK) || (vtype == VCHR))

/*
 * Pre-allocated memory buffers for the purpose of compression
 */
struct compbuf {
	void		*buf;
	uint32_t	bufsize;
	int		inuse;
};

/*
 * Need exactly 6 bytes to identify encrypted lofi image
 */
extern const char lofi_crypto_magic[6];
#define	LOFI_CRYPTO_MAGIC	{ 'C', 'F', 'L', 'O', 'F', 'I' }
#define	LOFI_CRYPTO_VERSION	((uint16_t)0)
#define	LOFI_CRYPTO_DATA_SECTOR	((uint32_t)16)		/* for version 0 */

/*
 * Crypto metadata for encrypted lofi images
 * The fields here only satisfy initial implementation requirements.
 */
struct crypto_meta {
	char		magic[6];		/* LOFI_CRYPTO_MAGIC */
	uint16_t	version;		/* version of encrypted lofi */
	char		reserved1[96];		/* future use */
	uint32_t	data_sector;		/* start of data area */
	char		pad[404];		/* end on DEV_BSIZE bdry */
	/* second header block is not defined at this time */
};

struct lofi_state {
	vnode_t		*ls_vp;		/* open real vnode */
	vnode_t		*ls_stacked_vp;	/* open vnode */
	kmutex_t	ls_io_lock;	/* protects ls_vp and I/O path */
	kcondvar_t	ls_io_cv;	/* signal changes that impacts I/O */
	uint32_t	ls_io_count;	/* # pending I/O requests */
	boolean_t	ls_close_req;	/* force close requested */
	u_offset_t	ls_vp_size;
	uint32_t	ls_blk_open;
	uint32_t	ls_chr_open;
	uint32_t	ls_lyr_open_count;
	int		ls_openflag;
	int		ls_open_rw_err;
	boolean_t	ls_cleanup;	/* cleanup on close */
	taskq_t		*ls_taskq;
	kstat_t		*ls_kstat;
	kmutex_t	ls_kstat_lock;
	struct dk_geom	ls_dkg;
	struct vtoc	ls_vtoc;
	struct dk_cinfo	ls_ci;
	uint_t		ls_lbs;		/* logical blocksize */
	uint_t		ls_pbs;		/* physical blocksize */
	zone_ref_t	ls_zone;
	list_node_t	ls_list;	/* all lofis */
	dev_t		ls_dev;		/* this node's dev_t */

	/* the following fields are required for compression support */
	int		ls_comp_algorithm_index; /* idx into compress_table */
	char		ls_comp_algorithm[MAXALGLEN];
	uint32_t	ls_uncomp_seg_sz; /* sz of uncompressed segment */
	uint32_t	ls_comp_index_sz; /* number of index entries */
	uint32_t	ls_comp_seg_shift; /* exponent for byte shift */
	uint32_t	ls_uncomp_last_seg_sz; /* sz of last uncomp segment */
	uint64_t	ls_comp_offbase; /* offset of actual compressed data */
	uint64_t	*ls_comp_seg_index; /* array of index entries */
	caddr_t		ls_comp_index_data; /* index pages loaded from file */
	uint32_t	ls_comp_index_data_sz;
	u_offset_t	ls_vp_comp_size; /* actual compressed file size */

	/* pre-allocated list of buffers for compressed segment data */
	kmutex_t	ls_comp_bufs_lock;
	struct compbuf	*ls_comp_bufs;

	/* lock and anchor for compressed segment caching */
	kmutex_t	ls_comp_cache_lock;	/* protects ls_comp_cache */
	list_t		ls_comp_cache;		/* cached decompressed segs */
	uint32_t	ls_comp_cache_count;

	/* the following fields are required for encryption support */
	boolean_t		ls_crypto_enabled;
	u_offset_t		ls_crypto_offset;	/* crypto meta size */
	struct crypto_meta	ls_crypto;
	crypto_mechanism_t	ls_mech;	/* for data encr/decr */
	crypto_key_t		ls_key;		/* for data encr/decr */
	crypto_mechanism_t	ls_iv_mech;	/* for iv derivation */
	size_t			ls_iv_len;	/* for iv derivation */
	iv_method_t		ls_iv_type;	/* for iv derivation */
	kmutex_t		ls_crypto_lock;
	crypto_ctx_template_t	ls_ctx_tmpl;

};

#endif	/* _KERNEL */

/*
 * Common signature for all lofi compress functions
 */
typedef int lofi_compress_func_t(void *src, size_t srclen, void *dst,
	size_t *destlen, int level);

/*
 * Information about each compression function
 */
typedef struct lofi_compress_info {
	lofi_compress_func_t	*l_decompress;
	lofi_compress_func_t	*l_compress;
	int			l_level;
	char			*l_name;	/* algorithm name */
} lofi_compress_info_t;

enum lofi_compress {
	LOFI_COMPRESS_GZIP = 0,
	LOFI_COMPRESS_GZIP_6 = 1,
	LOFI_COMPRESS_GZIP_9 = 2,
	LOFI_COMPRESS_LZMA = 3,
	LOFI_COMPRESS_FUNCTIONS
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LOFI_H */
