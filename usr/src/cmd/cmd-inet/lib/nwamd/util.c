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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * util.c contains a set of miscellaneous utility functions which,
 * among other things:
 * - start a child process
 * - look up the zone name
 * - look up/set SMF properties
 * - turn on/off privs
 */

#include <assert.h>
#include <auth_attr.h>
#include <auth_list.h>
#include <errno.h>
#include <libdllink.h>
#include <limits.h>
#include <libscf.h>
#include <net/if.h>
#include <priv_utils.h>
#include <pthread.h>
#include <pwd.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stropts.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>
#include <zone.h>

#include "util.h"
#include "llp.h"

extern char **environ;
extern sigset_t original_sigmask;

/*
 * A holder for all the resources needed to get a property value
 * using libscf.
 */
typedef struct scf_resources {
	scf_handle_t *sr_handle;
	scf_instance_t *sr_inst;
	scf_snapshot_t *sr_snap;
	scf_propertygroup_t *sr_pg;
	scf_property_t *sr_prop;
	scf_value_t *sr_val;
	scf_transaction_t *sr_tx;
	scf_transaction_entry_t *sr_ent;
} scf_resources_t;

/*
 * mutex used while setting privileges and effective UID.
 */
static pthread_mutex_t priv_mutex = PTHREAD_MUTEX_INITIALIZER;
/*
 * Some actions related to sysevents require us to be root.  This counts the
 * number of threads wanting to be root at any time.  It is protected by
 * priv_mutex.
 */
static int root_cnt = 0;
/*
 * When changing the effective UID to root, this variable is used to remember
 * the effective privilege set at that time so that it can be restored when
 * UID is switched back to netadm.
 */
static priv_set_t *eset = NULL;
/*
 * An entry for our privilege table.  It contains the names of the privileges
 * and individual privilege sets that we will ever request along with the
 * count of the number of threads that have requested it at any moment.
 */
struct priv_data {
	const char	*pname;
	priv_set_t	*pset;
	int		pcnt;
} *priv_table;
/*
 * Number of entries in our priv_table.
 */
static int priv_table_sz;

/*
 * List of privileges we'll ever need.  It is used to populate our priv_table
 * that is used for counting the number of threads currently wanting the
 * privilege turned ON.
 */
#define	PRIV_NEEDED	PRIV_PROC_SETID, PRIV_PROC_AUDIT, PRIV_SYS_MOUNT, \
			PRIV_FILE_DAC_WRITE, PRIV_IPC_DAC_WRITE, \
			PRIV_FILE_CHOWN_SELF, PRIV_FILE_OWNER, \
			PRIV_SYS_DL_CONFIG, PRIV_SYS_IP_CONFIG,	\
			PRIV_NET_RAWACCESS, PRIV_NET_PRIVADDR, \
			"zone"

#define	RBAC_FMRI	"svc:/system/rbac:default"

/*
 * Initializes the priv_table and returns a privilege set that has the
 * privileges to include in the permitted set.
 */
static priv_set_t *
nwamd_init_priv_table(int flag, ...)
{
	va_list ap;
	const char *priv;
	int i = 0;
	priv_set_t *perm;

	/* build the permitted privilege set as we traverse the va_list */
	if ((perm = priv_allocset()) == NULL) {
		pfail("could not allocate permitted priv set: %s",
		    strerror(errno));
	}
	priv_basicset(perm);

	/*
	 * Count the number of privileges that will be in the priv_table first
	 * before allocating the privilege table.
	 */
	priv_table_sz = 0;
	va_start(ap, flag);
	while ((priv = va_arg(ap, const char *)) != NULL)
		priv_table_sz++;
	va_end(ap);

	(void) pthread_mutex_lock(&priv_mutex);

	priv_table = calloc(priv_table_sz, sizeof (struct priv_data));
	if (priv_table == NULL)
		pfail("couldn't allocate priv_table: %s", strerror(errno));

	/* Add the given privileges to the privilege table */
	va_start(ap, flag);
	while ((priv = va_arg(ap, const char *)) != NULL) {
		priv_table[i].pset = priv_str_to_set(priv, ",", NULL);
		if (priv_table[i].pset == NULL) {
			pfail("could not create priv set for '%s': %s",
			    priv, strerror(errno));
		}
		priv_table[i].pname = priv;
		priv_table[i].pcnt = 0;

		/*
		 * At this point, nwamd is running as root with all available
		 * privileges.  Add this privilege to the permitted set only
		 * if the privilege exists in the effective set.  In a zone,
		 * some privileges may not exist.  In such a case, don't
		 * include it in the permitted set.  If a function requires
		 * this privilege, then that call will fail.
		 */
		if (strcmp(priv, "zone") == 0 || priv_ineffect(priv))
			(void) priv_union(priv_table[i].pset, perm);
		i++;
	}
	va_end(ap);

	(void) pthread_mutex_unlock(&priv_mutex);

	return (perm);
}

