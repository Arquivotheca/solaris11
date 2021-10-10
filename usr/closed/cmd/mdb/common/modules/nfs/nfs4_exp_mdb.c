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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/mdb_modapi.h>
#include <sys/pkp_hash.h>
#include <nfs/nfs_srv_inst_impl.h>
#include <nfs/nfs4.h>
#include <nfs/export.h>
#include <mdb/mdb_ks.h>
#include <mdb/mdb_ctf.h>
#include "nfs_mdb.h"
#include "nfs4_mdb.h"

/*
 * flavor/sec types are specified in nfssec.conf, and the mapping
 * of sec type strings to sec service numbers happens in user
 * space in libnsl.  If there were handy defines for each sec type
 * they'd be used here.  Instead, they're hard coded to the same
 * values listed in /etc/nfssec.conf
 */
#define	SELFMASK 0xffffffffffffffffuLL
static const mdb_bitmask_t bm_flav[] = {
	{ "none ",	SELFMASK,	0},
	{ "sys  ",	SELFMASK,	1},
	{ "dh   ",	SELFMASK,	3},
	{ "krb5 ",	SELFMASK,	390003},
	{ "krb5i",	SELFMASK,	390004},
	{ "krb5p",	SELFMASK,	390005},
	{ NULL,		0,		0}
};

static const mdb_bitmask_t bm_secflg[] = {
	{"M_RO",	M_RO,			M_RO},
	{"M_ROL",	M_ROL,			M_ROL},
	{"M_RW",	M_RW,			M_RW},
	{"M_RWL",	M_RWL,			M_RWL},
	{"M_ROOT",	M_ROOT,			M_ROOT},
	{"M_EXP",	M_4SEC_EXPORTED,	M_4SEC_EXPORTED},
	{ NULL,		0,		0}
};

static const mdb_bitmask_t bm_expflg[] = {
	{"EX_NOSUID",		EX_NOSUID,	EX_NOSUID},
	{"EX_ACLOK",		EX_ACLOK,	EX_ACLOK},
	{"EX_PUBLIC",		EX_PUBLIC,	EX_PUBLIC},
	{"EX_NOSUB",		EX_NOSUB,	EX_NOSUB},
	{"EX_INDEX",		EX_INDEX,	EX_INDEX},
	{"EX_LOG",		EX_LOG,		EX_LOG},
	{"EX_LOG_ALLOPS",	EX_LOG_ALLOPS,	EX_LOG_ALLOPS},
	{"EX_PSEUDO",		EX_PSEUDO,	EX_PSEUDO},
	{"EX_ROOT",		EX_ROOT,	EX_ROOT},
	{ NULL,		0,		0}
};

typedef struct exptable_walk_data {
	uintptr_t	*ew_table;
	int		ew_table_sz;
	int		ew_ndx;
	uintptr_t	ew_exi;
	size_t		ew_offset;
} exptable_walk_data_t;

typedef struct expvis_walk_data {
	uintptr_t	vw_next;
} expvis_walk_data_t;

static void dump_exportinfo(uintptr_t, struct exportinfo *, void *);
static void dump_visible(uintptr_t, struct exp_visible *, void *);
static void nfs4_dump_secinfo(uintptr_t, int);
static int  expinfo_dcmd_guts(uintptr_t, struct exportinfo *, void *);

int
exi_fid_walk_init(mdb_walk_state_t *wsp)
{
	exptable_walk_data_t *ew;
	uintptr_t exptable;
	rfs_inst_t *rip;

	rip = get_rfs_inst();
	if (rip == NULL)
		return (WALK_ERR);
	exptable = (uintptr_t)rip->ri_nm.rn_exptable;

	ew = mdb_zalloc(sizeof (exptable_walk_data_t), UM_SLEEP);
	ew->ew_table_sz = EXPTABLESIZE;
	ew->ew_table = mdb_alloc(ew->ew_table_sz * sizeof (uintptr_t),
	    UM_SLEEP);
	ew->ew_ndx = 0;
	ew->ew_exi = NULL;
	ew->ew_offset = offsetof(exportinfo_t, fid_hash.next);

	if (mdb_vread(ew->ew_table, ew->ew_table_sz * sizeof (uintptr_t),
	    exptable) == -1) {
		mdb_warn("failed to read exptable\n");
		return (WALK_ERR);
	}

	wsp->walk_data = ew;
	return (WALK_NEXT);
}

