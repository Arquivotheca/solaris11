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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <priv_utils.h>
#include <libshare.h>
#include <libzfs.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/libsmb.h>
#include <smb/smb.h>
#include <smbsrv/smb_share.h>
#include "smbd.h"

#define	SMBD_SHARE_PUBLISH		0
#define	SMBD_SHARE_UNPUBLISH		1

typedef struct share_qentry {
	list_node_t	sqe_lnd;
	char		sqe_name[MAXNAMELEN];
	char		sqe_container[MAXPATHLEN];
	char		sqe_op;
} share_qentry_t;

/*
 * publish queue states
 */
#define	SMBD_SHARE_STATE_NOQUEUE	0
#define	SMBD_SHARE_STATE_READY		1	/* the queue is ready */
#define	SMBD_SHARE_STATE_PUBLISHING	2	/* publisher is running */
#define	SMBD_SHARE_STATE_STOPPING	3

/*
 * share publishing queue
 */
typedef struct share_queue {
	list_t		sq_list;
	mutex_t		sq_mtx;
	cond_t		sq_cv;
	uint32_t	sq_state;
} share_queue_t;

static share_queue_t share_queue;

typedef struct share_transient {
	char		*st_name;
	char		*st_cmnt;
	char		*st_path;
	char		*st_mntpnt;
	char		st_drive;
	boolean_t	st_check;
} share_transient_t;

static share_transient_t tshare[] = {
	{ "c$",   "Default Share",	SMB_CVOL,	NULL,	'C',  B_FALSE },
	{ "vss$", "VSS",		SMB_VSS,	NULL,	'V',  B_TRUE }
};

static uint32_t smbd_share_add_transient(share_transient_t *);
static boolean_t smbd_share_is_empty(const char *);
static boolean_t smbd_share_is_dot_or_dotdot(const char *);
static void smbd_publisher_queue(const char *, const char *, char);
static void *smbd_share_publisher(void *);
static void smbd_share_publisher_send(smb_ads_handle_t *, list_t *);
static void smbd_share_publisher_flush(list_t *);

static int smbd_share_enable_all_privs(void);
static int smbd_share_expand_subs(char **, smb_shr_execinfo_t *);
static char **smbd_share_tokenize_cmd(char *);
static void smbd_share_sig_abnormal_term(int);
static void smbd_share_sig_child(int);

/*
 * Locking for process-wide settings (privileges)
 */
static void smbd_proc_initsem(void);	/* init (or re-init in child) */
static int smbd_proc_takesem(void);	/* call in parent before fork */
static void smbd_proc_givesem(void);	/* call in parent after fork */

static char smbd_share_exec_map[MAXPATHLEN];
static char smbd_share_exec_unmap[MAXPATHLEN];
static mutex_t smbd_share_exec_mtx;

/*
 * Semaphore held during temporary, process-wide changes
 * such as process privileges.  It is a seamaphore and
 * not a mutex so a child of fork can reset it.
 */
static sema_t smbd_proc_sem = DEFAULTSEMA;

void
smbd_share_start(void)
{
	share_transient_t	*st;
	uint32_t		nerr;
	int			i;

	smbd_quota_init();
	smbd_proc_initsem();

	for (i = 0; i < sizeof (tshare)/sizeof (tshare[0]); ++i) {
		st = &tshare[i];

		if ((st->st_mntpnt = malloc(MAXPATHLEN)) != NULL) {
			if (sa_get_mntpnt_for_path(st->st_path,
			    st->st_mntpnt, MAXPATHLEN,
			    NULL, 0, NULL, 0) != SA_OK)  {
				free(st->st_mntpnt);
				st->st_mntpnt = NULL;
			}
		}

		if (st->st_check && smbd_share_is_empty(st->st_path))
			continue;

		nerr = smbd_share_add_transient(st);
		if ((nerr != NERR_Success) && (nerr != NERR_DuplicateShare))
			smbd_log(LOG_NOTICE,
			    "smbd_share_add_transient: %s: error %u",
			    st->st_name, nerr);
	}

	(void) mutex_lock(&share_queue.sq_mtx);
	if (share_queue.sq_state != SMBD_SHARE_STATE_NOQUEUE) {
		(void) mutex_unlock(&share_queue.sq_mtx);
		return;
	}

	list_create(&share_queue.sq_list, sizeof (share_qentry_t),
	    offsetof(share_qentry_t, sqe_lnd));
	share_queue.sq_state = SMBD_SHARE_STATE_READY;
	(void) mutex_unlock(&share_queue.sq_mtx);

	(void) smbd_thread_create("AD share publisher", smbd_share_publisher,
	    NULL);
}

