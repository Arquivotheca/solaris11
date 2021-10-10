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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/dditypes.h>
#include <sys/mdb_modapi.h>
#include <sys/modctl.h>
#include <sys/sunddi.h>
#include <sys/lpif.h>
#include <sys/stmf.h>
#include <sys/portif.h>
#include <stmf_impl.h>
#include <lun_map.h>
#include <stmf_state.h>

#include <sys/fct.h>
#include <fct_impl.h>

#include "cmd_options.h"

#define	STMF_VERBOSE	0x00000001
#define	STMF_VE_LIST	0x00000002

#define	STMF_GUID_SZ	16
#define	STMF_BUF_MAX	2048
#define	ARRAY_SIZE(a)	(sizeof (a) / sizeof (*a))

/* structure to pass arguments to mdb_walker callback function */
typedef struct stmf_cb_s {
	uint32_t flag;
	char sp[4];
} stmf_cb_t;

static const char *stmf_id_data_type_desc[] = {
	"STMF_ID_TYPE_HOST",
	"STMF_ID_TYPE_TARGET",
	"STMF_ID_TYPE_LU_GUID",
	"STMF_ID_TYPE_HOST_GROUP",
	"STMF_ID_TYPE_TARGET_GROUP"
};

static int
stmf_ilport_walk_i(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		struct stmf_state state;

		if (mdb_readsym(&state, sizeof (struct stmf_state),
		    "stmf_state") == -1) {
			mdb_warn("failed to read stmf_state");
			return (WALK_ERR);
		}
		wsp->walk_addr = (uintptr_t)state.stmf_ilportlist;
	}

	wsp->walk_data = mdb_alloc(sizeof (stmf_i_local_port_t), UM_SLEEP);
	return (WALK_NEXT);
}

static int
stmf_ilport_walk_s(mdb_walk_state_t *wsp)
{
	int status = WALK_NEXT;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(wsp->walk_data, sizeof (struct stmf_i_local_port),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read stmf_i_local_port_t at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}

	if (wsp->walk_callback)
		status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
		    wsp->walk_cbdata);

	wsp->walk_addr = (uintptr_t)
	    (((struct stmf_i_local_port *)wsp->walk_data)->ilport_next);

	return (status);
}

static void
stmf_ilport_walk_f(mdb_walk_state_t *wsp)
{
	mdb_free(wsp->walk_data, sizeof (struct stmf_i_local_port));
}