/*
 * Initializes the privilege table and sets the effective, permitted and
 * inheritable privilege sets.
 */
void
nwamd_init_privileges(void)
{
	priv_set_t *perm, *basic;

	if ((basic = priv_allocset()) == NULL)
		pfail("could not allocate basic priv set: %s", strerror(errno));
	priv_basicset(basic);

	perm = nwamd_init_priv_table(0, PRIV_NEEDED, NULL);

	(void) pthread_mutex_lock(&priv_mutex);

	/* Set the permitted privilege set */
	if (setppriv(PRIV_SET, PRIV_PERMITTED, perm) != 0) {
		pfail("could not set the permitted priv set: %s",
		    strerror(errno));
	}
	/* Set the effective and inheritable privilege set to the basic set */
	if (setppriv(PRIV_SET, PRIV_INHERITABLE, basic) != 0) {
		pfail("could not set the inheritable priv set: %s",
		    strerror(errno));
	}
	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, basic) != 0) {
		pfail("could not set the effective priv set: %s",
		    strerror(errno));
	}

	(void) pthread_mutex_unlock(&priv_mutex);

	priv_freeset(perm);
	priv_freeset(basic);
}

/*
 * Common PRIV_ON and PRIV_OFF operations on the effective privilege set.
 */
static void
nwamd_priv_common(boolean_t add, va_list ap)
{
	const char *priv;
	int i;

	(void) pthread_mutex_lock(&priv_mutex);

	while ((priv = va_arg(ap, const char *)) != NULL) {
		/* search our privilege table for this privilege */
		for (i = 0; i < priv_table_sz; i++) {
			if (strcasecmp(priv_table[i].pname, priv) == 0)
				break;
		}
		if (i == priv_table_sz) {
			nlog(LOG_ERR, "nwamd_priv_common: "
			    "trying to add/remove priv '%s': not in our table",
			    priv);
			(void) pthread_mutex_unlock(&priv_mutex);
			return;
		}

		/* found it */
		if (add) {
			if (priv_table[i].pcnt == 0) {
				(void) setppriv(PRIV_ON, PRIV_EFFECTIVE,
				    priv_table[i].pset);
				(void) setppriv(PRIV_ON, PRIV_INHERITABLE,
				    priv_table[i].pset);
			}
			priv_table[i].pcnt++;

			/*
			 * If currently running as root, add the requested
			 * privilege to the saved eset so that it can be set
			 * if root is released.
			 */
			if (root_cnt != 0)
				(void) priv_addset(eset, priv);
		} else {
			priv_table[i].pcnt--;

			/*
			 * Only drop the privileges if not running as root and
			 * count is 0.
			 */
			if (root_cnt == 0 && priv_table[i].pcnt == 0) {
				(void) setppriv(PRIV_OFF, PRIV_EFFECTIVE,
				    priv_table[i].pset);
				(void) setppriv(PRIV_OFF, PRIV_INHERITABLE,
				    priv_table[i].pset);
			}

			/*
			 * If running as root, remove the requested privilege
			 * set from the saved eset so that it is not set when
			 * root is released.
			 */
			if (root_cnt != 0 && priv_table[i].pcnt == 0)
				(void) priv_delset(eset, priv);
		}
	}

	(void) pthread_mutex_unlock(&priv_mutex);
}

/*
 * Add the given set of privileges to the effective set.
 */
