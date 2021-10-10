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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cpuvar.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/fs/snode.h>
#include <sys/pathname.h>
#include <sys/callb.h>
#include <vm/anon.h>
#include <sys/fs/swapnode.h>	/* for swapfs_minfree */
#include <sys/kmem.h>
#include <sys/cpr.h>
#include <sys/conf.h>
#include <sys/machclock.h>
#include <sys/dkio.h>		/* Disk operations */

/*
 * CPR miscellaneous support routines
 */
#define	cpr_open(path, mode,  vpp)	(vn_open(path, UIO_SYSSPACE, \
		mode, 0600, vpp, CRCREAT, 0))
#define	cpr_rdwr(rw, vp, basep, cnt)	(vn_rdwr(rw, vp,  (caddr_t)(basep), \
		cnt, 0LL, UIO_SYSSPACE, 0, (rlim64_t)MAXOFF_T, CRED(), \
		(ssize_t *)NULL))

extern void clkset(time_t);
extern cpu_t *i_cpr_bootcpu(void);
extern caddr_t i_cpr_map_setup(void);
extern void i_cpr_free_memory_resources(void);

extern kmutex_t cpr_slock;
extern size_t cpr_buf_size;
extern char *cpr_buf;
extern size_t cpr_pagedata_size;
extern char *cpr_pagedata;
extern int cpr_bufs_allocated;
extern int cpr_bitmaps_allocated;

static struct cprconfig cprconfig;
static int cprconfig_loaded = 0;
static int cpr_statefile_ok(vnode_t *, int);
#if defined(__sparc)
static int cpr_p_online(cpu_t *, int);
static void cpr_save_mp_state(void);
#endif

int cpr_is_zfs(struct vfs *);
static int cpr_get_config(void);

char cpr_default_path[] = CPR_DEFAULT;

#define	COMPRESS_PERCENT 40	/* approx compression ratio in percent */
#define	SIZE_RATE	115	/* increase size by 15% */
#define	INTEGRAL	100	/* for integer math */


/*
 * cmn_err() followed by a 1/4 second delay; this gives the
 * logging service a chance to flush messages and helps avoid
 * intermixing output from prom_printf().
 */
/*PRINTFLIKE2*/
void
cpr_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
	drv_usecwait(MICROSEC >> 2);
}


int
cpr_init(int fcn)
{
	int rc;

	/*
	 * Allow only one suspend/resume process.
	 */
	if (mutex_tryenter(&cpr_slock) == 0)
		return (EBUSY);

	if ((rc = cpr_get_config()) != 0)
		return (rc);

	CPR->c_flags = 0;
	CPR->c_substate = 0;
	CPR->c_cprboot_magic = 0;
	CPR->c_alloc_cnt = 0;
	CPR->c_fcn = fcn;
	if (fcn == AD_CPR_REUSABLE)
		CPR->c_flags |= C_REUSABLE;
	else
		CPR->c_flags |= C_SUSPENDING;

	/* If not saving the state, there is no reason to do mapping. */
	if (pm_cpr_save_state == 0) {
		return (0);
	}

	cpr_set_bitmap_size();

	/*
	 * verify the statefile exists and can hold the image.
	 */
	if (cpr_verify_statefile() != 0) {
		return (EINVAL);
	}

	/*
	 * reserve pages as transfer buffers for cpr_dump()
	 */
	CPR->c_mapping_area = i_cpr_map_setup();
	if (CPR->c_mapping_area == 0) {		/* no space in kernelmap */
		cpr_err(CE_CONT, "Unable to alloc from kernelmap.\n");
		return (EAGAIN);
	}
	if (cpr_debug & CPR_DEBUG3) {
		cpr_err(CE_CONT, "Reserved virtual range from 0x%p for writing "
		    "kas\n", (void *)CPR->c_mapping_area);
	}

	/*
	 * To be efficient, and since the statefile is OK, go ahead and
	 * allocate buffers and bitmaps used to write the buffer.
	 * This is a great place to try to optimize the buffers and
	 * their layouts.  See dumpsubr.c for assist on how dumpsys()
	 * handles this.
	 */
	if (cpr_buf == NULL) {
		ASSERT(cpr_pagedata == NULL);
		if (rc = cpr_alloc_bufs())
			return (rc);
	}

	/* allocate bitmaps */
	if (CPR->c_bmda == NULL) {
		if (rc = i_cpr_alloc_bitmaps()) {
			cpr_err(CE_WARN, "cannot allocate bitmaps");
			return (rc);
		}
	}
	return (0);
}