static int
dump_ilport(struct stmf_i_local_port *ilportp, int verbose)
{
	if (ilportp == NULL)
		return (DCMD_OK);

	mdb_printf("%p\n", ilportp);

	if (verbose) {
		/* here assume the alias is maximumly 1024 bytes */
		char alias[255];
		struct stmf_local_port lport;
		struct stmf_i_local_port ilport;

		if (mdb_vread(&ilport, sizeof (ilport), (uintptr_t)ilportp)
		    == -1) {
			mdb_warn("failed to read stmf_i_local_port at %p",
			    ilportp);
			return (DCMD_ERR);
		}

		memset(alias, 0, sizeof (alias));
		if (mdb_vread(&lport, sizeof (lport),
		    (uintptr_t)ilport.ilport_lport) == -1) {
			mdb_warn("failed to read stmf_local_port at %p",
			    ilport.ilport_lport);
			return (DCMD_ERR);
		}
		if (lport.lport_alias && mdb_vread(alias, sizeof (alias),
		    (uintptr_t)lport.lport_alias) == -1) {
			mdb_warn("failed to read memory at %p",
			    lport.lport_alias);
			return (DCMD_ERR);
		}

		mdb_printf("  lport: %p\n", ilport.ilport_lport);
		if (lport.lport_alias)
			mdb_printf("  port alias: %s\n", alias);
		mdb_printf("  port provider: %p\n", lport.lport_pp);
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
stmf_ilports(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int i;
	int verbose = 0;
	mdb_walk_state_t ws = {NULL, };

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (stmf_ilport_walk_i(&ws) == WALK_ERR)
		return (DCMD_ERR);

	dump_ilport((stmf_i_local_port_t *)ws.walk_addr, verbose);

	while (stmf_ilport_walk_s(&ws) == WALK_NEXT)
		dump_ilport((stmf_i_local_port_t *)ws.walk_addr, verbose);

	stmf_ilport_walk_f(&ws);
	return (DCMD_OK);
}

struct stmf_i_local_port *
next_stmf_port(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		if (stmf_ilport_walk_i(wsp) == WALK_ERR) {
			stmf_ilport_walk_f(wsp);
			return (NULL);
		}
		if (wsp->walk_addr == NULL)
			stmf_ilport_walk_f(wsp);
		return ((struct stmf_i_local_port *)wsp->walk_addr);
	}

	if (stmf_ilport_walk_s(wsp) == WALK_ERR) {
		stmf_ilport_walk_f(wsp);
		return (NULL);
	}
	if (wsp->walk_addr == NULL)
		stmf_ilport_walk_f(wsp);
	return ((struct stmf_i_local_port *)wsp->walk_addr);
}


/*ARGSUSED*/
static int
stmf_iss(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct stmf_i_local_port iport;
	struct stmf_i_scsi_session *issp;
	struct stmf_i_scsi_session iss;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (addr == NULL) {
		mdb_warn("address of stmf_i_local_port should be specified\n");
		return (DCMD_ERR);
	}

	/*
	 * Input should be stmf_i_local_port_t.
	 */
	if (mdb_vread(&iport, sizeof (struct stmf_i_local_port), addr)
	    != sizeof (struct stmf_i_local_port)) {
		mdb_warn("Unable to read in stmf_i_local_port at %p\n", addr);
		return (DCMD_ERR);
	}

	issp = iport.ilport_ss_list;

	while (issp) {
		if (mdb_vread(&iss, sizeof (iss), (uintptr_t)issp) == -1) {
			mdb_warn("failed to read stmf_i_scsi_session_t at %p",
			    issp);
			return (DCMD_ERR);
		}

		mdb_printf("%p\n", issp);
		if (verbose) {
			mdb_printf("  scsi session: %p\n", iss.iss_ss);
		}

		issp = iss.iss_next;
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
stmf_ilus(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct stmf_state state;
	struct stmf_i_lu ilu;
	struct stmf_i_lu *ilup;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (mdb_readsym(&state, sizeof (struct stmf_state), "stmf_state")
	    == -1) {
		mdb_warn("failed to read stmf_state");
		return (DCMD_ERR);
	}

	ilup = state.stmf_ilulist;
	while (ilup) {
		if (mdb_vread(&ilu, sizeof (struct stmf_i_lu), (uintptr_t)ilup)
		    == -1) {
			mdb_warn("failed to read stmf_i_lu_t at %p", ilup);
			return (DCMD_ERR);
		}

		mdb_printf("%p\n", ilup);
		if (verbose) {
			mdb_printf("  lu: %p\n", ilu.ilu_lu);

			/* XXX lu_alias? what is its size? */
		}

		ilup = ilu.ilu_next;
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
stmf_i_lu_providers(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	struct stmf_state state;
	struct stmf_i_lu_provider ilp;
	struct stmf_i_lu_provider *ilpp;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (mdb_readsym(&state, sizeof (struct stmf_state), "stmf_state")
	    == -1) {
		mdb_warn("failed to read stmf_state");
		return (DCMD_ERR);
	}

	ilpp = state.stmf_ilplist;
	while (ilpp) {
		if (mdb_vread(&ilp, sizeof (stmf_i_lu_provider_t),
		    (uintptr_t)ilpp) == -1) {
			mdb_warn("failed to read stmf_i_lu_provider_t at %p",
			    ilpp);
			return (DCMD_ERR);
		}

		mdb_printf("%p\n", ilpp);
		if (verbose) {
			mdb_printf("  lu provider: %p\n", ilp.ilp_lp);
		}

		ilpp = ilp.ilp_next;
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
stmf_i_port_providers(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	struct stmf_state state;
	struct stmf_i_port_provider ipp;
	struct stmf_i_port_provider *ippp;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (mdb_readsym(&state, sizeof (struct stmf_state), "stmf_state")
	    == -1) {
		mdb_warn("failed to read stmf_state");
		return (DCMD_ERR);
	}

	ippp = state.stmf_ipplist;
	while (ippp) {
		if (mdb_vread(&ipp, sizeof (stmf_i_port_provider_t),
		    (uintptr_t)ippp) == -1) {
			mdb_warn("failed to read stmf_i_port_provider_t at %p",
			    ippp);
			return (DCMD_ERR);
		}

		mdb_printf("%p\n", ippp);
		if (verbose) {
			mdb_printf("  port provider: %p\n", ipp.ipp_pp);
		}

		ippp = ipp.ipp_next;
	}

	return (DCMD_OK);
}

char *
stmf_getstr(uintptr_t addr, char *str, int maxlen) {
	if ((addr == 0) || (mdb_readstr(str, maxlen, addr) == -1))
		str = NULL;
	return (str);
}

static uint64_t
nhconvert_8bytes(const void *src) {
	uint64_t dest;
	mdb_nhconvert(&dest, src, 8);
	return (dest);
}

void
stmf_print_id_type(uint16_t id_type,  uint16_t expected, char *sp) {
	mdb_printf("%s\ttype:        %s%d => %s\n", sp,
	    (id_type != expected) ?  "ERROR, Wrong type:- " : "", id_type,
	    (id_type < ARRAY_SIZE(stmf_id_data_type_desc)) ?
	    stmf_id_data_type_desc[id_type] : "ILLEGAL");
}

static int
stmf_id_data_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("<stmf_id_data addr>::walk stmf_id_data\n");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

static int
stmf_id_data_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t	addr = wsp->walk_addr;
	stmf_id_data_t	iddata;

	if (addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&iddata, sizeof (stmf_id_data_t), addr) == -1) {
		mdb_warn("failed to read stmf_id_data_t at %p\n", addr);
		return (WALK_ERR);
	}
	wsp->walk_addr = (uintptr_t)iddata.id_next;
	return (wsp->walk_callback(addr, &iddata, wsp->walk_cbdata));
}

static int
stmf_ve_cb(uintptr_t addr, const stmf_view_entry_t *ve, stmf_cb_t *cb_st)
{
	uint16_t lun;

	if (cb_st->flag & STMF_VERBOSE) {
		mdb_nhconvert(&lun, &(ve->ve_lun[0]), 2);
		mdb_printf("\n%s%p  <-- VE Entry\n", cb_st->sp, addr);
		mdb_printf("\t%sLU num: %d\n", cb_st->sp, lun);
		mdb_printf("\t%shg:     %-?p\n", cb_st->sp, ve->ve_hg);
		mdb_printf("\t%stg:     %-?p\n", cb_st->sp, ve->ve_tg);
		mdb_printf("\t%sluid:   %-?p\n", cb_st->sp, ve->ve_luid);
	} else {
		mdb_printf("%p\n", addr);
	}
	return (WALK_NEXT);
}

static int
stmf_ve_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("<stmf_view_entry addr>::walk stmf_ve\n");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

static int
stmf_ve_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t	addr = wsp->walk_addr;
	stmf_view_entry_t ve;

	if (addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&ve, sizeof (stmf_view_entry_t), addr) == -1) {
		mdb_warn("failed to read stmf_view_entry_t at %p\n", addr);
		return (WALK_ERR);
	}
	wsp->walk_addr = (uintptr_t)ve.ve_next;
	return (wsp->walk_callback(addr, &ve, wsp->walk_cbdata));
}

static int
stmf_target_cb(uintptr_t addr, const stmf_id_data_t *id, stmf_cb_t *cb_st)
{
	char buf[STMF_BUF_MAX];
	size_t buflen;

	if (cb_st->flag & STMF_VERBOSE) {
		if (mdb_vread(buf, id->id_data_size, (uintptr_t)id->id_data)
		    == -1) {
			mdb_printf("Failed to read host name at %p\n",
			    id->id_data);
		}
		mdb_printf("\n%s%p  <-- Target\n", cb_st->sp, addr);
		stmf_print_id_type(id->id_type, STMF_ID_TYPE_TARGET, cb_st->sp);
		mdb_printf("%s\tdata size:   %d\n", cb_st->sp,
		    id->id_data_size);
		if (id->id_type == STMF_ID_TYPE_TARGET) {
			buflen = (STMF_BUF_MAX > id->id_data_size) ?
			    id->id_data_size + 1 : STMF_BUF_MAX;
			mdb_printf("%s\tname:        %s\n", cb_st->sp,
			    stmf_getstr((uintptr_t)id->id_data, buf, buflen));
		}
	}
	return (WALK_NEXT);
}

static int
stmf_host_cb(uintptr_t addr, const stmf_id_data_t *id, stmf_cb_t *cb_st)
{
	char buf[STMF_BUF_MAX];
	size_t buflen;

	if (cb_st->flag & STMF_VERBOSE) {
		if (mdb_vread(buf, id->id_data_size, (uintptr_t)id->id_data)
		    == -1) {
			mdb_printf("Failed to read host name at %p\n",
			    id->id_data);
		}
		mdb_printf("\n%s%p  <-- Host\n", cb_st->sp, addr);
		stmf_print_id_type(id->id_type,  STMF_ID_TYPE_HOST, cb_st->sp);
		mdb_printf("%s\tdata size:   %d\n", cb_st->sp,
		    id->id_data_size);
		if (id->id_type == STMF_ID_TYPE_HOST) {
			buflen = (STMF_BUF_MAX > id->id_data_size) ?
			    id->id_data_size + 1 : STMF_BUF_MAX;
			mdb_printf("%s\tname:        %s\n", cb_st->sp,
			    stmf_getstr((uintptr_t)id->id_data, buf, buflen));
		}
	}
	return (WALK_NEXT);
}

static int
stmf_hg_cb(uintptr_t addr, const stmf_id_data_t *id, stmf_cb_t *cb_st)
{
	char buf[STMF_BUF_MAX];
	size_t buflen;
	char sp[4];
	stmf_id_list_t idlist;

	if (cb_st->flag & STMF_VERBOSE) {
		mdb_printf("\n%p    ", addr);
		if (id->id_type == STMF_ID_TYPE_HOST_GROUP) {
			buflen = (STMF_BUF_MAX > id->id_data_size) ?
			    id->id_data_size + 1 : STMF_BUF_MAX;
			stmf_getstr((uintptr_t)id->id_data, buf, buflen);
			mdb_printf("\tHG Name:   %s%s\n", buf,
			    (id->id_data_size == 1 && strncmp(buf, "*", 1) == 0)
			    ? "  => <default>" : "");
		}
		stmf_print_id_type(id->id_type, STMF_ID_TYPE_HOST_GROUP, "");
		mdb_printf("\tdata size:   %d\n", id->id_data_size);
		mdb_printf("\thost list:   %p\n", id->id_impl_specific);
		if (id->id_impl_specific) {
			if (mdb_vread(&idlist, sizeof (stmf_id_list_t),
			    (uintptr_t)id->id_impl_specific) == -1) {
				mdb_warn("failed to read stmf_id_list_t at "
				    "%p\n", addr);
				return (WALK_ERR);
			}
			mdb_printf("\ttotal hosts:   %d\n", idlist.id_count);
			if (idlist.id_count > 0) {
				strcpy(sp, cb_st->sp);
				strcat(cb_st->sp, "\t");
				if (mdb_pwalk("stmf_id_data",
				    (mdb_walk_cb_t)stmf_host_cb, cb_st,
				    (uintptr_t)idlist.idl_head)
				    == -1) {
					mdb_warn("failed to walk host list\n");
				}
				strcpy(cb_st->sp, sp);
			}
		}
	} else {
		mdb_printf("%p\n", addr);
	}
	return (WALK_NEXT);
}

static int
stmf_hg(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t		verbose = FALSE;
	stmf_state_t	state;
	stmf_id_data_t	iddata;
	stmf_cb_t	cb_st = {0};

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose, NULL) != argc)
		return (DCMD_USAGE);

	if (verbose)
		cb_st.flag |= STMF_VERBOSE;

	if (flags & DCMD_ADDRSPEC) {
		cb_st.flag |= STMF_VERBOSE;
		if (mdb_vread(&iddata, sizeof (stmf_id_data_t), addr) == -1) {
			mdb_warn("failed to read stmf_id_data_t at %p\n", addr);
			return (DCMD_ERR);
		}
		if (stmf_hg_cb(addr, &iddata, &cb_st) == WALK_ERR)
			return (DCMD_ERR);
	} else {
		if (mdb_readsym(&state, sizeof (struct stmf_state),
		    "stmf_state") == -1) {
			mdb_warn("failed to read stmf_state");
			return (DCMD_ERR);
		}

		if (state.stmf_hg_list.id_count > 0) {
			if (verbose)
				mdb_printf("Total number of HGs = %d\n",
				    state.stmf_hg_list.id_count);
			if (mdb_pwalk("stmf_id_data", (mdb_walk_cb_t)stmf_hg_cb,
			    &cb_st, (uintptr_t)state.stmf_hg_list.idl_head)
			    == -1) {
				mdb_warn("failed to walk stmf_hg_list\n");
				return (DCMD_ERR);
			}
		} else if (verbose) {
			mdb_printf("No HG found, stmf_hg_list.id_count is 0\n");
		}
	}
	return (DCMD_OK);
}

static int
stmf_tg_cb(uintptr_t addr, const stmf_id_data_t *id, stmf_cb_t *cb_st)
{
	char buf[STMF_BUF_MAX];
	size_t buflen;
	char sp[4];
	stmf_id_list_t idlist;

	if (cb_st->flag & STMF_VERBOSE) {
		mdb_printf("\n%p    ", addr);
		if (id->id_type == STMF_ID_TYPE_TARGET_GROUP) {
			buflen = (STMF_BUF_MAX > id->id_data_size) ?
			    id->id_data_size + 1 : STMF_BUF_MAX;
			stmf_getstr((uintptr_t)id->id_data, buf, buflen);
			mdb_printf("\tTG Name:   %s%s\n", buf,
			    (id->id_data_size == 1 && strncmp(buf, "*", 1) == 0)
			    ? "  => <default>" : "");
		}
		stmf_print_id_type(id->id_type, STMF_ID_TYPE_TARGET_GROUP, "");
		mdb_printf("\tdata size:   %d\n", id->id_data_size);
		mdb_printf("\ttarget list: %p\n", id->id_impl_specific);
		if (id->id_impl_specific) {
			if (mdb_vread(&idlist, sizeof (stmf_id_list_t),
			    (uintptr_t)id->id_impl_specific) == -1) {
				mdb_warn("failed to read stmf_id_list_t at "
				    "%p\n", addr);
				return (WALK_ERR);
			}
			mdb_printf("\ttotal targets:   %d\n", idlist.id_count);
			if (idlist.id_count > 0) {
				strcpy(sp, cb_st->sp);
				strcat(cb_st->sp, "\t");
				if (mdb_pwalk("stmf_id_data",
				    (mdb_walk_cb_t)stmf_target_cb, cb_st,
				    (uintptr_t)idlist.idl_head) == -1) {
					mdb_warn("failed to walk target list"
					    "\n");
				}
				strcpy(cb_st->sp, sp);
			}
		}
	} else {
		mdb_printf("%p\n", addr);
	}
	return (WALK_NEXT);
}

static int
stmf_tg(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t		verbose = FALSE;
	stmf_state_t	state;
	stmf_id_data_t	iddata;
	stmf_cb_t	cb_st = {0};

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose, NULL) != argc)
		return (DCMD_USAGE);

	if (verbose)
		cb_st.flag |= STMF_VERBOSE;

	if (flags & DCMD_ADDRSPEC) {
		cb_st.flag |= STMF_VERBOSE;
		if (mdb_vread(&iddata, sizeof (stmf_id_data_t), addr) == -1) {
			mdb_warn("failed to read stmf_id_data_t at %p\n", addr);
			return (DCMD_ERR);
		}
		if (stmf_tg_cb(addr, &iddata, &cb_st) == WALK_ERR)
			return (DCMD_ERR);
	} else {
		if (mdb_readsym(&state, sizeof (struct stmf_state),
		    "stmf_state") == -1) {
			mdb_warn("failed to read stmf_state");
			return (DCMD_ERR);
		}
		if (state.stmf_tg_list.id_count > 0) {
			if (verbose)
				mdb_printf("Total number of TGs = %d\n",
				    state.stmf_tg_list.id_count);
			if (mdb_pwalk("stmf_id_data", (mdb_walk_cb_t)stmf_tg_cb,
			    &cb_st, (uintptr_t)state.stmf_tg_list.idl_head)
			    == -1) {
				mdb_warn("failed to walk stmf_tg_list\n");
				return (DCMD_ERR);
			}
		} else if (verbose) {
			mdb_printf("No TG found, stmf_tg_list.id_count is 0\n");
		}
	}
	return (DCMD_OK);
}