void
smbd_share_stop(void)
{
	int i;
	share_transient_t *st;

	(void) mutex_lock(&share_queue.sq_mtx);
	switch (share_queue.sq_state) {
	case SMBD_SHARE_STATE_READY:
	case SMBD_SHARE_STATE_PUBLISHING:
		share_queue.sq_state = SMBD_SHARE_STATE_STOPPING;
		(void) cond_signal(&share_queue.sq_cv);
		break;
	default:
		break;
	}
	(void) mutex_unlock(&share_queue.sq_mtx);

	for (i = 0; i < sizeof (tshare)/sizeof (tshare[0]); ++i) {
		st = &tshare[i];
		if (st->st_mntpnt != NULL) {
			free(st->st_mntpnt);
			st->st_mntpnt = NULL;
		}
	}

	smbd_quota_fini();
}

void
smbd_share_load_execinfo(void)
{
	(void) mutex_lock(&smbd_share_exec_mtx);
	(void) smb_config_get_execinfo(smbd_share_exec_map,
	    smbd_share_exec_unmap, MAXPATHLEN);
	(void) mutex_unlock(&smbd_share_exec_mtx);
}

int
smbd_share_publish_admin(char *mntpnt)
{
	int i;
	uint32_t nerr;
	share_transient_t *st;

	for (i = 0; i < sizeof (tshare)/sizeof (tshare[0]); ++i) {
		st = &tshare[i];

		if (st->st_mntpnt == NULL ||
		    strcmp(mntpnt, st->st_mntpnt) != 0)
			continue;

		if (st->st_check && smbd_share_is_empty(st->st_path))
			continue;

		nerr = smbd_share_add_transient(st);
		if ((nerr != NERR_Success) && (nerr != NERR_DuplicateShare))
			smbd_log(LOG_NOTICE,
			    "smbd_share_add_transient: %s: error %u",
			    st->st_name, nerr);
	}
	return (ERROR_SUCCESS);
}

/*
 * Add transient shares: admin shares etc.
 */
static uint32_t
smbd_share_add_transient(share_transient_t *st)
{
	smb_share_t	si;
	uint32_t	nerr;

	if (st->st_name == NULL)
		return (NERR_InternalError);

	bzero(&si, sizeof (smb_share_t));
	si.shr_name = strdup(st->st_name);
	si.shr_path = strdup(st->st_path);
	si.shr_cmnt = strdup(st->st_cmnt);

	if (si.shr_name == NULL || si.shr_path == NULL ||
	    si.shr_cmnt == NULL) {
		smb_share_free(&si);
		return (ERROR_NOT_ENOUGH_MEMORY);
	}

	si.shr_type = STYPE_DISKTREE;
	si.shr_drive = st->st_drive;
	si.shr_flags = SMB_SHRF_TRANS;

	nerr = smb_share_add(&si);

	smb_share_free(&si);
	return (nerr);
}


/*
 * Returns true if the specified directory is empty,
 * otherwise returns false.
 */
static boolean_t
smbd_share_is_empty(const char *path)
{
	DIR *dirp;
	struct dirent *dp;

	if (path == NULL)
		return (B_TRUE);

	if ((dirp = opendir(path)) == NULL)
		return (B_TRUE);

	while ((dp = readdir(dirp)) != NULL) {
		if (!smbd_share_is_dot_or_dotdot(dp->d_name)) {
			(void) closedir(dirp);
			return (B_FALSE);
		}
	}

	(void) closedir(dirp);
	return (B_TRUE);
}

