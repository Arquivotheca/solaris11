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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "nfs_mdb.h"
#include "nfs4_mdb.h"
#include <mdb/mdb_ctf.h>

static int nfs_io_stats(kstat_t *);
static int async_counter(uintptr_t, const void *, void *);
static int mntinfo4_info(uintptr_t, mntinfo4_t *, int);
static int mntinfo_info(mntinfo_t *, int);

extern int nfs4_diag_walk(uintptr_t, void *, int);

void
nfs_mntinfo_help(void)
{
	mdb_printf(
	"<mntinfo >::nfs_mntinfo     -> gives mntinfo_t information\n"
	"          ::nfs_mntinfo     -> walks thru all NFSv2/v3 mntinfo_t\n"
	"\nEach of these formats also takes the following argument\n"
	"	-v	-> Verbose output\n");
}

void
nfs4_mntinfo_help(void)
{
	mdb_printf(
	"<mntinfo4>::nfs4_mntinfo     -> gives mntinfo4_t information\n"
	"          ::nfs4_mntinfo     -> walks thru all NFSv4 mntinfo4_t\n"
	"\nEach of these formats also takes the following argument\n"
	"	-v	-> Verbose output\n");
}

static const mdb_bitmask_t bm_vfs[] = {
	{ "VFS_RDONLY",		VFS_RDONLY,		VFS_RDONLY	},
	{ "VFS_NOMNTTAB",	VFS_NOMNTTAB,		VFS_NOMNTTAB	},
	{ "VFS_NOSUID",		VFS_NOSETUID,		VFS_NOSETUID	},
	{ "VFS_REMOUNT",	VFS_REMOUNT,		VFS_REMOUNT	},
	{ "VFS_NOTRUNC",	VFS_NOTRUNC,		VFS_NOTRUNC	},
	{ "VFS_UNLINKABLE",	VFS_UNLINKABLE,		VFS_UNLINKABLE	},
	{ "VFS_PXFS",		VFS_PXFS,		VFS_PXFS	},
	{ "VFS_UNMOUNTED",	VFS_UNMOUNTED,		VFS_UNMOUNTED	},
	{ "VFS_NBMAND",		VFS_NBMAND,		VFS_NBMAND	},
	{ "VFS_XATTR",		VFS_XATTR,		VFS_XATTR	},
	{ NULL, 0, 0 }
};

static const mdb_bitmask_t bm_mi[] = {
	{ "MI_HARD",		MI_HARD,		MI_HARD		},
	{ "MI_PRINTED",		MI_PRINTED,		MI_PRINTED	},
	{ "MI_INT",		MI_INT,			MI_INT		},
	{ "MI_DOWN",		MI_DOWN,		MI_DOWN		},
	{ "MI_NOAC",		MI_NOAC,		MI_NOAC		},
	{ "MI_NOCTO",		MI_NOCTO,		MI_NOCTO	},
	{ "MI_DYNAMIC",		MI_DYNAMIC,		MI_DYNAMIC	},
	{ "MI_LLOCK",		MI_LLOCK,		MI_LLOCK	},
	{ "MI_GRPID",		MI_GRPID,		MI_GRPID	},
	{ "MI_RPCTIMESYNC",	MI_RPCTIMESYNC,		MI_RPCTIMESYNC	},
	{ "MI_LINK",		MI_LINK,		MI_LINK		},
	{ "MI_SYMLINK",		MI_SYMLINK,		MI_SYMLINK	},
	{ "MI_READDIRONLY",	MI_READDIRONLY,		MI_READDIRONLY	},
	{ "MI_ACL",		MI_ACL,			MI_ACL		},
	{ "MI_BINDINPROG",	MI_BINDINPROG,		MI_BINDINPROG	},
	{ "MI_LOOPBACK",	MI_LOOPBACK,		MI_LOOPBACK	},
	{ "MI_SEMISOFT",	MI_SEMISOFT,		MI_SEMISOFT	},
	{ "MI_NOPRINT",		MI_NOPRINT,		MI_NOPRINT	},
	{ "MI_DIRECTIO",	MI_DIRECTIO,		MI_DIRECTIO	},
	{ "MI_EXTATTR",		MI_EXTATTR,		MI_EXTATTR	},
	{ "MI_ASYNC_MGR_STOP",	MI_ASYNC_MGR_STOP,	MI_ASYNC_MGR_STOP },
	{ "MI_DEAD",		MI_DEAD,		MI_DEAD	},
	{ NULL, 0, 0 }
};