void
nwamd_add_privs(int flag, ...)
{
	va_list ap;

	va_start(ap, flag);
	nwamd_priv_common(B_TRUE, ap);
	va_end(ap);
}

/*
 * Drop the given set of privileges from the effective set.
 */
void
nwamd_drop_privs(int flag, ...)
{
	va_list ap;

	va_start(ap, flag);
	nwamd_priv_common(B_FALSE, ap);
	va_end(ap);
}

/*
 * Decrement the root_cnt and find the "zone" entry in the privilege table and
 * decrease the pcnt entry for it.  This function MUST be called with the
 * priv_mutex held.
 */
static void
nwamd_decrement_root_cnt_locked()
{
	int i;

	root_cnt--;
	for (i = 0; i < priv_table_sz; i++) {
		if (strcasecmp(priv_table[i].pname, "zone") == 0) {
			priv_table[i].pcnt--;
			break;
		}
	}
}

/*
 * Some actions related to sysevents require nwamd to be root.
 */
void
nwamd_become_root(void)
{
	(void) pthread_mutex_lock(&priv_mutex);
	if (root_cnt == 0) {
		if ((eset = priv_allocset()) == NULL) {
			nlog(LOG_ERR, "nwamd_become_root: could not allocate "
			    "for the currently effective priv set: %s",
			    strerror(errno));
			(void) pthread_mutex_unlock(&priv_mutex);
			return;
		}
		/*
		 * Retrieve the currently effective privilege so that it can be
		 * restored when the root is released.
		 */
		if (getppriv(PRIV_EFFECTIVE, eset) != 0) {
			nlog(LOG_ERR, "nwamd_become_root: could not get "
			    "the currently effective priv set: %s",
			    strerror(errno));
			priv_freeset(eset);
			(void) pthread_mutex_unlock(&priv_mutex);
			return;
		}
	}
	root_cnt++;
	(void) pthread_mutex_unlock(&priv_mutex);

	/* add the "zone" privilege set and increment its pcnt */
	nwamd_add_privs(0, "zone", NULL);

	(void) pthread_mutex_lock(&priv_mutex);
	if (root_cnt == 1 && setuid(0) == -1) {
		nlog(LOG_ERR, "nwamd_become_root: setuid(0) failed: %s",
		    strerror(errno));

		/* decrement root_cnt and "zone" pcnt */
		nwamd_decrement_root_cnt_locked();

		/* revert the effective and inheritable privilege set */
		if (setppriv(PRIV_SET, PRIV_EFFECTIVE, eset) != 0) {
			nlog(LOG_ERR, "nwamd_become_root: "
			    "could not set currently effective priv set: %s",
			    strerror(errno));
		}
		if (setppriv(PRIV_SET, PRIV_INHERITABLE, eset) != 0) {
			nlog(LOG_ERR, "nwamd_become_root: "
			    "could not set currently inheritable priv set: %s",
			    strerror(errno));
		}

		priv_freeset(eset);
		(void) pthread_mutex_unlock(&priv_mutex);
		return;
	} else {
		(void) pthread_mutex_unlock(&priv_mutex);
	}

	nlog(LOG_INFO, "nwamd now running as root");
}

/*
 * Stop running as root and switch back to netadm.
 */
void
nwamd_release_root(void)
{
	nlog(LOG_DEBUG, "nwamd_release_root()");

	(void) pthread_mutex_lock(&priv_mutex);

	/*
	 * Rather than calling nwamd_drop_privs() with the "zone" privilege
	 * set to drop all the privileges and decrease the pcnt for the "zone"
	 * entry, simply update the pcnt entry here and set the effective and
	 * inheritable privilege set to the saved eset.
	 */
	nwamd_decrement_root_cnt_locked();

	if (root_cnt == 0) {
		if (setppriv(PRIV_SET, PRIV_EFFECTIVE, eset) != 0) {
			nlog(LOG_ERR, "nwamd_release_root: could not set the "
			    "currently effective priv set: %s",
			    strerror(errno));
		}
		if (setppriv(PRIV_SET, PRIV_INHERITABLE, eset) != 0) {
			nlog(LOG_ERR, "nwamd_release_root: could not set the "
			    "currently inheritable priv set: %s",
			    strerror(errno));
		}
		priv_freeset(eset);
		(void) pthread_mutex_unlock(&priv_mutex);

		nwamd_become_netadm();
		nlog(LOG_INFO, "nwamd now back to running as netadm");
	} else {
		(void) pthread_mutex_unlock(&priv_mutex);
	}
}

