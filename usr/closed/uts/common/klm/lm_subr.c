/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 *
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is general subroutines used by both the server and client side of
 * the Lock Manager.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/netconfig.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/schedctl.h>
#include <sys/user.h>
#include <sys/cmn_err.h>
#include <sys/pathname.h>
#include <sys/utsname.h>
#include <sys/flock.h>
#include <sys/share.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpcsvc/sm_inter.h>
#include <nfs/lm.h>
#include <nfs/lm_impl.h>
#include <nfs/lm_server.h>
#include <sys/varargs.h>
#include <sys/zone.h>
#include <sys/id_space.h>

/*
 * Convert negative values to -1, positive values to +1, and leave zero
 * alone.
 */
#define	SCALE(n)	((n) == 0 ? (n) : ((n) > 0 ? 1 : -1))

/*
 * Clustering hook: set to non-NULL on systems booted as part of a cluster.
 * Function returns 1 (true) if file locks are held by a remote client
 * identified by argument 1 at any Local Lock Manager (LLM).  returns 0
 * (false) otherwise.
 */
int (*lm_has_file_locks)(int, int) = NULL;

/*
 * Clustering hook: set to non-NULL on systems booted as part of a cluster.
 * Function attempts to free elements in the lm_sysid kernel cache. Called
 * ONLY when the kmem_cache_reap thread is run by the VM system to free
 * up memory when the VM has determined that memory is low.  It spawns
 * a cluster deferred task that will call base Solaris routine
 * lm_free_sysid_table() to free sysids in a separate thread.
 * This prevents the reaper thread from blocking at a crucial time.
 */
void (*lm_free_nlm_sysid_table)(void) = NULL;

/*
 * Definitions and declarations.
 */
struct			lm_client;	/* forward reference only */
struct lm_stat		lm_stat;
zone_key_t		lm_zone_key;
list_t			lm_global_list;
kmutex_t		lm_global_list_lock;

/*
 * DEFAULT_RECLAIM_FACTOR is a value which is multiplied by hz.  The product
 * is passed into delay_sig which delay for the specified number of ticks.
 *
 * lm_reclaim_factor is a tunable which can be modified to adjust the
 * behavior of the delay.
 */
#define	DEFAULT_RECAIM_FACTOR 2
int lm_reclaim_factor = DEFAULT_RECAIM_FACTOR;

/*
 * For the benefit of warlock.  The "scheme" that protects lm_sa is
 * that it is only modified at initialization in lm_svc().  While multiple
 * kernel threads may modify it simultaneously, they are all writing the
 * same data to it - all other accesses are reads.
 */
#ifndef lint
_NOTE(SCHEME_PROTECTS_DATA("LM svc init", lm_globals::lm_sa))
#endif

/*
 * When trying to contact a portmapper that doesn't speak the version we're
 * using, we should theoretically get back RPC_PROGVERSMISMATCH.
 * Unfortunately, some (all?) 4.x hosts return an accept_stat of
 * PROG_UNAVAIL, which gets mapped to RPC_PROGUNAVAIL, so we have to check
 * for that, too.
 */
#define	PMAP_WRONG_VERSION(s)	((s) == RPC_PROGVERSMISMATCH || \
	(s) == RPC_PROGUNAVAIL)

/*
 * Function prototypes.
 */
#ifdef LM_DEBUG_SUPPORT
static void lm_hex_byte(zoneid_t, int);
#endif /* LM_DEBUG_SUPPORT */
static void lm_clnt_destroy(CLIENT **);
static void lm_rel_client(lm_globals_t *, struct lm_client *, int);
static int nbcmp(const struct netbuf *, const struct netbuf *);
static enum clnt_stat lm_get_client(lm_globals_t *, struct lm_sysid *,
    rpcprog_t, rpcvers_t, int, struct lm_client **, bool_t, bool_t, bool_t);
static enum clnt_stat lm_get_port(struct lm_sysid *, struct knetconfig *,
    rpcprog_t, rpcvers_t, struct netbuf *, int);
static enum clnt_stat lm_get_rpc_handle(struct lm_sysid *, struct knetconfig *,
    struct netbuf *, rpcprog_t, rpcvers_t, int, bool_t, bool_t,
    CLIENT **clientp);
static int ls_has_locks(struct lm_sysid *);
static void ls_destroy(lm_globals_t *, struct lm_sysid *);
static int in6cmp(struct sockaddr_in6 *, struct sockaddr_in6 *);

/*
 * Debug routines.
 */
#ifdef LM_DEBUG_SUPPORT

static void
lm_hex_byte(zoneid_t zoneid, int n)
{
	static char hex[] = "0123456789ABCDEF";
	int i;

	zprintf(zoneid, " ");
	if ((i = (n & 0xF0) >> 4) != 0)
		zprintf(zoneid, "%c", hex[i]);
	zprintf(zoneid, "%c", hex[n & 0x0F]);
}

/*PRINTFLIKE3*/
void
lm_debug(int level, char *function, const char *fmt, ...)
{
	va_list adx;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_sa.debug >= level) {
		zprintf(lm->lm_zoneid,
		    "%" PRIx64 " %s:\t", curthread->t_did, function);
		va_start(adx, fmt);
		vprintf(fmt, adx);
		va_end(adx);
		zprintf(lm->lm_zoneid, "\n");
	}
}

/*
 * print an alock structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_alock(int level, char *function, nlm_lock *alock)
{
	int i;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_sa.debug >= level) {
		zprintf(lm->lm_zoneid,
		    "%" PRIx64 " %s:\tcaller= %s, svid= %u, offset= %u, "
		    "len= %u", curthread->t_did, function,
		    alock->caller_name, alock->svid, alock->l_offset,
		    alock->l_len);
		zprintf(lm->lm_zoneid, "\nfh=");

		for (i = 0;  i < alock->fh.n_len;  i++)
			lm_hex_byte(lm->lm_zoneid, alock->fh.n_bytes[i]);
		zprintf(lm->lm_zoneid, "\n");
	}
}

/*
 * print a shareargs structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_d_nsa(int level, char *function, nlm_shareargs *nsa)
{
	int i;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_sa.debug >= level) {
		zprintf(lm->lm_zoneid,
		    "%" PRIx64 " %s:\tcaller= %s, mode= %d, access= %d, "
		    "reclaim= %d", curthread->t_did, function,
		    nsa->share.caller_name, nsa->share.mode,
		    nsa->share.access, nsa->reclaim);
		zprintf(lm->lm_zoneid, "\nfh=");

		for (i = 0;  i < nsa->share.fh.n_len;  i++)
			lm_hex_byte(lm->lm_zoneid, nsa->share.fh.n_bytes[i]);
		zprintf(lm->lm_zoneid, "\noh=");

		for (i = 0;  i < nsa->share.oh.n_len;  i++)
			lm_hex_byte(lm->lm_zoneid, nsa->share.oh.n_bytes[i]);
		zprintf(lm->lm_zoneid, "\n");
	}
}

void
lm_n_buf(int level, char *function, char *name, struct netbuf *addr)
{
	int	i;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_sa.debug >= level) {
		zprintf(lm->lm_zoneid,
		    "%" PRIx64 " %s:\t%s=", curthread->t_did, function, name);
		if (! addr)
			zprintf(lm->lm_zoneid, "(NULL)\n");
		else {
			for (i = 0;  i < addr->len;  i++)
				lm_hex_byte(lm->lm_zoneid,
				    ((char *)addr->buf)[i]);
			zprintf(lm->lm_zoneid, " (%d)\n", addr->maxlen);
		}
	}
}

/*
 * print an alock structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_alock4(int level, char *function, nlm4_lock *alock)
{
	int i;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_sa.debug >= level) {
		zprintf(lm->lm_zoneid,
		    "%" PRIx64 " %s:\tcaller= %s, svid= %u, offset= %llu, "
		    "len= %llu", curthread->t_did, function,
		    alock->caller_name, alock->svid, alock->l_offset,
		    alock->l_len);
		zprintf(lm->lm_zoneid, "\nfh=");

		for (i = 0; i < alock->fh.n_len; i++)
			lm_hex_byte(lm->lm_zoneid, alock->fh.n_bytes[i]);
		zprintf(lm->lm_zoneid, "\n");
	}
}

/*
 * print a shareargs structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_d_nsa4(int level, char *function, nlm4_shareargs *nsa)
{
	int i;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	if (lm->lm_sa.debug >= level) {
		zprintf(lm->lm_zoneid,
		    "%" PRIx64 " %s:\tcaller= %s, mode= %d, access= %d, "
		    "reclaim= %d", curthread->t_did, function,
		    nsa->share.caller_name, nsa->share.mode,
		    nsa->share.access, nsa->reclaim);
		zprintf(lm->lm_zoneid, "\nfh=");

		for (i = 0;  i < nsa->share.fh.n_len;  i++)
			lm_hex_byte(lm->lm_zoneid, nsa->share.fh.n_bytes[i]);
		zprintf(lm->lm_zoneid, "\noh=");

		for (i = 0;  i < nsa->share.oh.n_len;  i++)
			lm_hex_byte(lm->lm_zoneid, nsa->share.oh.n_bytes[i]);
		zprintf(lm->lm_zoneid, "\n");
	}
}
#endif /*  LM_DEBUG_SUPPORT */

/*
 * Utilities
 */

char *
lm_dup(char *str, size_t len)
{
	char *s = kmem_zalloc(len, KM_SLEEP);
	bcopy(str, s, len);
	return (s);
}

/*
 * Fill in the given byte array with the owner handle that corresponds to
 * the given pid.  If you change this code, be sure to update LM_OH_LEN.
 */
/*ARGSUSED*/
void
lm_set_oh(lm_globals_t *lm, char *array, size_t array_len, pid_t pid)
{
	char *ptr;

	ASSERT(array_len >= LM_OH_LEN);
	ptr = array;
	bcopy(&pid, ptr, sizeof (pid));
	ptr += sizeof (pid);
	bcopy(&lm->lm_owner_handle_sys, ptr, LM_OH_SYS_LEN);
}

/*
 * Returns a reference to the lm_sysid for myself.  This is a loopback
 * kernel RPC endpoint used to talk with our own statd.
 */
struct lm_sysid *
lm_get_me(void)
{
	int error;
	struct knetconfig config;
	struct netbuf addr;
	char keyname[SYS_NMLN + 16];
	struct vnode *vp;
	struct lm_sysid *ls;
	static bool_t found_xprt = TRUE;

	config.knc_semantics = NC_TPI_COTS_ORD;
	config.knc_protofmly = NC_LOOPBACK;
	config.knc_proto = NC_NOPROTO;
	error = lookupname("/dev/ticotsord", UIO_SYSSPACE, FOLLOW, NULLVPP,
	    &vp);
	if (error != 0) {
		/*
		 * This can fail if the loopback transport is not properly
		 * configured, or (more likely) if the process
		 * has invoked chroot() and there is no such device
		 * in the new hierarchy.  This means our statd
		 * is out of touch for now.
		 *
		 * `found_xprt' simply prevents us from printing
		 * the warning message more often than would be
		 * friendly.  It isn't critical enough to warrant
		 * mutex protection.
		 */
		if (found_xprt == TRUE) {
			found_xprt = FALSE;
			nfs_cmn_err(error, CE_WARN,
			"lockd: can't get loopback transport (%m), continuing");
		}
		return ((struct lm_sysid *)NULL);
	}
	found_xprt = TRUE;

	config.knc_rdev = vp->v_rdev;
	VN_RELE(vp);

	/*
	 * Get a unique (node,service) name from which we
	 * build up a netbuf.
	 */
	(void) strcpy(keyname, uts_nodename());
	(void) strcat(keyname, ".");
	addr.buf = keyname;
	addr.len = addr.maxlen = (unsigned int)strlen(keyname);

	LM_DEBUG((8, "get_me", "addr = %s", (char *)addr.buf));

	ls = lm_get_sysid(&config, &addr, "me", NULL);

	if (ls == NULL) {
		panic("lm_get_me: cached entry not found");
		/*NOTREACHED*/
	}

	return (ls);
}

/*
 * Taking the netconfig and address together, compare two host addresses.
 * Port information is ignored if chkport is LM_IGN_PORT.  Returns -1, 0,
 * or +1 (less than, equal, or greater than).  The definition of
 * less/greater than is somewhat arbitrary.  To return equal, the address
 * family and address must match, but the protocol may be different in some
 * cases (e.g., TCP and UDP are treated as equivalent).  If the addresses
 * are same except for the protocol, *protochgp is set to TRUE (assuming
 * protochgp is non-NULL).
 */