static int
stmf_luid_cb(uintptr_t addr, const stmf_id_data_t *id, stmf_cb_t *cb_st)
{
	char buf[STMF_BUF_MAX];
	char sp[4];

	if (cb_st->flag & STMF_VERBOSE) {
		mdb_printf("\n%p ", addr);
		if (id->id_type == STMF_ID_TYPE_LU_GUID &&
		    id->id_data_size == STMF_GUID_SZ) {
			if (mdb_vread(buf, STMF_GUID_SZ,
			    (uintptr_t)id->id_data) == -1) {
				mdb_printf("\tGUID:      %-?p  ERROR, "
				    "Data read error\n", id->id_data);
			} else {
				mdb_printf("\tGUID:   %016llX%016llX\n",
				    nhconvert_8bytes(&buf[0]),
				    nhconvert_8bytes(&buf[8]));
			}
		}
		stmf_print_id_type(id->id_type,  STMF_ID_TYPE_LU_GUID, "");
		mdb_printf("\tdata size: %d%s\n", id->id_data_size,
		    (id->id_data_size != STMF_GUID_SZ) ?
		    " => ERROR, Bad guid size": "");
		mdb_printf("\tstmf_i_lu: %-?p  \n", id->id_pt_to_object);
		mdb_printf("\tVE List:   %-?p  \n", id->id_impl_specific);
		if (cb_st->flag & STMF_VE_LIST) {
			strcpy(sp, cb_st->sp);
			strcat(cb_st->sp, "\t");
			if (mdb_pwalk("stmf_ve", (mdb_walk_cb_t)stmf_ve_cb,
			    cb_st, (uintptr_t)id->id_impl_specific) == -1) {
				mdb_warn("failed to walk ve list\n");
				return (DCMD_ERR);
			}
			strcpy(cb_st->sp, sp);
		}
	} else {
		mdb_printf("%p\n", addr);
	}

	return (WALK_NEXT);
}

static int
stmf_luid(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t		verbose = FALSE, velist = FALSE;
	stmf_state_t	state;
	stmf_id_data_t	iddata;
	stmf_cb_t	cb_st = {0};

	if (mdb_getopts(argc, argv,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    'e', MDB_OPT_SETBITS, TRUE, &velist, NULL) != argc)
		return (DCMD_USAGE);

	if (verbose || velist)
		cb_st.flag |= STMF_VERBOSE;
	if (velist)
		cb_st.flag |= STMF_VE_LIST;

	if (flags & DCMD_ADDRSPEC) {
		cb_st.flag |= STMF_VERBOSE;
		if (mdb_vread(&iddata, sizeof (stmf_id_data_t), addr) == -1) {
			mdb_warn("failed to read stmf_id_data_t at %p\n", addr);
			return (DCMD_ERR);
		}
		if (stmf_luid_cb(addr, &iddata, &cb_st) == WALK_ERR)
			return (DCMD_ERR);
	} else {
		if (mdb_readsym(&state, sizeof (struct stmf_state),
		    "stmf_state") == -1) {
			mdb_warn("failed to read stmf_state");
			return (DCMD_ERR);
		}
		if (state.stmf_luid_list.id_count > 0) {
			if (verbose)
				mdb_printf("Total number of GUIDs =  %d\n",
				    state.stmf_luid_list.id_count);
			if (mdb_pwalk("stmf_id_data",
			    (mdb_walk_cb_t)stmf_luid_cb, &cb_st,
			    (uintptr_t)state.stmf_luid_list.idl_head) == -1) {
				mdb_warn("failed to walk stmf_luid_list\n");
				return (DCMD_ERR);
			}
		} else if (verbose) {
			mdb_printf("No LU GUIDs found, stmf_luid_list.id_count "
			    "is 0\n");
		}
	}
	return (DCMD_OK);
}
static int
stmf_ver_hg_cb(uintptr_t addr, const stmf_ver_hg_t *vehg, stmf_cb_t *cb_st)
{
	const stmf_lun_map_t *lmap;
	size_t plen;
	char sp[4];
	int i;
	stmf_view_entry_t **pary;
	stmf_view_entry_t ve;

	lmap = &vehg->verh_ve_map;
	if (cb_st->flag & STMF_VERBOSE) {
		mdb_printf("\n%s%p  <-- VE HG\n", cb_st->sp, addr);
		mdb_printf("\t%snluns:     %p\n", cb_st->sp, lmap->lm_nluns);
		mdb_printf("\t%snentries:  %p\n", cb_st->sp, lmap->lm_nentries);
		mdb_printf("\t%sve ptr:    %p\n", cb_st->sp, lmap->lm_plus);
		plen = lmap->lm_nentries * sizeof (stmf_view_entry_t *);
		if (lmap->lm_nentries > 0) {
			if ((pary = (stmf_view_entry_t **)mdb_alloc(plen,
			    UM_NOSLEEP|UM_GC)) == NULL)
				return (WALK_NEXT);
			if (mdb_vread(pary, plen, (uintptr_t)lmap->lm_plus)
			    == -1) {
				mdb_warn("failed to read view entries at %p\n",
				    lmap->lm_plus);
				return (WALK_ERR);
			}
		}

		strcpy(sp, cb_st->sp);
		strcat(cb_st->sp, "\t");
		for (i = 0; i < lmap->lm_nentries; i++) {
			if (pary[i] != NULL) {
				if (mdb_vread(&ve, sizeof (stmf_view_entry_t),
				    (uintptr_t)pary[i]) == -1)
					mdb_warn("failed to read view entries "
					    "at %p\n", lmap->lm_plus);
				else if (stmf_ve_cb((uintptr_t)pary[i], &ve,
				    cb_st) == WALK_ERR)
					mdb_warn("view entry walk failed \n");
			}
		}
		strcpy(cb_st->sp, sp);
	} else {
		mdb_printf("%p\n", addr);
	}
	return (WALK_NEXT);
}