/*
 * This routine releases any resources used during the checkpoint.
 */
void
cpr_done(void)
{
	cpr_stat_cleanup();
	i_cpr_bitmap_cleanup();

	/*
	 * Free pages used by cpr buffers (via cpr_alloc_bufs).
	 */
	if (cpr_buf) {
		kmem_free(cpr_buf, cpr_buf_size);
		cpr_buf = NULL;
	}
	if (cpr_pagedata) {
		kmem_free(cpr_pagedata, cpr_pagedata_size);
		cpr_pagedata = NULL;
	}

	i_cpr_free_memory_resources();
	mutex_exit(&cpr_slock);
	cpr_err(CE_CONT, "System has been resumed.\n");
}


/*
 * reads config data into cprconfig
 */
static int
cpr_get_config(void)
{
	static char config_path[MAXPATHLEN];
	struct cprconfig *cf = &cprconfig;
	struct vnode *vp;
	char *fmt;
	int err;

	if (cprconfig_loaded)
		return (0);

	fmt = "cannot %s config file \"%s\", error %d\n";
	if (err = i_cpr_get_config_path(config_path)) {
		cpr_err(CE_CONT, fmt, "find", CPR_CONFIG, err);
		return (err);
	}
	PMD(PMD_SX, ("cpr_config: %s\n", config_path))

	if (err = vn_open(config_path, UIO_SYSSPACE, FREAD, 0, &vp, 0, 0)) {
		cpr_err(CE_CONT, fmt, "open", config_path, err);
		return (err);
	}

	err = cpr_rdwr(UIO_READ, vp, cf, sizeof (*cf));
	(void) VOP_CLOSE(vp, FREAD, 1, (offset_t)0, CRED(), NULL);
	VN_RELE(vp);
	if (err) {
		cpr_err(CE_CONT, fmt, "read", config_path, err);
		return (err);
	}

	if (cf->cf_magic == CPR_CONFIG_MAGIC)
		cprconfig_loaded = 1;
	else {
		cpr_err(CE_CONT, "invalid config file \"%s\", "
		    "rerun poweradm(1M)\n", config_path);
		err = EINVAL;
	}

	return (err);
}

/*
 * Verify that the information in the configuration file regarding the
 * location for the statefile is still valid, depending on cf_type.
 * for CFT_SPEC and CFT_ZVOL, cf_path must be the path to a block
 *      special file, it must have no file system mounted on it,
 *	and the translation of that device to a node in the prom's
 *	device tree must be the same as when poweradm was last run.
 *
 * Note that when we are done, cpr_state->c_vp references a valid
 * commonvp to the statefile.
 *
 * This is one of the few places where we have sparc and x86 specific
 * defines.  This is mostly due to the difference between sparc and
 * x86 boots.
 */