static const mdb_bitmask_t bm_mi4[] = {
	{ "MI4_HARD",		MI4_HARD,		MI4_HARD	},
	{ "MI4_PRINTED",	MI4_PRINTED,		MI4_PRINTED	},
	{ "MI4_INT",		MI4_INT,		MI4_INT		},
	{ "MI4_DOWN",		MI4_DOWN,		MI4_DOWN	},
	{ "MI4_NOAC",		MI4_NOAC,		MI4_NOAC	},
	{ "MI4_NOCTO",		MI4_NOCTO,		MI4_NOCTO	},
	{ "MI4_LLOCK",		MI4_LLOCK,		MI4_LLOCK	},
	{ "MI4_GRPID",		MI4_GRPID,		MI4_GRPID	},
	{ "MI4_SHUTDOWN",	MI4_SHUTDOWN,		MI4_SHUTDOWN	},
	{ "MI4_LINK",		MI4_LINK,		MI4_LINK	},
	{ "MI4_SYMLINK",	MI4_SYMLINK,		MI4_SYMLINK	},
	/* MI4_READDIR used to live here */
	{ "MI4_ACL",		MI4_ACL,		MI4_ACL		},
	/* MI4_BINDINPROG replaced by MI4_MIRRORMOUNT */
	{ "MI4_MIRRORMOUNT",	MI4_MIRRORMOUNT,	MI4_MIRRORMOUNT	},
	/* MI4_LOOPBACK used to live here */
	/* MI4_SEMISOFT used to live here */
	{ "MI4_NOPRINT",	MI4_NOPRINT,		MI4_NOPRINT	},
	{ "MI4_DIRECTIO",	MI4_DIRECTIO,		MI4_DIRECTIO	},
	/* MI4_EXTATTR used to live here */
	{ "MI4_RECOV_ACTIV",	MI4_RECOV_ACTIV,	MI4_RECOV_ACTIV	},
	{ "MI4_REMOVE_ON_LAST_CLOSE", MI4_REMOVE_ON_LAST_CLOSE,
					MI4_REMOVE_ON_LAST_CLOSE	},
	{ "MI4_RECOV_FAIL",	MI4_RECOV_FAIL,		MI4_RECOV_FAIL	},
	{ "MI4_PUBLIC",		MI4_PUBLIC,		MI4_PUBLIC	},
	{ "MI4_MOUNTING",	MI4_MOUNTING,		MI4_MOUNTING	},
	{ "MI4_POSIX_LOCK",	MI4_POSIX_LOCK,		MI4_POSIX_LOCK	},
	{ "MI4_LOCK_DEBUG",	MI4_LOCK_DEBUG,		MI4_LOCK_DEBUG	},
	{ "MI4_DEAD",		MI4_DEAD,		MI4_DEAD	},
	{ "MI4_INACTIVE_IDLE",	MI4_INACTIVE_IDLE,	MI4_INACTIVE_IDLE },
	{ "MI4_BADOWNER_DEBUG",	MI4_BADOWNER_DEBUG,	MI4_BADOWNER_DEBUG },
	{ "MI4_ASYNC_MGR_STOP",	MI4_ASYNC_MGR_STOP,	MI4_ASYNC_MGR_STOP },
	{ "MI4_TIMEDOUT",	MI4_TIMEDOUT,		MI4_TIMEDOUT	},
	{ NULL, 0, 0  }
};

static const mdb_bitmask_t bm_mi4_r[] = {
	{ "MI4R_NEED_CLIENTID",	MI4R_NEED_CLIENTID,	MI4R_NEED_CLIENTID },
	{ "MI4R_REOPEN_FILES",	MI4R_REOPEN_FILES,	MI4R_REOPEN_FILES },
	{ "MI4R_NEED_SECINFO",	MI4R_NEED_SECINFO,	MI4R_NEED_SECINFO },
	{ "MI4R_NEED_NEW_SERVER", MI4R_NEED_NEW_SERVER,	MI4R_NEED_NEW_SERVER },
	{ "MI4R_REMAP_FILES",	MI4R_REMAP_FILES,	MI4R_REMAP_FILES },
	{ "MI4R_SRV_REBOOT",	MI4R_SRV_REBOOT,	MI4R_SRV_REBOOT	},
	{ "MI4R_LOST_STATE",	MI4R_LOST_STATE,	MI4R_LOST_STATE },
	{ "MI4R_BAD_SEQID",	MI4R_BAD_SEQID,		MI4R_BAD_SEQID },
	{ NULL, 0, 0  }
};