typedef enum {LM_CHECK_PORT, LM_IGN_PORT} lm_cmpport_t;

int
lm_cmp_addr(struct knetconfig *config1, struct netbuf *addr1,
	struct knetconfig *config2, struct netbuf *addr2,
	lm_cmpport_t chkport, bool_t *protochgp)
{
	struct sockaddr_in *si1;
	struct sockaddr_in *si2;
	struct sockaddr_in6 *s6i1;
	struct sockaddr_in6 *s6i2;
	int result;

	result = strcmp(config1->knc_protofmly, config2->knc_protofmly);
	if (result != 0)
		return (SCALE(result));

	if (strcmp(config1->knc_protofmly, NC_INET) == 0) {
		/*
		 * See if the IP address matches. Netconfig could be different
		 * if klmmod chose to use different protocol because lockd was
		 * not registered with the same protocol as nfs.
		 */
		si1 = (struct sockaddr_in *)(addr1->buf);
		si2 = (struct sockaddr_in *)(addr2->buf);
		if (si1->sin_family < si2->sin_family)
			return (-1);
		else if (si1->sin_family > si2->sin_family)
			return (1);
		if (si1->sin_addr.s_addr < si2->sin_addr.s_addr)
			return (-1);
		else if (si1->sin_addr.s_addr > si2->sin_addr.s_addr)
			return (1);
		if (chkport == LM_CHECK_PORT) {
			if (si1->sin_port > si2->sin_port)
				return (1);
			if (si1->sin_port < si2->sin_port)
				return (-1);
		}
		if (strcmp(config1->knc_proto, config2->knc_proto) != 0) {
			/* Protocol is changed */
			if (protochgp != NULL)
				*protochgp = B_TRUE;
		}
	} else if (strcmp(config1->knc_protofmly, NC_INET6) == 0) {
		s6i1 = (struct sockaddr_in6 *)(addr1->buf);
		s6i2 = (struct sockaddr_in6 *)(addr2->buf);
		result = in6cmp(s6i1, s6i2);
		if (result != 0)
			return (result);
		if (chkport == LM_CHECK_PORT) {
			if (s6i1->sin6_port > s6i2->sin6_port)
				return (1);
			if (s6i1->sin6_port < s6i2->sin6_port)
				return (-1);
		}
		if (strcmp(config1->knc_proto, config2->knc_proto) != 0) {
			/* Protocol is changed */
			if (protochgp != NULL)
				*protochgp = B_TRUE;
		}
	} else if (strcmp(config1->knc_protofmly, NC_LOOPBACK) == 0) {
		/*
		 * Loopback addresses can only refer to the current host.
		 */
		if (chkport == LM_CHECK_PORT) {
			result = nbcmp(addr1, addr2);
			if (result != 0)
				return (result);
		}
		if (strcmp(config1->knc_proto, config2->knc_proto) != 0) {
			/* Protocol is changed */
			if (protochgp != NULL)
				*protochgp = B_TRUE;
		}
	} else {
		panic("lm_cmp_addr: unsupported protocol family (%s)",
		    config1->knc_protofmly);
	}

	return (result);
}

/*
 * Comparison routine for lm_sysid's.  Returns -1, 0, or +1, if the first
 * is less than, equal to, or greater than the second, using a somewhat
 * arbitrary definition of "less than" and "greater than".
 */
static int
lm_cmp_sysid(const void *p1, const void *p2)
{
	struct lm_sysid *ls1 = (struct lm_sysid *)p1;
	struct lm_sysid *ls2 = (struct lm_sysid *)p2;
	int result;

	/*
	 * To avoid deadlock, lock the two lm_sysids in increasing address
	 * order.
	 */
	ASSERT(p1 != p2);
	if ((uintptr_t)p1 < (uintptr_t)p2) {
		mutex_enter(&ls1->lock);
		mutex_enter(&ls2->lock);
	} else {
		mutex_enter(&ls2->lock);
		mutex_enter(&ls1->lock);
	}

	result = lm_cmp_addr(&ls1->config, &ls1->addr, &ls2->config,
	    &ls2->addr, LM_IGN_PORT, NULL);

	mutex_exit(&ls1->lock);
	mutex_exit(&ls2->lock);

	return (result);
}

/*
 * Routine to compare two IPv6 addresses, returning -1, 0, or 1 (less than,
 * equal, greater than), for some definition of less than and greater
 * than.
 */
static int
in6cmp(struct sockaddr_in6 *a1, struct sockaddr_in6 *a2)
{
	uint32_t *p1, *p2;
	int i;

	if (a1->sin6_scope_id < a2->sin6_scope_id)
		return (-1);
	else if (a1->sin6_scope_id > a2->sin6_scope_id)
		return (1);

	p1 = a1->sin6_addr._S6_un._S6_u32;
	p2 = a2->sin6_addr._S6_un._S6_u32;
	for (i = 3; i >= 0; i--) {
		if (p1[i] < p2[i])
			return (-1);
		else if (p1[i] > p2[i])
			return (1);
	}

	return (0);
}

/*
 * Return -1, 0, or 1 if the first byte string is less than, equal to, or
 * greater than the second one.
 */
static int
nbcmp(const struct netbuf *addr1, const struct netbuf *addr2)
{
	uchar_t c1, c2;
	int i;

	if (addr1->len > addr2->len)
		return (1);
	if (addr1->len < addr2->len)
		return (-1);
	for (i = 0; i < addr1->len; ++i) {
		c1 = ((uchar_t *)addr1->buf)[i];
		c2 = ((uchar_t *)addr2->buf)[i];
		if (c1 > c2)
			return (1);
		if (c1 < c2)
			return (-1);
	}

	return (0);
}

/*
 * Returns non-zero if the two netobjs have the same contents, zero if they
 * do not.
 */
int
lm_netobj_eq(netobj *obj1, netobj *obj2)
{
	if (obj1->n_len != obj2->n_len)
		return (0);
	/*
	 * Lengths are equal if we get here. Thus if obj1->n_len == 0, then
	 * obj2->n_len == 0. If both lengths are 0, the objects are
	 * equal.
	 */
	if (obj1->n_len == 0)
		return (1);
	return (bcmp(obj1->n_bytes, obj2->n_bytes, obj1->n_len) == 0);
}

/*
 * lm_copy_config():
 *	Copies config from one place to another.
 */
void
lm_copy_config(struct knetconfig *config, const struct knetconfig *sconfig)
{
#ifdef DEBUG
	if (strncmp(sconfig->knc_proto, "udp", strlen("udp")) == 0) {
		ASSERT(sconfig->knc_semantics == NC_TPI_CLTS);
	}
	if (strncmp(sconfig->knc_proto, "tcp", strlen("tcp")) == 0) {
		ASSERT(sconfig->knc_semantics == NC_TPI_COTS_ORD);
	}
#endif
	config->knc_semantics = sconfig->knc_semantics;
	config->knc_protofmly = lm_dup(sconfig->knc_protofmly,
	    strlen(sconfig->knc_protofmly) + 1);
	config->knc_proto = lm_dup(sconfig->knc_proto,
	    strlen(sconfig->knc_proto) + 1);
	config->knc_rdev = sconfig->knc_rdev;
}

/*
 * lm_dup_config(): copy "config" to member mi->mi_klmconfig.
 */
void
lm_dup_config(const struct knetconfig *config, mntinfo_t *mi)
{

	ASSERT(mi != NULL);
	mutex_enter(&mi->mi_lock);
	if (mi->mi_klmconfig == NULL)
		mi->mi_klmconfig = kmem_zalloc(sizeof (struct knetconfig),
		    KM_SLEEP);
	else
		lm_free_config(mi->mi_klmconfig);

	LM_DEBUG((7, "dup_config", "config = %p klmconfig = %p",
	    (void *)config, (void *)mi->mi_klmconfig));
	lm_copy_config(mi->mi_klmconfig, config);
	mutex_exit(&mi->mi_lock);
}

/*
 * lm_free_config():
 * Frees up strings pointed by knetconfig
 */
void
lm_free_config(struct knetconfig *config)
{
	caddr_t p;

	ASSERT(config != NULL);
	if ((p = config->knc_protofmly) != NULL)
		kmem_free(p, strlen(p)+1);
	config->knc_protofmly = NULL;
	if ((p = config->knc_proto) != NULL)
		kmem_free(p, strlen(p)+1);
	config->knc_proto = NULL;
}

id_space_t *lmsysid_space;		/* for sysid_t allocation */

/*
 * Initialization for the code that deals with lm_sysids.
 */
void
lm_sysid_init()
{
	lmsysid_space = id_space_create("lmsysid_space", LM_SYSID,
	    LM_SYSID_MAX+1);
}

/*
 * Inverse of lm_sysid_init(): clean it up.
 */
void
lm_sysid_fini()
{
	id_space_destroy(lmsysid_space);
}

/*
 * lm_get_sysid
 * Returns a reference to the sysid associated with the knetconfig
 * and address.
 *
 * If name == NULL then set name to "NoName".
 *
 * netconfig_change is an output parameter. It is set to TRUE if
 * the returned lm_sysid has a different config->knc_proto value.
 * Otherwise, it is set to FALSE.
 */
struct lm_sysid *
lm_get_sysid(struct knetconfig *config, struct netbuf *addr, char *name,
    bool_t *netconfig_change)
{
	struct lm_sysid *ls;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	rw_enter(&lm->lm_sysids_lock, RW_READER);
	ls = lm_get_sysid_locked(lm, config, addr, name, TRUE, NULL,
	    netconfig_change);
	rw_exit(&lm->lm_sysids_lock);

	return (ls);
}

/*
 * Like lm_get_sysid(), but the caller holds lm_sysids_lock as a reader.
 *
 * If alloc is TRUE and the entry can't be found, this routine
 * upgrades lm_sysids_lock to a writer lock, inserts the entry into the
 * list, and downgrades the lock back to a reader lock.  This may
 * involve dropping lm_sysids_lock and reacquiring it.  Callers that
 * pass alloc == FALSE expect that lm_sysids_lock cannot be dropped
 * without panicking; they are asserting that the lm_sysid is already
 * there.
 *
 * If dropped_read_lock != NULL, *dropped_read_lock is set to TRUE
 * if the lock was dropped; else it is set to FALSE.  If
 * dropped_read_lock == NULL, there is no way to convey this information to
 * the caller (which therefore must assume that the current set of
 * lm_sysids has changed).
 */