static int
cpr_verify_statefile_path(void)
{
	struct cprconfig *cf = &cprconfig;
	static const char long_name[] = "Statefile pathname is too long.\n";
	static const char lookup_fmt[] = "Lookup failed for "
	    "cpr statefile device %s.\n";
#if defined(__sparc)  /* for CFT_SPEC */
	static const char path_chg_fmt[] = "Device path for statefile "
	    "has changed from %s to %s.\t%s\n";
	static const char rerun[] = "Please rerun poweradm(1m).";
#endif
	int error;
	struct vnode *vp;
	extern struct vnode *common_specvp(struct vnode *vp);
	char *errstr;
#if defined(__sparc)
	char devpath[OBP_MAXPATHLEN];
#endif

	ASSERT(cprconfig_loaded);
	/*
	 * We need not worry about locking or the timing of releasing
	 * the vnode, since we when we are using it, we will be
	 * single threaded.
	 */

	switch (cf->cf_type) {
	/*
	 * SPECFS as statefile location only available on Sparc machines
	 * (though there really isn't a big reason why it couldn't be
	 * used on x86, it is that zfs is far better for a statefile on
	 * x86 platforms).
	 */
	case CFT_SPEC:
#if defined(__sparc)
		error = i_devname_to_promname(cf->cf_devfs, devpath,
		    OBP_MAXPATHLEN);
		if (error || strcmp(devpath, cf->cf_dev_prom)) {
			cpr_err(CE_CONT, path_chg_fmt,
			    cf->cf_dev_prom, devpath, rerun);
			return (error);
		}
		/*FALLTHROUGH*/
#else
		return (EINVAL);
#endif
	case CFT_ZVOL:
		if (strlen(cf->cf_path) > MAXNAMELEN) {
			cpr_err(CE_CONT, long_name);
			return (ENAMETOOLONG);
		}
		if ((error = lookupname(cf->cf_devfs,
		    UIO_SYSSPACE, FOLLOW, NULLVPP, &vp)) != 0) {
			cpr_err(CE_CONT, lookup_fmt, cf->cf_devfs);
			return (error);
		}


		if (vp->v_type != VBLK)
			errstr = "statefile must be a block device";
		else if (vfs_devismounted(vp->v_rdev))
			errstr = "statefile device must not "
			    "have a file system mounted on it";
		else if (IS_SWAPVP(vp))
			errstr = "statefile device must not "
			    "be configured as swap file";
		else
			errstr = NULL;

		if (errstr) {
			cpr_err(CE_CONT, "%s.\n", errstr);
			/* Release vp, since we are on an error path. */
			VN_RELE(vp);
			return (ENOTSUP);
		}
		C_VP = common_specvp(vp);
		return (0);
	case CFT_UFS:
		/* UFS no longer supported */
		errstr = "UFS statefile not supported";
		cpr_err(CE_CONT, "%s.\n", errstr);
		return (EINVAL);
	default:
		cpr_err(CE_PANIC, "invalid cf_type");
		return (EINVAL);
	}
}

/*
 * Make sure that the statefile can be used as a block special statefile
 * (meaning that is exists and has nothing mounted on it)
 * Returns errno if not a valid statefile.
 */
int
cpr_check_spec_statefile(void)
{
	int err;

	if (err = cpr_get_config())
		return (err);
	ASSERT(cprconfig.cf_type == CFT_SPEC ||
	    cprconfig.cf_type == CFT_ZVOL);

	if (cprconfig.cf_devfs == NULL)
		return (ENXIO);

	return (cpr_verify_statefile_path());

}

int
cpr_verify_statefile(void)
{
	int rc = 0;
	char *path = cpr_build_statefile_path();

	/*
	 * Open an existing file for writing, the state file needs to be
	 * pre-allocated since we can't and don't want to do allocation
	 * during checkpoint (too much of the OS is disabled, and takes
	 * too much time). So:
	 *    - check the vp to make sure it's the right type.
	 *    - check that the statefile exists and is useable.
	 */

	if (path == NULL) {
		PMD(PMD_SX, ("cpr_suspend: verify statefile - no path\n"))
		return (ENXIO);
	} else if (rc = cpr_verify_statefile_path()) {
		return (rc);
	}

	ASSERT(MUTEX_HELD(&cpr_slock));

	/*
	 * Use VOP_OPEN to open the statefile.
	 */
	if (C_VP) {
		if ((rc = VOP_OPEN(&C_VP, FREAD | FWRITE, kcred, NULL)) != 0) {
			cpr_err(CE_WARN, "cannot open statefile %s", path);
			VN_RELE(C_VP);
			C_VP = 0;
			return (rc);
		}
	} else {
		PMD(PMD_SX,
		    ("cpr_suspend: don't have a vp\n"))
		return (EBADF);
	}


	/* Verfiy statefile exists and is usable */
	rc = cpr_statefile_ok(C_VP, 0);
	if (rc) {
		PMD(PMD_SX, ("cpr_suspend: statefile not OK\n"))
		return (rc);
	}

	return (0);
}

/*
 * Lookup device size and return available space in bytes.
 * NOTE: Since prop_op(9E) can't tell the difference between a character
 * and a block reference, it is ok to ask for "Size" instead of "Nblocks".
 */