#define	MNTLEN 41

static void
nfs_mntopts(vfs_t *vfs)
{
	int count = vfs->vfs_mntopts.mo_count;
	uintptr_t mntoptp = (uintptr_t)vfs->vfs_mntopts.mo_list;
	mntopt_t mntopt;

	char moname[MNTLEN] = "";
	char moarg[MNTLEN] = "";

	while (count-- > 0) {

		if (mdb_vread(&mntopt, sizeof (mntopt), mntoptp) == -1) {
			mdb_warn("couldn't read mntopt at %-?p\n",
			    mntoptp);
			return;
		}

		if (mntopt.mo_name == 0)
			break;

		if (mdb_readstr(moname, MNTLEN,
		    (uintptr_t)mntopt.mo_name) == -1) {
			mdb_warn("couldn't read mo_name at %-?p\n",
			    mntopt.mo_name);
			return;
		}

		if (mntopt.mo_arg != 0) {
			if (mdb_readstr(moarg, MNTLEN,
			    (uintptr_t)mntopt.mo_arg) == -1) {
				mdb_warn("couldn't read mo_arg at %-?p\n",
				    mntopt.mo_arg);
				return;
			}
			mdb_printf("%s(%s)", moname, moarg);
		} else {
			mdb_printf("%s", moname);
		}

		if (count) {
			mdb_printf(", ");
			mntoptp += sizeof (mntopt);
		}
	}
	mdb_printf("\n");
}

void
pr_vfs_mntpnts(vfs_t *vfs)
{
	int	len;
	char 	buf[50] = {0};

	/*
	 * Get the mount point string.
	 */
	if ((len = mdb_readstr(buf, 50,
	    (uintptr_t)vfs->vfs_mntpt->rs_string)) <= 0) {
		mdb_printf("   mount point: %-?p\n",
		    (uintptr_t)vfs->vfs_mntpt->rs_string);
	} else {
		if (len == 50)
			strcpy(&buf[len - 4], "...");
		mdb_printf("   mount point: %s\n", buf);
	}

	/*
	 * Now the resource.
	 */
	if ((len = mdb_readstr(buf, 50,
	    (uintptr_t)vfs->vfs_resource->rs_string)) <= 0) {
		mdb_printf("    mount from: %-?p\n",
		    (uintptr_t)vfs->vfs_resource->rs_string);
	} else {
		if (len == 50)
			strcpy(&buf[len - 4], "...");
		mdb_printf("    mount from: %s\n", buf);
	}
}

/*
 * Passed in addr must be address of mntinfo4_t, this
 * function will return address of mi_msg_head.
 */
uintptr_t
nfs4_get_mimsg(uintptr_t addr)
{
	mdb_ctf_id_t	mnti4_id;
	ulong_t		msghead_off;

	/*
	 * Using CTF get the offset of mi_msg_head. If we are
	 * unable to get ctf offsetof we'll default to the compile
	 * time offset and hope it's okay.
	 */
	if ((mdb_ctf_lookup_by_name("mntinfo4_t", &mnti4_id) == 0) &&
	    (mdb_ctf_offsetof(mnti4_id, "mi_msg_head", &msghead_off) == 0) &&
	    (msghead_off % (sizeof (uintptr_t) * NBBY) == 0)) {
		msghead_off /= NBBY;
	} else {
		msghead_off = offsetof(mntinfo4_t, mi_msg_list);
	}

	return (addr + msghead_off);
}