/* ARGSUSED */
static void *
nwamd_become_netadm_thread(void *arg)
{
	(void) sleep(1);
	nwamd_become_netadm();
	return (NULL);
}

/*
 * Change the UID and GID of nwamd to netadm. If the RBAC service is not
 * online yet or if the changing of UID and/or GID fails, a new thread is
 * created that periodically tries to change the UID and GID to netadm.  This
 * function must be called WITHOUT the priv_mutex held.
 */
void
nwamd_become_netadm(void)
{
	struct passwd *pwd = NULL;
	char *rbac_state = NULL;
	int rc = -1;

	if (getuid() == UID_NETADM) {
		nlog(LOG_DEBUG, "nwamd_become_netadm: already netadm");
		return;
	}

	/*
	 * If the netadm user already has the solaris.network.autoconf.read
	 * authorization, then we don't need to wait for the RBAC service to
	 * be online as that user's authorization has already been set.  This
	 * is the usual case after the very first boot.
	 */
	if ((pwd = getpwuid(UID_NETADM)) == NULL) {
		nlog(LOG_ERR, "nwamd_become_netadm: getpwuid(%d) failed: %s",
		    UID_NETADM, strerror(errno));
		goto thread;
	}
	if (chkauthattr(AUTOCONF_READ_AUTH, pwd->pw_name)) {
		/* authorization exists */
		goto set;
	}

	/*
	 * The authorization does not exist.  Change the UID and GID to netadm
	 * only if the RBAC service is online.
	 */
	if ((rbac_state = smf_get_state(RBAC_FMRI)) == NULL) {
		nlog(LOG_ERR, "nwamd_become_netadm: "
		    "couldn't get the state of %s", RBAC_FMRI);
		goto thread;
	}
	if (strcmp(rbac_state, SCF_STATE_STRING_ONLINE) != 0) {
		nlog(LOG_INFO, "nwamd_become_netadm: %s is not online yet",
		    RBAC_FMRI);
		goto thread;
	}

set:
	nwamd_add_privs(0, PRIV_PROC_SETID, NULL);
	if ((rc = setuid(UID_NETADM)) == -1) {
		nlog(LOG_ERR, "nwamd_become_netadm: "
		    "setuid() to netadm failed: %s", strerror(errno));
	}
	if (rc != -1 && (rc = setgid(GID_NETADM)) == -1) {
		nlog(LOG_ERR, "nwamd_become_netadm: "
		    "setgid() to netadm failed: %s", strerror(errno));
	}
	nwamd_drop_privs(0, PRIV_PROC_SETID, NULL);

thread:
	endpwent();
	free(rbac_state);

	/*
	 * If we could not set UID and/or GID to netadm, start a detached
	 * thread that periodically tries to change the UID and GID to netadm.
	 */
	if (rc == -1) {
		pthread_attr_t attr;

		nlog(LOG_DEBUG, "nwamd_become_netadm: "
		    "starting thread to change UID to netadm");

		(void) pthread_attr_init(&attr);
		(void) pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		(void) pthread_create(NULL, &attr, nwamd_become_netadm_thread,
		    NULL);
		(void) pthread_attr_destroy(&attr);
	}
}

/*
 *
 * This starts a child process determined by command.  If command contains a
 * slash then it is assumed to be a full path; otherwise the path is searched
 * for an executable file with the name command.  Command is also used as
 * argv[0] of the new process.  The rest of the arguments of the function
 * up to the first NULL make up pointers to arguments of the new process.
 *
 * This function returns child exit status on success and -1 on failure.
 *
 * NOTE: original_sigmask must be set before this function is called.
 */