size_t
cpr_get_devsize(dev_t dev)
{
	size_t bytes = 0;

	bytes = cdev_Size(dev);
	if (bytes == 0)
		bytes = cdev_size(dev);

	if (bytes > CPR_SPEC_OFFSET)
		bytes -= CPR_SPEC_OFFSET;
	else
		bytes = 0;

	return (bytes);
}

/*
 * do a simple estimate of the space needed to hold the statefile
 * taking compression into account, but be fairly conservative
 * so we have a better chance of completing; when dump fails,
 * the retry cost is fairly high.
 *
 * If possible, adjust CPR buffers for the most optimum transfer rate.
 *
 * Do disk blocks allocation for the state file if no space has
 * been allocated yet. Since the state file will not be removed,
 * allocation should only be done once.
 *
 * Note: ufs statefile no longer supported, and the zvol or specfs
 * must exist and be of proper size *before* calling suspend.
 */
static int
cpr_statefile_ok(vnode_t *vp, int alloc_retry)
{
	extern size_t cpr_bitmap_size;
	const int UCOMP_RATE = 20; /* comp. ratio*10 for user pages */
	u_longlong_t size, ksize, raw_data;
	char *str, *est_fmt;
	size_t space;
	int error = 0;
	u_longlong_t cpd_size;
	pgcnt_t npages, nback;
	int ndvram = 0;
	vnode_t *cdev_vp;

	/*
	 * Shouldn't be reentering on an alloc_retry, so ASSRT for it
	 * on debug kernels, and fail on others.
	 */
	ASSERT(!alloc_retry);
	if (alloc_retry) {
		return (ENOMEM);
	}

	/*
	 * The dump device *must* be VBLK (the ZFS dataset is VBLK).
	 */
	if (vp->v_type != VBLK) {
		cpr_err(CE_CONT,
		    "Statefile must be block special file.");
		return (EINVAL);
	}

	/*
	 * number of pages short for swapping.
	 */
	STAT->cs_nosw_pages = k_anoninfo.ani_mem_resv;

	str = "cpr_statefile_ok:";

	CPR_DEBUG(CPR_DEBUG9, "Phys swap: max=%lu resv=%lu\n",
	    k_anoninfo.ani_max, k_anoninfo.ani_phys_resv);
	/*LINTED*/
	CPR_DEBUG(CPR_DEBUG9, "Mem swap: max=%ld resv=%lu\n",
	    MAX(availrmem - swapfs_minfree, 0),
	    k_anoninfo.ani_mem_resv);
	CPR_DEBUG(CPR_DEBUG9, "Total available swap: %ld\n",
	    CURRENT_TOTAL_AVAILABLE_SWAP);

	/*
	 * Try and compute necessary size, starting with ndvram.
	 */
	(void) callb_execute_class(CB_CL_CPR_FB,
	    (int)(uintptr_t)&ndvram);

	if (cpr_debug & (CPR_DEBUG1 | CPR_DEBUG6))
		prom_printf("ndvram size = %d\n", ndvram);

	/*
	 * estimate 1 cpd_t for every (CPR_MAXCONTIG / 2) pages
	 *
	 * Note that this calculation was originally for Sparc
	 * platforms, but has been retained for others as there
	 * is an appearance that it is accurate.  Should this
	 * assumption change, the calculation will need to be moved
	 * to the impl files.
	 */
	npages = cpr_count_kpages(REGULAR_BITMAP, cpr_nobit);
	cpd_size = sizeof (cpd_t) * (npages / (CPR_MAXCONTIG / 2));
	raw_data = cpd_size + cpr_bitmap_size;
	ksize = ndvram + mmu_ptob(npages);

	est_fmt = "%s estimated size with %scompression %lld, ksize %lld\n";

	nback = mmu_ptob(STAT->cs_nosw_pages);
	if (CPR->c_flags & C_COMPRESSING) {
		size = ((ksize * COMPRESS_PERCENT) / INTEGRAL) +
		    raw_data + ((nback * 10) / UCOMP_RATE);
		CPR_DEBUG(CPR_DEBUG1, est_fmt, str, "", size, ksize);
	} else {
		size = ksize + raw_data + nback;
		CPR_DEBUG(CPR_DEBUG1, est_fmt, str, "no ", size, ksize);
	}

	/*
	 * Now calculate how much space we have in the statefile.
	 */
	space = cpr_get_devsize(vp->v_rdev);
	if (cpr_debug & (CPR_DEBUG1 | CPR_DEBUG6))
		prom_printf("statefile dev size %lu\n", space);

	/*
	 * Export the estimated filesize info, this value will be
	 * compared before dumping out the statefile in the case of
	 * no compression.
	 */
	STAT->cs_est_statefsz = size + (size / 10);
	if (cpr_debug & (CPR_DEBUG1 | CPR_DEBUG6))
		prom_printf("%s Estimated statefile size %llu, "
		    "space %lu\n", str, size, space);
	if (size > space) {
		cpr_err(CE_CONT, "Statefile partition too small.");
		return (ENOMEM);
	}

	/*
	 * See if we can get transfer information from the
	 * corresponding character device, and use it to try
	 * and setup the optimal performance.
	 * "Borrowed" from dumpinit() (and should probably
	 * be part of common "dumpsubr" code).
	 */
	cdev_vp = makespecvp(VTOS(vp)->s_dev, VCHR);
	if (cdev_vp != NULL) {

		if (VOP_OPEN(&cdev_vp, FREAD|FWRITE, kcred, NULL) == 0) {
			size_t blk_size;
			struct dk_cinfo dki;
			struct dk_minfo minf;

			if (VOP_IOCTL(cdev_vp, DKIOCGMEDIAINFO,
			    (intptr_t)&minf, FKIOCTL, kcred, NULL,
			    NULL) == 0 && minf.dki_lbsize != 0)
				blk_size = minf.dki_lbsize;
			else
				blk_size = DEV_BSIZE;

			if (VOP_IOCTL(cdev_vp, DKIOCINFO,
			    (intptr_t)&dki, FKIOCTL, kcred, NULL,
			    NULL) == 0) {
				CPR->c_iosize =
				    dki.dki_maxtransfer * blk_size;
				/*
				 * Need a resizing function
				 * similar to dumpbuf_resize()
				 * For now, be happy that we have
				 * recorded the best transfer size.
				 */
			}
			/*
			 * since we are working with a zvol, dumpify
			 * it.
			 */
			if ((error = VOP_IOCTL(cdev_vp, DKIOCPREALLOCINIT,
			    NULL, FKIOCTL, kcred, NULL, NULL)) != 0) {
				return (EINVAL);
			}

			(void) VOP_CLOSE(cdev_vp, FREAD | FWRITE, 1, 0,
			    kcred, NULL);
		}

		VN_RELE(cdev_vp);
	}

	return (error);
}