int
nfs_vfs(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	/* these are loaded with vfs_op addresses at startup */
	extern uintptr_t vfs_op2, vfs_op3, vfs_op4;

	vfs_t vfs;
	uintptr_t mntinfo_ptr;

	mntinfo_t mnt;
	mntinfo4_t mnt4;

	int opts = 0;

	/*
	 * If no address specified walk the vfs table looking
	 * for NFS vfs entries and call this dcmd.
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		mdb_arg_t *argvv;

		argvv = mdb_alloc((argc)*sizeof (mdb_arg_t), UM_SLEEP);
		bcopy(argv, argvv, argc*sizeof (mdb_arg_t));
		status = mdb_walk_dcmd("nfs_vfs", "nfs_vfs",
		    argc, argvv);
		mdb_free(argvv, (argc)*sizeof (mdb_arg_t));
		return (status);
	}

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;

	NFS_OBJ_FETCH(addr, vfs_t, &vfs, DCMD_ERR);

	mdb_printf("vfs_t->%-?p, ", addr);

	if (vfs.vfs_flag & VFS_UNMOUNTED) {
		mdb_warn("VFS is being unmounted\n");
		return (DCMD_OK);
	}

	mdb_printf("data = %-?p, ", vfs.vfs_data);
	mdb_printf("ops = %-?p\n", vfs.vfs_op);

	pr_vfs_mntpnts(&vfs);

	/* get out here if not verbose */
	if (!(opts & NFS_MDB_OPT_VERBOSE)) {
		return (DCMD_OK);
	}

	mdb_printf("      vfs_flags: %b\n", vfs.vfs_flag, bm_vfs);
	mdb_printf("     mount-time: %Y\n", (time_t)vfs.vfs_mtime);
	mdb_printf("     mount opts: ");
	nfs_mntopts(&vfs);

	mntinfo_ptr = (uintptr_t)vfs.vfs_data;

	/* pick the correct mntinfo dumper */
	if (vfs.vfs_op == (vfsops_t *)vfs_op4) {
		NFS_OBJ_FETCH(mntinfo_ptr, mntinfo4_t, &mnt4, DCMD_ERR);
		return (mntinfo4_info(mntinfo_ptr, &mnt4, opts));
	}

	if (vfs.vfs_op != (vfsops_t *)vfs_op2 &&
	    vfs.vfs_op != (vfsops_t *)vfs_op3) {
		mdb_warn("VFS structure is not an NFS filesystem!\n");
		return (DCMD_ERR);
	}

	NFS_OBJ_FETCH(mntinfo_ptr, mntinfo_t, &mnt, DCMD_ERR);

	return (mntinfo_info(&mnt, opts));
}

int
nfs_mntinfo(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mntinfo_t mnt;

	int opts = NFS_MDB_OPT_SHWMP;

	/*
	 * If no address specified walk the vfs table looking
	 * for NFS vfs entries and call this dcmd with the
	 * mntinfo pointer
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		mdb_arg_t *argvv;

		argvv = mdb_alloc((argc)*sizeof (mdb_arg_t), UM_SLEEP);
		bcopy(argv, argvv, argc*sizeof (mdb_arg_t));
		status = mdb_walk_dcmd("nfs_mnt", "nfs_mntinfo",
		    argc, argvv);
		mdb_free(argvv, (argc)*sizeof (mdb_arg_t));
		return (status);
	}

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	/*
	 * pick up global mdb options and make sure we
	 * print out the mount point infomation
	 */
	opts |= nfs4_mdb_opt;

	NFS_OBJ_FETCH(addr, mntinfo_t, &mnt, DCMD_ERR);

	return (mntinfo_info(&mnt, opts));
}