int
exi_path_walk_init(mdb_walk_state_t *wsp)
{
	exptable_walk_data_t *ew;
	uintptr_t exptable_path;
	rfs_inst_t *rip;

	rip = get_rfs_inst();
	if (rip == NULL)
		return (WALK_ERR);
	exptable_path = (uintptr_t)rip->ri_nm.rn_exptable_path_hash;

	ew = mdb_zalloc(sizeof (exptable_walk_data_t), UM_SLEEP);
	ew->ew_table_sz = PKP_HASH_SIZE;
	ew->ew_table = mdb_alloc(ew->ew_table_sz * sizeof (uintptr_t),
	    UM_SLEEP);
	ew->ew_ndx = 0;
	ew->ew_exi = NULL;
	ew->ew_offset = offsetof(exportinfo_t, path_hash.next);

	if (mdb_vread(ew->ew_table, ew->ew_table_sz * sizeof (uintptr_t),
	    exptable_path) == -1) {
		mdb_warn("failed to read exptable_path_hash\n");
		return (WALK_ERR);
	}

	wsp->walk_data = ew;
	return (WALK_NEXT);
}

int
exi_walk_step(mdb_walk_state_t *wsp)
{
	exptable_walk_data_t *ew = wsp->walk_data;
	struct exportinfo exi;
	uintptr_t addr;

again:
	while (ew->ew_exi == NULL && ew->ew_ndx < ew->ew_table_sz)
		ew->ew_exi = ew->ew_table[ew->ew_ndx++];

	if (ew->ew_exi == NULL)
		return (WALK_DONE);

	if (mdb_vread(&exi, sizeof (exi), addr = ew->ew_exi) == -1) {
		mdb_warn("failed to read exportinfo at %p\n", ew->ew_exi);
		ew->ew_exi = NULL;
		goto again;
	}
	/* LINTED pointer cast may result in improper alignment */
	ew->ew_exi = *((uintptr_t *)((char *)&exi + ew->ew_offset));
	wsp->walk_callback(addr, &exi, wsp->walk_cbdata);

	return (WALK_NEXT);
}

void
exi_walk_fini(mdb_walk_state_t *wsp)
{
	exptable_walk_data_t *ew = wsp->walk_data;

	mdb_free(ew->ew_table, ew->ew_table_sz * sizeof (uintptr_t));
	mdb_free(ew, sizeof (exptable_walk_data_t));
}

int
vis_walk_init(mdb_walk_state_t *wsp)
{
	expvis_walk_data_t *vw;

	if (wsp->walk_addr == NULL) {
		mdb_warn("no address provided\n");
		return (WALK_ERR);
	}

	vw = mdb_zalloc(sizeof (expvis_walk_data_t), UM_SLEEP);

	vw->vw_next = wsp->walk_addr;
	wsp->walk_data = vw;
	return (WALK_NEXT);
}

int
vis_walk_step(mdb_walk_state_t *wsp)
{
	expvis_walk_data_t *vw = wsp->walk_data;
	struct exp_visible vis;
	uintptr_t addr;

	if (vw->vw_next == NULL)
		return (WALK_DONE);

	addr = vw->vw_next;
	if (mdb_vread(&vis, sizeof (vis), addr) == -1) {
		mdb_warn("failed to read visible struct at %p\n", addr);
		return (WALK_DONE);
	}

	vw->vw_next = (uintptr_t)vis.vis_next;
	wsp->walk_callback(addr, &vis, wsp->walk_cbdata);
	return (WALK_NEXT);
}

void
vis_walk_fini(mdb_walk_state_t *wsp)
{
	expvis_walk_data_t *vw = wsp->walk_data;

	mdb_free(vw, sizeof (expvis_walk_data_t));
}


/*ARGSUSED*/
int
nfs4_exp_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct exportinfo exi;

	if (! (flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_vread(&exi, sizeof (exi), addr) != sizeof (exi)) {
		mdb_warn("error reading exportinfo at %p\n", addr);
		return (DCMD_ERR);
	}

	return (expinfo_dcmd_guts(addr, &exi, NULL));
}

