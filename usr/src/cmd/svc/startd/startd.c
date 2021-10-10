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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * startd.c - the master restarter
 *
 * svc.startd comprises two halves.  The graph engine is based in graph.c and
 * maintains the service dependency graph based on the information in the
 * repository.  For each service it also tracks the current state and the
 * restarter responsible for the service.  Based on the graph, events from the
 * repository (mostly administrative requests from svcadm), and messages from
 * the restarters, the graph engine makes decisions about how the services
 * should be manipulated and sends commands to the appropriate restarters.
 * Communication between the graph engine and the restarters is embodied in
 * protocol.c.
 *
 * The second half of svc.startd is the restarter for services managed by
 * svc.startd and is primarily contained in restarter.c.  It responds to graph
 * engine commands by executing methods, updating the repository, and sending
 * feedback (mostly state updates) to the graph engine.
 *
 * Error handling
 *
 * In general, when svc.startd runs out of memory it reattempts a few times,
 * sleeping inbetween, before giving up and exiting (see startd_alloc_retry()).
 * When a repository connection is broken (libscf calls fail with
 * SCF_ERROR_CONNECTION_BROKEN, librestart and internal functions return
 * ECONNABORTED), svc.startd calls libscf_rebind_handle(), which coordinates
 * with the svc.configd-restarting thread, fork_configd_thread(), via
 * st->st_configd_live_cv, and rebinds the repository handle.  Doing so resets
 * all libscf state associated with that handle, so functions which do this
 * should communicate the event to their callers (usually by returning
 * ECONNRESET) so they may reset their state appropriately.
 *
 * External references
 *
 * svc.configd generates special security audit events for changes to some
 * restarter related properties.  See the special_props_list array in
 * usr/src/cmd/svc/configd/rc_node.c for the properties that cause these audit
 * events.  If you change the semantics of these propereties within startd, you
 * will probably need to update rc_node.c
 */

#include <stdio.h>
#include <stdio_ext.h>
#include <sys/mnttab.h>		/* uses FILE * without including stdio.h */
#include <alloca.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libintl.h>
#include <libscf.h>
#include <libscf_priv.h>
#include <libuutil.h>
#include <locale.h>
#include <paths.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "startd.h"
#include "protocol.h"

ssize_t max_scf_name_size;
ssize_t max_scf_fmri_size;
ssize_t max_scf_value_size;

mode_t fmask;
mode_t dmask;

graph_update_t *gu;
restarter_update_t *ru;

startd_state_t *st;

boolean_t booting_to_single_user = B_FALSE;

const char * const admin_actions[] = {
    SCF_PROPERTY_DEGRADED,
    SCF_PROPERTY_MAINT_OFF,
    SCF_PROPERTY_MAINT_ON,
    SCF_PROPERTY_MAINT_ON_IMMEDIATE,
    SCF_PROPERTY_REFRESH,
    SCF_PROPERTY_RESTART
};

const int admin_events[NACTIONS] = {
    RESTARTER_EVENT_TYPE_ADMIN_DEGRADED,
    RESTARTER_EVENT_TYPE_ADMIN_MAINT_OFF,
    RESTARTER_EVENT_TYPE_ADMIN_MAINT_ON,
    RESTARTER_EVENT_TYPE_ADMIN_MAINT_ON_IMMEDIATE,
    RESTARTER_EVENT_TYPE_ADMIN_REFRESH,
    RESTARTER_EVENT_TYPE_ADMIN_RESTART
};

const char * const instance_state_str[] = {
	"none",
	"uninitialized",
	"maintenance",
	"offline",
	"disabled",
	"online",
	"degraded"
};

static int finished = 0;
static int opt_reconfig = 0;
static uint8_t prop_reconfig = 0;

#define	INITIAL_REBIND_ATTEMPTS	5
#define	INITIAL_REBIND_DELAY	3

pthread_mutexattr_t mutex_attrs;

const char *
_umem_debug_init(void)
{
	return ("default,verbose");	/* UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents");	/* UMEM_LOGGING setting */
}

/*
 * startd_alloc_retry()
 *   Wrapper for allocation functions.  Retries with a decaying time
 *   value on failure to allocate, and aborts startd if failure is
 *   persistent.
 */