static int
stmf_ver_hg_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("<stmf_ver_hg addr>::walk stmf_ver_hg\n");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

static int
stmf_ver_hg_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t	addr = wsp->walk_addr;
	stmf_ver_hg_t	vehg;

	if (addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&vehg, sizeof (stmf_ver_hg_t), addr) == -1) {
		mdb_warn("failed to read stmf_ver_hg_t at %p\n", addr);
		return (WALK_ERR);
	}
	wsp->walk_addr = (uintptr_t)vehg.verh_next;
	return (wsp->walk_callback(addr, &vehg, wsp->walk_cbdata));
}

static int
stmf_ver_tg_cb(uintptr_t addr, const stmf_ver_tg_t *vetg, stmf_cb_t *cb_st)
{
	if (cb_st->flag & STMF_VERBOSE) {
		mdb_printf("\n%p  <-- VE TG\n", addr);
		strcpy(cb_st->sp, "\t");
		if (mdb_pwalk("stmf_ver_hg",
		    (mdb_walk_cb_t)stmf_ver_hg_cb, cb_st,
		    (uintptr_t)vetg->vert_verh_list) == -1) {
			mdb_warn("failed to walk vert_verh_list\n");
		}
		strcpy(cb_st->sp, "");
		mdb_printf("---------------------------------------------------"
		    "\n");
	} else {
		mdb_printf("%p\n", addr);
	}
	return (WALK_NEXT);
}

static int
stmf_ver_tg_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr == NULL) {
		mdb_warn("<stmf_ver_tg addr>::walk stmf_ver_tg\n");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

static int
stmf_ver_tg_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t	addr = wsp->walk_addr;
	stmf_ver_tg_t	vetg;

	if (addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&vetg, sizeof (stmf_ver_tg_t), addr) == -1) {
		mdb_warn("failed to read stmf_ver_tg_t at %p\n", addr);
		return (WALK_ERR);
	}
	wsp->walk_addr = (uintptr_t)vetg.vert_next;
	return (wsp->walk_callback(addr, &vetg, wsp->walk_cbdata));
}

static int
stmf_lun_map(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	stmf_state_t	state;
	stmf_cb_t	cb_st = {0};
	uintptr_t	tg_head_addr = addr;

	if (mdb_getopts(argc, argv, NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_readsym(&state, sizeof (struct stmf_state),
		    "stmf_state") == -1) {
			mdb_warn("failed to read stmf_state");
			return (DCMD_ERR);
		}
		if (state.stmf_ver_tg_head == NULL) {
			mdb_printf("VE Target Group List is empty\n");
			return (DCMD_OK);
		}
		tg_head_addr = (uintptr_t)state.stmf_ver_tg_head;
	}

	cb_st.flag |= STMF_VERBOSE;
	if (mdb_pwalk("stmf_ver_tg", (mdb_walk_cb_t)stmf_ver_tg_cb, &cb_st,
	    tg_head_addr) == -1) {
		mdb_warn("failed to walk stmf_luid_list\n");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

int string2wwn(const char *s, uint8_t wwn[8]);

static uint16_t port_max_logins;
static int	rp_index;

/*
 * Cervert stmf_i_local_port to fct_i_local_port
 */
/*ARGSUSED*/
static struct fct_i_local_port *
__ilport2iport(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct stmf_i_local_port iport;
	struct stmf_local_port   lport;
	struct fct_local_port    fport;

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("stmf_i_local_port address should be specified");
		return (NULL);
	}

	/*
	 * Input should be stmf_i_local_port_t.
	 */
	if (mdb_vread(&iport, sizeof (struct stmf_i_local_port), addr)
	    != sizeof (struct stmf_i_local_port)) {
		mdb_warn("Unable to read in stmf_i_local_port\n");
		return (NULL);
	}

	if (mdb_vread(&lport, sizeof (stmf_local_port_t),
	    (uintptr_t)iport.ilport_lport) != sizeof (stmf_local_port_t)) {
		mdb_warn("Unable to read in stmf_local_port\n");
		return (NULL);
	}

	if (mdb_vread(&fport, sizeof (fct_local_port_t),
	    (uintptr_t)lport.lport_port_private)
	    != sizeof (fct_local_port_t)) {
		mdb_warn("Unable to read in fct_local_port\n");
		return (NULL);
	}

	return (fport.port_fct_private);
}

static int
ilport2iport(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct fct_i_local_port *iportp;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}


	iportp = __ilport2iport(addr, flags, argc, argv);
	if (iportp) {
		mdb_printf("%p\n", iportp);
		if (verbose) {
			struct fct_i_local_port iport;
			/* is the alias always 16 bytes in size ? */
			char alias[16];

			memset(alias, 0, sizeof (alias));
			if (mdb_vread(&iport, sizeof (fct_i_local_port_t),
			    (uintptr_t)iportp)
			    != sizeof (fct_i_local_port_t)) {
				mdb_warn("Unable to read in fct_i_local_port"
				    "at %p\n", iportp);
				return (DCMD_ERR);
			}
			if (iport.iport_alias &&
			    mdb_vread(alias, sizeof (alias),
			    (uintptr_t)iport.iport_alias)
			    != sizeof (alias)) {
				mdb_warn("Unable to read in memory at %p",
				    iport.iport_alias);
				return (DCMD_ERR);
			}
			mdb_printf("  port: %p\n", iport.iport_port);
			if (iport.iport_alias)
				mdb_printf("  alias: %s\n", alias);
		}
	}
	return (DCMD_OK);
}

/*
 * by wwn, we can only find one local port
 */
static struct stmf_i_local_port *
find_lport_by_wwn(uint8_t wwn[8])
{
	struct stmf_i_local_port *siport;
	struct fct_i_local_port *fiport;
	struct fct_i_local_port iport;
	struct fct_local_port fport;
	mdb_walk_state_t ws = {NULL, };

	while ((siport = next_stmf_port(&ws)) != NULL) {
		fiport = __ilport2iport((uintptr_t)siport, DCMD_ADDRSPEC,
		    0, NULL);
		if (fiport == NULL)
			return (NULL);

		if (mdb_vread(&iport, sizeof (fct_i_local_port_t),
		    (uintptr_t)fiport)
		    != sizeof (fct_i_local_port_t)) {
			mdb_warn("Unable to read in fct_i_local_port\n");
			return (NULL);
		}
		if (mdb_vread(&fport, sizeof (fct_local_port_t),
		    (uintptr_t)iport.iport_port)
		    != sizeof (fct_local_port_t)) {
			mdb_warn("Unable to read in fct_local_port\n");
			return (NULL);
		}

#if 0
		mdb_printf("pwwn=%02x%02x%02x%02x%02x%02x%02x%02x\n",
		    fport.port_pwwn[0], fport.port_pwwn[1],
		    fport.port_pwwn[2], fport.port_pwwn[3],
		    fport.port_pwwn[4], fport.port_pwwn[5],
		    fport.port_pwwn[6], fport.port_pwwn[7]);
#endif
		if (memcmp(fport.port_pwwn, wwn, 8) == 0) {
			return (siport);
		}
	}

	return (NULL);
}

/*ARGSUSED*/
static int
stmf_find_ilport(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct find_options *options;
	struct stmf_i_local_port *siport;

	options = parse_options(argc, argv);
	/* need to free options manually ? */
	if (options == NULL || ! options->lpname_defined) {
		mdb_printf("lpname=<wwn.12345678 or 12345678> "
		    "should be specified\n");
		return (DCMD_OK);
	}

	if ((siport = find_lport_by_wwn(options->lpname)) != NULL)
		mdb_printf("%p\n", siport);

	return (DCMD_OK);
}