static int
mntinfo_info(mntinfo_t *mnt, int flags)
{
	int i;
	vfs_t vfs;

	mdb_printf("NFS Version: %d\n", mnt->mi_vers);
	mdb_printf("   mi_flags: %b\n", mnt->mi_flags, bm_mi);

	if (flags & NFS_MDB_OPT_SHWMP) {
		NFS_OBJ_FETCH((uintptr_t)mnt->mi_vfsp, vfs_t, &vfs, DCMD_ERR);
		pr_vfs_mntpnts(&vfs);
	}

	/* get out here if not verbose option */
	if (!(flags & NFS_MDB_OPT_VERBOSE)) {
		return (DCMD_OK);
	}

	mdb_printf("mi_zone=%p\n", (uintptr_t)mnt->mi_zone);
	mdb_printf("mi_curread=%d, mi_curwrite=%d, mi_retrans=%d, "
	    "mi_timeo=%d\n",
	    mnt->mi_curread, mnt->mi_curwrite,
	    mnt->mi_retrans, mnt->mi_timeo);
	mdb_printf("mi_acregmin=%lu, mi_acregmax=%lu, "
	    "mi_acdirmin=%lu, mi_acdirmax=%lu\n",
	    mnt->mi_acregmin, mnt->mi_acregmax,
	    mnt->mi_acdirmin, mnt->mi_acdirmax);

	mdb_printf("Server list: %-?p\n", (uintptr_t)mnt->mi_servers);
	mdb_pwalk_dcmd("nfs_serv", "nfs_servinfo", 0, NULL,
	    (uintptr_t)mnt->mi_servers);
	mdb_printf("\nCurrent Server: %-?p ",
	    (uintptr_t)mnt->mi_curr_serv);
	mdb_call_dcmd("nfs_servinfo", (uintptr_t)mnt->mi_curr_serv,
	    DCMD_ADDRSPEC, 0, NULL);
	mdb_printf("\n");

	mdb_printf("Total: Server Non-responses = %u ", mnt->mi_noresponse);
	mdb_printf("Server Failovers = %u\n", mnt->mi_failover);

	(void) nfs_io_stats(mnt->mi_io_kstats);

	mdb_printf("Async Request queue: "
	    "max_threads = %u active_threads = %u\n",
	    mnt->mi_max_threads, mnt->mi_threads[0]);
	mdb_printf("Async reserved page operation only active threads = %u\n",
	    mnt->mi_threads[1]);
	mdb_inc_indent(5);
	mdb_printf("number requests queued:\n");
	for (i = 0; i < NFS_ASYNC_TYPES; i++) {
		int count = 0;
		const char *str_async[NFS_ASYNC_TYPES] = {
			"   PUTPAGE",
			"    PAGEIO",
			"    COMMIT",
			"READ_AHEAD",
			"   READDIR",
			"  INACTIVE",
		};
		if (mdb_pwalk("nfs_async", async_counter, &count,
		    (uintptr_t)mnt->mi_async_reqs[i])) {
			mdb_warn("Walking async requests failed\n");
			return (DCMD_ERR);
		}
		mdb_printf("%s = %d ", str_async[i], count);
	}
	mdb_dec_indent(5);

	if (mnt->mi_printftime)
		mdb_printf("\nLast error report time = %Y\n",
		    (time_t)mnt->mi_printftime);
	mdb_printf("\n");
	return (DCMD_OK);
}