void *
startd_alloc_retry(void *f(size_t, int), size_t sz)
{
	void *p;
	uint_t try, msecs;

	p = f(sz, UMEM_DEFAULT);
	if (p != NULL || sz == 0)
		return (p);

	msecs = ALLOC_DELAY;

	for (try = 0; p == NULL && try < ALLOC_RETRY; ++try) {
		(void) poll(NULL, 0, msecs);
		msecs *= ALLOC_DELAY_MULT;
		p = f(sz, UMEM_DEFAULT);
		if (p != NULL)
			return (p);
	}

	uu_die("Insufficient memory.\n");
	/* NOTREACHED */
}

void *
safe_realloc(void *p, size_t sz)
{
	uint_t try, msecs;

	p = realloc(p, sz);
	if (p != NULL || sz == 0)
		return (p);

	msecs = ALLOC_DELAY;

	for (try = 0; errno == EAGAIN && try < ALLOC_RETRY; ++try) {
		(void) poll(NULL, 0, msecs);
		p = realloc(p, sz);
		if (p != NULL)
			return (p);
		msecs *= ALLOC_DELAY_MULT;
	}

	uu_die("Insufficient memory.\n");
	/* NOTREACHED */
}

char *
safe_strdup(const char *s)
{
	uint_t try, msecs;
	char *d;

	d = strdup(s);
	if (d != NULL)
		return (d);

	msecs = ALLOC_DELAY;

	for (try = 0;
	    (errno == EAGAIN || errno == ENOMEM) && try < ALLOC_RETRY;
	    ++try) {
		(void) poll(NULL, 0, msecs);
		d = strdup(s);
		if (d != NULL)
			return (d);
		msecs *= ALLOC_DELAY_MULT;
	}

	uu_die("Insufficient memory.\n");
	/* NOTREACHED */
}


void
startd_free(void *p, size_t sz)
{
	umem_free(p, sz);
}

/*
 * Creates a uu_list_pool_t with the same retry policy as startd_alloc().
 * Only returns NULL for UU_ERROR_UNKNOWN_FLAG and UU_ERROR_NOT_SUPPORTED.
 */
uu_list_pool_t *
startd_list_pool_create(const char *name, size_t e, size_t o,
    uu_compare_fn_t *f, uint32_t flags)
{
	uu_list_pool_t *pool;
	uint_t try, msecs;

	pool = uu_list_pool_create(name, e, o, f, flags);
	if (pool != NULL)
		return (pool);

	msecs = ALLOC_DELAY;

	for (try = 0; uu_error() == UU_ERROR_NO_MEMORY && try < ALLOC_RETRY;
	    ++try) {
		(void) poll(NULL, 0, msecs);
		pool = uu_list_pool_create(name, e, o, f, flags);
		if (pool != NULL)
			return (pool);
		msecs *= ALLOC_DELAY_MULT;
	}

	if (try < ALLOC_RETRY)
		return (NULL);

	uu_die("Insufficient memory.\n");
	/* NOTREACHED */
}

/*
 * Creates a uu_list_t with the same retry policy as startd_alloc().  Only
 * returns NULL for UU_ERROR_UNKNOWN_FLAG and UU_ERROR_NOT_SUPPORTED.
 */
uu_list_t *
startd_list_create(uu_list_pool_t *pool, void *parent, uint32_t flags)
{
	uu_list_t *list;
	uint_t try, msecs;

	list = uu_list_create(pool, parent, flags);
	if (list != NULL)
		return (list);

	msecs = ALLOC_DELAY;

	for (try = 0; uu_error() == UU_ERROR_NO_MEMORY && try < ALLOC_RETRY;
	    ++try) {
		(void) poll(NULL, 0, msecs);
		list = uu_list_create(pool, parent, flags);
		if (list != NULL)
			return (list);
		msecs *= ALLOC_DELAY_MULT;
	}

	if (try < ALLOC_RETRY)
		return (NULL);

	uu_die("Insufficient memory.\n");
	/* NOTREACHED */
}