static int
fct_irp_walk_i(mdb_walk_state_t *wsp)
{
	struct fct_local_port port;
	struct fct_i_local_port iport;

	if (wsp->walk_addr == NULL) {
		mdb_warn("Can not perform global walk");
		return (WALK_ERR);
	}

	/*
	 * Input should be fct_i_local_port_t.
	 */
	if (mdb_vread(&iport, sizeof (struct fct_i_local_port), wsp->walk_addr)
	    != sizeof (struct fct_i_local_port)) {
		mdb_warn("Unable to read in fct_i_local_port\n");
		return (WALK_ERR);
	}

	if (mdb_vread(&port, sizeof (struct fct_local_port),
	    (uintptr_t)iport.iport_port)
	    != sizeof (struct fct_local_port)) {
		mdb_warn("Unable to read in fct_local_port\n");
		return (WALK_ERR);
	}

	port_max_logins = port.port_max_logins;
	rp_index = 0;
	wsp->walk_addr = (uintptr_t)iport.iport_rp_slots;

	return (WALK_NEXT);
}

static int
fct_irp_walk_s(mdb_walk_state_t *wsp)
{
	int status = WALK_NEXT;
	fct_i_remote_port_t *rp;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	if (rp_index++ >= port_max_logins)
		return (WALK_DONE);

	if (mdb_vread(&rp, sizeof (fct_i_remote_port_t *),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read address of fct_i_remote_port_t at %p",
		    wsp->walk_addr);
		return (WALK_DONE);
	}

	if (rp != NULL && wsp->walk_callback != NULL)
		status = wsp->walk_callback((uintptr_t)rp, rp,
		    wsp->walk_cbdata);

	wsp->walk_addr = (uintptr_t)
	    &(((fct_i_remote_port_t **)wsp->walk_addr)[1]);

	return (status);
}

static void
fct_irp_walk_f(mdb_walk_state_t *wsp)
{
	wsp->walk_addr = NULL;
}

/*
 * to set remote_port
 */
/*ARGSUSED*/
static int
walk_fct_irp_cb(uintptr_t p, const void * arg, void *cbdata)
{
	*((uintptr_t *)cbdata) = p;
	return (WALK_NEXT);
}

static int
fct_irps(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	static uint64_t cbdata = 0;
	mdb_walk_state_t ws = {walk_fct_irp_cb, &cbdata, addr};
	fct_i_remote_port_t *irpp;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("fct_i_local_port_t address should be specified");
		return (DCMD_ERR);
	}

	fct_irp_walk_i(&ws);
	while (fct_irp_walk_s(&ws) == WALK_NEXT) {
		irpp = *((fct_i_remote_port_t **)ws.walk_cbdata);

		if (irpp) {
			*((fct_i_remote_port_t **)ws.walk_cbdata) = NULL;

			mdb_printf("%p\n", irpp);
			if (verbose) {
				fct_i_remote_port_t irp;

				if (mdb_vread(&irp, sizeof (irp),
				    (uintptr_t)irpp) != sizeof (irp)) {
					mdb_warn("Unable to read in "
					    "fct_i_remote_port at %p\n", irpp);
					return (DCMD_ERR);
				}
				mdb_printf("  remote port: %p\n", irp.irp_rp);
				mdb_printf("  port id: %x\n", irp.irp_portid);
			}
		}
	}
	fct_irp_walk_f(&ws);

	return (DCMD_OK);
}

static uintptr_t cur_iport_for_irp_loop = NULL;

static fct_i_remote_port_t *
next_rport(struct fct_i_local_port *iport)
{
	static uint64_t cbdata = 0;
	static mdb_walk_state_t ws = {walk_fct_irp_cb, &cbdata};
	int ret;
	fct_i_remote_port_t *irp;

	if (ws.walk_addr == NULL || cur_iport_for_irp_loop !=
	    (uintptr_t)iport) {
		*((fct_i_remote_port_t **)ws.walk_cbdata) = NULL;
		cur_iport_for_irp_loop = (uintptr_t)iport;
		ws.walk_addr = (uintptr_t)iport;
		if (fct_irp_walk_i(&ws) == WALK_ERR) {
			fct_irp_walk_f(&ws);
			return (NULL);
		}
		if (ws.walk_addr == NULL) {
			fct_irp_walk_f(&ws);
			return (NULL);
		}
	}

	while ((ret = fct_irp_walk_s(&ws)) == WALK_NEXT) {
		if (*((fct_i_remote_port_t **)ws.walk_cbdata) != 0) {
			irp = *((fct_i_remote_port_t **)ws.walk_cbdata);
			*((fct_i_remote_port_t **)ws.walk_cbdata) = NULL;
			return (irp);
		}
	}
	fct_irp_walk_f(&ws);

	/*
	 * If it is WALK_DONE, there may be one remote port there
	 */
	if (ret == WALK_DONE) {
		irp = *((fct_i_remote_port_t **)ws.walk_cbdata);
		*((fct_i_remote_port_t **)ws.walk_cbdata) = NULL;
		return (irp);
	}
	return (NULL);
}

static struct stmf_i_local_port *
irp_to_ilport(struct fct_i_remote_port *irpp)
{
	struct fct_i_remote_port irp;
	struct fct_remote_port rp;
	struct fct_local_port port;
	struct stmf_local_port lport;

	if (mdb_vread(&irp, sizeof (struct fct_i_remote_port),
	    (uintptr_t)irpp)
	    != sizeof (struct fct_i_remote_port)) {
		mdb_warn("Unable to read in fct_i_remote_port\n");
		return (NULL);
	}
	if (mdb_vread(&rp, sizeof (struct fct_remote_port),
	    (uintptr_t)irp.irp_rp)
	    != sizeof (struct fct_remote_port)) {
		mdb_warn("Unable to read in fct_remote_port\n");
		return (NULL);
	}

	if (mdb_vread(&port, sizeof (struct fct_local_port),
	    (uintptr_t)rp.rp_port)
	    != sizeof (struct fct_local_port)) {
		mdb_warn("Unable to read in fct_local_port\n");
		return (NULL);
	}
	if (mdb_vread(&lport, sizeof (struct stmf_local_port),
	    (uintptr_t)port.port_lport)
	    != sizeof (struct stmf_local_port)) {
		mdb_warn("Unable to read in stmf_local_port\n");
		return (NULL);
	}
	return (lport.lport_stmf_private);
}

/*
 * by wwn, we may find more than one remote port, so we need to know its
 * corresponding local port
 */
static struct fct_i_remote_port *
find_irp_by_wwn(struct stmf_i_local_port *siport, uint8_t wwn[8])
{
	struct fct_i_local_port *fiport;
	fct_i_remote_port_t *irpp;
	struct fct_i_remote_port irp;
	struct fct_remote_port rp;
	fct_i_remote_port_t *ret = NULL;

	fiport = __ilport2iport((uintptr_t)siport, DCMD_ADDRSPEC, 0, NULL);
	if (fiport == NULL)
		return (NULL);

	while ((irpp = next_rport(fiport)) != NULL) {
		if (mdb_vread(&irp, sizeof (struct fct_i_remote_port),
		    (uintptr_t)irpp)
		    != sizeof (struct fct_i_remote_port)) {
			mdb_warn("Unable to read in fct_i_remote_port\n");
			break;
		}
		if (mdb_vread(&rp, sizeof (struct fct_remote_port),
		    (uintptr_t)irp.irp_rp)
		    != sizeof (struct fct_remote_port)) {
			mdb_warn("Unable to read in fct_remote_port\n");
			break;
		}

		if (memcmp(rp.rp_pwwn, wwn, 8) == 0) {
			ret = irpp;
			break;
		}
	}
	cur_iport_for_irp_loop = NULL;
	return (ret);
}

/*ARGSUSED*/
static int
stmf_find_fct_irp(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct stmf_i_local_port *siport;
	struct find_options *options;
	fct_i_remote_port_t *irpp;
	mdb_walk_state_t ws = {NULL, };

	options = parse_options(argc, argv);
	/* need to free options manually ? */
	if (options == NULL || (options->rpname_defined == 0 &&
	    options->rp_defined == 0)) {
		mdb_printf("rpname=<wwn.12345678> or rp=<3000586778734>"
		    " should be specified\n");
		return (DCMD_OK);
	}
	if (options->rpname_defined && options->rp_defined) {
		mdb_printf("rpname=<wwn.12345678> or rp=<3000586778734>"
		    " should be specified, but not both\n");
		return (DCMD_OK);
	}

	if (options->rp_defined) {
		siport = irp_to_ilport(options->rp);
		if (siport != NULL)
			mdb_printf("stmf_i_local_port=%p,"
			    " fct_i_remote_port=%p\n",
			    siport, options->rp);
		return (DCMD_OK);
	}

	/* if options->rpname_defined */
	while ((siport = next_stmf_port(&ws)) != NULL) {
		if ((irpp = find_irp_by_wwn(siport, options->rpname)) != NULL)
			mdb_printf("stmf_i_local_port=%p, "
			    "fct_i_remote_port=%p\n",
			    siport, irpp);
	}

	return (DCMD_OK);
}