void
cpr_statef_close(void)
{
	if (C_VP) {
		(void) VOP_CLOSE(C_VP, FWRITE, 1, (offset_t)0, CRED(), NULL);
		VN_RELE(C_VP);
		C_VP = 0;
	}
}


/*
 * open cpr default file and display error
 */
int
cpr_open_deffile(int mode, vnode_t **vpp)
{
	int error;

	if (error = cpr_open(cpr_default_path, mode, vpp))
		cpr_err(CE_CONT, "cannot open \"%s\", error %d\n",
		    cpr_default_path, error);
	return (error);
}


/*
 * write cdef_t to disk.  This contains the original values of prom
 * properties that we modify.  We fill in the magic number of the file
 * here as a signal to the booter code that the state file is valid.
 * Be sure the file gets synced, since we may be shutting down the OS.
 */
int
cpr_write_deffile(cdef_t *cdef)
{
	struct vnode *vp;
	char *str;
	int rc;

	if (rc = cpr_open_deffile(FCREAT|FWRITE, &vp))
		return (rc);

	if (rc = cpr_rdwr(UIO_WRITE, vp, cdef, sizeof (*cdef)))
		str = "write";
	else if (rc = VOP_FSYNC(vp, FSYNC, CRED(), NULL))
		str = "fsync";
	(void) VOP_CLOSE(vp, FWRITE, 1, (offset_t)0, CRED(), NULL);
	VN_RELE(vp);

	if (rc) {
		cpr_err(CE_WARN, "%s error %d, file \"%s\"",
		    str, rc, cpr_default_path);
	}
	return (rc);
}