/*
 * Returns true if name is "." or "..", otherwise returns false.
 */
static boolean_t
smbd_share_is_dot_or_dotdot(const char *name)
{
	if (*name != '.')
		return (B_FALSE);

	if ((name[1] == '\0') || (name[1] == '.' && name[2] == '\0'))
		return (B_TRUE);

	return (B_FALSE);
}

void
smbd_share_publish(smb_shr_notify_t *sn)
{
	char	mountpoint[MAXPATHLEN];
	char	*dfs_root;
	int	rc;

	smbd_publisher_queue(sn->sn_name, sn->sn_container,
	    SMBD_SHARE_PUBLISH);

	if (smb_getmountpoint(sn->sn_path, mountpoint, MAXPATHLEN) == 0)
		smbd_quota_add_fs(mountpoint);

	if (!sn->sn_dfsroot)
		return;

	if ((dfs_root = strdup(sn->sn_name)) == NULL) {
		smbd_log(LOG_ERR,
		    "smbd_share_publish: unable to allocate dfs name %s: %s",
		    sn->sn_name, strerror(errno));
		return;
	}

	rc = smbd_thread_run("DFS", dfs_ns_export, dfs_root);
	if (rc != 0) {
		smbd_log(LOG_ERR,
		    "smbd_share_publish: unable to export dfs namespace %s: %s",
		    sn->sn_name, strerror(rc));
		free(dfs_root);
	}
}

void
smbd_share_unpublish(smb_shr_notify_t *sn)
{
	char	mountpoint[MAXPATHLEN];

	smbd_publisher_queue(sn->sn_name, sn->sn_container,
	    SMBD_SHARE_UNPUBLISH);

	if (smb_getmountpoint(sn->sn_path, mountpoint, MAXPATHLEN) == 0)
		smbd_quota_remove_fs(mountpoint);

	if (sn->sn_dfsroot)
		dfs_ns_unexport(sn->sn_name);
}

void
smbd_share_republish(smb_shr_notify_t *sn)
{
	smbd_publisher_queue(sn->sn_name, sn->sn_container,
	    SMBD_SHARE_UNPUBLISH);

	smbd_publisher_queue(sn->sn_name, sn->sn_newcontainer,
	    SMBD_SHARE_PUBLISH);
}

/*
 * In domain mode, put a share on the publisher queue.
 * This is a no-op if the smb service is in Workgroup mode.
 */
static void
smbd_publisher_queue(const char *sharename, const char *container, char op)
{
	share_qentry_t *item = NULL;

	if (container == NULL || *container == '\0')
		return;

	if (smb_config_get_secmode() != SMB_SECMODE_DOMAIN)
		return;

	(void) mutex_lock(&share_queue.sq_mtx);
	switch (share_queue.sq_state) {
	case SMBD_SHARE_STATE_READY:
	case SMBD_SHARE_STATE_PUBLISHING:
		break;
	default:
		(void) mutex_unlock(&share_queue.sq_mtx);
		return;
	}
	(void) mutex_unlock(&share_queue.sq_mtx);

	if ((item = malloc(sizeof (share_qentry_t))) == NULL)
		return;

	item->sqe_op = op;
	(void) strlcpy(item->sqe_name, sharename, sizeof (item->sqe_name));
	(void) strlcpy(item->sqe_container, container,
	    sizeof (item->sqe_container));

	(void) mutex_lock(&share_queue.sq_mtx);
	list_insert_tail(&share_queue.sq_list, item);
	(void) cond_signal(&share_queue.sq_cv);
	(void) mutex_unlock(&share_queue.sq_mtx);
}

/*
 * This is the publisher thread.  While running, the thread waits
 * on a conditional variable until notified that a share needs to be
 * [un]published or that the thread should be terminated.
 *
 * Entries may remain in the outgoing queue if the Active Directory
 * service is inaccessible, in which case the thread wakes up every 60
 * seconds to retry.
 */