int
nwamd_start_childv(const char *command, char const * const *argv)
{
	posix_spawnattr_t attr;
	sigset_t fullset;
	int i, rc, status, n;
	pid_t pid;
	char vbuf[1024];

	vbuf[0] = 0;
	n = sizeof (vbuf);
	for (i = 1; argv[i] != NULL && n > 2; i++) {
		n -= strlcat(vbuf, " ", n);
		n -= strlcat(vbuf, argv[i], n);
	}
	if (argv[i] != NULL || n < 0)
		nlog(LOG_ERR, "nwamd_start_childv can't log full arg vector");

	if ((rc = posix_spawnattr_init(&attr)) != 0) {
		nlog(LOG_DEBUG, "posix_spawnattr_init %d %s\n",
		    rc, strerror(rc));
		return (-1);
	}
	(void) sigfillset(&fullset);
	if ((rc = posix_spawnattr_setsigdefault(&attr, &fullset)) != 0) {
		nlog(LOG_DEBUG, "setsigdefault %d %s\n", rc, strerror(rc));
		return (-1);
	}
	if ((rc = posix_spawnattr_setsigmask(&attr, &original_sigmask)) != 0) {
		nlog(LOG_DEBUG, "setsigmask %d %s\n", rc, strerror(rc));
		return (-1);
	}
	if ((rc = posix_spawnattr_setflags(&attr,
	    POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETSIGMASK)) != 0) {
		nlog(LOG_DEBUG, "setflags %d %s\n", rc, strerror(rc));
		return (-1);
	}

	if ((rc = posix_spawnp(&pid, command, NULL, &attr, (char * const *)argv,
	    environ)) > 0) {
		nlog(LOG_DEBUG, "posix_spawnp failed errno %d", rc);
		return (-1);
	}

	if ((rc = posix_spawnattr_destroy(&attr)) != 0) {
		nlog(LOG_DEBUG, "posix_spawn_attr_destroy %d %s\n",
		    rc, strerror(rc));
		return (-1);
	}

	(void) waitpid(pid, &status, 0);
	if (WIFSIGNALED(status) || WIFSTOPPED(status)) {
		i = WIFSIGNALED(status) ? WTERMSIG(status) : WSTOPSIG(status);
		nlog(LOG_ERR, "'%s%s' %s with signal %d (%s)", command, vbuf,
		    (WIFSIGNALED(status) ? "terminated" : "stopped"), i,
		    strsignal(i));
		return (-2);
	} else {
		nlog(LOG_INFO, "'%s%s' completed normally: %d", command, vbuf,
		    WEXITSTATUS(status));
		return (WEXITSTATUS(status));
	}
}

/*
 * For global zone, check if the link is used by a non-global
 * zone, note that the non-global zones doesn't need this check,
 * because zoneadm has taken care of this when the zone boots.
 * In the global zone, we ignore events for local-zone-owned links
 * since these are taken care of by the local zone's network
 * configuration services.
 */