/*
 * Clear the magic number in the defaults file.  This tells the booter
 * program that the state file is not current and thus prevents
 * any attempt to restore from an obsolete state file.
 */
void
cpr_clear_definfo(void)
{
	struct vnode *vp;
	cmini_t mini;

	if ((CPR->c_cprboot_magic != CPR_DEFAULT_MAGIC) ||
	    cpr_open_deffile(FCREAT|FWRITE, &vp))
		return;
	mini.magic = mini.reusable = 0;
	(void) cpr_rdwr(UIO_WRITE, vp, &mini, sizeof (mini));
	(void) VOP_CLOSE(vp, FWRITE, 1, (offset_t)0, CRED(), NULL);
	VN_RELE(vp);
}

/*
 * If the cpr default file is invalid, then we must not be in reusable mode
 * if it is valid, it tells us our mode
 */
int
cpr_get_reusable_mode(void)
{
	struct vnode *vp;
	cmini_t mini;
	int rc;

	if (cpr_open(cpr_default_path, FREAD, &vp))
		return (0);

	rc = cpr_rdwr(UIO_READ, vp, &mini, sizeof (mini));
	(void) VOP_CLOSE(vp, FREAD, 1, (offset_t)0, CRED(), NULL);
	VN_RELE(vp);
	if (rc == 0 && mini.magic == CPR_DEFAULT_MAGIC)
		return (mini.reusable);

	return (0);
}

/*
 * clock/time related routines
 */
static time_t   cpr_time_stamp;


void
cpr_tod_get(cpr_time_t *ctp)
{
	timestruc_t ts;

	mutex_enter(&tod_lock);
	ts = TODOP_GET(tod_ops);
	mutex_exit(&tod_lock);
	ctp->tv_sec = (time32_t)ts.tv_sec;
	ctp->tv_nsec = (int32_t)ts.tv_nsec;
}

void
cpr_tod_status_set(int tod_flag)
{
	mutex_enter(&tod_lock);
	tod_status_set(tod_flag);
	mutex_exit(&tod_lock);
}

void
cpr_save_time(void)
{
	cpr_time_stamp = gethrestime_sec();
}

/*
 * correct time based on saved time stamp or hardware clock
 */
void
cpr_restore_time(void)
{
	clkset(cpr_time_stamp);
}

int
cpr_is_zfs(struct vfs *vfsp)
{
	char *fsname;

	fsname = vfssw[vfsp->vfs_fstype].vsw_name;
	return (strcmp(fsname, "zfs") == 0);
}

#if defined(__sparc)
/*
 * CPU ONLINE/OFFLINE CODE
 * Only used on Sparc.
 */
int
cpr_mp_offline(void)
{
	cpu_t *cp, *bootcpu;
	int rc = 0;
	int brought_up_boot = 0;

	/*
	 * Do nothing for UP.
	 */
	if (ncpus == 1)
		return (0);

	mutex_enter(&cpu_lock);

	cpr_save_mp_state();

	bootcpu = i_cpr_bootcpu();
	if (!CPU_ACTIVE(bootcpu)) {
		if ((rc = cpr_p_online(bootcpu, CPU_CPR_ONLINE))) {
			mutex_exit(&cpu_lock);
			return (rc);
		}
		brought_up_boot = 1;
	}

	cp = cpu_list;
	do {
		if (cp == bootcpu)
			continue;
		if (cp->cpu_flags & CPU_OFFLINE)
			continue;
		if ((rc = cpr_p_online(cp, CPU_CPR_OFFLINE))) {
			mutex_exit(&cpu_lock);
			return (rc);
		}
	} while ((cp = cp->cpu_next) != cpu_list);
	if (brought_up_boot && (cpr_debug & (CPR_DEBUG1 | CPR_DEBUG6)))
		prom_printf("changed cpu %p to state %d\n",
		    (void *)bootcpu, CPU_CPR_ONLINE);
	mutex_exit(&cpu_lock);

	return (rc);
}