int
nfs4_mntinfo(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mntinfo4_t mnt4;
	int opts = NFS_MDB_OPT_SHWMP;

	/*
	 * If no address specified walk the vfs table looking
	 * for NFSv4 vfs entries and call this dcmd with the
	 * mntinfo4_t pointer
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		mdb_arg_t *argvv;

		argvv = mdb_alloc((argc)*sizeof (mdb_arg_t), UM_SLEEP);
		bcopy(argv, argvv, argc*sizeof (mdb_arg_t));
		status = mdb_walk_dcmd("nfs4_mnt", "nfs4_mntinfo",
		    argc, argvv);
		mdb_free(argvv, (argc)*sizeof (mdb_arg_t));
		return (status);
	}

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, NFS_MDB_OPT_VERBOSE, &opts,
	    'm', MDB_OPT_SETBITS, NFS_MDB_OPT_SHWMMSG, &opts,
	    NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;

	NFS_OBJ_FETCH(addr, mntinfo4_t, &mnt4, DCMD_ERR);

	return (mntinfo4_info(addr, &mnt4, opts));
}


static int
mntinfo4_info(uintptr_t addr, mntinfo4_t *mnt, int flags)
{
	int i;
	vfs_t vfs;

	mdb_printf("+--------------------------------------+\n");
	mdb_printf("    mntinfo4_t: 0x%-?p\n", addr);
	mdb_printf("   NFS Version: %d\n", mnt->mi_vers);
	mdb_printf("      mi_flags: %b\n", mnt->mi_flags, bm_mi4);
	mdb_printf("      mi_error: %d\n", mnt->mi_error);
	mdb_printf(" mi_open_files: %d\n", mnt->mi_open_files);
	mdb_printf("  mi_msg_count: %d\n", mnt->mi_msg_count);
	mdb_printf(" mi_recovflags: %b\n", mnt->mi_recovflags, bm_mi4_r);
	mdb_printf("mi_recovthread: 0x%-?p\n", mnt->mi_recovthread);
	mdb_printf("mi_in_recovery: %d\n", mnt->mi_in_recovery);

	/* if we need to show the mount point go do it! */
	if (flags & NFS_MDB_OPT_SHWMP) {
		NFS_OBJ_FETCH((uintptr_t)mnt->mi_vfsp, vfs_t, &vfs, DCMD_ERR);
		pr_vfs_mntpnts(&vfs);
	}

	/* get out here if not verbose option */
	if (! (flags & NFS_MDB_OPT_VERBOSE)) {
		/* if the user specified -m dump out the mount info msgs */
		if (flags & NFS_MDB_OPT_SHWMMSG) {
			mdb_printf(
			    "=============================================\n");
			mdb_printf("Messages queued:\n");
			if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_diag_walk,
			    NULL, nfs4_get_mimsg(addr)) == -1) {
				mdb_warn("Failed to walk mi_msg_list list\n");
			}
			mdb_printf(
			    "=============================================\n");
		}
		return (DCMD_OK);
	}

	mdb_printf("mi_zone=%p\n", (uintptr_t)mnt->mi_zone);
	mdb_printf("mi_curread=%d, mi_curwrite=%d, mi_retrans=%d,"
	    " mi_timeo=%d\n",
	    mnt->mi_curread, mnt->mi_curwrite,
	    mnt->mi_retrans, mnt->mi_timeo);
	mdb_printf("mi_acregmin=%lu, mi_acregmax=%lu,"
	    "mi_acdirmin=%lu, mi_acdirmax=%lu\n", mnt->mi_acregmin,
	    mnt->mi_acregmax, mnt->mi_acdirmin, mnt->mi_acdirmax);

	mdb_printf(" Server list: %-?p\n", (uintptr_t)mnt->mi_servers);
	mdb_pwalk_dcmd("nfs_serv4", "nfs4_servinfo", 0, NULL,
	    (uintptr_t)mnt->mi_servers);
	mdb_printf("\n Current Server: %-?p ",
	    (uintptr_t)mnt->mi_curr_serv);
	mdb_call_dcmd("nfs4_servinfo", (uintptr_t)mnt->mi_curr_serv,
	    DCMD_ADDRSPEC, 0, NULL);
	mdb_printf("\n");

	mdb_printf("  Total: Server Non-responses=%u; "
	    "Server Failovers=%u\n",
	    mnt->mi_noresponse, mnt->mi_failover);

	(void) nfs_io_stats(mnt->mi_io_kstats);

	mdb_printf(" Async Request queue:\n");
	mdb_inc_indent(5);
	mdb_printf("max threads = %u active threads = %u\n",
	    mnt->mi_max_threads, mnt->mi_threads[0]);
	mdb_printf("Async reserved page operation only active threads = %u\n",
	    mnt->mi_threads[1]);
	mdb_printf("number requests queued:\n");

	for (i = 0; i < NFS4_ASYNC_TYPES; i++) {
		int count = 0;
		const char *str_async[NFS4_ASYNC_TYPES] = {
			"   PUTPAGE",
			"    PAGEIO",
			"    COMMIT",
			"READ_AHEAD",
			"   READDIR",
			"  INACTIVE"
		};
		if (mdb_pwalk("nfs4_async", async_counter, &count,
		    (uintptr_t)mnt->mi_async_reqs[i])) {
			mdb_warn("Walking async requests failed\n");
			return (DCMD_ERR);
		}
		mdb_printf("%s = %d ", str_async[i], count);
	}
	mdb_dec_indent(5);
	if (mnt->mi_printftime)
		mdb_printf("\nLast error report time = %Y\n",
		    (time_t)mnt->mi_printftime);
	mdb_printf("\n");
	mdb_printf("=============================================\n");
	mdb_printf("Messages queued:\n");
	if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_diag_walk,
	    NULL, nfs4_get_mimsg(addr)) == -1) {
		mdb_warn("Failed to walk mi_msg_list list\n");
	}
	mdb_printf("=============================================\n");
	return (DCMD_OK);
}