struct lm_sysid *
lm_get_sysid_locked(lm_globals_t *lm, struct knetconfig *config,
    struct netbuf *addr, char *name, bool_t alloc, bool_t *dropped_read_lock,
    bool_t *netconfig_change)
{
	struct lm_sysid *ls;
	sysid_t	new_sysid;
	bool_t writer = FALSE;
	struct lm_sysid key;
	avl_index_t where;

	LM_DEBUG((3, "get_sysid", "config= %p addr= %p name= %s alloc= %s",
	    (void *)config, (void *)addr, name,
	    ((alloc == TRUE) ? "TRUE" : "FALSE")));

	/*
	 * We can't verify that caller has lm_sysids_lock as a reader the
	 * way we'd like to, but at least we can assert that somebody does.
	 */
	ASSERT(RW_READ_HELD(&lm->lm_sysids_lock));
	if (dropped_read_lock != NULL)
		*dropped_read_lock = FALSE;

	/*
	 * Initialize *netconfig_change
	 */
	if (netconfig_change)
		*netconfig_change = FALSE;

	key.config = *config;
	key.addr = *addr;
	mutex_init(&key.lock, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * Try to find an existing lm_sysid that matches the requested
	 * zone and host address.
	 */

start:
	ls = avl_find(&lm->lm_sysids, &key, &where);
	if (ls != NULL) {
		mutex_enter(&ls->lock);
		ASSERT(ls->refcnt >= 0);
		ls->refcnt++;
		/* we're really just interested in netconfig_change */
		(void) lm_cmp_addr(config, addr, &ls->config, &ls->addr,
		    LM_IGN_PORT, netconfig_change);
		mutex_exit(&ls->lock);
		if (writer == TRUE)
			rw_downgrade(&lm->lm_sysids_lock);

		return (ls);
	}

	if (alloc == FALSE) {
		panic("lm_get_sysid: cached entry not found");
		/*NOTREACHED*/
	}

	/*
	 * It's necessary to get write access to the lm_sysids list here.
	 * Since we already own a READER lock, we acquire the WRITER lock
	 * with some care.
	 *
	 * In particular, if we fail to upgrade to writer immediately, there
	 * is already a writer or there are one or more other threads that
	 * are already blocking waiting to become writers.  In this case,
	 * we wait to acquire the writer lock, re-search the list,
	 * and then add our new entry onto the list.  The next time past
	 * here we're already a writer, so we skip this stuff altogether.
	 */
	if (writer == FALSE) {
		if (rw_tryupgrade(&lm->lm_sysids_lock) == 0) {
			rw_exit(&lm->lm_sysids_lock);
			if (dropped_read_lock != NULL) {
				*dropped_read_lock = TRUE;
			}
			rw_enter(&lm->lm_sysids_lock, RW_WRITER);
			writer = TRUE;
			goto start;
		} else {
			if (dropped_read_lock != NULL) {
				*dropped_read_lock = FALSE;
			}
		}
	}

	/*
	 * We will need to create a new lm_sysid.  Allocate an integer
	 * sysid for it.
	 */
	new_sysid = lm_alloc_sysidt();
	if (new_sysid == LM_NOSYSID) {
		ls = NULL;
		goto no_ids;
	}

	/*
	 * We have acquired the WRITER lock.  Create and add a
	 * new lm_sysid to the pool.
	 */
	ls = kmem_cache_alloc(lm->lm_sysid_cache, KM_SLEEP);
	ls->config.knc_semantics = config->knc_semantics;
	ls->config.knc_protofmly = lm_dup(config->knc_protofmly,
	    strlen(config->knc_protofmly) + 1);
	ls->config.knc_proto = (config->knc_proto ?
	    lm_dup(config->knc_proto, strlen(config->knc_proto) + 1) :
	    NULL);
	ls->config.knc_rdev = config->knc_rdev;
	ls->addr.buf = lm_dup(addr->buf, addr->maxlen);
	ls->addr.len = addr->len;
	ls->addr.maxlen = addr->maxlen;
	ls->name = name ? lm_dup(name, strlen(name) + 1) : "NoName";
	ls->sysid = new_sysid;
	ls->sm_client = FALSE;
	ls->sm_server = FALSE;
	ls->sm_state = 0;
	ls->in_recovery = FALSE;
	ls->refcnt = 1;
	avl_insert(&lm->lm_sysids, ls, where);
	lm->lm_sysid_len++;

	LM_DEBUG((3, "get_sysid", "name= %s, sysid= %x, sysids= %d",
	    ls->name, ls->sysid, lm->lm_sysid_len));
	LM_DEBUG((3, "get_sysid", "semantics= %d protofmly= %s proto= %s",
	    ls->config.knc_semantics, ls->config.knc_protofmly,
	    ls->config.knc_proto));

	/*
	 * Make sure we return still holding just the READER lock.
	 */
no_ids:
	rw_downgrade(&lm->lm_sysids_lock);
	return (ls);
}

/*
 * Increment the reference count for an lm_sysid.
 */
void
lm_ref_sysid(struct lm_sysid *ls)
{
	mutex_enter(&ls->lock);

	/*
	 * Most callers should already have a reference to the lm_sysid.
	 * Some routines walk the lm_sysids list, though, in which case the
	 * reference count could be zero.
	 */
	ASSERT(ls->refcnt >= 0);

	ls->refcnt++;

	mutex_exit(&ls->lock);
}

/*
 * Release the reference to an lm_sysid.  If the reference count goes to
 * zero, the lm_sysid is left around.  This avoids an expensive check for
 * locks or share reservations using ls->sysid, and it avoid recreating ls
 * if it is needed again.
 */
void
lm_rel_sysid(struct lm_sysid *ls)
{
	mutex_enter(&ls->lock);

	ls->refcnt--;
	ASSERT(ls->refcnt >= 0);

	mutex_exit(&ls->lock);
}

/*
 * Try to free all the lm_sysid's that we have registered.
 * In-use entries are left alone.
 */
void
lm_free_sysids_impl(lm_globals_t *lm)
{
	struct lm_sysid *ls;
	struct lm_sysid *nextls = NULL;
	int has_file_locks, has_shr_locks;

	/*
	 * Free all the lm_client's that are unused.  This must be done
	 * first, so that they drop their references to the lm_sysid's.
	 */
	lm_flush_clients_mem(lm);

	rw_enter(&lm->lm_sysids_lock, RW_WRITER);

	LM_DEBUG((5, "free_sysid", "start length: %d\n", lm->lm_sysid_len));

	for (ls = avl_first(&lm->lm_sysids); ls != NULL; ls = nextls) {
		mutex_enter(&ls->lock);
		ASSERT(ls->refcnt >= 0);
		nextls = AVL_NEXT(&lm->lm_sysids, ls);

		if (ls->refcnt == 0) {
			has_file_locks = ls_has_locks(ls);
			if (!has_file_locks) {
#if 0 /* notyet */
				has_shr_locks =
				    lm_shr_sysid_has_locks(lm, ls->sysid);
#else
				has_shr_locks = 0;
#endif
				if (!has_shr_locks) {
					ls_destroy(lm, ls);
					continue;
				}
			}
		}

		/* can't free now */
		LM_DEBUG((6, "free_sysid", "%x (%s) kept (ref %d)\n",
		    ls->sysid, ls->name, ls->refcnt));
		mutex_exit(&ls->lock);
	}

	LM_DEBUG((5, "free_sysid", "end length: %d\n", lm->lm_sysid_len));
	rw_exit(&lm->lm_sysids_lock);
}

/*
 * Only ever called from cluster for the global zone.
 */
/*ARGSUSED*/
void
lm_free_sysids(void *cdrarg)
{
	lm_globals_t *lm;
	lm = zone_getspecific(lm_zone_key, global_zone);
	lm_free_sysids_impl(lm);
}

/*
 * Try to free all the lm_sysid's that we have registered.  In-use entries
 * are left alone.
 */
void
lm_free_sysid_table(lm_globals_t *lm)
{
	lm_free_sysids_impl(lm);
}

/*
 * Only ever called via kmem_cache_reap().
 */
void
ls_reclaim(void *cdrarg)
{
	lm_globals_t *lm = (lm_globals_t *)cdrarg;

	/*
	 * If the PXFS server module is loaded (we're in cluster mode).
	 * then we want to create a deferred task in the cluster
	 * ORB to execute lm_free_sysids() in the background.  Otherwise,
	 * just free the sysids immediately if possible.
	 */
	if (lm_free_nlm_sysid_table != NULL) {
		(*lm_free_nlm_sysid_table)();
	} else {
		lm_free_sysids_impl(lm);
	}
}

/*
 * Return non-zero if the given lm_sysid has one or more locks associated
 * with it in the local locking code.
 */
static int
ls_has_locks(struct lm_sysid *ls)
{
	int	chklck;
	int	new_sysid;
	int	has_file_locks = 0;

	chklck = FLK_QUERY_ACTIVE | FLK_QUERY_SLEEPING;

	/*
	 * Clustering.  First try the pxfs module if it's loaded.  If the
	 * pxfs module is not loaded, call the local locking code
	 * directly.
	 */
	if (lm_has_file_locks) {	/* is pxfs module loaded? */
		/*
		 * Make new 32-bit quantity of sysid.  Need
		 * to do this so that we encode the node id
		 * of the node (in a cluster where this NLM
		 * server runs) in this new quantity.
		 */
		new_sysid = ls->sysid;
		lm_set_nlmid_flk(&new_sysid);

		has_file_locks = (*lm_has_file_locks)(new_sysid, chklck) ||
		    (*lm_has_file_locks)(ls->sysid | LM_SYSID_CLIENT, chklck);
	}
	if (has_file_locks)
		return (has_file_locks);

	return (flk_sysid_has_locks(ls->sysid, chklck) ||
	    flk_sysid_has_locks(ls->sysid | LM_SYSID_CLIENT, chklck));

}

/*
 * Destroy the given lm_sysid.
 */
static void
ls_destroy(lm_globals_t *lm, struct lm_sysid *ls)
{
	ASSERT(RW_WRITE_HELD(&lm->lm_sysids_lock));

	LM_DEBUG((6, "ls_destroy", "%x (%s) freed\n",
	    ls->sysid, ls->name));
	avl_remove(&lm->lm_sysids, ls);
	ASSERT(lm->lm_sysid_len != 0);
	--lm->lm_sysid_len;
	lm_free_sysidt(ls->sysid);
	kmem_free(ls->config.knc_protofmly,
	    strlen(ls->config.knc_protofmly) + 1);
	kmem_free(ls->config.knc_proto, strlen(ls->config.knc_proto) + 1);
	kmem_free(ls->addr.buf, ls->addr.maxlen);
	kmem_free(ls->name, strlen(ls->name) + 1);
	mutex_exit(&ls->lock);
	kmem_cache_free(lm->lm_sysid_cache, ls);
}

sysid_t
lm_sysidt(struct lm_sysid *ls)
{
	return (ls->sysid);
}

/*
 * kmem_cache constructor.
 */
/*ARGSUSED*/
static int
ls_constructor(void *buf, void *arg, int size)
{
	struct lm_sysid *ls = buf;

	mutex_init(&ls->lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ls->cv, NULL, CV_DEFAULT, NULL);

	return (0);
}

/*
 * kmem_cache destructor.
 */
/*ARGSUSED*/
static void
ls_destructor(void *buf, void *arg)
{
	struct lm_sysid *ls = buf;

	mutex_destroy(&ls->lock);
	cv_destroy(&ls->cv);
}

/*
 * Save the knetconfig information that was passed down to us from
 * userland during the _nfssys(LM_SVC) call.  This is used by the server
 * code ONLY to map fp -> config.
 */
struct lm_config *
lm_saveconfig(lm_globals_t *lm, struct file *fp, struct knetconfig *config)
{
	struct lm_config *ln;

	LM_DEBUG((7, "saveconfig",
	    "fp= %p semantics= %d protofmly= %s proto= %s",
	    (void *)fp, config->knc_semantics, config->knc_protofmly,
	    config->knc_proto));

	mutex_enter(&lm->lm_lock);
	for (ln = lm->lm_configs; ln; ln = ln->next) {
		if (ln->fp == fp) {	/* happens with multiple svc threads */
			mutex_exit(&lm->lm_lock);
			LM_DEBUG((7, "saveconfig", "found ln= %p", (void *)ln));
			return (ln);
		}
	}

	ln = kmem_cache_alloc(lm->lm_config_cache, KM_SLEEP);
	ln->fp = fp;
	ln->config = *config;
	ln->next = lm->lm_configs;
	lm->lm_configs = ln;
	lm->lm_numconfigs++;

	LM_DEBUG((7, "saveconfig", "ln= %p fp= %p next= %p",
	    (void *)ln, (void *)fp, (void *)ln->next));
	mutex_exit(&lm->lm_lock);
	return (ln);
}

/*
 * Fetch lm_config corresponding to an fp.  Server code only.
 */
struct lm_config *
lm_getconfig(lm_globals_t *lm, struct file *fp)
{
	struct lm_config *ln;

	LM_DEBUG((7, "getconfig", "fp= %p", (void *)fp));

	mutex_enter(&lm->lm_lock);
	for (ln = lm->lm_configs; ln != NULL; ln = ln->next) {
		if (ln->fp == fp)
			break;
	}
	mutex_exit(&lm->lm_lock);

	LM_DEBUG((7, "getconfig", "ln= %p", (void *)ln));
	return (ln);
}

/*
 * Remove an entry from the config table and decrement the config count.
 * This routine does not return the number of remaining entries, because
 * the caller probably wants to check it while holding lm->lm_lock.
 */
void
lm_rmconfig(lm_globals_t *lm, struct file *fp)
{
	struct lm_config *ln;
	struct lm_config *prev_ln;

	LM_DEBUG((7, "rmconfig", "fp=%p", (void *)fp));

	mutex_enter(&lm->lm_lock);

	for (ln = lm->lm_configs, prev_ln = NULL; ln != NULL; ln = ln->next) {
		if (ln->fp == fp)
			break;
		prev_ln = ln;
	}

	if (ln == NULL) /*EMPTY*/ {
#ifdef DEBUG
		cmn_err(CE_WARN,
		    "lm_rmconfig: couldn't find config for fp %p", (void *)fp);
#endif
	} else {
		LM_DEBUG((7, "rmconfig", "ln=%p", (void *)ln));
		if (prev_ln == NULL) {
			lm->lm_configs = ln->next;
		} else {
			prev_ln->next = ln->next;
		}
		lm->lm_numconfigs--;
		/*
		 * no need to call lm_clear_knetconfig, because all of its
		 * strings are statically allocated.
		 */
		kmem_cache_free(lm->lm_config_cache, ln);
	}

	mutex_exit(&lm->lm_lock);
}

/*
 * When an entry in the lm_client cache is released, it is just marked
 * unused.  If space is tight, it can be freed later.  Because client
 * handles are potentially expensive to keep around, we try to reuse old
 * lm_client entries only if there are lm_max_clients entries or more
 * allocated.
 *
 * If time == 0, the client handle is not valid.
 */
struct lm_client {
	struct lm_client *next;
	struct lm_sysid *sysid;
	rpcprog_t prog;
	rpcvers_t vers;
	struct netbuf addr;	/* Address to this <prog,vers> on sysid */
	time_t time;		/* In seconds */
	struct knetconfig lc_config;
	CLIENT *client;
	bool_t in_use;
};

/*
 * For the benefit of warlock.  lm->lm_lock must be held when allocating or
 * freeing an lm_client.  The other members of lm_client are protected by
 * the "in_use" reference flag.  lm_async works the same way.  The original
 * design supported freeing an lm_client while not holding lm->lm_lock (just
 * clear the "in_use" flag), but this seemed too likely to confuse people
 * working on the code.
 */
#ifndef lint
_NOTE(SCHEME_PROTECTS_DATA("LM ref flag",
    lm_client::{sysid prog vers addr time client}))
#endif

int lm_max_clients = 10;

#ifndef lint
_NOTE(READ_ONLY_DATA(lm_max_clients))
#endif

/*
 * We need an version of CLNT_DESTROY which also frees the auth structure.
 */
static void
lm_clnt_destroy(CLIENT **clp)
{
	if (*clp) {
		if ((*clp)->cl_auth) {
			AUTH_DESTROY((*clp)->cl_auth);
			(*clp)->cl_auth = NULL;
		}
		CLNT_DESTROY(*clp);
		*clp = NULL;
	}
}

/*
 * Release this lm_client entry.
 *
 * The RPC client handle may also be destroyed, depending on the given
 * errno value:
 * EINTR	do not destroy
 * ETIMEDOUT	do not destroy
 * all others	destroy the client handle
 *
 * If the client handle is destroyed, the entry is also marked as
 * not-valid, by setting time=0.
 */
static void
lm_rel_client(lm_globals_t *lm, struct lm_client *lc, int error)
{
	LM_DEBUG((7, "rel_clien", "addr = (%p, %d %d)\n",
	    (void *)lc->addr.buf, lc->addr.len, lc->addr.maxlen));
	ASSERT(MUTEX_HELD(&lm->lm_lock));

	if (error && error != EINTR && error != ETIMEDOUT) {
		LM_DEBUG((7, "rel_clien", "destroying addr = (%p, %d %d)\n",
		    (void *)lc->addr.buf, lc->addr.len, lc->addr.maxlen));
		lm_clnt_destroy(&lc->client);
		if (lc->addr.buf) {
			kmem_free(lc->addr.buf, lc->addr.maxlen);
			lc->addr.buf = NULL;
		}
		lm_free_config(&lc->lc_config);
		lc->time = 0;
	}
	lc->in_use = FALSE;
}

/*
 * Return a lm_client to the <ls,prog,vers>.
 * The lm_client found is marked as in_use.
 * It is the responsibility of the caller to release the lm_client by
 * calling lm_rel_client().
 *
 * Returns:
 * RPC_SUCCESS		Success.
 * RPC_CANTSEND		Temporarily cannot send to this sysid.
 * RPC_TLIERROR		Unspecified TLI error.
 * RPC_UNKNOWNPROTO	ls->config is from an unrecognized protocol family.
 * RPC_PROGNOTREGISTERED The NLM prog `prog' isn't registered on the server.
 * RPC_RPCBFAILURE	Couldn't contact portmapper on remote host.
 * Any unsuccessful return codes from CLNT_CALL().
 */
static enum clnt_stat
lm_get_client(lm_globals_t *lm, struct lm_sysid *ls, rpcprog_t prog,
    rpcvers_t vers, int timout, struct lm_client **lcp, bool_t ignore_signals,
    bool_t isreclaim, bool_t switch_proto)
{
	struct lm_client *lc = NULL;
	struct lm_client *lc_old = NULL;
	enum clnt_stat status = RPC_SUCCESS;

	mutex_enter(&lm->lm_lock);

	/*
	 * Search for an lm_client that is free, valid, and matches.
	 */
	for (lc = lm->lm_clients; lc; lc = lc->next) {
		if (! lc->in_use) {
			if (lc->time && lc->sysid == ls && lc->prog == prog &&
			    lc->vers == vers) {
				/* Found a valid and matching lm_client. */
				break;
			} else if (lc_old == NULL || lc->time < lc_old->time) {
				/* Possibly reuse this one. */
				lc_old = lc;
			}
		}
	}

	LM_DEBUG((7, "get_client", "Found lc= %p, lc_old= %p, timout= %d",
	    (void *)lc, (void *)lc_old, timout));

	if (lc == NULL) {
		/*
		 * We did not find an entry to use.
		 * Decide if we should reuse lc_old or create a new entry.
		 */
		if (lc_old == NULL || lm->lm_client_len < lm_max_clients) {
			/*
			 * No entry to reuse, or we are allowed to create
			 * extra.
			 */
			lm->lm_client_len++;
			lc = kmem_cache_alloc(lm->lm_client_cache, KM_SLEEP);
			lc->time = 0;
			lc->client = NULL;
			lc->next = lm->lm_clients;
			lc->sysid = NULL;
			lm->lm_clients = lc;
			bzero(&lc->lc_config, sizeof (lc->lc_config));
		} else {
			lm_rel_client(lm, lc_old, EINVAL);
			lc = lc_old;
		}

		/*
		 * Update the sysid reference counts.  We get the new
		 * reference before dropping the old one in case they're
		 * the same.  This is to prevent the ref count from going
		 * to zero, which could make the sysid vanish.
		 */
		lm_ref_sysid(ls);
		if (lc->sysid != NULL) {
			lm_rel_sysid(lc->sysid);
			lm_free_config(&lc->lc_config);
		}
		lc->sysid = ls;
		mutex_enter(&ls->lock);
		lm_copy_config(&lc->lc_config, &ls->config);
		mutex_exit(&ls->lock);

		lc->prog = prog;
		lc->vers = vers;
		lc->addr.buf = lm_dup(ls->addr.buf, ls->addr.maxlen);
		lc->addr.len = ls->addr.len;
		lc->addr.maxlen = ls->addr.maxlen;
	}
	lc->in_use = TRUE;

	mutex_exit(&lm->lm_lock);

	lm_n_buf(7, "get_client", "addr", &lc->addr);

	/*
	 * If timout == 0 then one way RPC calls are used, and the CLNT_CALL
	 * will always return RPC_TIMEDOUT. Thus we will never know whether
	 * a client handle is still OK. Therefore don't use the handle if
	 * time is older than lm_sa.timout. Note, that lm_sa.timout == 0
	 * disables the client cache for one way RPC-calls.
	 */
	if (timout == 0) {
		if (lm->lm_sa.timout <= time - lc->time) {  /* Invalidate? */
			lc->time = 0;
		}
	}

	if (lc->time == 0) {
		status = lm_get_rpc_handle(ls, &lc->lc_config, &lc->addr,
		    prog, vers, ignore_signals, isreclaim, switch_proto,
		    &lc->client);
		if (status != RPC_SUCCESS)
			goto out;
		lc->time = time;
	} else {
		/*
		 * Consecutive calls to CLNT_CALL() on the same client handle
		 * get the same transaction ID.  We want a new xid per call,
		 * so we first reinitialise the handle.
		 */
		(void) clnt_tli_kinit(lc->client, &lc->lc_config,
		    &lc->addr, 0, 0, CRED());
	}

out:
	LM_DEBUG((7, "get_client",
	    "End: lc= %p status= %d, time= %lx, client= %p, clients= %d",
	    (void *)lc, status,
	    (lc ? lc->time : -1),
	    (void *)(lc ? lc->client : NULL),
	    lm->lm_client_len));

	if (status == RPC_SUCCESS) {
		*lcp = lc;
	} else {
		if (lc != NULL) {
			mutex_enter(&lm->lm_lock);
			lm_rel_client(lm, lc, EINVAL);
			mutex_exit(&lm->lm_lock);
		}
		*lcp = NULL;
	}

	return (status);
}

/*
 * Return non-zero if the given struct lm_client is still correct according
 * to the remote portmapper.
 */
static int
lm_client_valid(struct lm_client *lc)
{
	struct netbuf nbuf;
	struct lm_sysid	*ls;
	struct knetconfig config;
	enum clnt_stat status;
	int valid = 0;
	bool_t protochg;

	bzero(&config, sizeof (config));

	nbuf.buf = kmem_alloc(sizeof (struct sockaddr_storage), KM_SLEEP);
	nbuf.len = lc->addr.len;
	nbuf.maxlen = sizeof (struct sockaddr_storage);
	ASSERT(nbuf.maxlen >= nbuf.len);
	bcopy(lc->addr.buf, nbuf.buf, nbuf.len);
	ls = lc->sysid;
	status = lm_get_port(ls, &config, lc->prog, lc->vers, &nbuf, FALSE);
	if (status == RPC_SUCCESS &&
	    lm_cmp_addr(&lc->lc_config, &lc->addr, &config, &nbuf,
	    LM_CHECK_PORT, &protochg) == 0 &&
	    !protochg)
		valid = 1;

	lm_free_config(&config);
	kmem_free(nbuf.buf, nbuf.maxlen);
	return (valid);
}

/*
 * Get the RPC client handle to talk to the service (prog, vers) at the
 * host specified by addrp.
 *
 * Returns:
 * RPC_SUCCESS		Success.
 * RPC_RPCBFAILURE	Couldn't talk to the remote portmapper (e.g.,
 * 			timeouts).
 * RPC_INTR		Caught a signal before we could successfully return.
 * RPC_TLIERROR		Couldn't initialize the handle after talking to the
 * 			remote portmapper (shouldn't happen).
 *
 * The contents of addrp may be updated to track the current port.
 */
static enum clnt_stat
lm_get_rpc_handle(struct lm_sysid *ls, struct knetconfig *configp,
    struct netbuf *addrp, rpcprog_t prog, rpcvers_t vers, int ignore_signals,
    bool_t isreclaim, bool_t switch_proto, CLIENT **clientp)
{
	enum clnt_stat status;
	k_sigset_t oldmask;
	k_sigset_t newmask;
	int error;
	char *newproto = NULL;
	char *newdev = NULL;
	vnode_t *kkvp;

	/*
	 * It's not clear whether this function should have a retry loop,
	 * as long as things like a portmapper timeout cause the
	 * higher-level code to retry.
	 */

	/*
	 * If the caller has suggested that we try a different transport,
	 * try a different one.  So far this only works for Internet
	 * protocols, not, e.g., the loopback transport.
	 */

	if (switch_proto && !isreclaim &&
	    (strcmp(configp->knc_protofmly, NC_INET) == 0 ||
	    strcmp(configp->knc_protofmly, NC_INET6) == 0)) {
		ASSERT(configp->knc_protofmly != NULL);
		ASSERT(configp->knc_proto != NULL);
		if (strcmp(configp->knc_protofmly, NC_INET) == 0) {
			if (strcmp(configp->knc_proto, NC_TCP) == 0) {
				newproto = NC_UDP;
				newdev = "/dev/udp";
			} else {
				newproto = NC_TCP;
				newdev = "/dev/tcp";
			}
		} else if (strcmp(configp->knc_protofmly, NC_INET6) == 0) {
			if (strcmp(configp->knc_proto, NC_TCP) == 0) {
				newproto = NC_UDP;
				newdev = "/dev/udp6";
			} else {
				newproto = NC_TCP;
				newdev = "/dev/tcp6";
			}
		}
		/*
		 * create a new netconfig.
		 */
		if ((error = lookupname(newdev, UIO_SYSSPACE,
		    FOLLOW, NULLVPP, &kkvp)) != 0) {
			LM_DEBUG((7, "get_port",
			    "lookupname failed %d", error));
			return (RPC_FAILED);
		}
		configp->knc_rdev = kkvp->v_rdev;
		VN_RELE(kkvp);
		/* protofamily remains NC_INET */
		if (strcmp(newproto, NC_UDP) == 0)
			configp->knc_semantics = NC_TPI_CLTS;
		else
			configp->knc_semantics = NC_TPI_COTS_ORD;
		if (strlen(configp->knc_proto) != strlen(newproto)) {
			(void) kmem_free(configp->knc_proto,
			    strlen(configp->knc_proto) + 1);
			configp->knc_proto =
			    kmem_alloc(strlen(newproto) + 1, KM_SLEEP);
		}
		(void) strcpy(configp->knc_proto, newproto);
		mutex_enter(&ls->lock);
		lm_free_config(&ls->config);
		lm_copy_config(&ls->config, configp);
		mutex_exit(&ls->lock);
	}

	/*
	 * Try to get the address from either portmapper or rpcbind.
	 */
	status = lm_get_port(ls, configp, prog, vers, addrp, ignore_signals);

	if (status == RPC_TIMEDOUT)
		status = RPC_RPCBFAILURE;
	if (status != RPC_SUCCESS)
		goto bailout;

	lm_clnt_destroy(clientp);

	/*
	 * Mask signals for the duration of the handle creation,
	 * allowing relatively normal operation with a signal
	 * already posted to our thread (e.g., when we are
	 * sending an NLM_CANCEL in response to catching a signal).
	 *
	 * Any further exit paths from this routine must restore
	 * the original signal mask.
	 */
	sigfillset(&newmask);
	sigreplace(&newmask, &oldmask);
	if ((error = clnt_tli_kcreate(configp, addrp, prog,
	    vers, 0, 0, CRED(), clientp)) != 0) {
		status = RPC_TLIERROR;
		sigreplace(&oldmask, (k_sigset_t *)NULL);
		LM_DEBUG((7, "get_client", "kcreate(prog) returned %d", error));
		goto bailout;
	}
	(*clientp)->cl_nosignal = 1;
	sigreplace(&oldmask, (k_sigset_t *)NULL);

bailout:
	return (status);
}

/*
 * Try to get the address for the desired service, by asking rpcbind or the
 * portmapper.  addr has the tentative address and is updated with whatever
 * rpcbind/portmapper returns.  config is filled in with the knetconfig
 * from ls, and any strings that it had previously are freed.
 *
 * Ignores signals if "ignore_signals" is non-zero.
 */
static enum clnt_stat
lm_get_port(struct lm_sysid *ls, struct knetconfig *config, rpcprog_t prog,
    rpcvers_t vers, struct netbuf *addr, int ignore_signals)
{
	ushort_t port = 0;
	int error;
	enum clnt_stat status;
	CLIENT *client = NULL;
	struct pmap parms;
	struct timeval tmo;
	k_sigset_t oldmask;
	k_sigset_t newmask;
	int restore_sigmask = 0;

	mutex_enter(&ls->lock);
	lm_free_config(config);
	lm_copy_config(config, &ls->config);
	mutex_exit(&ls->lock);

	LM_DEBUG((7, "get_port", "semantics= %d, protofmly= %s, proto= %s",
	    config->knc_semantics, config->knc_protofmly,
	    config->knc_proto));
	lm_n_buf(7, "get_port", "addr", addr);

	/*
	 * For Internet (IPv4) addresses, set up to call rpcbind version 2
	 * or earlier (SunOS portmapper, remote only), then do the
	 * remaining work below.  For everything else, just get the answer
	 * directly from rpcbind_getaddr().  The reason we don't use
	 * rpcbind_getaddr() for everything is so that we can interoperate
	 * with hosts that only support version 2 of the protocol.
	 */
	if (strcmp(config->knc_protofmly, NC_INET) == 0) {
		put_inet_port(addr, htons(PMAPPORT));
	} else {
		LM_DEBUG((7, "get_port", "trying rpcbind for: %s",
		    config->knc_protofmly));
		if (strcmp(config->knc_protofmly, NC_LOOPBACK) == 0) {
			char *hostname;
			ssize_t namelen;

			/*
			 * rpcbind_getaddr() expects a loopback address to
			 * look like "<hostname>.", since the loopback
			 * address for rpcbind is <hostname>.rpc.  But the
			 * passed-in address might look different, so force
			 * it to look like what rpcbind_getaddr() expects.
			 */
			hostname = uts_nodename();
			namelen = strlen(hostname);
			if (addr->maxlen < namelen + 1) {
				kmem_free(addr->buf, addr->maxlen);
				addr->buf = kmem_zalloc(namelen + 1, KM_SLEEP);
				addr->maxlen = namelen + 1;
			}
			(void) strcpy(addr->buf, hostname);
			((char *)addr->buf)[namelen] = '.';
			addr->len = namelen + 1;
		}
		status = rpcbind_getaddr(config, prog, vers, addr);
		if (IS_UNRECOVERABLE_RPC(status))
			status = RPC_RPCBFAILURE;
		LM_DEBUG((7, "get_port", "rpcbind status %d", status));
		goto out;
	}

	/*
	 * Mask signals for the duration of the handle creation and
	 * RPC call.  This allows relatively normal operation with a
	 * signal already posted to our thread (e.g., when we are
	 * sending an NLM_CANCEL in response to catching a signal).
	 *
	 * Any further exit paths from this routine must restore
	 * the original signal mask.
	 */
	sigfillset(&newmask);
	sigreplace(&newmask, &oldmask);
	restore_sigmask = 1;

	if ((error = clnt_tli_kcreate(config, addr, PMAPPROG,
	    PMAPVERS, 0, 0, CRED(), &client)) != RPC_SUCCESS) {
		status = RPC_TLIERROR;
		LM_DEBUG((7, "get_pmap_addr", "kcreate() returned %d", error));
		goto out;
	}

	parms.pm_prog = prog;
	parms.pm_vers = vers;
	parms.pm_port = 0;
	if (strcmp(config->knc_proto, NC_TCP) == 0) {
		parms.pm_prot = IPPROTO_TCP;
	} else {
		parms.pm_prot = IPPROTO_UDP;
	}

	tmo.tv_sec = LM_PMAP_TIMEOUT;
	tmo.tv_usec = 0;
	client->cl_nosignal = 1;

	status = CLNT_CALL(client, PMAPPROC_GETPORT, xdr_pmap,
	    (char *)&parms, xdr_u_short, (char *)&port, tmo);
	LM_DEBUG((7, "get_port", "CLNT_CALL(GETPORT) returned %d",
	    status));
	if (status != RPC_SUCCESS) {
		if (IS_UNRECOVERABLE_RPC(status) &&
		    status != RPC_UNKNOWNPROTO &&
		    !PMAP_WRONG_VERSION(status)) {
			status = RPC_RPCBFAILURE;
		}
		goto out;
	}
	if (port == 0)
		status = RPC_PROGNOTREGISTERED;
	else
		put_inet_port(addr, ntohs(port));

out:
	if (restore_sigmask)
		sigreplace(&oldmask, (k_sigset_t *)NULL);

	LM_DEBUG((7, "get_port", "port= %d, status= %d", port, status));

	if (client)
		lm_clnt_destroy(&client);
	if (!ignore_signals && lm_sigispending()) {
		LM_DEBUG((7, "get_port",
		    "posted signal,RPC stat= %d", status));
		status = RPC_INTR;
	}
	return (status);
}

/*
 * Free all RPC client-handles for the machine ls.  If ls is NULL, free all
 * RPC client handles.
 */
void
lm_flush_clients(lm_globals_t *lm, struct lm_sysid *ls)
{
	struct lm_client *lc;

	mutex_enter(&lm->lm_lock);

	for (lc = lm->lm_clients; lc; lc = lc->next) {
		LM_DEBUG((1, "flush_clients", "flushing lc %p, in_use %d",
		    (void *)lc, lc->in_use));
		if (! lc->in_use)
			if ((! ls) || (lc->sysid == ls))
				lm_rel_client(lm, lc, EINVAL);
	}
	mutex_exit(&lm->lm_lock);
}

/*
 * Try to free all lm_client objects, their RPC handles, and their
 * associated memory.  In-use entries are left alone.
 */
void
lm_flush_clients_mem(lm_globals_t *lm)
{
	struct lm_client *lc;
	struct lm_client *nextlc = NULL;
	struct lm_client *prevlc = NULL; /* previous kept element */

	mutex_enter(&lm->lm_lock);

	for (lc = lm->lm_clients; lc; lc = nextlc) {
		nextlc = lc->next;
		if (lc->in_use) {
			prevlc = lc;
		} else {
			if (prevlc == NULL) {
				lm->lm_clients = nextlc;
			} else {
				prevlc->next = nextlc;
			}
			lm->lm_client_len--;
			lm_clnt_destroy(&lc->client);
			if (lc->addr.buf) {
				kmem_free(lc->addr.buf, lc->addr.maxlen);
			}
			if (lc->sysid) {
				lm_rel_sysid(lc->sysid);
			}
			lm_free_config(&lc->lc_config);
			kmem_cache_free(lm->lm_client_cache, lc);
		}
	}
	mutex_exit(&lm->lm_lock);
}

void
lm_client_reclaim(void *cdrarg)
{
	lm_globals_t *lm = (lm_globals_t *)cdrarg;
	lm_flush_clients_mem(lm);
}

/*
 * Make an RPC call to addr via config.
 *
 * Returns:
 * 0		Success.
 * EIO		Couldn't get client handle, timed out, or got unexpected
 *		RPC status within LM_RETRY attempts.
 * EINVAL	Unrecoverable error in RPC call.  Causes client handle
 *		to be destroyed.
 * EINTR	RPC call was interrupted within LM_RETRY attempts.
 */
int
lm_callrpc(lm_globals_t *lm, struct lm_sysid *ls, rpcprog_t prog,
    rpcvers_t vers, rpcproc_t proc, xdrproc_t inproc, caddr_t in,
    xdrproc_t outproc, caddr_t out, int timout, int tries, bool_t isreclaim)
{
	struct timeval tmo;
	struct lm_client *lc = NULL;
	enum clnt_stat stat;
	int error, rpc_error;
	int signalled;
	int iscancel;
	k_sigset_t oldmask;
	k_sigset_t newmask;
	bool_t switch_proto = FALSE;
	clock_t reclaim_delay = (lm_reclaim_factor * hz);

	ASSERT(proc != LM_IGNORED);

	LM_DEBUG((6, "callrpc", "Calling [%u, %u, %u] on '%s' (%x) via '%s'",
	    prog, vers, proc, ls->name, ls->sysid,
	    ls->config.knc_proto));

	tmo.tv_sec = timout;
	tmo.tv_usec = 0;
	signalled = 0;
	sigfillset(&newmask);
	iscancel = (prog == NLM_PROG &&
	    (proc == NLM_CANCEL || proc == NLMPROC4_CANCEL ||
	    proc == NLM_UNLOCK || proc == NLMPROC4_UNLOCK));

	/*
	 * If we are currently recovering locks from the server, delay any
	 * non-reclaim requests.  In an ideal world, this synchronization
	 * would not be necessary because the server would correctly order
	 * reclaim and non-reclaim requests.  But if the server allows
	 * reclaim requests after the grace period (which has been observed),
	 * there could be trouble.  The synchronization between non-reclaim
	 * and reclaim threads is not real tight, but it should be
	 * sufficient to keep requests from stomping on each other.
	 */
	error = 0;
	mutex_enter(&ls->lock);
	while (ls->in_recovery && !isreclaim) {
		LM_DEBUG((7, "callrpc", "delaying until recovery is done"));
		if (iscancel)
			cv_wait(&ls->cv, &ls->lock);
		else {
			if (cv_wait_sig(&ls->cv, &ls->lock) == 0) {
				LM_DEBUG((7, "callrpc",
				    "signal during recovery delay"));
				error = EINTR;
				break;
			}
		}
	}
	mutex_exit(&ls->lock);
	if (error != 0)
		goto done;

	while (tries--) {
		/*
		 * If the process is signaled, "error" is set to EINTR, so that
		 * we can bail out before doing all "tries" RPC calls.  But we
		 * also need to remember the error (if any) from the RPC call,so
		 * that we can clean up properly from it. So if error == EINTR
		 * rpc_error is the error from the RPC call (if any),
		 * if rpc_error is zero, error will be 0 or the RPC call return
		 */
		error = 0;
		rpc_error = 0;
		lc = NULL;
		/*
		 * If any signal has been posted to our (user) thread,
		 * bail out as quickly as possible.  The exception is
		 * if we are doing any type of CANCEL/UNLOCK:  in that case,
		 * we may already have a posted signal and we need to
		 * live with it.
		 */
		if (lm_sigispending()) {
			LM_DEBUG((6, "callrpc", "posted signal"));
			if (iscancel == 0) {
				error = EINTR;
				break;
			}
		}

		stat = lm_get_client(lm, ls, prog, vers, timout, &lc,
		    iscancel, isreclaim, switch_proto);
		switch_proto = FALSE;
		if (IS_UNRECOVERABLE_RPC(stat)) {
			error = EINVAL;
			goto rel_client;
		} else if (stat == RPC_PROGNOTREGISTERED) {
			if (isreclaim)
				(void) delay_sig(reclaim_delay);
			else
				switch_proto = TRUE;
			error = EIO;
			continue;
		} else if (stat != RPC_SUCCESS) {
			error = EIO;
			continue;
		}

		if (lm_sigispending()) {
			LM_DEBUG((6, "callrpc",
			    "posted signal after lm_get_client"));
			if (iscancel == 0) {
				error = EINTR;
				rpc_error = EIO;
				goto rel_client;
			}
		}
		ASSERT(lc != NULL);
		ASSERT(lc->client != NULL);

		lm_n_buf(6, "callrpc", "addr", &lc->addr);

		sigreplace(&newmask, &oldmask);
		stat = CLNT_CALL(lc->client, proc, inproc, in,
		    outproc, out, tmo);
		sigreplace(&oldmask, (k_sigset_t *)NULL);

		if (lm_sigispending()) {
			LM_DEBUG((6, "callrpc",
			    "posted signal after CLNT_CALL"));
			signalled = 1;
		}

		switch (stat) {
		case RPC_SUCCESS:
			/*
			 * Update the timestamp on the client cache entry.
			 */
			lc->time = time;
			error = 0;
			break;

		case RPC_TIMEDOUT:
			LM_DEBUG((6, "callrpc", "RPC_TIMEDOUT"));
			if (signalled && (iscancel == 0)) {
				error = EINTR;
				rpc_error = EIO;
				break;
			}
			if (timout == 0) {
				/*
				 * We will always time out when timout == 0.
				 * Don't update the lc->time stamp. We do not
				 * know if the client handle is still OK.
				 */
				error = 0;
				break;
			}
			lc->time = time;
			if (lm_client_valid(lc)) {
				LM_DEBUG((7, "callrpc", "client still valid"));
				rpc_error = ETIMEDOUT;
			}
			error = EIO;
			break;

		case RPC_CANTSEND:
		case RPC_XPRTFAILED:
		default:
			if (IS_UNRECOVERABLE_RPC(stat)) {
				error = EINVAL;
			} else if (signalled && (iscancel == 0)) {
				error = EINTR;
				rpc_error = EIO;
			} else {
				error = EIO;
			}
		}

rel_client:
		LM_DEBUG((6, "callrpc", "RPC stat= %d error= %d", stat, error));
		if (lc != NULL) {
			mutex_enter(&lm->lm_lock);
			lm_rel_client(lm, lc, rpc_error ? rpc_error : error);
			mutex_exit(&lm->lm_lock);
		}

		/*
		 * If EIO, loop else we're done.
		 */
		if (error != EIO) {
			break;
		}
	}

done:
	mutex_enter(&lm_stat.lock);
	lm_stat.tot_out++;
	lm_stat.bad_out += (error != 0);
	if (prog == NLM_PROG) {
		ASSERT(proc < NLM_NUMRPCS); /* rpcproc_t unsigned */
		lm_stat.proc_out[proc]++;
	}

	LM_DEBUG((6, "callrpc", "End: error= %d, tries= %d, tot= %u, bad= %u",
	    error, tries, lm_stat.tot_out, lm_stat.bad_out));

	mutex_exit(&lm_stat.lock);
	return (error);
}

/*
 * lm_async
 *
 * List of outstanding asynchronous RPC calls.
 * An entry in the list is free iff in_use == FALSE.
 * Only nlm_stats are expected as replies (can easily be extended).
 */
struct lm_async {
	struct lm_async *next;
	int cookie;
	kcondvar_t cv;
	enum nlm_stats stat;
	bool_t reply;
	bool_t in_use;
};

/*
 * lm_asynrpc
 *
 * Make an asynchronous RPC call.
 * Since the call is asynchronous, we put ourselves onto a list and wait for
 * the reply to arrive.  If a reply has not arrived within timout seconds,
 * we retransmit.  So far, this routine is only used by lm_block_lock()
 * to send NLM_GRANTED_MSG to asynchronous (NLM_LOCK_MSG-using) clients.
 * Note: the stat given by caller is updated in lm_asynrply().
 */
int
lm_asynrpc(lm_globals_t *lm, struct lm_sysid *ls, rpcprog_t prog,
    rpcvers_t vers, rpcproc_t proc, xdrproc_t inproc, caddr_t in, int cookie,
    enum nlm_stats *stat, int timout, int tries)
{
	int error;
	struct lm_async *la;
	clock_t delta = (clock_t)timout * hz;

	/*
	 * Start by inserting the call in lm_asyncs.
	 * Find an empty entry, or create one.
	 */
	mutex_enter(&lm->lm_lock);
	for (la = lm->lm_asyncs; la; la = la->next)
		if (! la->in_use)
			break;
	if (!la) {
		la = kmem_cache_alloc(lm->lm_async_cache, KM_SLEEP);
		cv_init(&la->cv, NULL, CV_DEFAULT, NULL);
		la->next = lm->lm_asyncs;
		lm->lm_asyncs = la;
		lm->lm_async_len++;
	}
	la->cookie = cookie;
	*stat = la->stat = -1;
	la->reply = FALSE;
	la->in_use = TRUE;

	LM_DEBUG((5, "asynrpc",
	    "la= %p cookie= %d stat= %d reply= %d in_use= %d asyncs= %d",
	    (void *)la, la->cookie, la->stat, la->reply, la->in_use,
	    lm->lm_async_len));
	mutex_exit(&lm->lm_lock);

	/*
	 * Call the host asynchronously, i.e. with no timeout.
	 * Sleep timout seconds or until a reply has arrived.
	 * Note that the sleep may NOT be interrupted (we're
	 * a kernel thread).
	 */
	while (tries--) {
		if (error = lm_callrpc(lm, ls, prog, vers, proc, inproc, in,
		    xdr_void, NULL, LM_NO_TIMOUT, 1, FALSE))
			break;

		mutex_enter(&lm->lm_lock);
		(void) cv_reltimedwait(&la->cv, &lm->lm_lock, delta,
		    TR_CLOCK_TICK);

		/*
		 * Our thread may have been cv_signal'ed.
		 */
		if (la->reply == TRUE) {
			error = 0;
		} else {
			LM_DEBUG((5, "asynrpc", "timed out"));
			error = EIO;
		}

		if (error == 0) {
			*stat = la->stat;
			LM_DEBUG((5, "asynrpc", "End: tries= %d, stat= %d",
			    tries, la->stat));
			mutex_exit(&lm->lm_lock);
			break;
		}

		mutex_exit(&lm->lm_lock);
		LM_DEBUG((5, "asynrpc", "Timed out. tries= %d", tries));
	}

	/*
	 * Release entry in lm_asyncs.
	 */
	mutex_enter(&lm->lm_lock);
	la->in_use = FALSE;
	mutex_exit(&lm->lm_lock);
	return (error);
}

/*
 * lm_asynrply():
 * Find the lm_async and update reply and stat.
 * Don't bother if lm_async does not exist.
 *
 * Note that the caller can identify the async call with just the cookie.
 * This is because we generated the async call and we know that each new
 * call gets a new cookie.
 */
void
lm_asynrply(lm_globals_t *lm, int cookie, enum nlm_stats stat)
{
	struct lm_async *la;

	LM_DEBUG((5, "asynrply", "cookie= %d stat= %d", cookie, stat));

	mutex_enter(&lm->lm_lock);
	for (la = lm->lm_asyncs; la; la = la->next) {
		if (la->in_use)
			if (cookie == la->cookie)
				break;
		LM_DEBUG((5, "asynrply", "passing la= %p in_use= %d cookie= %d",
		    (void *)la, la->in_use, la->cookie));
	}

	if (la) {
		la->stat = stat;
		la->reply = TRUE;
		LM_DEBUG((5, "asynrply", "signalling la= %p", (void *)la));
		cv_signal(&la->cv);
	} else {
		LM_DEBUG((5, "asynrply", "Couldn't find matching la"));
	}
	mutex_exit(&lm->lm_lock);
}

/*
 * Free any unused lm_async structs.
 */
void
lm_async_free(lm_globals_t *lm)
{
	struct lm_async *la;
	struct lm_async *nextla = NULL;
	struct lm_async *prevla = NULL;

	mutex_enter(&lm->lm_lock);
	for (la = lm->lm_asyncs; la; la = nextla) {
		nextla = la->next;
		if (la->in_use)
			continue;
		if (prevla == NULL) {
			lm->lm_asyncs = nextla;
		} else {
			prevla->next = nextla;
		}
		kmem_cache_free(lm->lm_async_cache, la);
		lm->lm_async_len--;
	}
	mutex_exit(&lm->lm_lock);
}

/*
 * lm_waitfor_granted
 *
 * Wait for an NLM_GRANTED corresponding to the given lm_sleep.
 *
 * Return value:
 *  0	an NLM_GRANTED has arrived.
 * EINTR sleep was interrupted.
 * -1	the sleep timed out.
 *
 * Side effects:
 * - sets or clears the vnode's VNOCACHE flag, taking into account whether
 *   the requested lock makes it safe to cache the file.
 * - drops the rnode's r_lkserlock while waiting.
 * - if CPR information is provided, makes the thread suspend-safe while
 *   waiting.
 */
int
lm_waitfor_granted(lm_globals_t *lm, struct lm_sleep *lslp, callb_cpr_t *cprp)
{
	int	error = 0;
	clock_t	time;
	clock_t time_left;
	rnode_t *rp;
	vnode_t *vp;
	klwp_t	*lwp;

	vp = lslp->vp;
	ASSERT(vp != NULL);
	rp = VTOR(vp);
	ASSERT(nfs_rw_lock_held(&rp->r_lkserlock, RW_WRITER));

	/*
	 * If the locks (including this pending request) make mapping
	 * unsafe, mark the vnode so that nobody can map it while we're
	 * waiting for the lock to be granted.  Then drop r_lkserlock to
	 * let other processes proceed.
	 */
	if (!lm_safemap(vp)) {
		mutex_enter(&vp->v_lock);
		vp->v_flag |= VNOCACHE;
		mutex_exit(&vp->v_lock);
	} else {
		mutex_enter(&vp->v_lock);
		vp->v_flag &= ~VNOCACHE;
		mutex_exit(&vp->v_lock);
	}
	nfs_rw_exit(&rp->r_lkserlock);

	/* Allow stops now that r_lkserlock has been released. */
	if ((lwp = ttolwp(curthread)) != NULL) {
		ASSERT(lwp->lwp_nostop > 0);
		lwp->lwp_nostop--;
	}

	mutex_enter(&lm->lm_lock);
	/*
	 * Make thread suspend-safe if CPR information
	 * was provided.
	 */
	if (cprp) {
		mutex_enter(cprp->cc_lockp);
		CALLB_CPR_SAFE_BEGIN(cprp);
		mutex_exit(cprp->cc_lockp);
	}

	time = ddi_get_lbolt() + (clock_t)LM_BLOCK_SLP * hz;
	error = 0;
	while (lslp->waiting && (error == 0)) {
		time_left = cv_timedwait_sig(&lslp->cv, &lm->lm_lock, time);
		switch (time_left) {
		case -1:		/* timed out */
			error = -1;
			break;
		case 0:			/* caught a signal */
			error = EINTR;
			break;
		default:		/* cv_signal woke us */
			error = 0;
			break;
		}
	};
	if (cprp) {
		mutex_enter(cprp->cc_lockp);
		CALLB_CPR_SAFE_END(cprp, cprp->cc_lockp);
		mutex_exit(cprp->cc_lockp);
	}
	mutex_exit(&lm->lm_lock);

	if (lwp)
		lwp->lwp_nostop++;

	(void) nfs_rw_enter_sig(&rp->r_lkserlock, RW_WRITER, FALSE);

	LM_DEBUG((5, "waitfor_granted", "End: error= %d", error));

	return (error);
}

/*
 * lm_signal_granted():
 * Find the lm_sleep corresponding to the given arguments.
 * If lm_sleep is found, wakeup process and return 0.
 * Otherwise return -1.
 */
int
lm_signal_granted(lm_globals_t *lm, pid_t pid, struct netobj *fh,
    struct netobj *oh, u_offset_t offset, u_offset_t length)
{
	struct lm_sleep *lslp;

	mutex_enter(&lm->lm_lock);
	for (lslp = lm->lm_sleeps; lslp != NULL; lslp = lslp->next) {
		/*
		 * Theoretically, only the oh comparison is necessary to
		 * determine a match.  The other comparisons are for
		 * additional safety.  (Remember that a false match would
		 * cause a process to think it has a lock when it doesn't,
		 * which can cause file corruption.)  We can't compare
		 * sysids because the callback might come in using a
		 * different netconfig than the one the lock request went
		 * out on.
		 */
		if (lslp->in_use && pid == lslp->pid &&
		    lslp->offset == offset && lslp->length == length &&
		    lslp->oh.n_len == oh->n_len &&
		    bcmp(lslp->oh.n_bytes, oh->n_bytes, oh->n_len) == 0 &&
		    lslp->fh.n_len == fh->n_len &&
		    bcmp(lslp->fh.n_bytes, fh->n_bytes, fh->n_len) == 0)
			break;
	}

	if (lslp) {
		lslp->waiting = FALSE;
		cv_signal(&lslp->cv);
	}

	LM_DEBUG((5, "signal_granted", "pid= %d, in_use= %d, sleeps= %d",
	    (lslp ? lslp->pid : -1),
	    (lslp ? lslp->in_use : -1), lm->lm_sleep_len));
	mutex_exit(&lm->lm_lock);

	return (lslp ? 0 : -1);
}

/*
 * Allocate and fill in an lm_sleep, and put it in the global list.
 *
 * Side effects: obtains a reference for vp.
 */
struct lm_sleep *
lm_get_sleep(lm_globals_t *lm, struct lm_sysid *ls, struct netobj *fh,
    struct netobj *oh, u_offset_t offset, len_t length, vnode_t *vp)
{
	struct lm_sleep *lslp;

	mutex_enter(&lm->lm_lock);
	for (lslp = lm->lm_sleeps; lslp; lslp = lslp->next)
		if (! lslp->in_use)
			break;
	if (lslp == NULL) {
		lslp = kmem_cache_alloc(lm->lm_sleep_cache, KM_SLEEP);
		cv_init(&lslp->cv, NULL, CV_DEFAULT, NULL);
		lslp->next = lm->lm_sleeps;
		lm->lm_sleeps = lslp;
		lm->lm_sleep_len++;
	}

	lslp->pid = curproc->p_pid;
	lslp->in_use = TRUE;
	lslp->waiting = TRUE;
	lslp->sysid = ls;
	lm_ref_sysid(ls);
	lslp->fh.n_len = fh->n_len;
	lslp->fh.n_bytes = lm_dup(fh->n_bytes, fh->n_len);
	lslp->oh.n_len = oh->n_len;
	lslp->oh.n_bytes = lm_dup(oh->n_bytes, oh->n_len);
	lslp->offset = offset;
	lslp->length = length;
	lslp->vp = vp;
	VN_HOLD(lslp->vp);

	LM_DEBUG((5, "get_sleep", "pid= %d, in_use= %d, sleeps= %d",
	    lslp->pid, lslp->in_use, lm->lm_sleep_len));

	mutex_exit(&lm->lm_lock);

	return (lslp);
}

/*
 * Release the given lm_sleep.  Resets its contents and frees any memory
 * that it owns.
 */
void
lm_rel_sleep(lm_globals_t *lm, struct lm_sleep *lslp)
{
	mutex_enter(&lm->lm_lock);
	lslp->in_use = FALSE;

	lm_rel_sysid(lslp->sysid);
	lslp->sysid = NULL;

	kmem_free(lslp->fh.n_bytes, lslp->fh.n_len);
	lslp->fh.n_bytes = NULL;
	lslp->fh.n_len = 0;

	kmem_free(lslp->oh.n_bytes, lslp->oh.n_len);
	lslp->oh.n_bytes = NULL;
	lslp->oh.n_len = 0;

	VN_RELE(lslp->vp);
	lslp->vp = NULL;

	mutex_exit(&lm->lm_lock);
}

/*
 * Free any unused lm_sleep structs.
 */
void
lm_free_sleep(lm_globals_t *lm)
{
	struct lm_sleep *prevslp = NULL; /* previously kept sleep */
	struct lm_sleep *nextslp;
	struct lm_sleep *slp;

	mutex_enter(&lm->lm_lock);
	LM_DEBUG((5, "free_sleep", "start length: %d\n", lm->lm_sleep_len));

	for (slp = lm->lm_sleeps; slp != NULL; slp = nextslp) {
		nextslp = slp->next;
		if (slp->in_use) {
			prevslp = slp;
		} else {
			if (prevslp == NULL) {
				lm->lm_sleeps = nextslp;
			} else {
				prevslp->next = nextslp;
			}
			--lm->lm_sleep_len;
			ASSERT(slp->sysid == NULL);
			ASSERT(slp->fh.n_bytes == NULL);
			ASSERT(slp->oh.n_bytes == NULL);
			cv_destroy(&slp->cv);
			kmem_cache_free(lm->lm_sleep_cache, slp);
		}
	}

	LM_DEBUG((5, "free_sleep", "end length: %d\n", lm->lm_sleep_len));
	mutex_exit(&lm->lm_lock);
}

void
lm_sleep_reclaim(void *cdrarg)
{
	lm_globals_t *lm = (lm_globals_t *)cdrarg;
	lm_free_sleep(lm);
}

/*
 * Initialize the per-zone data structures associated with the lock manager.
 */
/* ARGSUSED */
void *
lm_zone_init(zoneid_t zoneid)
{
	char nm[30];
	lm_globals_t *lm;

	lm = kmem_zalloc(sizeof (*lm), KM_SLEEP);
	mutex_init(&lm->lm_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&lm->lm_vnodes_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&lm->lm_sysids_lock, NULL, RW_DEFAULT, NULL);
	cv_init(&lm->lm_status_cv, NULL, CV_DEFAULT, NULL);

	lm->lm_lockd_pid = 0;
	lm->lm_server_status = LM_UP;
	lm->lm_num_outstanding = 0;
	lm->lm_numconfigs = 0;
	lm->lm_start_time = 0;
	lm->lm_grace_started = TRUE;
	lm->lm_zoneid = zoneid;

	(void) snprintf(nm, sizeof (nm), "lm_xprt_%p", (void *)lm);
	lm->lm_xprt_cache = kmem_cache_create(nm, sizeof (struct lm_xprt),
	    0, NULL, NULL, NULL, NULL, NULL, 0);

	(void) snprintf(nm, sizeof (nm), "lm_vnode_%p", (void *)lm);
	lm->lm_vnode_cache = kmem_cache_create(nm, sizeof (struct lm_vnode),
	    0, NULL, NULL, lm_free_vnode, (void *)lm, NULL, 0);

	(void) snprintf(nm, sizeof (nm), "lm_sysid_%p", (void *)lm);
	lm->lm_sysid_cache = kmem_cache_create(nm, sizeof (struct lm_sysid),
	    0, ls_constructor, ls_destructor, ls_reclaim, (void *)lm, NULL, 0);

	(void) snprintf(nm, sizeof (nm), "lm_client_%p", (void *)lm);
	lm->lm_client_cache = kmem_cache_create(nm, sizeof (struct lm_client),
	    0, NULL, NULL, lm_client_reclaim, (void *)lm, NULL, 0);

	(void) snprintf(nm, sizeof (nm), "lm_async_%p", (void *)lm);
	lm->lm_async_cache = kmem_cache_create(nm, sizeof (struct lm_async),
	    0, NULL, NULL, NULL, NULL, NULL, 0);

	(void) snprintf(nm, sizeof (nm), "lm_sleep_%p", (void *)lm);
	lm->lm_sleep_cache = kmem_cache_create(nm,
	    sizeof (struct lm_sleep), 0, NULL, NULL, lm_sleep_reclaim,
	    (void *)lm, NULL, 0);

	(void) snprintf(nm, sizeof (nm), "lm_config_%p", (void *)lm);
	lm->lm_config_cache = kmem_cache_create(nm,
	    sizeof (struct lm_config), 0, NULL, NULL, NULL, NULL, NULL, 0);

	avl_create(&lm->lm_sysids, lm_cmp_sysid, sizeof (struct lm_sysid),
	    offsetof(struct lm_sysid, lsnode));

	mutex_enter(&lm_global_list_lock);
	list_insert_tail(&lm_global_list, lm);
	mutex_exit(&lm_global_list_lock);

	return (lm);
}

/* ARGSUSED */
void
lm_zone_fini(zoneid_t zoneid, void *data)
{
	lm_globals_t *lm = data;

	mutex_enter(&lm_global_list_lock);
	list_remove(&lm_global_list, lm);
	mutex_exit(&lm_global_list_lock);

	lm_free_all_vnodes(lm);
	lm_free_sleep(lm);
	lm_flush_clients_mem(lm);
	lm_free_sysids_impl(lm);
	lm_free_xprt_map(lm);
	lm_async_free(lm);

	ASSERT(lm->lm_vnode_len == 0);
	ASSERT(lm->lm_sleep_len == 0);
	ASSERT(lm->lm_client_len == 0);
	ASSERT(lm->lm_sysid_len == 0);
	ASSERT(lm->lm_async_len == 0);

	avl_destroy(&lm->lm_sysids);
	kmem_cache_destroy(lm->lm_config_cache);
	kmem_cache_destroy(lm->lm_sleep_cache);
	kmem_cache_destroy(lm->lm_async_cache);
	kmem_cache_destroy(lm->lm_client_cache);
	kmem_cache_destroy(lm->lm_sysid_cache);
	kmem_cache_destroy(lm->lm_vnode_cache);
	kmem_cache_destroy(lm->lm_xprt_cache);
	cv_destroy(&lm->lm_status_cv);
	rw_destroy(&lm->lm_sysids_lock);
	mutex_destroy(&lm->lm_vnodes_lock);
	mutex_destroy(&lm->lm_lock);
	kmem_free(lm, sizeof (*lm));
}

/*
 * Determine whether or not a signal is pending on the calling thread.
 * If so, return 1 else return 0.
 *
 * XXX: Fixes to this code should probably be propagated to (or from)
 * the common signal-handling code in sig.c.  See bugid 1201594.
 */
int
lm_sigispending(void)
{
	klwp_t *lwp;
	int cancel_pending = 0;

	/*
	 * Some callers may be non-signallable kernel threads, in
	 * which case we always return 0.  Allowing such a thread
	 * to (pointlessly) call ISSIG() would result in a panic.
	 */
	lwp = ttolwp(curthread);
	if (lwp == NULL) {
		return (0);
	}

	/*
	 * lwp_asleep and lwp_sysabort are modified only for the sake of
	 * /proc, and should always be set to 0 after the ISSIG call.
	 * Note that the lwp may sleep for a long time inside
	 * ISSIG(FORREAL) - a human being may be single-stepping in a
	 * debugger, for example - so we must not hold any mutexes or
	 * other critical resources here.
	 */
	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	/* ASSERT(no mutexes or rwlocks are held) */
	if (ISSIG(curthread, FORREAL) || lwp->lwp_sysabort ||
	    MUSTRETURN(curproc, curthread) ||
	    (cancel_pending = schedctl_cancel_pending()) != 0) {
		lwp->lwp_asleep = 0;
		lwp->lwp_sysabort = 0;
		if (cancel_pending)
			schedctl_cancel_eintr();
		return (1);
	}

	lwp->lwp_asleep = 0;
	return (0);
}
/*
 * When a Checkpoint (CPR suspend) occurs and a remote lock is held,
 * keep track of the state of system (server).
 */
static void
lm_cprsuspend_zone(lm_globals_t *lm)
{
	struct lm_sysid *ls;
	sm_name		arg;
	int		error;
	sm_stat_res	res;

	rw_enter(&lm->lm_sysids_lock, RW_READER);
	/*
	 * Check if there are at least one active lock relating
	 * to this sysid
	 */
	for (ls = avl_first(&lm->lm_sysids); ls != NULL;
	    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
		lm_ref_sysid(ls);
		/*
		 * If there is a remote lock held on this system
		 * get and store current state of statd.  Otherwise,
		 * set state to 0.
		 */
		if (flk_sysid_has_locks(ls->sysid | LM_SYSID_CLIENT,
		    FLK_QUERY_ACTIVE)) {
			arg.mon_name = ls->name;
			error = lm_callrpc(lm, ls, SM_PROG, SM_VERS,
			    SM_STAT, xdr_sm_name, (caddr_t)&arg,
			    xdr_sm_stat_res, (caddr_t)&res,
			    LM_CR_TIMOUT, LM_RETRY, FALSE);
			/*
			 * If an error occurred while getting the
			 * state of server statd, set state to -1.
			 */
			mutex_enter(&ls->lock);
			if (error != 0) {
				nfs_cmn_err(error, CE_WARN,
				    "lockd: cannot contact statd "
				    "(%m), continuing");
				ls->sm_state = -1;
			} else
				ls->sm_state = res.state;
			mutex_exit(&ls->lock);
		} else {
			mutex_enter(&ls->lock);
			ls->sm_state = 0;
			mutex_exit(&ls->lock);
		}
		lm_rel_sysid(ls);
	}
	rw_exit(&lm->lm_sysids_lock);
}

/*
 * When CPR Resume occurs, determine if state has changed and if
 * so, either reclaim lock or send SIGLOST to process depending
 * on state.
 */
static void
lm_cprresume_zone(lm_globals_t *lm)
{
	struct lm_sysid *ls;
	sm_name arg;
	int error = 0;
	sm_stat_res res;
	locklist_t *llp, *next_llp;

	rw_enter(&lm->lm_sysids_lock, RW_READER);
	for (ls = avl_first(&lm->lm_sysids); ls != NULL;
	    ls = AVL_NEXT(&lm->lm_sysids, ls)) {
		lm_ref_sysid(ls);
		/* No remote active locks were held when we suspended. */
		mutex_enter(&ls->lock);
		if (ls->sm_state == 0) {
			mutex_exit(&ls->lock);
			lm_rel_sysid(ls);
			continue;
		}
		/*
		 * Statd on server has is an older version
		 * such that it returns -1 as the state.
		 */
		if (ls->sm_state == -1) {
			mutex_exit(&ls->lock);
			/* Get active locks */
			llp = flk_get_active_locks(ls->sysid | LM_SYSID_CLIENT,
			    NOPID);
			while (llp) {
				/*
				 * lockd should not affect NFSv4 locks, so only
				 * signal the process for v2/v3 vnodes.
				 */
				if (vn_matchops(llp->ll_vp, nfs3_vnodeops) ||
				    vn_matchops(llp->ll_vp, nfs_vnodeops)) {
					LM_DEBUG((1, "cprresume",
					    "calling lm_send_siglost(%p)",
					    (void *)llp));
					lm_send_siglost(lm, &llp->ll_flock, ls);
				}
				next_llp = llp->ll_next;
				VN_RELE(llp->ll_vp);
				kmem_free(llp, sizeof (*llp));
				llp = next_llp;
			}
			lm_rel_sysid(ls);
			continue;
		}

		mutex_exit(&ls->lock);

		/* Check current state of server statd */
		arg.mon_name = ls->name;
		error = lm_callrpc(lm, ls, SM_PROG, SM_VERS, SM_STAT,
		    xdr_sm_name, (caddr_t)&arg, xdr_sm_stat_res,
		    (caddr_t)&res, LM_CR_TIMOUT, LM_RETRY, FALSE);

		if (error != 0) {
			nfs_cmn_err(error, CE_WARN,
			"lockd: cannot contact statd (%m), continuing");
			continue;
		}
		/*
		 * Server went down while we were in
		 * suspend mode and thus try to reclaim locks.
		 */
		mutex_enter(&ls->lock);
		if (ls->sm_state != res.state) {
			mutex_exit(&ls->lock);
			lm_flush_clients(lm, ls);

			/* Get active locks */
			llp = flk_get_active_locks(
			    ls->sysid | LM_SYSID_CLIENT, NOPID);
			while (llp) {
				LM_DEBUG((1, "rlck_serv",
				    "calling lm_reclaim(%p)", (void *)llp));
				lm_reclaim_lock(lm, llp->ll_vp, &llp->ll_flock);
				next_llp = llp->ll_next;
				VN_RELE(llp->ll_vp);
				kmem_free(llp, sizeof (*llp));
				llp = next_llp;
			}
		} else {
			mutex_exit(&ls->lock);
		}
		lm_rel_sysid(ls);
	}
	rw_exit(&lm->lm_sysids_lock);
}

void
lm_cprsuspend(void)
{
	lm_globals_t *lm;

	mutex_enter(&lm_global_list_lock);
	for (lm = list_head(&lm_global_list); lm != NULL;
	    lm = list_next(&lm_global_list, lm)) {
		lm_cprsuspend_zone(lm);
	}
	mutex_exit(&lm_global_list_lock);
}

void
lm_cprresume(void)
{
	lm_globals_t *lm;

	mutex_enter(&lm_global_list_lock);
	for (lm = list_head(&lm_global_list); lm != NULL;
	    lm = list_next(&lm_global_list, lm)) {
		lm_cprresume_zone(lm);
	}
	mutex_exit(&lm_global_list_lock);
}

/*
 *  Sends a SIGLOST to process associated with file.  This occurs
 *  if locks can not be reclaimed or after Checkpoint and Resume
 *  where an older version of statd is encountered.
 */
void
lm_send_siglost(lm_globals_t *lm, struct flock64 *flkp, struct lm_sysid *ls)
{
	proc_t  *p;

	/*
	 * No need to discard the local locking layer's cached copy
	 * of the lock, since the normal closeall when process
	 * exits due to SIGLOST will clean up accordingly
	 * and/or application if it catches SIGLOST is assumed to deal
	 * with unlocking prior to handling it.  If it is
	 * discarded here only, then an unlock request will never reach
	 * server and thus causing an orphan lock.  NOTE: If
	 * application does not do unlock prior to continuing
	 * it will still hold a lock until the application exits.
	 *
	 * Find the proc and signal it that the
	 * lock could not be reclaimed.
	 */
	mutex_enter(&pidlock);
	p = prfind(flkp->l_pid);
	if (p)
		psignal(p, SIGLOST);
	mutex_exit(&pidlock);
	zcmn_err(lm->lm_zoneid, CE_NOTE, "lockd: pid %d lost lock on server %s",
	    flkp->l_pid, ls->name);
}

/*
 * lm_safelock:
 *
 * Return non-zero if the given lock request can be handled without
 * violating the constraints on concurrent mapping and locking.
 */
int
lm_safelock(vnode_t *vp, const struct flock64 *bfp, cred_t *cr)
{
	rnode_t *rp = VTOR(vp);
	struct vattr va;
	int error;

	ASSERT(rp->r_mapcnt >= 0);
	LM_DEBUG((3, "safelock", "%s (%" PRId64 ", %" PRId64
	    "); mapcnt = %ld",
	    (bfp->l_type == F_WRLCK ? "write" :
	    (bfp->l_type == F_RDLCK ? "read" : "unlock")),
	    bfp->l_start, bfp->l_len, rp->r_mapcnt));
	if (rp->r_mapcnt == 0)
		return (1);		/* always safe if not mapped */

	/*
	 * If the file is already mapped and there are locks, then they
	 * should be all safe locks.  So adding or removing a lock is safe
	 * as long as the new request is safe (i.e., whole-file, meaning
	 * length and starting offset are both zero).
	 */

	if (bfp->l_start != 0 || bfp->l_len != 0)
		return (0);

	/* mandatory locking and mapping don't mix */
	va.va_mask = AT_MODE;
	error = VOP_GETATTR(vp, &va, 0, cr, NULL);
	if (error != 0) {
		LM_DEBUG((3, "safelock", "getattr error %d",
		    error));
		return (0);		/* treat errors conservatively */
	}
	if (MANDLOCK(vp, va.va_mode)) {
		LM_DEBUG((3, "safelock", "mandlock"));
		return (0);
	}

	return (1);
}

/*
 * Return non-zero if the given file can be safely memory mapped.
 */
int
lm_safemap(const vnode_t *vp)
{
	locklist_t *llp, *next_llp;
	int safe = 1;
	struct lm_sleep *lslp;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	ASSERT(nfs_rw_lock_held(&VTOR(vp)->r_lkserlock, RW_WRITER));

	LM_DEBUG((2, "safemap", "vp = %p", (void *)vp));

	/*
	 * Review all the locks for the vnode, both ones that have been
	 * acquired and ones that are pending.  Locks are safe if
	 * whole-file (length and offset are both zero).  We assume that
	 * flk_active_locks_for_vp() has merged any locks that can be
	 * merged (so that if a process has the entire file locked, it is
	 * represented as a single lock).
	 *
	 * Note that we can't bail out of the loop if we find a non-safe
	 * lock, because we have to free all the elements in the llp list.
	 * We might be able to speed up this code slightly by not looking
	 * at each lock's l_start and l_len fields once we've found a
	 * non-safe lock.
	 */

	llp = flk_active_locks_for_vp(vp);
	while (llp) {
		LM_DEBUG((6, "safemap", "active lock (%" PRId64
		    ", %" PRId64 ")",
		    llp->ll_flock.l_start, llp->ll_flock.l_len));
		if (llp->ll_flock.l_start != 0 ||
		    llp->ll_flock.l_len != 0) {
			safe = 0;
			LM_DEBUG((4, "safemap",
			    "unsafe active lock (%" PRId64 ", %" PRId64 ")",
			    llp->ll_flock.l_start,
			    llp->ll_flock.l_len));
		}
		next_llp = llp->ll_next;
		VN_RELE(llp->ll_vp);
		kmem_free(llp, sizeof (*llp));
		llp = next_llp;
	}

	mutex_enter(&lm->lm_lock);
	for (lslp = lm->lm_sleeps; safe && lslp != NULL; lslp = lslp->next) {
		if (!lslp->in_use || lslp->vp != vp)
			continue;
		LM_DEBUG((6, "safemap", "sleeping lock (%llu, %llu)",
		    lslp->offset, lslp->length));

		if (lslp->offset != 0 || lslp->length != 0) {
			safe = 0;
			LM_DEBUG((4, "safemap",
			    "unsafe blocked lock (%llu, %llu)",
			    lslp->offset, lslp->length));
		}
	}
	mutex_exit(&lm->lm_lock);

	LM_DEBUG((2, "safemap", safe ? "safe" : "unsafe"));
	return (safe);
}

/*
 * Return non-zero if the given vnode has a blocked lock, zero if it does
 * not.
 */
int
lm_has_sleep(const vnode_t *vp)
{
	struct lm_sleep *lslp;
	int found = 0;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	mutex_enter(&lm->lm_lock);
	for (lslp = lm->lm_sleeps; lslp != NULL; lslp = lslp->next) {
		if (lslp->in_use && lslp->vp == vp) {
			found = 1;
			break;
		}
	}
	mutex_exit(&lm->lm_lock);

	return (found);
}

/*
 * Allocate a sysid_t.  Returns LM_NOSYSID on failure (all sysid_t's in
 * use, or the system is low on memory).
 * Also called from nfs server and smb server to allocate from same pool.
 */
sysid_t
lm_alloc_sysidt()
{
#if LM_NOSYSID != -1
#error Need to make sure id_alloc_nosleep failure maps to LM_NOSYSID.
#endif
	return (id_alloc_nosleep(lmsysid_space));
}

/*
 * Release a sysid_t, making it available for reuse.
 */
void
lm_free_sysidt(sysid_t s)
{
	ASSERT(s != LM_NOSYSID);
	id_free(lmsysid_space, s);
}