int
cpr_mp_online(void)
{
	cpu_t *cp, *bootcpu = CPU;
	int rc = 0;

	/*
	 * Do nothing for UP.
	 */
	if (ncpus == 1)
		return (0);

	/*
	 * cpr_save_mp_state() sets CPU_CPR_ONLINE in cpu_cpr_flags
	 * to indicate a cpu was online at the time of cpr_suspend();
	 * now restart those cpus that were marked as CPU_CPR_ONLINE
	 * and actually are offline.
	 */
	mutex_enter(&cpu_lock);
	for (cp = bootcpu->cpu_next; cp != bootcpu; cp = cp->cpu_next) {
		/*
		 * Clear the CPU_FROZEN flag in all cases.
		 */
		cp->cpu_flags &= ~CPU_FROZEN;

		if (CPU_CPR_IS_OFFLINE(cp))
			continue;
		if (CPU_ACTIVE(cp))
			continue;
		if ((rc = cpr_p_online(cp, CPU_CPR_ONLINE))) {
			mutex_exit(&cpu_lock);
			return (rc);
		}
	}

	/*
	 * turn off the boot cpu if it was offlined
	 */
	if (CPU_CPR_IS_OFFLINE(bootcpu)) {
		if ((rc = cpr_p_online(bootcpu, CPU_CPR_OFFLINE))) {
			mutex_exit(&cpu_lock);
			return (rc);
		}
	}
	mutex_exit(&cpu_lock);
	return (0);
}

static void
cpr_save_mp_state(void)
{
	cpu_t *cp;

	ASSERT(MUTEX_HELD(&cpu_lock));

	cp = cpu_list;
	do {
		cp->cpu_cpr_flags &= ~CPU_CPR_ONLINE;
		if (CPU_ACTIVE(cp))
			CPU_SET_CPR_FLAGS(cp, CPU_CPR_ONLINE);
	} while ((cp = cp->cpu_next) != cpu_list);
}

/*
 * change cpu to online/offline
 */
static int
cpr_p_online(cpu_t *cp, int state)
{
	int rc;

	ASSERT(MUTEX_HELD(&cpu_lock));

	switch (state) {
	case CPU_CPR_ONLINE:
		rc = cpu_online(cp);
		break;
	case CPU_CPR_OFFLINE:
		rc = cpu_offline(cp, CPU_FORCED);
		break;
	}
	if (rc) {
		cpr_err(CE_WARN, "Failed to change processor %d to "
		    "state %d, (errno %d)", cp->cpu_id, state, rc);
	}
	return (rc);
}
#endif /* __sparc */

/* Misc routines used for debugging. */
#ifdef DEBUG
/*
 * Verify that cpu's are properly parked for suspend.
 */
boolean_t
cpr_cpus_parked(void) {
	cpu_t	*cp;
	cpu_t	*curcpu = CPU;
	/*
	 * If only one cpu, already OK.
	 */
	if (ncpus == 1)
		return (B_TRUE);

	if (cpus_paused())
		return (B_TRUE);

	/*
	 * Check to see that cpu's are offlined.
	 */
	cp = cpu_list;
	do {
		/* Obviously this cpu should be online. */
		if ((cp == curcpu) && (cp->cpu_flags & CPU_CPR_ONLINE)) {
			continue;
		}
		/* if any other cpu is online, return FALSE */
		if (!(cp->cpu_flags & CPU_OFFLINE)) {
			return (B_FALSE);
		}
	} while ((cp = cp->cpu_next) != cpu_list);

	/* If we got here, all checks above passed */
	return (B_TRUE);
}
#endif /* DEBUG */
/*
 * Construct the pathname of the state file and return a pointer to
 * caller.  Read the config file to get the mount point of the
 * filesystem and the pathname within fs.
 */
char *
cpr_build_statefile_path(void)
{
	struct cprconfig *cf = &cprconfig;

	if (cpr_get_config())
		return (NULL);

	switch (cf->cf_type) {
	case CFT_UFS:
		cpr_err(CE_CONT, "UFS Statefile no longer supported.\n");
		return (NULL);
	case CFT_ZVOL:
		/*FALLTHROUGH*/
	case CFT_SPEC:
		return (cf->cf_devfs);
	default:
		cpr_err(CE_PANIC, "invalid statefile type");
		/*NOTREACHED*/
		return (NULL);
	}
}