static int
nfs_io_stats(kstat_t *kp)
{
	kstat_io_t io;
	kstat_t ks;

	NFS_OBJ_FETCH((uintptr_t)kp, kstat_t, &ks, DCMD_ERR);

	NFS_OBJ_FETCH((uintptr_t)ks.ks_data, kstat_io_t, &io, DCMD_ERR);

	mdb_printf("IO statistics for this mount \n");
	mdb_printf("	No. of bytes read         %7d\n", io.nread);
	mdb_printf("	No. of read operations    %7d\n", io.reads);
	mdb_printf("	No. of bytes written      %7d\n", io.nwritten);
	mdb_printf("	No. of write operations   %7d\n", io.writes);

	return (0);
}

void
nfs_servinfo_help(void)
{
	mdb_printf("<servinfo_t addr>::nfs_servinfo [-v]\n");
}

void
nfs4_servinfo_help(void)
{
	mdb_printf("<servinfo4_t addr>::nfs_servinfo4 [-v]\n");
}

/*ARGSUSED*/
int
nfs4_servinfo(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct knetconfig kn;
	servinfo4_t sr;
	char *buf;
	int opts = 0;

	/* must specify an address */
	if (! (flags & DCMD_ADDRSPEC)) {
		return (DCMD_USAGE);
	}

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;

	NFS_OBJ_FETCH(addr, servinfo4_t, &sr, DCMD_ERR);

	if (!(opts & NFS_MDB_OPT_VERBOSE)) {
		mdb_inc_indent(5);
		nfs_print_netbuf(&sr.sv_addr);
		mdb_printf("\n");
		mdb_dec_indent(5);
		return (DCMD_OK);
	}

	/* Verbose output from here on.. */
	mdb_printf("secdata ptr = %-?p\n", sr.sv_secdata);

	NFS_OBJ_FETCH((uintptr_t)sr.sv_knconf, struct knetconfig,
	    &kn, DCMD_ERR);

	mdb_printf("address = ");
	nfs_print_netconfig(&kn);
	nfs_print_netbuf(&sr.sv_addr);
	mdb_printf("\n");

	buf = mdb_alloc(sr.sv_hostnamelen, UM_SLEEP);
	if (mdb_vread(buf, sr.sv_hostnamelen,
	    (uintptr_t)sr.sv_hostname) == -1) {
		mdb_warn("failed to read servinfo hostname (%d bytes"
		    " at %-?p\n", sr.sv_hostnamelen, sr.sv_hostname);
		mdb_free(buf, sr.sv_hostnamelen);
		return (DCMD_ERR);
	}

	mdb_printf("hostname = %s\n", buf);
	mdb_free(buf, sr.sv_hostnamelen);

	mdb_printf("server filehandle = ");
	nfs_print_hex(sr.sv_fhandle.fh_buf, sr.sv_fhandle.fh_len);

	mdb_printf("\nparent dir filehandle = ");
	nfs_print_hex(sr.sv_pfhandle.fh_buf, sr.sv_pfhandle.fh_len);

	mdb_printf("\n");

	return (DCMD_OK);
}