typedef void (*cmd_filter_t) (struct fct_i_cmd *,
    struct find_options *, void *);

/*ARGSUSED*/
static void
print_tasks(struct fct_i_cmd *icmdp, struct find_options *options, void *arg)
{
	struct fct_i_cmd icmd;
	struct fct_cmd cmd;

	if (mdb_vread(&icmd, sizeof (struct fct_i_cmd),
	    (uintptr_t)icmdp) != sizeof (struct fct_i_cmd)) {
		mdb_warn("Unable to read in fct_i_cmd\n");
		return;
	}
	if (mdb_vread(&cmd, sizeof (struct fct_cmd),
	    (uintptr_t)icmd.icmd_cmd) != sizeof (struct fct_cmd)) {
		mdb_warn("Unable to read in fct_cmd\n");
		return;
	}

	if (cmd.cmd_type == FCT_CMD_FCP_XCHG) {
		struct scsi_task task;
		int colon_printed = 0;

		if (mdb_vread(&task, sizeof (struct scsi_task),
		    (uintptr_t)cmd.cmd_specific)
		    != sizeof (struct scsi_task)) {
			mdb_warn("Unable to read in scsi_task\n");
			return;
		}

		mdb_printf("%p", cmd.cmd_specific);
		if (options->show_task_flags) {
			mdb_printf(":");
			colon_printed = 1;
			mdb_printf(" task_flags=%x", task.task_flags);
		}

		if (options->show_lport) {
			if (colon_printed == 0) {
				mdb_printf(":");
				colon_printed = 1;
			}
			mdb_printf(" lport=%p", task.task_lport);
		}
		mdb_printf("\n");
	}
}

static void
print_tasks_on_rp(struct fct_i_cmd *icmdp, struct find_options *options,
    void *arg)
{
	struct fct_i_cmd icmd;
	struct fct_cmd cmd;
	fct_i_remote_port_t irp;

	if (mdb_vread(&icmd, sizeof (struct fct_i_cmd),
	    (uintptr_t)icmdp) != sizeof (struct fct_i_cmd)) {
		mdb_warn("Unable to read in fct_i_cmd\n");
		return;
	}
	if (mdb_vread(&cmd, sizeof (struct fct_cmd),
	    (uintptr_t)icmd.icmd_cmd) != sizeof (struct fct_cmd)) {
		mdb_warn("Unable to read in fct_cmd\n");
		return;
	}

	/* arg is a pointer to fct_i_remote_port */
	if (mdb_vread(&irp, sizeof (struct fct_i_remote_port),
	    (uintptr_t)arg) != sizeof (struct fct_i_remote_port)) {
		mdb_warn("Unable to read in fct_i_remote_port\n");
		return;
	}

	if (cmd.cmd_type == FCT_CMD_FCP_XCHG && cmd.cmd_rp == irp.irp_rp) {
		struct scsi_task task;
		int colon_printed = 0;

		if (mdb_vread(&task, sizeof (struct scsi_task),
		    (uintptr_t)cmd.cmd_specific)
		    != sizeof (struct scsi_task)) {
			mdb_warn("Unable to read in scsi_task\n");
			return;
		}

		mdb_printf("%p", cmd.cmd_specific);
		if (options->show_task_flags) {
			mdb_printf(":");
			colon_printed = 1;
			mdb_printf(" task_flags=%x", task.task_flags);
		}

		if (options->show_lport) {
			if (colon_printed == 0) {
				mdb_printf(":");
				colon_printed = 1;
			}
			mdb_printf(" lport=%p", task.task_lport);
		}
		mdb_printf("\n");
	}
}

/*ARGSUSED*/
static void
print_all_cmds(struct fct_i_cmd *icmd, struct find_options *options, void *arg)
{
	mdb_printf("%p\n", icmd);
}

/*
 * find outstanding cmds (fct_i_cmd) on local port
 */
static int
outstanding_cmds_on_lport(struct stmf_i_local_port *siport, cmd_filter_t filter,
    struct find_options *options, void *arg)
{
	struct fct_i_local_port *iportp;
	struct fct_i_local_port iport;
	struct fct_local_port port;
	struct fct_cmd_slot *slotp;
	struct fct_cmd_slot slot;
	int i;

	iportp = __ilport2iport((uintptr_t)siport, DCMD_ADDRSPEC, 0, NULL);
	if (iportp == NULL)
		return (DCMD_ERR);

	if (mdb_vread(&iport, sizeof (struct fct_i_local_port),
	    (uintptr_t)iportp) != sizeof (struct fct_i_local_port)) {
		mdb_warn("Unable to read in fct_i_local_port\n");
		return (DCMD_ERR);
	}
	if (mdb_vread(&port, sizeof (struct fct_local_port),
	    (uintptr_t)iport.iport_port) != sizeof (struct fct_local_port)) {
		mdb_warn("Unable to read in fct_local_port\n");
		return (DCMD_ERR);
	}

	slotp = iport.iport_cmd_slots;
	for (i = 0; i < port.port_max_xchges; i++) {
		if (mdb_vread(&slot, sizeof (struct fct_cmd_slot),
		    (uintptr_t)slotp) != sizeof (struct fct_cmd_slot)) {
			mdb_warn("Unable to read in fct_cmd_slot\n");
			return (DCMD_ERR);
		}
		if (slot.slot_cmd != NULL) {
			if (filter == NULL)
				mdb_printf("%p\n", slot.slot_cmd);
			else
				filter(slot.slot_cmd, options, arg);
		}
		slotp ++;
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
stmf_find_tasks(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct find_options *options;
	struct stmf_i_local_port *siport;

	options = parse_options(argc, argv);
	if (options == NULL ||
	    (options->lpname_defined == 0 && options->rpname_defined == 0)) {
		mdb_printf("lpname=<wwn.12345678> or rpname=<wwn.12345678>"
		    " should be specified\n");
		return (DCMD_OK);
	}

	if (options->lpname_defined) {
		siport = find_lport_by_wwn(options->lpname);
		if (siport == NULL)
			return (DCMD_ERR);

		outstanding_cmds_on_lport(siport, print_tasks, options, NULL);
		return (DCMD_OK);
	}

	if (options->rpname_defined) {
		mdb_walk_state_t ws = {NULL, };
		fct_i_remote_port_t *irpp;

		while ((siport = next_stmf_port(&ws)) != NULL) {
			if ((irpp = find_irp_by_wwn(siport, options->rpname))
			    != NULL) {
				outstanding_cmds_on_lport(siport,
				    print_tasks_on_rp, options, irpp);
			}
		}
	}

	return (DCMD_OK);
}

/*ARGSUSED*/
static int
fct_find_cmds(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct find_options *options;
	struct stmf_i_local_port *siport;

	options = parse_options(argc, argv);
	if (options == NULL || options->lpname_defined == 0) {
		mdb_printf("lpname=<wwn.12345678> should be specified\n");
		return (DCMD_OK);
	}

	siport = find_lport_by_wwn(options->lpname);
	if (siport == NULL)
		return (DCMD_ERR);

	outstanding_cmds_on_lport(siport, print_all_cmds, options, NULL);
	return (DCMD_OK);
}

static int
fct_icmds(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct fct_i_local_port iport;
	struct fct_i_cmd icmd;
	struct fct_i_cmd *icmdp;
	int i;
	int verbose = 0;

	for (i = 0; i < argc; i++) {
		char *ptr = (char *)argv[i].a_un.a_str;

		if (ptr[0] == '-')
			ptr++;
		while (*ptr) {
			if (*ptr == 'v')
				verbose = 1;
			ptr++;
		}
	}

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("fct_i_local_port_t address should be specified");
		return (DCMD_ERR);
	}

	if (mdb_vread(&iport, sizeof (struct fct_i_local_port), addr)
	    != sizeof (struct fct_i_local_port)) {
		mdb_warn("Unable to read in fct_i_local_port at %p\n", addr);
		return (DCMD_ERR);
	}

	icmdp = iport.iport_cached_cmdlist;
	while (icmdp) {
		if (mdb_vread(&icmd, sizeof (struct fct_i_cmd),
		    (uintptr_t)icmdp) == -1) {
			mdb_warn("failed to read fct_i_cmd at %p", icmdp);
			return (DCMD_ERR);
		}

		mdb_printf("%p\n", icmdp);
		if (verbose) {
			mdb_printf("  fct cmd: %p\n", icmd.icmd_cmd);
		}

		icmdp = icmd.icmd_next;
	}

	return (DCMD_OK);
}

/*
 * Walker to list the addresses of all the active STMF scsi tasks (scsi_task_t),
 * given a stmf_worker address
 *
 * To list all the active STMF scsi tasks, use
 * "::walk stmf_worker |::walk stmf_scsi_task"
 * To list the active tasks of a particular worker, use
 * <stmf_worker addr>::walk stmf_scsi_task
 */
static int
stmf_scsi_task_walk_init(mdb_walk_state_t *wsp)
{
	stmf_worker_t	worker;

	/*
	 * Input should be a stmf_worker, so read it to get the
	 * worker_task_head to get the start of the task list
	 */
	if (wsp->walk_addr == NULL) {
		mdb_warn("<worker addr>::walk stmf_scsi_task\n");
		return (WALK_ERR);
	}

	if (mdb_vread(&worker, sizeof (stmf_worker_t), wsp->walk_addr) !=
	    sizeof (stmf_worker_t)) {
		mdb_warn("failed to read in the task address\n");
		return (WALK_ERR);
	}

	wsp->walk_addr = (uintptr_t)(worker.worker_task_head);
	wsp->walk_data = mdb_alloc(sizeof (scsi_task_t), UM_SLEEP);

	return (WALK_NEXT);
}

static int
stmf_scsi_task_walk_step(mdb_walk_state_t *wsp)
{
	stmf_i_scsi_task_t	itask;
	int			status;

	if (wsp->walk_addr == NULL) {
		return (WALK_DONE);
	}

	/* Save the stmf_i_scsi_task for use later to get the next entry */
	if (mdb_vread(&itask, sizeof (stmf_i_scsi_task_t),
	    wsp->walk_addr) != sizeof (stmf_i_scsi_task_t)) {
		mdb_warn("failed to read stmf_i_scsi_task at %p",
		    wsp->walk_addr);
		return (WALK_DONE);
	}

	wsp->walk_addr = (uintptr_t)itask.itask_task;

	if (mdb_vread(wsp->walk_data, sizeof (scsi_task_t),
	    wsp->walk_addr) != sizeof (scsi_task_t)) {
		mdb_warn("failed to read scsi_task_t at %p", wsp->walk_addr);
		return (DCMD_ERR);
	}

	status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
	    wsp->walk_cbdata);