pthread_t
startd_thread_create(void *(*func)(void *), void *ptr)
{
	int err;
	pthread_t tid;

	err = pthread_create(&tid, NULL, func, ptr);
	if (err != 0) {
		assert(err == EAGAIN);
		uu_die("Could not create thread.\n");
	}

	err = pthread_detach(tid);
	assert(err == 0);

	return (tid);
}

extern int info_events_all;

static int
startd_set_online(void)
{
	scf_handle_t *hndl;
	instance_data_t idata;
	uint_t count = 0, msecs = ALLOC_DELAY;
	int bind_fails = 0, r;

	/*
	 * Read "options" property group.
	 */
	for (hndl = libscf_handle_create_bound(SCF_VERSION); hndl == NULL;
	    hndl = libscf_handle_create_bound(SCF_VERSION), bind_fails++) {
		(void) sleep(INITIAL_REBIND_DELAY);

		if (bind_fails > INITIAL_REBIND_ATTEMPTS) {
			/*
			 * In the case that we can't bind to the repository
			 * (which should have been started), we need to allow
			 * the user into maintenance mode to determine what's
			 * failed.
			 */
			log_framework(LOG_INFO, "Couldn't fetch "
			    "default settings: %s\n",
			    scf_strerror(scf_error()));

			return (-1);
		}
	}

	/* Set svc/restarter state to 'online' */
	idata.i_fmri = SCF_SERVICE_STARTD;
	idata.i_state = RESTARTER_STATE_NONE;
	idata.i_next_state = RESTARTER_STATE_NONE;

timestamp:
	switch (r = _restarter_commit_states(hndl, &idata,
	    RESTARTER_STATE_ONLINE, RESTARTER_STATE_NONE,
	    restarter_get_str_short(restarter_str_insert_in_graph))) {
	case 0:
		break;

	case ENOMEM:
		++count;
		if (count < ALLOC_RETRY) {
			(void) poll(NULL, 0, msecs);
			msecs *= ALLOC_DELAY_MULT;
			goto timestamp;
		}

		uu_die("Insufficient memory.\n");
		/* NOTREACHED */

	case ECONNABORTED:
		libscf_handle_rebind(hndl);
		goto timestamp;

	case ENOENT:
	case EPERM:
	case EACCES:
	case EROFS:
		log_error(LOG_INFO, "Could set state of %s: %s.\n",
		    idata.i_fmri, strerror(r));
		break;

	case EINVAL:
	default:
		bad_error("_restarter_commit_states", r);
	}

	(void) scf_handle_unbind(hndl);
	scf_handle_destroy(hndl);

	return (0);
}