boolean_t
nwamd_link_belongs_to_this_zone(const char *linkname)
{
	zoneid_t zoneid;
	char zonename[ZONENAME_MAX];
	int ret;

	zoneid = getzoneid();
	if (zoneid == GLOBAL_ZONEID) {
		datalink_id_t linkid;
		dladm_status_t status;
		char errstr[DLADM_STRSIZE];

		if ((status = dladm_name2info(dld_handle, linkname, &linkid,
		    NULL, NULL, NULL)) != DLADM_STATUS_OK) {
			nlog(LOG_DEBUG, "nwamd_link_belongs_to_this_zone: "
			    "could not get linkid for %s: %s",
			    linkname, dladm_status2str(status, errstr));
			return (B_FALSE);
		}
		zoneid = ALL_ZONES;
		ret = zone_check_datalink(&zoneid, linkid);
		if (ret == 0) {
			(void) getzonenamebyid(zoneid, zonename, ZONENAME_MAX);
			nlog(LOG_DEBUG, "nwamd_link_belongs_to_this_zone: "
			    "%s is used by non-global zone: %s",
			    linkname, zonename);
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

/*
 * Inputs:
 *   res is a pointer to the scf_resources_t to be released.
 */
static void
release_scf_resources(scf_resources_t *res)
{
	scf_entry_destroy(res->sr_ent);
	scf_transaction_destroy(res->sr_tx);
	scf_value_destroy(res->sr_val);
	scf_property_destroy(res->sr_prop);
	scf_pg_destroy(res->sr_pg);
	scf_snapshot_destroy(res->sr_snap);
	scf_instance_destroy(res->sr_inst);
	(void) scf_handle_unbind(res->sr_handle);
	scf_handle_destroy(res->sr_handle);
}

/*
 * Inputs:
 *   fmri is the instance to look up
 * Outputs:
 *   res is a pointer to an scf_resources_t.  This is an internal
 *   structure that holds all the handles needed to get a specific
 *   property from the running snapshot; on a successful return it
 *   contains the scf_value_t that should be passed to the desired
 *   scf_value_get_foo() function, and must be freed after use by
 *   calling release_scf_resources().  On a failure return, any
 *   resources that may have been assigned to res are released, so
 *   the caller does not need to do any cleanup in the failure case.
 * Returns:
 *    0 on success
 *   -1 on failure
 */

static int
create_scf_resources(const char *fmri, scf_resources_t *res)
{
	res->sr_tx = NULL;
	res->sr_ent = NULL;
	res->sr_inst = NULL;
	res->sr_snap = NULL;
	res->sr_pg = NULL;
	res->sr_prop = NULL;
	res->sr_val = NULL;

	if ((res->sr_handle = scf_handle_create(SCF_VERSION)) == NULL) {
		return (-1);
	}

	if (scf_handle_bind(res->sr_handle) != 0) {
		scf_handle_destroy(res->sr_handle);
		return (-1);
	}
	if ((res->sr_inst = scf_instance_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	if (scf_handle_decode_fmri(res->sr_handle, fmri, NULL, NULL,
	    res->sr_inst, NULL, NULL, SCF_DECODE_FMRI_REQUIRE_INSTANCE) != 0) {
		goto failure;
	}
	if ((res->sr_snap = scf_snapshot_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	if (scf_instance_get_snapshot(res->sr_inst, "running",
	    res->sr_snap) != 0) {
		goto failure;
	}
	if ((res->sr_pg = scf_pg_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	if ((res->sr_prop = scf_property_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	if ((res->sr_val = scf_value_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	if ((res->sr_tx = scf_transaction_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	if ((res->sr_ent = scf_entry_create(res->sr_handle)) == NULL) {
		goto failure;
	}
	return (0);

failure:
	nlog(LOG_ERR, "create_scf_resources failed: %s",
	    scf_strerror(scf_error()));
	release_scf_resources(res);
	return (-1);
}

/*
 * Inputs:
 *   fmri is the instance to look up
 *   pg is the property group to look up
 *   prop is the property within that group to look up
 *   running specifies if running snapshot is to be used
 * Outputs:
 *   res is a pointer to an scf_resources_t.  This is an internal
 *   structure that holds all the handles needed to get a specific
 *   property from the running snapshot; on a successful return it
 *   contains the scf_value_t that should be passed to the desired
 *   scf_value_get_foo() function, and must be freed after use by
 *   calling release_scf_resources().  On a failure return, any
 *   resources that may have been assigned to res are released, so
 *   the caller does not need to do any cleanup in the failure case.
 * Returns:
 *    0 on success
 *   -1 on failure
 */
static int
get_property_value(const char *fmri, const char *pg, const char *prop,
    boolean_t running, scf_resources_t *res)
{
	if (create_scf_resources(fmri, res) != 0)
		return (-1);

	if (scf_instance_get_pg_composed(res->sr_inst,
	    running ? res->sr_snap : NULL, pg, res->sr_pg) != 0) {
		goto failure;
	}
	if (scf_pg_get_property(res->sr_pg, prop, res->sr_prop) != 0) {
		goto failure;
	}
	if (scf_property_get_value(res->sr_prop, res->sr_val) != 0) {
		goto failure;
	}
	return (0);

failure:
	release_scf_resources(res);
	return (-1);
}

/*
 * Inputs:
 *   lfmri is the instance fmri to look up
 *   lpg is the property group to look up
 *   lprop is the property within that group to look up
 * Outputs:
 *   answer is a pointer to the property value
 * Returns:
 *    0 on success
 *   -1 on failure
 * If successful, the property value is retured in *answer.
 * Otherwise, *answer is undefined, and it is up to the caller to decide
 * how to handle that case.
 */
int
nwamd_lookup_boolean_property(const char *lfmri, const char *lpg,
    const char *lprop, boolean_t *answer)
{
	int result = -1;
	scf_resources_t res;
	uint8_t prop_val;

	if (get_property_value(lfmri, lpg, lprop, B_TRUE, &res) != 0) {

		/*
		 * an error was already logged by get_property_value,
		 * and it released any resources assigned to res before
		 * returning.
		 */
		return (result);
	}
	if (scf_value_get_boolean(res.sr_val, &prop_val) != 0) {
		goto cleanup;
	}
	*answer = (boolean_t)prop_val;
	result = 0;
cleanup:
	release_scf_resources(&res);
	return (result);
}

/*
 * Inputs:
 *   lfmri is the instance fmri to look up
 *   lpg is the property group to look up
 *   lprop is the property within that group to look up
 *   buf is the place to put the answer
 *   bufsz is the size of buf
 * Outputs:
 *
 * Returns:
 *    0 on success
 *   -1 on failure
 * If successful, the property value is retured in buf.
 * Otherwise, buf is undefined, and it is up to the caller to decide
 * how to handle that case.
 */
int
nwamd_lookup_string_property(const char *lfmri, const char *lpg,
    const char *lprop, char *buf, size_t bufsz)
{
	int result = -1;
	scf_resources_t res;

	if (get_property_value(lfmri, lpg, lprop, B_TRUE, &res) != 0) {
		/*
		 * The above function fails when trying to get a
		 * non-persistent property group from the running snapshot.
		 * Try going for the non-running snapshot.
		 */
		if (get_property_value(lfmri, lpg, lprop, B_FALSE, &res) != 0) {
			/*
			 * an error was already logged by get_property_value,
			 * and it released any resources assigned to res before
			 * returning.
			 */
			return (result);
		}
	}
	if (scf_value_get_astring(res.sr_val, buf, bufsz) == 0)
		goto cleanup;

	result = 0;
cleanup:
	release_scf_resources(&res);
	return (result);
}

/*
 * Inputs:
 *   lfmri is the instance fmri to look up
 *   lpg is the property group to look up
 *   lprop is the property within that group to look up
 * Outputs:
 *   answer is a pointer to the property value
 * Returns:
 *    0 on success
 *   -1 on failure
 * If successful, the property value is retured in *answer.
 * Otherwise, *answer is undefined, and it is up to the caller to decide
 * how to handle that case.
 */
int
nwamd_lookup_count_property(const char *lfmri, const char *lpg,
    const char *lprop, uint64_t *answer)
{
	int result = -1;
	scf_resources_t res;

	if (get_property_value(lfmri, lpg, lprop, B_TRUE, &res) != 0) {

		/*
		 * an error was already logged by get_property_value,
		 * and it released any resources assigned to res before
		 * returning.
		 */
		return (result);
	}
	if (scf_value_get_count(res.sr_val, answer) != 0) {
		goto cleanup;
	}
	result = 0;
cleanup:
	release_scf_resources(&res);
	return (result);
}

static int
set_property_value(scf_resources_t *res, const char *propname,
    scf_type_t proptype)
{
	int result = -1;
	boolean_t new;

retry:
	new = (scf_pg_get_property(res->sr_pg, propname, res->sr_prop) != 0);

	if (scf_transaction_start(res->sr_tx, res->sr_pg) == -1) {
		goto failure;
	}
	if (new) {
		if (scf_transaction_property_new(res->sr_tx, res->sr_ent,
		    propname, proptype) == -1) {
			goto failure;
		}
	} else {
		if (scf_transaction_property_change(res->sr_tx, res->sr_ent,
		    propname, proptype) == -1) {
			goto failure;
		}
	}

	if (scf_entry_add_value(res->sr_ent, res->sr_val) != 0) {
		goto failure;
	}

	result = scf_transaction_commit(res->sr_tx);
	if (result == 0) {
		scf_transaction_reset(res->sr_tx);
		if (scf_pg_update(res->sr_pg) == -1) {
			goto failure;
		}
		nlog(LOG_INFO, "set_property_value: transaction commit failed "
		    "for %s; retrying", propname);
		goto retry;
	}
	if (result == -1)
		goto failure;
	return (0);

failure:
	return (-1);
}

int
nwamd_set_count_property(const char *fmri, const char *pg, const char *prop,
    uint64_t count)
{
	scf_resources_t res;

	if (create_scf_resources(fmri, &res) != 0)
		return (-1);

	if (scf_instance_add_pg(res.sr_inst, pg, SCF_GROUP_APPLICATION,
	    SCF_PG_FLAG_NONPERSISTENT, res.sr_pg) != 0) {
		if (scf_error() != SCF_ERROR_EXISTS)
			goto failure;
		if (scf_instance_get_pg_composed(res.sr_inst, NULL, pg,
		    res.sr_pg) != 0)
			goto failure;
	}

	scf_value_set_count(res.sr_val, (uint64_t)count);

	if (set_property_value(&res, prop, SCF_TYPE_COUNT) != 0)
		goto failure;

	release_scf_resources(&res);
	return (0);

failure:
	nlog(LOG_INFO, "nwamd_set_count_property: scf failure %s while "
	    "setting %s", scf_strerror(scf_error()), prop);
	release_scf_resources(&res);
	return (-1);
}

int
nwamd_set_string_property(const char *fmri, const char *pg, const char *prop,
    const char *str)
{
	scf_resources_t res;

	if (create_scf_resources(fmri, &res) != 0)
		return (-1);

	if (scf_instance_add_pg(res.sr_inst, pg, SCF_GROUP_APPLICATION,
	    SCF_PG_FLAG_NONPERSISTENT, res.sr_pg) != 0) {
		if (scf_error() != SCF_ERROR_EXISTS)
			goto failure;
		if (scf_instance_get_pg_composed(res.sr_inst, NULL, pg,
		    res.sr_pg) != 0)
			goto failure;
	}

	if (scf_value_set_astring(res.sr_val, str) != 0)
		goto failure;

	if (set_property_value(&res, prop, SCF_TYPE_ASTRING) != 0)
		goto failure;

	release_scf_resources(&res);
	return (0);

failure:
	nlog(LOG_INFO, "nwamd_set_string_property: scf failure %s while "
	    "setting %s", scf_strerror(scf_error()), prop);
	release_scf_resources(&res);
	return (-1);
}

/*
 * Deletes property prop from property group pg in SMF instance fmri.
 * Returns 0 on success, -1 on failure.
 */
int
nwamd_delete_scf_property(const char *fmri, const char *pg, const char *prop)
{
	scf_resources_t res;
	int result = -1;

	if (create_scf_resources(fmri, &res) != 0)
		return (-1);

	if (scf_instance_add_pg(res.sr_inst, pg, SCF_GROUP_APPLICATION,
	    SCF_PG_FLAG_NONPERSISTENT, res.sr_pg) != 0) {
		if (scf_error() != SCF_ERROR_EXISTS)
			goto failure;
		if (scf_instance_get_pg_composed(res.sr_inst, NULL, pg,
		    res.sr_pg) != 0)
			goto failure;
	}

	if (scf_pg_get_property(res.sr_pg, prop, res.sr_prop) != 0)
		goto failure;
retry:
	if (scf_transaction_start(res.sr_tx, res.sr_pg) == -1)
		goto failure;

	if (scf_transaction_property_delete(res.sr_tx, res.sr_ent, prop) == -1)
		goto failure;

	result = scf_transaction_commit(res.sr_tx);
	if (result == 0) {
		scf_transaction_reset(res.sr_tx);
		if (scf_pg_update(res.sr_pg) == -1)
			goto failure;
		goto retry;
	}
	if (result == -1)
		goto failure;

	release_scf_resources(&res);
	return (0);
failure:
	release_scf_resources(&res);
	return (-1);
}


void
nwamd_create_daemonize_event(void)
{
	nwamd_event_t daemon_event;

	/* If nwamd has daemonized, do nothing */
	if (daemonized)
		return;

	daemon_event = nwamd_event_init(NWAM_EVENT_TYPE_DAEMONIZE,
	    NWAM_OBJECT_TYPE_UNKNOWN, 0, NULL);
	if (daemon_event != NULL)
		nwamd_event_enqueue(daemon_event);
}