/*
 * For the purpose of CPR, CFT_SPEC and CFT_ZVOL are the same.
 * However, if "flag" is non-zero, check only for specfs.
 */
int
cpr_statefile_is_spec(int flag)
{
	struct cprconfig *cf = &cprconfig;

	if (cpr_get_config())
		return (0);

	/*
	 * In many places, we can treat zfs and specfs the same.
	 * However, if flag is non-zero, the caller is specifically
	 * interested in specfs (such as Sparc OBP).
	 */
	if (flag != 0)
		return (cf->cf_type == CFT_SPEC);
	else
		return (cf->cf_type == CFT_SPEC || cf->cf_type == CFT_ZVOL);
}

char *
cpr_get_statefile_prom_path(void)
{
	struct cprconfig *cf = &cprconfig;

	ASSERT(cprconfig_loaded);
	ASSERT(cf->cf_magic == CPR_CONFIG_MAGIC);
	ASSERT(cf->cf_type == CFT_SPEC || cf->cf_type == CFT_ZVOL);
	return (cf->cf_dev_prom);
}


/*
 * This is a list of file systems that are allowed to be writeable when a
 * reusable statefile checkpoint is taken.  They must not have any state that
 * cannot be restored to consistency by simply rebooting using the checkpoint.
 * (In contrast to ufs and pcfs which have disk state that could get
 * out of sync with the in-kernel data).
 */
int
cpr_reusable_mount_check(void)
{
	struct vfs *vfsp;
	char *fsname;
	char **cpp;
	static char *cpr_writeok_fss[] = {
		"autofs", "devfs", "fd", "lofs", "mntfs", "namefs", "nfs",
		"proc", "tmpfs", "ctfs", "objfs", "dev", NULL
	};

	vfs_list_read_lock();
	vfsp = rootvfs;
	do {
		if (vfsp->vfs_flag & VFS_RDONLY) {
			vfsp = vfsp->vfs_next;
			continue;
		}
		fsname = vfssw[vfsp->vfs_fstype].vsw_name;
		for (cpp = cpr_writeok_fss; *cpp; cpp++) {
			if (strcmp(fsname, *cpp) == 0)
				break;
		}
		/*
		 * if the inner loop reached the NULL terminator,
		 * the current fs-type does not match any OK-type
		 */
		if (*cpp == NULL) {
			cpr_err(CE_CONT, "a filesystem of type %s is "
			    "mounted read/write.\nReusable statefile requires "
			    "no writeable filesystem of this type be mounted\n",
			    fsname);
			vfs_list_unlock();
			return (EINVAL);
		}
		vfsp = vfsp->vfs_next;
	} while (vfsp != rootvfs);
	vfs_list_unlock();
	return (0);
}

/*
 * return statefile offset in DEV_BSIZE units
 */
int
cpr_statefile_offset(void)
{
	return (cprconfig.cf_type != CFT_UFS ? btod(CPR_SPEC_OFFSET) : 0);
}

/*
 * Force a fresh read of the cprinfo per uadmin 3 call
 */
void
cpr_forget_cprconfig(void)
{
	cprconfig_loaded = 0;
}

int
cpr_validate_definfo(int reusable)
{
	cdef_t	def_info;

	bzero(&def_info, sizeof (cdef_t));
	def_info.mini.magic = CPR->c_cprboot_magic = CPR_DEFAULT_MAGIC;
	def_info.mini.reusable = reusable;
	return (cpr_write_deffile(&def_info));
}

void
cpr_notice(char *msg) {
	static char cstr[] = "\014" "\033[1P" "\033[18;21H";

	/* If debugging, don't clear the screen and center the cursor */
	if (!cpr_debug)
		prom_printf(cstr);
	prom_printf(msg);
}

void
cpr_spinning_bar(void)
{
	static char *spin_strings[] = { "|\b", "/\b", "-\b", "\\\b" };
	static int idx;

	prom_printf(spin_strings[idx]);
	if (++idx == 4)
		idx = 0;
}

void
cpr_print_val(int val)
{
	prom_printf("%d\r", val);
}

void
cpr_send_notice(void)
{
	cpr_notice("Saving System State. Please Wait... ");
}

void
cpr_resume_notice(void)
{
	cpr_notice("Restoring System State. Please Wait... ");
}