/*ARGSUSED*/
int
nfs_servinfo(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct knetconfig kn;
	servinfo_t sr;
	char *buf;
	int opts = 0;

	/* must specify an address */
	if (! (flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;

	NFS_OBJ_FETCH(addr, servinfo_t, &sr, DCMD_ERR);

	if (! (opts & NFS_MDB_OPT_VERBOSE)) {
		mdb_inc_indent(5);
		nfs_print_netbuf(&sr.sv_addr);
		mdb_printf("\n");
		mdb_dec_indent(5);
		return (DCMD_OK);
	}

	/* Verbose output from here on.. */
	mdb_printf("secdata ptr = %-?p\n", sr.sv_secdata);

	NFS_OBJ_FETCH((uintptr_t)sr.sv_knconf, struct knetconfig,
	    &kn, DCMD_ERR);

	mdb_printf("address = ");
	nfs_print_netconfig(&kn);
	nfs_print_netbuf(&sr.sv_addr);
	mdb_printf("\n");

	buf = mdb_alloc(sr.sv_hostnamelen, UM_SLEEP);
	if (mdb_vread(buf, sr.sv_hostnamelen,
	    (uintptr_t)sr.sv_hostname) == -1) {
		mdb_warn("failed to read servinfo hostname (%d bytes)"
		    " at %-?p\n", sr.sv_hostnamelen, sr.sv_hostname);
		mdb_free(buf, sr.sv_hostnamelen);
		return (DCMD_ERR);
	}
	mdb_printf("hostname = %s\n", buf);
	mdb_free(buf, sr.sv_hostnamelen);

	mdb_printf("filehandle = ");
	nfs_print_hex(sr.sv_fhandle.fh_buf, sr.sv_fhandle.fh_len);
	mdb_printf("\n");

	return (DCMD_OK);
}

/*ARGSUSED*/
int
nfs_serv_walk_init(mdb_walk_state_t *wsp)
{
	return (WALK_NEXT);
}

int
nfs_serv_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	servinfo_t serv;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	NFS_OBJ_FETCH(wsp->walk_addr, servinfo_t, &serv, WALK_ERR);

	status = wsp->walk_callback(wsp->walk_addr, &serv,
	    wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)serv.sv_next;
	return (status);
}

/*ARGSUSED*/
int
nfs4_serv_walk_init(mdb_walk_state_t *wsp)
{
	return (WALK_NEXT);
}

int
nfs4_serv_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	servinfo4_t serv;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	NFS_OBJ_FETCH(wsp->walk_addr, servinfo4_t, &serv, WALK_ERR);

	status = wsp->walk_callback(wsp->walk_addr, &serv,
	    wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)serv.sv_next;
	return (status);
}

/*ARGSUSED*/
int
nfs_async_walk_init(mdb_walk_state_t *wsp)
{
	return (WALK_NEXT);
}

int
nfs_async_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	struct nfs_async_reqs nar;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	NFS_OBJ_FETCH(wsp->walk_addr, struct nfs_async_reqs, &nar, WALK_ERR);

	status = wsp->walk_callback(wsp->walk_addr, &nar, wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)nar.a_next;
	return (status);
}

int
nfs4_async_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	struct nfs4_async_reqs nar;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	NFS_OBJ_FETCH(wsp->walk_addr, struct nfs4_async_reqs, &nar, WALK_ERR);

	status = wsp->walk_callback(wsp->walk_addr, &nar, wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)nar.a_next;
	return (status);
}

/*ARGSUSED*/
static int
async_counter(uintptr_t addr, const void *data, void *tr)
{
	int *i = ((int *)tr);
	(*i)++;
	return (WALK_NEXT);
}

int
nfs_print_netbuf(struct netbuf *addr)
{
	void *s;

	if (addr == NULL || addr->len == 0) {
		/* quietly return. no warning. */
		return (-1);
	}
	s = mdb_alloc(addr->len, UM_SLEEP);

	if (mdb_vread(s, addr->len, (uintptr_t)addr->buf) == -1) {
		mdb_warn("failed to read netbuf address %-?p\n", addr->buf);
		mdb_free(s, addr->len);
		return (-1);
	}
	nfs_print_netbuf_buf(s, addr->len);
	mdb_free(s, addr->len);
	return (0);
}


void
nfs_print_netbuf_buf(void *s, int len)
{
	void *raw;

	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	in_port_t port;

	switch (((struct sockaddr_in *)s)->sin_family) {

	case AF_INET:
		sa = (struct sockaddr_in *)s;
		mdb_nhconvert(&port, &sa->sin_port, sizeof (port));
		mdb_printf("%I:%u", sa->sin_addr.s_addr, port);
		break;

	/* XXX this is the wrong format for IN6 addresses */
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)s;
		mdb_nhconvert(&port, &sa6->sin6_port, sizeof (port));
		mdb_printf("%I.%I.%I.%I:%u",
		    sa6->sin6_addr.s6_addr32[0],
		    sa6->sin6_addr.s6_addr32[1],
		    sa6->sin6_addr.s6_addr32[2],
		    sa6->sin6_addr.s6_addr32[3],
		    port);
		break;

	default:
		/* print raw buffer */
		raw = mdb_alloc(len + 1, UM_SLEEP);
		mdb_snprintf(raw, len + 1, "%*s", len, s);
		mdb_printf("%s", raw);
		mdb_free(raw, len + 1);
	}
}