/*ARGSUSED*/
static int
expinfo_dcmd_guts(uintptr_t addr, struct exportinfo *exi, void *arg)
{
	dump_exportinfo(addr, exi, NULL);

	if (exi->exi_visible != NULL) {
		mdb_inc_indent(4);
		mdb_printf("PseudoFS Nodes:\n");
		mdb_inc_indent(4);
		if (mdb_pwalk("nfs_expvis", (mdb_walk_cb_t)dump_visible, NULL,
		    (uintptr_t)exi->exi_visible) == -1) {
			mdb_warn("couldn't %p::walk nfs_expvis\n",
			    exi->exi_visible);
			return (DCMD_ERR);
		}
		mdb_dec_indent(8);
	}

	return (DCMD_OK);
}


/*ARGSUSED*/
int
nfs4_exptbl_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (mdb_walk("nfs_expinfo", (mdb_walk_cb_t)expinfo_dcmd_guts,
	    NULL) == -1) {
		mdb_warn("couldn't ::walk nfs_expinfo\n");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
int
nfs4_exptbl_path_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	if (mdb_walk("nfs_expinfo_path", (mdb_walk_cb_t)expinfo_dcmd_guts,
	    NULL) == -1) {
		mdb_warn("couldn't ::walk nfs_expinfo_path\n");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

void
dump_exp_visible_path(treenode_t *node)
{
	exp_visible_t visible;
	vnode_t vp;
	char path[MAXPATHLEN + 1];

	if (node->tree_vis == 0) {
		mdb_printf("/\n"); /* root of tree has no visible */
		return;
	}

	if (mdb_vread(&visible, sizeof (visible), (uintptr_t)node->tree_vis)
	    != sizeof (visible)) {
		return;
	}

	if (mdb_vread(&vp, sizeof (vp), (uintptr_t)visible.vis_vp)
	    != sizeof (vp)) {
		mdb_warn("error reading vnode at %p\n", visible.vis_vp);
		return;
	}
	if (mdb_readstr(path, MAXPATHLEN, (uintptr_t)vp.v_path) != -1) {
		mdb_printf("%s\n", path);
	}
}

void
dump_treenode(treenode_t *node, uintptr_t addr, int flags)
{
	mdb_printf("\n\nTREENODE:\n");
	dump_exp_visible_path(node);
	mdb_inc_indent(2);
	if (flags & NFS_MDB_OPT_VERBOSE)
		mdb_printf("\nDump treenode:\n\n");
	mdb_printf("addr:             %-16p\n", addr);
	mdb_printf("tree_parent:      %-16p\n", node->tree_parent);
	mdb_printf("tree_child_first: %-16p\n", node->tree_child_first);
	mdb_printf("tree_sibling:     %-16p\n", node->tree_sibling);
	mdb_printf("tree_exi:         %-16p\n", node->tree_exi);
	mdb_printf("tree_vis:         %-16p\n", node->tree_vis);
	if (flags & NFS_MDB_OPT_VERBOSE) {
		/* Verbose is set */
		if (node->tree_exi) {
			exportinfo_t exi;
			if (mdb_vread(&exi, sizeof (exi),
			    (uintptr_t)node->tree_exi) != sizeof (exi)) {
				mdb_warn("error reading exportinfo at %p\n",
				    addr);
				return;
			}
			mdb_printf("\nDump exportinfo:\n");
			(void) expinfo_dcmd_guts((uintptr_t)node->tree_exi,
			    &exi, NULL);
		}
		if (node->tree_vis) {
			exp_visible_t vis;
			if (mdb_vread(&vis, sizeof (vis),
			    (uintptr_t)node->tree_vis) != sizeof (vis)) {
				mdb_warn("error reading tree_vis at %p\n",
				    addr);
				return;
			}
			mdb_printf("\nDump exp_visible:\n\n");
			dump_visible((uintptr_t)node->tree_vis, &vis, NULL);
		}
	}
	mdb_dec_indent(2);
}

int
process_treenode(treenode_t *node, uintptr_t addr, int flags)
{
	int ret;
	treenode_t child, *child_ptr;

	dump_treenode(node, addr, flags);
	mdb_inc_indent(4);

	/* dump all childrens */
	child_ptr = node->tree_child_first;
	while (child_ptr) {
		if (mdb_vread(&child, sizeof (child), (uintptr_t)child_ptr)
		    != sizeof (child)) {
			mdb_warn("error reading treenode at %p\n", child_ptr);
			return (DCMD_ERR);
		}
		ret = process_treenode(&child, (uintptr_t)child_ptr, flags);
		if (ret != DCMD_OK)
			return (ret);
		child_ptr = child.tree_sibling;
	}
	mdb_dec_indent(4);
	return (DCMD_OK);
}

static int
hash_dist(char *table_name, uintptr_t exptable, int n, size_t next_off,
    int flags)
{
	uintptr_t *exp_array;
	unsigned sum = 0;
	unsigned sum2 = 0;
	unsigned min = 0;
	unsigned max = 0;
	unsigned mean, smean, variance;
	int i;

	exp_array = mdb_zalloc(n * sizeof (uintptr_t), UM_SLEEP);
	if (mdb_vread(exp_array, n * sizeof (uintptr_t), exptable) == -1) {
		mdb_warn("failed to read table %s\n", table_name);
		mdb_free(exp_array, n * sizeof (uintptr_t));
		return (DCMD_ERR);
	}

	for (i = 0; i < n; i++) {
		exportinfo_t exp_val;
		uintptr_t exp = exp_array[i];
		int bucket_len = 0;

		while (exp) {
			bucket_len++;
			if (mdb_vread(&exp_val, sizeof (exp_val), exp) !=
			    sizeof (exp_val)) {
				mdb_warn("failed to read exportinfo %p\n", exp);
				mdb_free(exp_array, n * sizeof (uintptr_t));
				return (DCMD_ERR);
			}
			/* LINTED pointer improper alignment */
			exp = *(uintptr_t *)((char *)&exp_val + next_off);
		}
		if (flags & NFS_MDB_OPT_VERBOSE)
			mdb_printf("%d\n", bucket_len);
		if (i == 0)
			min = bucket_len;
		min = bucket_len < min ? bucket_len : min;
		max = bucket_len > max ? bucket_len : max;
		sum += bucket_len;
		sum2 += bucket_len * bucket_len;
	}

	mean = sum / n;
	smean = sum2 / n;
	variance = ((smean - mean * mean) * n) / (n - 1);
	mdb_printf("TABLE: %s\nitems/size = %d/%d\n", table_name, sum, n);
	mdb_printf("min/avg/max/variance = %d/%d/%d/%d\n",
	    min, mean, max, variance);
	mdb_free(exp_array, n * sizeof (uintptr_t));
	return (DCMD_OK);
}

/*ARGSUSED*/
int
nfs_fid_hashdist(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	rfs_inst_t *rip;
	uintptr_t exptable;

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	rip = get_rfs_inst();
	if (rip == NULL)
		return (DCMD_ERR);
	exptable = (uintptr_t)rip->ri_nm.rn_exptable;
	return (hash_dist("exptable", exptable, EXPTABLESIZE,
	    offsetof(exportinfo_t, fid_hash.next), opts));
}

/*ARGSUSED*/
int
nfs_path_hashdist(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int opts = 0;
	uintptr_t exptable_path;
	rfs_inst_t *rip;

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	rip = get_rfs_inst();
	if (rip == NULL)
		return (DCMD_ERR);
	exptable_path = (uintptr_t)rip->ri_nm.rn_exptable_path_hash;

	return (hash_dist("exptable_path_hash", exptable_path, PKP_HASH_SIZE,
	    offsetof(exportinfo_t, path_hash.next), opts));
}

/*ARGSUSED*/
int
nfs4_nstree_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	treenode_t node;
	int opts = 0;
	rfs_inst_t *rip;


	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS,
	    NFS_MDB_OPT_VERBOSE, &opts, NULL) != argc)
		return (DCMD_USAGE);

	rip = get_rfs_inst();
	if (rip == NULL)
		return (DCMD_ERR);

	addr = (uintptr_t)rip->ri_nm.rn_ns_root;
	if (addr) {
		if (mdb_vread(&node, sizeof (node), addr) != sizeof (node)) {
			mdb_warn("error reading treenode at %p\n", addr);
			return (DCMD_ERR);
		}
		return (process_treenode(&node, addr, opts));
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static void
dump_exportinfo(uintptr_t addr, struct exportinfo *exi, void *arg)
{
	char xpath[MAXPATHLEN + 1], *vroot;
	struct exportdata *exd = &exi->exi_export;
	int plen;
	struct vnode vno;

	plen = MIN(exd->ex_pathlen, MAXPATHLEN);
	if (mdb_vread(xpath, plen, (uintptr_t)exd->ex_path) != plen) {
		mdb_warn("error reading ex_path at %p",
		    exi->exi_export.ex_path);
		strcpy(xpath, "???");
	} else
		xpath[plen] = '\0';

	if ((mdb_vread(&vno, sizeof (vno), (uintptr_t)exi->exi_vp) ==
	    sizeof (vno)) &&
	    (vno.v_flag & VROOT))
		vroot = "VROOT";
	else
		vroot = "";

	mdb_printf("\n%s    %-16p\n", xpath, (uintptr_t)addr);
	mdb_inc_indent(4);
	mdb_printf("rtvp: %-16p    ref : %-8d  flag: 0x%x (%b) %s\n",
	    exi->exi_vp, exi->exi_count, exd->ex_flags, exd->ex_flags,
	    bm_expflg, vroot);
	mdb_printf("dvp : %-16p    anon: %-8d  logb: %-16p\n",
	    exi->exi_dvp, exd->ex_anon, exi->exi_logbuffer);
	mdb_printf("seci: %-16p    nsec: %-8d  fsid: (0x%x 0x%x)\n",
	    exd->ex_secinfo, exd->ex_seccnt, exi->exi_fsid.val[0],
	    exi->exi_fsid.val[1]);
	nfs4_dump_secinfo((uintptr_t)exd->ex_secinfo, exd->ex_seccnt);
	mdb_dec_indent(4);
}

void
nfs4_dump_secinfo(uintptr_t addr, int seccnt)
{
	int seclen, i;
	struct secinfo *sec;

	seclen = seccnt * sizeof (struct secinfo);
	sec = mdb_alloc(seclen, UM_SLEEP);

	if (mdb_vread(sec, seclen, addr) != seclen) {
		mdb_warn("error reading secinfo array 0x%p", addr);
		mdb_free(sec, seclen);
		return;
	}

	if (seccnt > 0) {
		mdb_printf("Security Flavors :\n");
		mdb_inc_indent(4);
		for (i = 0; i < seccnt; i++) {
			mdb_printf("%b     ref: %-8d flag: 0x%x (%b)\n",
			    sec[i].s_secinfo.sc_nfsnum, bm_flav,
			    sec[i].s_refcnt, sec[i].s_flags,
			    sec[i].s_flags, bm_secflg);
		}
		mdb_dec_indent(4);
	}
	mdb_free(sec, seclen);
}

/*ARGSUSED*/
int
nfs4_expvis_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct exp_visible vis;

	if (! (flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (DCMD_HDRSPEC(flags))
		mdb_printf("exportinfo->exp_visible = %p\n\n", addr);

	if (mdb_vread(&vis, sizeof (vis), addr) != sizeof (vis)) {
		mdb_warn("error reading struct visible at %p", addr);
		return (DCMD_ERR);
	}

	dump_visible(addr, &vis, NULL);

	return (DCMD_OK);
}

/*ARGSUSED*/
static void
dump_visible(uintptr_t addr, struct exp_visible *vis, void *arg)
{
	char vpath[MAXPATHLEN + 1];

	vpath[0] = vpath[MAXPATHLEN] = '\0';
	mdb_vnode2path((uintptr_t)vis->vis_vp, vpath, MAXPATHLEN);
	mdb_printf("%s\n", vpath);

	mdb_inc_indent(4);
	mdb_printf("addr: %16p   exp : %d    ref: %d\n", addr,
	    vis->vis_exported, vis->vis_count);
	mdb_printf("vp  : %16p   ino : %lld (0x%llx)\n", vis->vis_vp,
	    vis->vis_ino, vis->vis_ino);
	mdb_printf("seci: %16p   nsec: %d\n", vis->vis_secinfo,
	    vis->vis_seccnt);
	nfs4_dump_secinfo((uintptr_t)vis->vis_secinfo, vis->vis_seccnt);
	mdb_dec_indent(4);
}
void nfs4_exp_dcmd_help(void) {}
void nfs4_exptbl_dcmd_help(void) {}

void
nfs4_nstree_dcmd_help(void)
{
	mdb_printf("-v .. dumps also exportinfo and exp_visible structures\n");
}

void nfs4_expvis_dcmd_help(void) {}