	wsp->walk_addr = (uintptr_t)(itask.itask_worker_next);

	return (status);
}

static void
stmf_scsi_task_walk_fini(mdb_walk_state_t *wsp)
{
	mdb_free(wsp->walk_data, sizeof (scsi_task_t));
}

/*ARGSUSED*/
static int
stmf_scsi_task(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	stmf_worker_t		worker;
	stmf_i_scsi_task_t	itask;
	scsi_task_t		*task_addr, task;

	/*
	 * A stmf_worker address is given to the left of ::stmf_scsi_task
	 * i.e. display the scsi_task for the given worker
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("stmf_worker", "stmf_scsi_task", argc,
		    argv) == -1) {
			mdb_warn("Failed to walk the stmf_scsi_task entries");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%-19s %-10s %-19s%</u>\n",
		    "scsi_task_t", "Flags", "LPort");
	}

	if (mdb_vread(&worker, sizeof (stmf_worker_t),
	    addr) != sizeof (stmf_worker_t)) {
		mdb_warn("failed to read in the worker address");
		return (DCMD_ERR);
	}

	/* Read the scsi_task */
	if (worker.worker_task_head == NULL) {
		return (DCMD_OK);
	}

	if (mdb_vread(&itask, sizeof (stmf_i_scsi_task_t),
	    (uintptr_t)worker.worker_task_head) == -1) {
		mdb_warn("failed to read stmf_i_scsi_task_t at %p",
		    worker.worker_task_head);
		return (DCMD_ERR);
	}

	task_addr = itask.itask_task;

	if (mdb_vread(&task, sizeof (scsi_task_t),
	    (uintptr_t)task_addr) != sizeof (scsi_task_t)) {
		mdb_warn("failed to read scsi_task_t at %p", task_addr);
		return (DCMD_ERR);
	}

	if ((flags & DCMD_PIPE_OUT)) {
		mdb_printf("%p\n", task_addr);
	} else {
		/* pretty print */
		mdb_printf("%-19p %-10x %-19p\n",
		    task_addr, task.task_flags, task.task_lport);
	}

	return (DCMD_OK);
}

/*
 * Walker to list the addresses of all the stmf_worker in the queue
 */
typedef struct stmf_worker_walk_data {
	int		worker_current;
	int		worker_count;
} stmf_worker_walk_data_t;

/* stmf_workers_state definition from stmf.c (static) */
enum {
	STMF_WORKERS_DISABLED = 0,
	STMF_WORKERS_ENABLING,
	STMF_WORKERS_ENABLED
} stmf_workers_state;

/*
 * Initialize the stmf_worker_t walker by either using the given starting
 * address, or reading the value of the kernel's global stmf_workers pointer.
 */
/*ARGSUSED*/
static int
stmf_worker_walk_init(mdb_walk_state_t *wsp)
{
	int			worker_state;
	int			nworkers;
	stmf_worker_t		*worker;
	stmf_worker_walk_data_t	*walk_data;

	if (mdb_readvar(&worker_state, "stmf_workers_state") == -1) {
		mdb_warn("failed to read stmf_workers_state");
		return (WALK_ERR);
	}
	if (worker_state != STMF_WORKERS_ENABLED) {
		mdb_warn("stmf_workers_state not initialized");
		return (WALK_ERR);
	}

	/*
	 * Look up the stmf_nworkers_accepting_cmds to
	 * determine number of entries in the worker queue
	 */
	if (mdb_readvar(&nworkers, "stmf_nworkers_accepting_cmds") == -1) {
		mdb_warn("failed to read stmf_nworkers_accepting_cmds");
		return (WALK_ERR);
	}

	if (mdb_readvar(&worker, "stmf_workers") == -1) {
		mdb_warn("failed to read stmf_workers");
		return (WALK_ERR);
	}

	walk_data = mdb_alloc(sizeof (stmf_worker_walk_data_t), UM_SLEEP);
	walk_data->worker_current	= 0;
	walk_data->worker_count		= nworkers;

	wsp->walk_addr = (uintptr_t)worker;
	wsp->walk_data = walk_data;

	return (WALK_NEXT);
}

static int
stmf_worker_walk_step(mdb_walk_state_t *wsp)
{
	stmf_worker_walk_data_t	*walk_data = wsp->walk_data;
	int			status;

	if (wsp->walk_addr == NULL) {
		return (WALK_DONE);
	}

	if (walk_data->worker_current >= walk_data->worker_count) {
		return (WALK_DONE);
	}

	status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
	    wsp->walk_cbdata);

	walk_data->worker_current++;
	wsp->walk_addr += sizeof (stmf_worker_t);

	return (status);
}

static void
stmf_worker_walk_fini(mdb_walk_state_t *wsp)
{
	mdb_free(wsp->walk_data, sizeof (stmf_worker_walk_data_t));
}

int
stmf_worker(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	stmf_worker_t		worker;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("stmf_worker", "stmf_worker", argc,
		    argv) == -1) {
			mdb_warn("Failed to walk the stmf_worker entries");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	if (mdb_vread(&worker, sizeof (stmf_worker_t),
	    addr) != sizeof (stmf_worker_t)) {
		mdb_warn("failed to read stmf_worker at %p", addr);
		return (DCMD_ERR);
	}

	if (flags & DCMD_PIPE_OUT) {
		mdb_printf("%-19p\n", addr);
	} else {
		/* pretty print */
		if (DCMD_HDRSPEC(flags)) {
			mdb_printf("%<u>%-19s %-10s %-10s %-10s%</u>\n",
			    "stmf_worker_t", "State", "Ref_Count", "Tasks");
		}

		mdb_printf("%-19p %-10s %-10d %-5d%\n", addr,
		    (worker.worker_flags == STMF_WORKER_STARTED) ? "STARTED" :
		    (worker.worker_flags & STMF_WORKER_ACTIVE) ?
		    "ACTIVE" : "TERMINATED",
		    worker.worker_ref_count,
		    worker.worker_queue_depth);
	}

	return (DCMD_OK);
}