static int
read_startd_config(void)
{
	scf_handle_t *hndl;
	scf_instance_t *inst;
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_value_t *val;
	scf_iter_t *iter, *piter;
	char *buf, *vbuf;
	char *startd_options_fmri = uu_msprintf("%s/:properties/options",
	    SCF_SERVICE_STARTD);
	char *startd_reconfigure_fmri = uu_msprintf(
	    "%s/:properties/system/reconfigure", SCF_SERVICE_STARTD);
	char *env_opts, *lasts, *cp;
	int bind_fails = 0;
	int ret = 0;
	size_t sz;
	ctid_t ctid;
	uint64_t uint64;

	buf = startd_alloc(max_scf_fmri_size);

	if (startd_options_fmri == NULL || startd_reconfigure_fmri == NULL)
		uu_die("Allocation failure\n");

	st->st_log_prefix = LOG_PREFIX_EARLY;

	if ((st->st_log_file = getenv("STARTD_DEFAULT_LOG")) == NULL) {
		st->st_log_file = startd_alloc(strlen(STARTD_DEFAULT_LOG) + 1);

		(void) strcpy(st->st_log_file, STARTD_DEFAULT_LOG);
	}

	st->st_door_path = getenv("STARTD_ALT_DOOR");

	/*
	 * Read "options" property group.
	 */
	for (hndl = libscf_handle_create_bound(SCF_VERSION); hndl == NULL;
	    hndl = libscf_handle_create_bound(SCF_VERSION), bind_fails++) {
		(void) sleep(INITIAL_REBIND_DELAY);

		if (bind_fails > INITIAL_REBIND_ATTEMPTS) {
			/*
			 * In the case that we can't bind to the repository
			 * (which should have been started), we need to allow
			 * the user into maintenance mode to determine what's
			 * failed.
			 */
			log_framework(LOG_INFO, "Couldn't fetch "
			    "default settings: %s\n",
			    scf_strerror(scf_error()));

			ret = -1;

			goto noscfout;
		}
	}

	pg = safe_scf_pg_create(hndl);
	prop = safe_scf_property_create(hndl);
	val = safe_scf_value_create(hndl);
	inst = safe_scf_instance_create(hndl);

	/* set startd's restarter properties */
	if (scf_handle_decode_fmri(hndl, SCF_SERVICE_STARTD, NULL, NULL, inst,
	    NULL, NULL, SCF_DECODE_FMRI_EXACT) == 0) {
		(void) libscf_write_start_pid(inst, getpid());
		ctid = proc_get_ctid();
		if (ctid != -1) {
			uint64 = (uint64_t)ctid;
			(void) libscf_inst_set_count_prop(inst,
			    SCF_PG_RESTARTER, SCF_PG_RESTARTER_TYPE,
			    SCF_PG_RESTARTER_FLAGS, SCF_PROPERTY_CONTRACT,
			    uint64);
		}
		(void) libscf_note_method_log(inst, LOG_PREFIX_EARLY,
		    STARTD_DEFAULT_LOG);
		(void) libscf_note_method_log(inst, LOG_PREFIX_NORMAL,
		    STARTD_DEFAULT_LOG);
	}

	/* Read reconfigure property for recovery. */
	if (scf_handle_decode_fmri(hndl, startd_reconfigure_fmri, NULL, NULL,
	    NULL, NULL, prop, NULL) != -1 &&
	    scf_property_get_value(prop, val) == 0)
		(void) scf_value_get_boolean(val, &prop_reconfig);

	if (scf_handle_decode_fmri(hndl, startd_options_fmri, NULL, NULL, NULL,
	    pg, NULL, SCF_DECODE_FMRI_TRUNCATE) == -1) {
		/*
		 * No configuration options defined.
		 */
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			uu_warn("Couldn't read configuration from 'options' "
			    "group: %s\n", scf_strerror(scf_error()));
		goto scfout;
	}

	/*
	 * If there is no "options" group defined, then our defaults are fine.
	 */
	if (scf_pg_get_name(pg, NULL, 0) < 0)
		goto scfout;

	/* get info_events_all */
	info_events_all = libscf_get_info_events_all(pg);

	/* Iterate through. */
	iter = safe_scf_iter_create(hndl);

	(void) scf_iter_pg_properties(iter, pg);

	piter = safe_scf_iter_create(hndl);
	vbuf = startd_alloc(max_scf_value_size);

	while ((scf_iter_next_property(iter, prop) == 1)) {
		scf_type_t ty;

		if (scf_property_get_name(prop, buf, max_scf_fmri_size) < 0)
			continue;

		if (strcmp(buf, "logging") != 0 &&
		    strcmp(buf, "boot_messages") != 0)
			continue;

		if (scf_property_type(prop, &ty) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
			default:
				libscf_handle_rebind(hndl);
				continue;

			case SCF_ERROR_DELETED:
				continue;

			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
				bad_error("scf_property_type", scf_error());
			}
		}

		if (ty != SCF_TYPE_ASTRING) {
			uu_warn("property \"options/%s\" is not of type "
			    "astring; ignored.\n", buf);
			continue;
		}

		if (scf_property_get_value(prop, val) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
			default:
				return (ECONNABORTED);

			case SCF_ERROR_DELETED:
			case SCF_ERROR_NOT_FOUND:
				return (0);

			case SCF_ERROR_CONSTRAINT_VIOLATED:
				uu_warn("property \"options/%s\" has multiple "
				    "values; ignored.\n", buf);
				continue;

			case SCF_ERROR_PERMISSION_DENIED:
				uu_warn("property \"options/%s\" cannot be "
				    "read because startd has insufficient "
				    "permission; ignored.\n", buf);
				continue;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
				bad_error("scf_property_get_value",
				    scf_error());
			}
		}

		if (scf_value_get_astring(val, vbuf, max_scf_value_size) < 0)
			bad_error("scf_value_get_astring", scf_error());

		if (strcmp("logging", buf) == 0) {
			if (strcmp("verbose", vbuf) == 0) {
				st->st_boot_flags = STARTD_BOOT_VERBOSE;
				st->st_log_level_min = LOG_INFO;
			} else if (strcmp("debug", vbuf) == 0) {
				st->st_boot_flags = STARTD_BOOT_VERBOSE;
				st->st_log_level_min = LOG_DEBUG;
			} else if (strcmp("quiet", vbuf) == 0) {
				st->st_log_level_min = LOG_NOTICE;
			} else {
				uu_warn("unknown options/logging "
				    "value '%s' ignored\n", vbuf);
			}

		} else if (strcmp("boot_messages", buf) == 0) {
			if (strcmp("quiet", vbuf) == 0) {
				st->st_boot_flags = STARTD_BOOT_QUIET;
			} else if (strcmp("verbose", vbuf) == 0) {
				st->st_boot_flags = STARTD_BOOT_VERBOSE;
			} else {
				log_framework(LOG_NOTICE, "unknown "
				    "options/boot_messages value '%s' "
				    "ignored\n", vbuf);
			}

		}
	}

	startd_free(vbuf, max_scf_value_size);
	scf_iter_destroy(piter);

	scf_iter_destroy(iter);