/*ARGSUSED*/
static void *
smbd_share_publisher(void *arg)
{
	smb_ads_handle_t	*ah;
	list_t			publist;
	timestruc_t		pubretry;

	smbd_online_wait("smbd_share_publisher");

	(void) mutex_lock(&share_queue.sq_mtx);
	if (share_queue.sq_state != SMBD_SHARE_STATE_READY) {
		(void) mutex_unlock(&share_queue.sq_mtx);
		return (NULL);
	}
	share_queue.sq_state = SMBD_SHARE_STATE_PUBLISHING;
	(void) mutex_unlock(&share_queue.sq_mtx);

	list_create(&publist, sizeof (share_qentry_t),
	    offsetof(share_qentry_t, sqe_lnd));

	while (smbd_online()) {
		(void) mutex_lock(&share_queue.sq_mtx);

		while (list_is_empty(&share_queue.sq_list) &&
		    (share_queue.sq_state == SMBD_SHARE_STATE_PUBLISHING)) {
			if (list_is_empty(&publist)) {
				(void) cond_wait(&share_queue.sq_cv,
				    &share_queue.sq_mtx);
			} else {
				pubretry.tv_sec = 60;
				pubretry.tv_nsec = 0;
				(void) cond_reltimedwait(&share_queue.sq_cv,
				    &share_queue.sq_mtx, &pubretry);
				break;
			}
		}

		if (share_queue.sq_state != SMBD_SHARE_STATE_PUBLISHING) {
			(void) mutex_unlock(&share_queue.sq_mtx);
			break;
		}

		/*
		 * Transfer queued items to the local list so that
		 * the mutex can be released.
		 */
		list_move_tail(&publist, &share_queue.sq_list);
		(void) mutex_unlock(&share_queue.sq_mtx);

		if ((ah = smb_ads_open()) != NULL) {
			smbd_share_publisher_send(ah, &publist);
			smb_ads_close(ah);
		}
	}

	(void) mutex_lock(&share_queue.sq_mtx);
	smbd_share_publisher_flush(&share_queue.sq_list);
	list_destroy(&share_queue.sq_list);
	share_queue.sq_state = SMBD_SHARE_STATE_NOQUEUE;
	(void) mutex_unlock(&share_queue.sq_mtx);

	smbd_share_publisher_flush(&publist);
	list_destroy(&publist);
	smbd_thread_exit();
	return (NULL);
}

/*
 * Remove items from the specified queue and [un]publish them.
 */
static void
smbd_share_publisher_send(smb_ads_handle_t *ah, list_t *publist)
{
	char		hostname[MAXHOSTNAMELEN];
	share_qentry_t	*si;

	(void) smb_gethostname(hostname, MAXHOSTNAMELEN,
	    SMB_CASE_PRESERVE);

	while ((si = list_remove_head(publist)) != NULL) {
		if (si->sqe_op == SMBD_SHARE_PUBLISH)
			(void) smb_ads_publish_share(ah, si->sqe_name,
			    NULL, si->sqe_container, hostname);
		else
			(void) smb_ads_remove_share(ah, si->sqe_name,
			    NULL, si->sqe_container, hostname);

		free(si);
	}
}

/*
 * Flush all remaining items from the list.
 */
static void
smbd_share_publisher_flush(list_t *lst)
{
	share_qentry_t *si;

	while ((si = list_remove_head(lst)) != NULL)
		free(si);
}

/*
 * If the shared directory does not begin with a /, one will be
 * inserted as a prefix. If ipaddr is not zero, then also return
 * information about access based on the host level access lists, if
 * present. Also return access check if there is an IP address and
 * shr_accflags.
 *
 * The value of smb_chk_hostaccess is checked for an access match.
 * -1 is wildcard match
 * 0 is no match
 * 1 is match
 *
 * Precedence is none is checked first followed by ro then rw if
 * needed.  If x is wildcard (< 0) then check to see if the other
 * values are a match. If a match, that wins.
 *
 * ipv6 is wide open (returns SMB_SHRF_ACC_OPEN) for now until the underlying
 * functions support ipv6.
 */