struct find_options *
parse_options(int argc, const mdb_arg_t *argv)
{
	int i;
	struct find_options *options;
	int len;
	char *ptr;
	int ret;

	if (argc == 0)
		return (NULL);
	options = mdb_zalloc(sizeof (struct find_options), UM_SLEEP);
	for (i = 0; i < argc; i++) {
		switch (argv[i].a_type) {
		case MDB_TYPE_STRING:
			break;
		case MDB_TYPE_IMMEDIATE:
		case MDB_TYPE_CHAR:
			mdb_printf("unknown type\n");
		}
		if ((ptr = strchr(argv[i].a_un.a_str, '=')) == NULL) {
			mdb_printf("invalid argument: %s\n",
			    argv[i].a_un.a_str);
			goto out;
		}
		len = ptr - argv[i].a_un.a_str;
		ptr++;	/* point to value now */

		if (len == strlen("lpname") &&
		    strncmp(argv[i].a_un.a_str, "lpname", len) == 0) {
			if (strstr(ptr, "wwn.") == ptr)
				ptr += 4;
			ret = string2wwn(ptr, options->lpname);
			if (ret == -1)
				goto out;
#if 0
	mdb_printf("wwn=%02x%02x%02x%02x%02x%02x%02x%02x\n",
	    wwn[0], wwn[1], wwn[2], wwn[3], wwn[4], wwn[5], wwn[6], wwn[7]);
#endif
			options->lpname_defined = 1;

		} else if (len == strlen("rp") &&
		    strncmp(argv[i].a_un.a_str, "rp", len) == 0) {
			options->rp_defined = 1;
			options->rp =
			    (void *)(unsigned long)mdb_strtoull(ptr);

		} else if (len == strlen("rpname") &&
		    strncmp(argv[i].a_un.a_str, "rpname", len) == 0) {
			if (strstr(ptr, "wwn.") == ptr)
				ptr += 4;
			ret = string2wwn(ptr, options->rpname);
			if (ret == -1)
				goto out;
			options->rpname_defined = 1;

		} else if (len == strlen("show") &&
		    strncmp(argv[i].a_un.a_str, "show", len) == 0) {
			char *s;
			int l;

			for (;;) {
				s = strchr(ptr, ',');
				if (s)
					l = s - ptr;
				else
					l = strlen(ptr);
				if (l == strlen("task_flags") &&
				    strncmp(ptr, "task_flags", l) == 0)
					options->show_task_flags = 1;
				else if (l == strlen("lport") &&
				    strncmp(ptr, "lport", l) == 0)
					options->show_lport = 1;
				else {
					mdb_printf("unknown shower: %s\n",
					    ptr);
					goto out;
				}
				if (s == NULL)
					break;
				ptr = s + 1;
			}
		} else {
			mdb_printf("unknown argument: %s\n",
			    argv[i].a_un.a_str);
			goto out;
		}
	}

	return (options);
out:
	mdb_free(options, sizeof (struct find_options));
	return (NULL);
}

int
string2wwn(const char *s, uint8_t wwn[8])
{
	int i;
	char tmp[17];
	char *p;

	if (strlen(s) > 16) {
		mdb_printf("invalid wwn %s\n", s);
		return (-1);
	}

	strcpy(tmp, s);
	p = tmp + strlen(tmp) - 2;
	memset(wwn, 0, 8);
	/* figure out wwn from the tail to beginning */
	for (i = 7; i >= 0 && p >= tmp; i--, p -= 2) {
		wwn[i] = mdb_strtoull(p);
		*p = 0;
	}
	return (0);
}
void
stmf_luid_help(void)
{
	mdb_printf(
	    "Prints the details about LU GUIDs and its View Entries\n"
	    "Usage:  [addr]::stmf_luid [-ve]\n"
	    "Options:\n"
	    "   -v   verbose output\n"
	    "   -e   prints view entry details\n");
}

void
fct_find_cmds_help(void)
{
	mdb_printf(
	    "Find all cached fct_i_cmd_t for a local port. If a local port \n"
	    "name is specified, find all pending cmds for it and print the \n"
	    "address. Example:\n"
	    "    fct_find_cmds lpname=<wwn.12345678 or 12345678>\n");
}
void
stmf_find_ilport_help(void)
{
	mdb_printf(
	    "Find the fct_i_local_port if local port name is "
	    "specified. Example:\n"
	    "    stmf_find_ilport lpname=<wwn.12345678 or 12345678>\n");
}
void
stmf_find_fct_irp_help(void)
{
	mdb_printf(
	    "If a remote port name or stmf_i_remote_port_t address is\n"
	    "specified, loop through all local ports, to which this remote \n"
	    "port has logged in, print address for stmf_i_local_port_t and \n"
	    "stmf_i_remote_port. Example:\n"
	    "    stmf_find_fct_irp rpname=<wwn.12345678 or 12345678>\n"
	    "    stmf_find_fct_irp rp=<3000586778734>\n");
}

void
stmf_find_tasks_help(void)
{
	mdb_printf(
	    "Find all pending scsi_task_t for a given local port and/or\n"
	    "remote port. Various different fields for each task are printed\n"
	    "depending on what is requested. Example:\n"
	    "    stmf_find_tasks rpname=<wwn.12345678 or 12345678>\n"
	    "    stmf_find_tasks lpname=<wwn.12345678 or 12345678> "
	    "show=task_flags,lport\n");
}

void
stmf_scsi_task_help(void)
{
	mdb_printf(
	    "List all active scsi_task_t on a given stmf_worker_t. Example\n"
	    "    addr::stmf_scsi_task\n");
}

static const mdb_dcmd_t dcmds[] = {
	{ "stmf_ilports", "[-v]",
	    "Print a list of stmf_i_local_port", stmf_ilports },
	{ "ilport2iport", "?[-v]",
	    "Convert stmf_i_local_port to corresponding fct_i_local_port",
	    ilport2iport },
	{ "stmf_iss", "?[-v]",
	    "List all active sessions for a given local port",
	    stmf_iss },
	{ "stmf_ilus", "[-v]", "Print a list of stmf_i_lu", stmf_ilus },
	{ "stmf_i_lu_providers", "[-v]",
	    "Print a list of stmf_i_lu_provider", stmf_i_lu_providers },
	{ "stmf_i_port_providers", "[-v]",
	    "Print a list of stmf_i_port_provider", stmf_i_port_providers },
	{ "stmf_hg", "?[-v]", "Print the list of host groups", stmf_hg },
	{ "stmf_tg", "?[-v]", "Print the list of target groups", stmf_tg },
	{ "stmf_luid", "?[-ve]", "Print the list of lu UIDs", stmf_luid,
	    stmf_luid_help },
	{ "stmf_lun_map", "?", "Print the lun map", stmf_lun_map },
	{ "fct_irps", "?[-v]",
	    "Print all fct_i_remote_port for a given fct_i_local_port",
	    fct_irps },
	{ "fct_icmds", "?[-v]",
	    "Print all cached fct_i_cmd_t on fct_i_local_port",
	    fct_icmds },
	{ "fct_find_cmds", "lpname",
	    "Find all fct_i_cmd_t for a given local port",
	    fct_find_cmds, fct_find_cmds_help},
	{ "stmf_find_ilport", "lpname",
	    "Find local port information based on its wwn",
	    stmf_find_ilport, stmf_find_ilport_help},
	{ "stmf_find_fct_irp", "rpname|rp",
	    "Print fct remote port information based on its wwn",
	    stmf_find_fct_irp, stmf_find_fct_irp_help},
	{ "stmf_find_tasks", "lpname|rpname [show]",
	    "Find all pending task for a local port or remote port",
	    stmf_find_tasks, stmf_find_tasks_help},
	{ "stmf_worker", "?", "List all the stmf_worker entries", stmf_worker},
	{ "stmf_scsi_task", ":",
	    "List all the active STMF SCSI tasks per worker", stmf_scsi_task,
	    stmf_scsi_task_help},
	{ NULL }
};

static const mdb_walker_t walkers[] = {
	{ "stmf_worker", "Walk STMF worker queue", stmf_worker_walk_init,
	    stmf_worker_walk_step, stmf_worker_walk_fini},
	{ "stmf_scsi_task", "Walk active STMF SCSI tasks per worker",
	    stmf_scsi_task_walk_init,
	    stmf_scsi_task_walk_step, stmf_scsi_task_walk_fini },
	{ "stmf_id_data", "Walk stmf id data list", stmf_id_data_walk_init,
	    stmf_id_data_walk_step, NULL},
	{ "stmf_ve", "Walk stmf view entry list", stmf_ve_walk_init,
	    stmf_ve_walk_step, NULL},
	{ "stmf_ver_hg", "Walk stmf view entry list", stmf_ver_hg_walk_init,
	    stmf_ver_hg_walk_step, NULL},
	{ "stmf_ver_tg", "Walk stmf view entry list", stmf_ver_tg_walk_init,
	    stmf_ver_tg_walk_step, NULL},
	{ NULL }
};

static const mdb_modinfo_t modinfo = {
	MDB_API_VERSION, dcmds, walkers
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