scfout:
	scf_value_destroy(val);
	scf_pg_destroy(pg);
	scf_property_destroy(prop);
	scf_instance_destroy(inst);
	(void) scf_handle_unbind(hndl);
	scf_handle_destroy(hndl);

noscfout:
	startd_free(buf, max_scf_fmri_size);
	uu_free(startd_options_fmri);
	uu_free(startd_reconfigure_fmri);

	if (booting_to_single_user) {
		st->st_subgraph = startd_alloc(max_scf_fmri_size);
		sz = strlcpy(st->st_subgraph, "milestone/single-user:default",
		    max_scf_fmri_size);
		assert(sz < max_scf_fmri_size);
	}

	/*
	 * Options passed in as boot arguments override repository defaults.
	 */
	env_opts = getenv("SMF_OPTIONS");
	if (env_opts == NULL)
		return (ret);

	for (cp = strtok_r(env_opts, ",", &lasts); cp != NULL;
	    cp = strtok_r(NULL, ",", &lasts)) {
		if (strcmp(cp, "debug") == 0) {
			st->st_boot_flags = STARTD_BOOT_VERBOSE;
			st->st_log_level_min = LOG_DEBUG;

			/* -m debug should send messages to console */
			st->st_log_flags =
			    st->st_log_flags | STARTD_LOG_TERMINAL;
		} else if (strcmp(cp, "verbose") == 0) {
			st->st_boot_flags = STARTD_BOOT_VERBOSE;
			st->st_log_level_min = LOG_INFO;
		} else if (strcmp(cp, "seed") == 0) {
			uu_warn("SMF option \"%s\" unimplemented.\n", cp);
		} else if (strcmp(cp, "quiet") == 0) {
			st->st_log_level_min = LOG_NOTICE;
		} else if (strncmp(cp, "milestone=",
		    sizeof ("milestone=") - 1) == 0) {
			char *mp = cp + sizeof ("milestone=") - 1;

			if (booting_to_single_user)
				continue;

			if (st->st_subgraph == NULL) {
				st->st_subgraph =
				    startd_alloc(max_scf_fmri_size);
				st->st_subgraph[0] = '\0';
			}

			if (mp[0] == '\0' || strcmp(mp, "all") == 0) {
				(void) strcpy(st->st_subgraph, "all");
			} else if (strcmp(mp, "su") == 0 ||
			    strcmp(mp, "single-user") == 0) {
				(void) strcpy(st->st_subgraph,
				    "milestone/single-user:default");
			} else if (strcmp(mp, "mu") == 0 ||
			    strcmp(mp, "multi-user") == 0) {
				(void) strcpy(st->st_subgraph,
				    "milestone/multi-user:default");
			} else if (strcmp(mp, "mus") == 0 ||
			    strcmp(mp, "multi-user-server") == 0) {
				(void) strcpy(st->st_subgraph,
				    "milestone/multi-user-server:default");
			} else if (strcmp(mp, "none") == 0) {
				(void) strcpy(st->st_subgraph, "none");
			} else {
				log_framework(LOG_NOTICE,
				    "invalid milestone option value "
				    "'%s' ignored\n", mp);
			}
		} else {
			uu_warn("Unknown SMF option \"%s\".\n", cp);
		}
	}

	return (ret);
}