uint32_t
smbd_share_hostaccess(smb_inaddr_t *ipaddr, char *none_list, char *ro_list,
    char *rw_list, uint32_t flag)
{
	uint32_t acc = SMB_SHRF_ACC_NONE;
	int none = 0;
	int ro = 0;
	int rw = 0;

	if (!smb_inet_iszero(ipaddr)) {
		if (ipaddr->a_family == AF_INET6)
			return (SMB_SHRF_ACC_OPEN);

		if ((flag & SMB_SHRF_ACC_NONE) != 0)
			none = smb_chk_hostaccess(ipaddr, none_list);
		if ((flag & SMB_SHRF_ACC_RO) != 0)
			ro = smb_chk_hostaccess(ipaddr, ro_list);
		if ((flag & SMB_SHRF_ACC_RW) != 0)
			rw = smb_chk_hostaccess(ipaddr, rw_list);

		/* make first pass to get basic value */
		if (none != 0)
			acc = SMB_SHRF_ACC_NONE;
		else if (ro != 0)
			acc = SMB_SHRF_ACC_RO;
		else if (rw != 0)
			acc = SMB_SHRF_ACC_RW;

		/* make second pass to handle '*' case */
		if (none < 0) {
			acc = SMB_SHRF_ACC_NONE;
			if (ro > 0)
				acc = SMB_SHRF_ACC_RO;
			else if (rw > 0)
				acc = SMB_SHRF_ACC_RW;
		} else if (ro < 0) {
			acc = SMB_SHRF_ACC_RO;
			if (none > 0)
				acc = SMB_SHRF_ACC_NONE;
			else if (rw > 0)
				acc = SMB_SHRF_ACC_RW;
		} else if (rw < 0) {
			acc = SMB_SHRF_ACC_RW;
			if (none > 0)
				acc = SMB_SHRF_ACC_NONE;
			else if (ro > 0)
				acc = SMB_SHRF_ACC_RO;
		}
	}

	return (acc);
}

/*
 * Executes the map/unmap command associated with a share.
 *
 * Returns 0 on success.  Otherwise non-zero for errors.
 */
int
smbd_share_exec(smb_shr_execinfo_t *subs)
{
	char cmd[MAXPATHLEN], **cmd_tokens, *path, *ptr;
	pid_t child_pid;
	int child_status;
	struct sigaction pact, cact;

	*cmd = '\0';

	(void) mutex_lock(&smbd_share_exec_mtx);

	switch (subs->e_type) {
	case SMB_EXEC_MAP:
		(void) strlcpy(cmd, smbd_share_exec_map, sizeof (cmd));
		break;
	case SMB_EXEC_UNMAP:
		(void) strlcpy(cmd, smbd_share_exec_unmap, sizeof (cmd));
		break;
	default:
		(void) mutex_unlock(&smbd_share_exec_mtx);
		return (-1);
	}

	(void) mutex_unlock(&smbd_share_exec_mtx);

	if (*cmd == '\0')
		return (0);

	if (smbd_proc_takesem() != 0)
		return (-1);

	pact.sa_handler = smbd_share_sig_child;
	pact.sa_flags = 0;
	(void) sigemptyset(&pact.sa_mask);
	sigaction(SIGCHLD, &pact, NULL);

	(void) priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_PROC_FORK, NULL);

	if ((child_pid = fork()) == -1) {
		(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_PROC_FORK, NULL);
		smbd_proc_givesem();
		return (-1);
	}

	if (child_pid == 0) {

		/* child process */

		cact.sa_handler = smbd_share_sig_abnormal_term;
		cact.sa_flags = 0;
		(void) sigemptyset(&cact.sa_mask);
		sigaction(SIGTERM, &cact, NULL);
		sigaction(SIGABRT, &cact, NULL);
		sigaction(SIGSEGV, &cact, NULL);

		if (priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_PROC_EXEC,
		    PRIV_FILE_DAC_EXECUTE, NULL))
			_exit(-1);

		if (smbd_share_enable_all_privs())
			_exit(-1);

		smbd_proc_initsem();

		(void) trim_whitespace(cmd);
		(void) strcanon(cmd, " ");

		if ((cmd_tokens = smbd_share_tokenize_cmd(cmd)) != NULL) {

			if (smbd_share_expand_subs(cmd_tokens, subs) != 0) {
				free(cmd_tokens[0]);
				free(cmd_tokens);
				_exit(-1);
			}

			ptr = cmd;
			path = strsep(&ptr, " ");

			(void) execv(path, cmd_tokens);
		}

		_exit(-1);
	}

	(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_PROC_FORK, NULL);
	smbd_proc_givesem();

	/* parent process */

	while (waitpid(child_pid, &child_status, 0) < 0) {
		if (errno != EINTR)
			break;

		/* continue if waitpid got interrupted by a signal */
		errno = 0;
		continue;
	}

	if (WIFEXITED(child_status))
		return (WEXITSTATUS(child_status));

	return (child_status);
}

/*
 * Locking for process-wide settings (i.e. privileges)
 */
static void
smbd_proc_initsem(void)
{
	(void) sema_init(&smbd_proc_sem, 1, USYNC_THREAD, NULL);
}

static int
smbd_proc_takesem(void)
{
	return (sema_wait(&smbd_proc_sem));
}

static void
smbd_proc_givesem(void)
{
	(void) sema_post(&smbd_proc_sem);
}

/*
 * Enable all privileges in the inheritable set to execute command.
 */
static int
smbd_share_enable_all_privs(void)
{
	priv_set_t *pset;

	pset = priv_allocset();
	if (pset == NULL)
		return (-1);

	if (getppriv(PRIV_LIMIT, pset)) {
		priv_freeset(pset);
		return (-1);
	}

	if (setppriv(PRIV_ON, PRIV_INHERITABLE, pset)) {
		priv_freeset(pset);
		return (-1);
	}

	priv_freeset(pset);
	return (0);
}

/*
 * Tokenizes the command string and returns the list of tokens in an array.
 *
 * Returns NULL if there are no tokens.
 */
static char **
smbd_share_tokenize_cmd(char *cmdstr)
{
	char *cmd, *buf, *bp, *value;
	char **argv, **ap;
	int argc, i;

	if (cmdstr == NULL || *cmdstr == '\0')
		return (NULL);

	if ((buf = malloc(MAXPATHLEN)) == NULL)
		return (NULL);

	(void) strlcpy(buf, cmdstr, MAXPATHLEN);

	for (argc = 2, bp = cmdstr; *bp != '\0'; ++bp)
		if (*bp == ' ')
			++argc;

	if ((argv = calloc(argc, sizeof (char *))) == NULL) {
		free(buf);
		return (NULL);
	}

	ap = argv;
	for (bp = buf, i = 0; i < argc; ++i) {
		do {
			if ((value = strsep(&bp, " ")) == NULL)
				break;
		} while (*value == '\0');

		if (value == NULL)
			break;

		*ap++ = value;
	}

	/* get the filename of the command from the path */
	if ((cmd = strrchr(argv[0], '/')) != NULL)
		(void) strlcpy(argv[0], ++cmd, strlen(argv[0]));

	return (argv);
}

/*
 * Expands the command string for the following substitution tokens:
 *
 * %U - Windows username
 * %D - Name of the domain or workgroup of %U
 * %h - The server hostname
 * %M - The client hostname
 * %L - The server NetBIOS name
 * %m - The client NetBIOS name. This option is only valid for NetBIOS
 *      connections (port 139).
 * %I - The IP address of the client machine
 * %i - The local IP address to which the client is connected
 * %S - The name of the share
 * %P - The root directory of the share
 * %u - The UID of the Unix user
 *
 * Returns 0 on success.  Otherwise -1.
 */