/*
 * void set_boot_env()
 *
 * If -r was passed or /reconfigure exists, this is a reconfig
 * reboot.  We need to make sure that this information is given
 * to the appropriate services the first time they're started
 * by setting the system/reconfigure repository property,
 * as well as pass the _INIT_RECONFIG variable on to the rcS
 * start method so that legacy services can continue to use it.
 *
 * This function must never be called before contract_init(), as
 * it sets st_initial.  get_startd_config() sets prop_reconfig from
 * pre-existing repository state.
 */
static void
set_boot_env()
{
	struct stat sb;
	int r;

	/*
	 * Check if property still is set -- indicates we didn't get
	 * far enough previously to unset it.  Otherwise, if this isn't
	 * the first startup, don't re-process /reconfigure or the
	 * boot flag.
	 */
	if (prop_reconfig != 1 && st->st_initial != 1)
		return;

	/* If /reconfigure exists, also set opt_reconfig. */
	if (stat("/reconfigure", &sb) != -1)
		opt_reconfig = 1;

	/* Nothing to do.  Just return. */
	if (opt_reconfig == 0 && prop_reconfig == 0)
		return;

	/*
	 * Set startd's reconfigure property.  This property is
	 * then cleared by successful completion of the single-user
	 * milestone.
	 */
	if (prop_reconfig != 1) {
		r = libscf_set_reconfig(1);
		switch (r) {
		case 0:
			break;

		case ENOENT:
		case EPERM:
		case EACCES:
		case EROFS:
			log_error(LOG_WARNING, "Could not set reconfiguration "
			    "property: %s\n", strerror(r));
			break;

		default:
			bad_error("libscf_set_reconfig", r);
		}
	}
}

static void
startup(void)
{
	ctid_t configd_ctid;
	int err;

	/*
	 * Initialize data structures.
	 */
	gu = startd_zalloc(sizeof (graph_update_t));
	ru = startd_zalloc(sizeof (restarter_update_t));

	(void) pthread_cond_init(&st->st_load_cv, NULL);
	(void) pthread_cond_init(&st->st_configd_live_cv, NULL);
	(void) pthread_cond_init(&gu->gu_cv, NULL);
	(void) pthread_cond_init(&gu->gu_freeze_cv, NULL);
	(void) pthread_cond_init(&ru->restarter_update_cv, NULL);
	(void) pthread_mutex_init(&st->st_load_lock, &mutex_attrs);
	(void) pthread_mutex_init(&st->st_configd_live_lock, &mutex_attrs);
	(void) pthread_mutex_init(&gu->gu_lock, &mutex_attrs);
	(void) pthread_mutex_init(&gu->gu_freeze_lock, &mutex_attrs);
	(void) pthread_mutex_init(&ru->restarter_update_lock, &mutex_attrs);

	configd_ctid = contract_init();

	if (configd_ctid != -1)
		log_framework(LOG_DEBUG, "Existing configd contract %ld; not "
		    "starting svc.configd\n", configd_ctid);

	(void) startd_thread_create(fork_configd_thread, (void *)configd_ctid);

	/*
	 * Await, if necessary, configd's initial arrival.
	 */
	MUTEX_LOCK(&st->st_configd_live_lock);
	while (!st->st_configd_lives) {
		log_framework(LOG_DEBUG, "Awaiting cv signal on "
		    "configd_live_cv\n");
		err = pthread_cond_wait(&st->st_configd_live_cv,
		    &st->st_configd_live_lock);
		assert(err == 0);
	}
	MUTEX_UNLOCK(&st->st_configd_live_lock);

	utmpx_init();
	wait_init();

	if (read_startd_config())
		log_framework(LOG_INFO, "svc.configd unable to provide startd "
		    "optional settings\n");

	log_init();
	dict_init();
	timeout_init();
	restarter_protocol_init();
	restarter_init();

	/*
	 * svc.configd is started by fork_configd_thread so repository access is
	 * available, run early manifest import before continuing with starting
	 * graph engine and the rest of startd.
	 */
	log_framework(LOG_DEBUG, "Calling fork_emi...\n");
	fork_emi();

	if (startd_set_online())
		log_framework(LOG_INFO, "Unable to set state for %s\n",
		    SCF_SERVICE_STARTD);

	graph_protocol_init();
	graph_init();

	init_env();

	set_boot_env();
	restarter_start();
	graph_engine_start();
}

static void
usage(const char *name)
{
	uu_warn(gettext("usage: %s [-n]\n"), name);
	exit(UU_EXIT_USAGE);
}

static int
daemonize_start(void)
{
	pid_t pid;
	int fd;

	if ((pid = fork1()) < 0)
		return (-1);

	if (pid != 0)
		exit(0);

	(void) close(STDIN_FILENO);

	if ((fd = open("/dev/null", O_RDONLY)) == -1) {
		uu_warn(gettext("can't connect stdin to /dev/null"));
	} else if (fd != STDIN_FILENO) {
		(void) dup2(fd, STDIN_FILENO);
		startd_close(fd);
	}

	closefrom(3);
	(void) dup2(STDERR_FILENO, STDOUT_FILENO);

	(void) setsid();
	(void) chdir("/");

	/* Use default umask that init handed us, but 022 to create files. */
	dmask = umask(022);
	fmask = umask(dmask);

	return (0);
}

/*ARGSUSED*/
static void
die_handler(int sig, siginfo_t *info, void *data)
{
	finished = 1;
}

int
main(int argc, char *argv[])
{
	int opt;
	int daemonize = 1;
	struct sigaction act;
	sigset_t nullset;
	struct stat sb;

	(void) uu_setpname(argv[0]);

	st = startd_zalloc(sizeof (startd_state_t));

	(void) pthread_mutexattr_init(&mutex_attrs);
#ifndef	NDEBUG
	(void) pthread_mutexattr_settype(&mutex_attrs,
	    PTHREAD_MUTEX_ERRORCHECK);
#endif

	max_scf_name_size = scf_limit(SCF_LIMIT_MAX_NAME_LENGTH);
	max_scf_value_size = scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	max_scf_fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH);

	if (max_scf_name_size == -1 || max_scf_value_size == -1 ||
	    max_scf_value_size == -1)
		uu_die("Can't determine repository maximum lengths.\n");

	max_scf_name_size++;
	max_scf_value_size++;
	max_scf_fmri_size++;

	st->st_log_flags = STARTD_LOG_FILE | STARTD_LOG_SYSLOG;
	st->st_log_level_min = LOG_NOTICE;

	while ((opt = getopt(argc, argv, "nrs")) != EOF) {
		switch (opt) {
		case 'n':
			daemonize = 0;
			break;
		case 'r':			/* reconfiguration boot */
			opt_reconfig = 1;
			break;
		case 's':			/* single-user mode */
			booting_to_single_user = B_TRUE;
			break;
		default:
			usage(argv[0]);		/* exits */
		}
	}

	if (optind != argc)
		usage(argv[0]);

	(void) enable_extended_FILE_stdio(-1, -1);

	if (daemonize)
		if (daemonize_start() < 0)
			uu_die("Can't daemonize\n");

	log_init();

	if (stat(_PATH_SYSVOL "/resetting", &sb) != -1) {
		log_framework(LOG_NOTICE, "Restarter quiesced.\n");

		for (;;)
			(void) pause();
	}

	act.sa_sigaction = &die_handler;
	(void) sigfillset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	(void) sigaction(SIGINT, &act, NULL);
	(void) sigaction(SIGTERM, &act, NULL);

	startup();

	(void) sigemptyset(&nullset);
	while (!finished) {
		log_framework(LOG_DEBUG, "Main thread paused\n");
		(void) sigsuspend(&nullset);
	}

	(void) log_framework(LOG_DEBUG, "Restarter exiting.\n");
	return (0);
}