static int
smbd_share_expand_subs(char **cmd_toks, smb_shr_execinfo_t *subs)
{
	char *fmt, *sub_chr, *ptr;
	boolean_t unknown;
	char hostname[MAXHOSTNAMELEN];
	char ip_str[INET6_ADDRSTRLEN];
	char name[SMB_PI_MAX_HOST];
	smb_wchar_t wbuf[SMB_PI_MAX_HOST];
	int i;

	if (cmd_toks == NULL || *cmd_toks == NULL)
		return (-1);

	for (i = 1; cmd_toks[i]; i++) {
		fmt = cmd_toks[i];
		if (*fmt == '%') {
			sub_chr = fmt + 1;
			unknown = B_FALSE;

			switch (*sub_chr) {
			case 'U':
				ptr = strdup(subs->e_winname);
				break;
			case 'D':
				ptr = strdup(subs->e_userdom);
				break;
			case 'h':
				if (gethostname(hostname, MAXHOSTNAMELEN) != 0)
					unknown = B_TRUE;
				else
					ptr = strdup(hostname);
				break;
			case 'M':
				if (smb_getnameinfo(&subs->e_cli_ipaddr,
				    hostname, sizeof (hostname), 0) != 0)
					unknown = B_TRUE;
				else
					ptr = strdup(hostname);
				break;
			case 'L':
				if (smb_getnetbiosname(hostname,
				    NETBIOS_NAME_SZ) != 0)
					unknown = B_TRUE;
				else
					ptr = strdup(hostname);
				break;
			case 'm':
				if (*subs->e_cli_netbiosname == '\0')
					unknown = B_TRUE;
				else {
					(void) smb_mbstowcs(wbuf,
					    subs->e_cli_netbiosname,
					    SMB_PI_MAX_HOST - 1);

					if (ucstooem(name, wbuf,
					    SMB_PI_MAX_HOST, OEM_CPG_850) == 0)
						(void) strlcpy(name,
						    subs->e_cli_netbiosname,
						    SMB_PI_MAX_HOST);

					ptr = strdup(name);
				}
				break;
			case 'I':
				if (smb_inet_ntop(&subs->e_cli_ipaddr, ip_str,
				    SMB_IPSTRLEN(subs->e_cli_ipaddr.a_family))
				    != NULL)
					ptr = strdup(ip_str);
				else
					unknown = B_TRUE;
				break;
			case 'i':
				if (smb_inet_ntop(&subs->e_srv_ipaddr, ip_str,
				    SMB_IPSTRLEN(subs->e_srv_ipaddr.a_family))
				    != NULL)
					ptr = strdup(ip_str);
				else
					unknown = B_TRUE;
				break;
			case 'S':
				ptr = strdup(subs->e_sharename);
				break;
			case 'P':
				ptr = strdup(subs->e_sharepath);
				break;
			case 'u':
				(void) snprintf(name, sizeof (name), "%u",
				    subs->e_uid);
				ptr = strdup(name);
				break;
			default:
				/* unknown sub char */
				unknown = B_TRUE;
				break;
			}

			if (unknown)
				ptr = strdup("");

		} else {
			/* first char of cmd's arg is not '%' char */
			ptr = strdup("");
		}

		cmd_toks[i] = ptr;

		if (ptr == NULL) {
			for (i = 1; cmd_toks[i]; i++)
				free(cmd_toks[i]);

			return (-1);
		}
	}

	return (0);
}

/*ARGSUSED*/
static void
smbd_share_sig_abnormal_term(int sig_val)
{
	/*
	 * Calling _exit() prevents parent process from getting SIGTERM/SIGINT
	 * signal.
	 */
	_exit(-1);
}

/*ARGSUSED*/
static void
smbd_share_sig_child(int sig_val)
{
	/*
	 * Catch the signal and allow the exit status of the child process
	 * to be available for reaping.
	 */
}
