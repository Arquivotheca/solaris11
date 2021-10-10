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

/*
 * Daemon to manage and control background threads performing shadow
 * migration.
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <port.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/resource.h>
#include <sys/msg.h>
#include <libscf_priv.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <utime.h>
#include <locale.h>
#include <libintl.h>
#include <libshadowfs.h>
#include <libzfs.h>
#include <limits.h>
#include <sys/list.h>
#include <priv_utils.h>

shadow_conspiracy_t *Conspiracy;
libzfs_handle_t *Libzfshdl;

int Signal = 0;

pthread_mutex_t Shadowd_assess_mutex;
pthread_cond_t Shadowd_mnttab_changed;

boolean_t Shadowd_reassess = B_FALSE;
boolean_t Shadowd_exit = B_FALSE;

int Shadowd_port = -1;

static void
shadowd_fini()
{
	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	shadcons_fini(Conspiracy);
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);

	(void) pthread_cond_destroy(&Shadowd_mnttab_changed);
	(void) pthread_mutex_destroy(&Shadowd_assess_mutex);
}

static int
shadowd_init()
{
	struct passwd *pe;
	struct group *ge;
	uid_t uid;
	gid_t gid;

	errno = 0;
	if ((pe = getpwnam("daemon")) == NULL) {
		(void) fprintf(stderr,
		    gettext("Failed to determine uid for daemon: %s"),
		    strerror(errno));
		return (-1);
	} else if ((ge = getgrnam("daemon")) == NULL) {
		(void) fprintf(stderr,
		    gettext("Failed to determine gid for daemon: %s"),
		    strerror(errno));
		return (-1);
	}
	uid = pe->pw_uid;
	gid = ge->gr_gid;

	(void) umask(002);
	(void) mkdir(SHADOWD_STATUS_REPORTS_PARENT_DIR, 0775);
	(void) mkdir(SHADOWD_STATUS_REPORTS_DIR, 0770);
	if (chown(SHADOWD_STATUS_REPORTS_DIR, uid, gid) < 0) {
		(void) fprintf(stderr, "Failed to chown %s: %s",
		    SHADOWD_STATUS_REPORTS_DIR, strerror(errno));
		return (-1);
	}

	(void) pthread_mutex_init(&Shadowd_assess_mutex, NULL);
	(void) pthread_cond_init(&Shadowd_mnttab_changed, NULL);

	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	Conspiracy = shadcons_init(SHADOWD_INST_FMRI);
	if (Conspiracy == NULL) {
		(void) fprintf(stderr,
		    "Unable to initialize conspiracy for background work");
		(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
		shadowd_fini();
		return (-1);
	}
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
	return (0);
}

static void
shadowd_bail()
{
	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	Shadowd_exit = B_TRUE;
	(void) pthread_cond_signal(&Shadowd_mnttab_changed);
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
}

struct remount_needed {
	list_node_t next;
	char *special;
	int tries;
};

#define	REMOUNT_NO_DATASET	-2
#define	REMOUNT_FAILED		-1
#define	REMOUNT_OK		0

static void
shadowd_free_remount(list_t *head, struct remount_needed *rnp)
{
	list_remove(head, rnp);
	free(rnp->special);
	free(rnp);
}

static void
shadowd_free_remounts(list_t *head)
{
	struct remount_needed *rnp;

	while (rnp = list_head(head))
		shadowd_free_remount(head, rnp);
}

static int
shadowd_try_remount(const char *dataset)
{
	zfs_handle_t *zhp;
	int rv;

	zhp = zfs_open(Libzfshdl, dataset, ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL) {
		shadcons_dprintf(Conspiracy, "open failed for %s\n", dataset);
		return (REMOUNT_NO_DATASET);
	}
	rv = zfs_mount(zhp, "remount", 0);
	shadcons_dprintf(Conspiracy, "remount of %s %s\n", dataset,
	    rv == 0 ? "succeeded" : "failed");
	zfs_close(zhp);
	return (rv ? REMOUNT_FAILED : REMOUNT_OK);
}

static void
shadowd_try_remounts(list_t *remounts)
{
	struct remount_needed *rnp;
	int ret;

	for (rnp = list_head(remounts); rnp; ) {
		ret = shadowd_try_remount(rnp->special);
		++(rnp->tries);
		if (ret == REMOUNT_OK || ret == REMOUNT_NO_DATASET ||
		    rnp->tries >= 10) {
			struct remount_needed *nextnp;
			nextnp = list_next(remounts, rnp);
			if (rnp->tries >= 10) {
				shadcons_warn(Conspiracy,
				    gettext("Giving up after too many "
				    "failed attempts to mount:\n    %s\n"),
				    rnp->special);
			}
			shadowd_free_remount(remounts, rnp);
			rnp = nextnp;
		} else {
			rnp = list_next(remounts, rnp);
		}
	}
}

static void
shadowd_add_remount(list_t *head, const char *dataset)
{
	struct remount_needed *new;

	if ((new = malloc(sizeof (struct remount_needed))) == NULL ||
	    (new->special = strdup(dataset)) == NULL) {
		free(new);
		shadcons_warn(Conspiracy,
		    gettext("Out of memory to save remount info.\n"));
		shadowd_bail();
		return;
	}
	new->tries = 1;
	list_insert_tail(head, new);
}

void
shadowd_assess(list_t *remounts)
{
	static boolean_t first_assess = B_TRUE;
	FILE *mfp;
	struct mnttab fsmatch = { 0 };
	struct mnttab me = { 0 };
	int before, new = 0;

	assert(MUTEX_HELD(&Shadowd_assess_mutex));

	if (Shadowd_exit) {
		(void) shadcons_svc_stop(Conspiracy);
		return;
	}

	/*
	 * Maybe add some sanity throttling here, if we find we
	 * assess too frequently.
	 */

	if ((mfp = fopen(MNTTAB, "r")) == NULL) {
		(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
		return;
	}

	shadcons_lock(Conspiracy);

	before = shadcons_hash_count(Conspiracy);

	while (getmntent(mfp, &me) == 0) {
		boolean_t is_zfs = B_FALSE;
		boolean_t is_new;
		char *option;

		/*
		 * Currently only support ufs and zfs mounts, and we
		 * can only try remounts on zfs where the shadow
		 * source is known because of the shadow property
		 */
		is_zfs = (strcmp(me.mnt_fstype, "zfs") == 0);
		if (!is_zfs && strcmp(me.mnt_fstype, "ufs") != 0)
			continue;

		shadcons_dprintf(Conspiracy, "%s is mounted\n", me.mnt_mountp);
		option = hasmntopt(&me, "shadow");
		/* dataset name is me.mnt_special */
		if (option == NULL) {
			shadcons_unlock(Conspiracy);
			(void) shadcons_cancel(Conspiracy, me.mnt_special);
			shadcons_lock(Conspiracy);
			continue;
		}
		shadcons_dprintf(Conspiracy, "and it has shadow option %s\n",
		    option);
		if (strncmp(option, "shadow=standby", 14) == 0) {
			if (!first_assess || !is_zfs)
				continue;
			shadcons_dprintf(Conspiracy, "attempting remount...");
			shadcons_unlock(Conspiracy);
			if (shadowd_try_remount(me.mnt_special) != 0) {
				shadowd_add_remount(remounts, me.mnt_special);
				shadcons_lock(Conspiracy);
				continue;
			}
			shadcons_lock(Conspiracy);
		}
		shadcons_unlock(Conspiracy);
		if (shadcons_start(Conspiracy, me.mnt_special, me.mnt_mountp,
		    option, &is_new) != 0) {
			shadcons_warn(Conspiracy,
			    gettext("Unable to start any workers for %s\n"),
			    me.mnt_mountp);
		} else if (is_new) {
			new++;
		}
		shadcons_lock(Conspiracy);
	}

	if (!first_assess)
		shadowd_try_remounts(remounts);

	/*
	 * somebody was unmounted and we have to stop them being
	 * shadowed, or somebody finished (?)
	 */
	if (shadcons_hash_count(Conspiracy) != before + new) {
		for (void *he = shadcons_hash_first(Conspiracy);
		    he != NULL;
		    he = shadcons_hash_next(Conspiracy, he)) {
			fsmatch.mnt_special =
			    (char *)shadcons_hashentry_dataset(Conspiracy, he);
			if ((getmntany(mfp, &me, &fsmatch) != 0) ||
			    hasmntopt(&me, "shadow") == NULL) {
				shadcons_hash_remove(Conspiracy, he);
			}
		}
	}

	shadcons_unlock(Conspiracy);
	(void) fclose(mfp);
	first_assess = B_FALSE;
	Shadowd_reassess = B_FALSE;
}

/*ARGSUSED*/
static void *
shadowd_mntwaiter(void *arg)
{
	list_t *remounts = (list_t *)arg;

	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	for (;;) {
		(void) pthread_cond_wait(&Shadowd_mnttab_changed,
		    &Shadowd_assess_mutex);
		shadowd_assess(remounts);
		if (Shadowd_exit) {
			(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
			return (NULL);
		}
	}
}

/*ARGSUSED*/
static void *
shadowd_mntwatcher(void *arg)
{
	struct file_obj mntfi = { 0 };
	struct stat st;
	port_event_t pev;
	void *vp = NULL;
	int error;

	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	Shadowd_port = port_create();
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
	mntfi.fo_name = strdup(MNTTAB);

	for (;;) {
		if (stat(MNTTAB, &st) != 0) {
			shadcons_warn(Conspiracy,
			    gettext("unable to stat(2) %s\n"), MNTTAB);
			shadowd_bail();
			return (NULL);
		}
		mntfi.fo_atime = st.st_atim;
		mntfi.fo_mtime = st.st_mtim;
		mntfi.fo_ctime = st.st_ctim;

		/* printf("mod time is %x\n", st.st_mtime); */

		error = port_associate(Shadowd_port, PORT_SOURCE_FILE,
		    (uintptr_t)&mntfi, FILE_MODIFIED, vp);
		if (error) {
			shadcons_warn(Conspiracy,
			    gettext("could not port_associate: %s\n"),
			    strerror(errno));
			shadowd_bail();
			return (NULL);
		}

		if (error = port_get(Shadowd_port, &pev, NULL)) {
			shadcons_warn(Conspiracy,
			    gettext("port_get failed: %s\n"),
			    strerror(errno));
			shadowd_bail();
			return (NULL);
		}

		(void) pthread_mutex_lock(&Shadowd_assess_mutex);
		Shadowd_reassess = B_TRUE;
		(void) pthread_cond_signal(&Shadowd_mnttab_changed);
		(void) pthread_mutex_unlock(&Shadowd_assess_mutex);

		if (Shadowd_exit)
			return (NULL);
	}
}

static void
free_messages(list_t *head)
{
	shadstatus_list_t *ssl;

	while (ssl = list_head(head)) {
		list_remove(head, ssl);
		free(ssl);
	}
}

static int
compose_messages(shadow_conspiracy_status_t *stats, size_t nstats,
    list_t *msglst)
{
	shadstatus_list_t *next;
	int i = 0;
	char *write_cursor, *write_limit;

needbuffer:
	next = malloc(sizeof (shadstatus_list_t));
	if (next == NULL) {
		shadcons_warn(Conspiracy,
		    gettext("No memory to create outgoing status message "
		    "list element\n"));
		free_messages(msglst);
		return (1);
	}
	next->ssl_status.ss_type = 0;
	next->ssl_status.ss_buf[0] = '\0';

	list_insert_tail(msglst, next);

	write_cursor = next->ssl_status.ss_buf;
	write_limit = next->ssl_status.ss_buf + SHADOWD_STATUS_BUFSZ;

	if (nstats == 0) {
		(void) snprintf(write_cursor,
		    SHADOWD_STATUS_BUFSZ - strlen(next->ssl_status.ss_buf),
		    "^%s^ 0 0 0 0 Y\n", SHADOWD_STATUS_NOMIGRATIONS);
		next->ssl_status.ss_type = SHADOWD_STATUS_FINAL;
		return (0);
	}

	while (i < nstats) {
		int entry_len;

		entry_len = snprintf(NULL, 0,
		    "^%s^ %llu %llu %d %llu %c\n",
		    stats[i].scs_ds_name, stats[i].scs_xferred,
		    stats[i].scs_remaining, stats[i].scs_errors,
		    stats[i].scs_elapsed, stats[i].scs_complete ? 'Y' : 'N');
		if (entry_len > SHADOWD_STATUS_BUFSZ) {
			shadcons_warn(Conspiracy,
			    gettext("Stats too large to fit in message "
			    "buffer!\n"));
			break;
		}
		if (write_cursor + entry_len + 1 >= write_limit) {
			next->ssl_status.ss_type = SHADOWD_STATUS_PARTIAL;
			goto needbuffer;
		}
		(void) snprintf(write_cursor,
		    SHADOWD_STATUS_BUFSZ - strlen(next->ssl_status.ss_buf),
		    "^%s^ %llu %llu %d %llu %c\n",
		    stats[i].scs_ds_name, stats[i].scs_xferred,
		    stats[i].scs_remaining, stats[i].scs_errors,
		    stats[i].scs_elapsed, stats[i].scs_complete ? 'Y' : 'N');
		write_cursor += entry_len;
		i++;
	}

	next->ssl_status.ss_type = SHADOWD_STATUS_FINAL;
	return (0);
}

static void
publish_stats(shadow_conspiracy_status_t *stats, size_t nstats)
{
	struct dirent *dep;
	struct msqid_ds buf;
	list_t msgs;
	char fbuf[MAXPATHLEN];
	key_t key;
	int msqid;
	DIR *sdp;

	if ((sdp = opendir(SHADOWD_STATUS_REPORTS_DIR)) == NULL) {
		shadcons_warn(Conspiracy,
		    gettext("Cannot open status reporting directory %s: %s\n"),
		    SHADOWD_STATUS_REPORTS_DIR, strerror(errno));
		return;
	}

	list_create(&msgs, sizeof (shadstatus_list_t),
	    offsetof(shadstatus_list_t, ssl_next));

	while ((dep = readdir(sdp)) != NULL) {
		if (strncmp(dep->d_name, SHADOWD_STATUS_PREFIX,
		    strlen(SHADOWD_STATUS_PREFIX)) != 0)
			continue;

		(void) snprintf(fbuf, MAXPATHLEN, "%s/%s",
		    SHADOWD_STATUS_REPORTS_DIR, dep->d_name);

		if ((key = ftok(fbuf, 0)) == -1) {
			shadcons_warn(Conspiracy,
			    gettext("Cannot get key for %s: %s\n"), fbuf,
			    strerror(errno));
			continue;
		}
		if ((msqid = msgget(key, 0664)) == -1) {
			shadcons_warn(Conspiracy,
			    gettext("Cannot msgget status of %s: %s\n"), fbuf,
			    strerror(errno));
			continue;
		}
		/* Retrieve stats to analyse queue activity */
		if (msgctl(msqid, IPC_STAT, &buf) == -1) {
			shadcons_warn(Conspiracy,
			    gettext("Cannot msgctl status of %s: %s\n"), fbuf,
			    strerror(errno));
			continue;
		}
		/*
		 * If buf.msg_qnum > MAX_PENDING
		 * _and_ msg_stime is more than 10s after msg_rtime -
		 * indicating message(s) have been hanging around unclaimed -
		 * we destroy the queue as the client has most likely gone
		 * away. This can happen if a registered client hits Ctrl-C.
		 */
		if (buf.msg_qnum > SHADOWD_STATUS_MAX_PENDING &&
		    ((buf.msg_stime + SHADOWD_STATUS_WAIT) > buf.msg_rtime)) {
			shadcons_warn(Conspiracy,
			    gettext("Listener %s appears dead, %d qnum, "
			    "last receive %ld, last send %ld, removing its "
			    "queue\n"), fbuf, buf.msg_qnum, buf.msg_rtime,
			    buf.msg_stime);
			(void) msgctl(msqid, IPC_RMID, NULL);
			continue;
		}

		if (list_is_empty(&msgs)) {
			if (compose_messages(stats, nstats, &msgs) != 0)
				break;
		}

		/*
		 * This shouldn't ever block.
		 * If it does then log an error and clean up the queue.
		 */
		for (shadstatus_list_t *mlp = list_head(&msgs); mlp != NULL;
		    mlp = list_next(&msgs, mlp)) {
			if (msgsnd(msqid, (struct msgbuf *)&mlp->ssl_status,
			    sizeof (long) + strlen(mlp->ssl_status.ss_buf),
			    IPC_NOWAIT) == -1) {
				shadcons_warn(Conspiracy,
				    gettext("Cannot msgsnd to %s: %s, "
				    "removing its queue\n"),
				    fbuf, strerror(errno));
				(void) msgctl(msqid, IPC_RMID, NULL);
				continue;
			}
		}
	}
	free_messages(&msgs);
	(void) closedir(sdp);
}

/*ARGSUSED*/
static void *
shadowd_stat_publisher(void *arg)
{
	timespec_t tv;

	tv.tv_sec = 10;
	tv.tv_nsec = 0;

	for (;;) {
		shadow_conspiracy_status_t *scsa;
		size_t nscsa;

		if (Shadowd_exit)
			return (NULL);

		(void) nanosleep(&tv, NULL);

		(void) pthread_mutex_lock(&Shadowd_assess_mutex);

		if (Shadowd_exit) {
			(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
			return (NULL);
		}

		if (shadcons_status_all(Conspiracy, &scsa, &nscsa) != 0) {
			(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
			continue;
		}

		publish_stats(scsa, nscsa);

		for (int e = 0; e < nscsa; e++)
			free(scsa[e].scs_ds_name);
		free(scsa);

		(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
	}
}

static void
handler(int sig)
{
	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	Shadowd_exit = B_TRUE;
	/* this should wake up the mnttab watcher */
	(void) port_alert(Shadowd_port, PORT_ALERT_SET, -1, NULL);
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
	Signal = sig;
}

/*ARGSUSED*/
static void
ignorer(int sig)
{
}

/*ARGSUSED*/
static void
restarter(int sig)
{
	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	if (shadcons_svc_refresh(Conspiracy) != 0) {
		Shadowd_exit = B_TRUE;
		/* this should wake up the mnttab watcher */
		(void) port_alert(Shadowd_port, PORT_ALERT_SET, -1, NULL);
		Signal = SIGABRT;
	}
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);
}

/*ARGSUSED*/
static void
quitter(int sig)
{
	abort();
}

static int
daemon_init(void)
{
	struct group *ge;
	sigset_t set, oset;
	int status, pfds[2];
	gid_t gid;
	pid_t pid;

	errno = 0;
	if ((ge = getgrnam("daemon")) == NULL) {
		(void) fprintf(stderr,
		    gettext("Failed to determine gid of daemon: %s"),
		    strerror(errno));
		return (-1);
	}
	gid = ge->gr_gid;

	/*
	 * Downgrade our privileges.  Unfortunately we must still run as
	 * root because mounts and remounts need to be done as root when the
	 * mountpoint is owned by root.
	 */
	if (__init_daemon_priv(PU_RESETGROUPS | PU_LIMITPRIVS | PU_INHERITPRIVS,
	    0, gid, /* root, daemon */
	    PRIV_SYS_MOUNT, PRIV_NET_PRIVADDR, PRIV_IPC_DAC_WRITE, NULL) != 0) {
		(void) fprintf(stderr,
		    gettext("additional privileges required to run\n"));
		return (-1);
	}

	/*
	 * Block all signals prior to the fork and leave them blocked in the
	 * parent so we don't get in a situation where the parent gets SIGINT
	 * and returns non-zero exit status and the child is actually running.
	 * In the child, restore the signal mask once we've done our setsid().
	 */
	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT);
	(void) sigprocmask(SIG_BLOCK, &set, &oset);

	if (pipe(pfds) == -1) {
		(void) fprintf(stderr, "Failed to create pipe for daemonize\n");
		return (-1);
	}

	if ((pid = fork()) == -1) {
		(void) fprintf(stderr, "Failed to fork\n");
		(void) close(pfds[0]);
		(void) close(pfds[1]);
		return (-1);
	}

	/*
	 * If we're the parent process, wait for either the child to send us
	 * the appropriate exit status over the pipe or for the read to fail
	 * (presumably with 0 for EOF if our child terminated abnormally).
	 * If the read fails, exit with either the child's exit status if it
	 * exited or with FMD_EXIT_ERROR if it died from a fatal signal.
	 */
	if (pid != 0) {
		(void) close(pfds[1]);

		if (read(pfds[0], &status, sizeof (status)) == sizeof (status))
			_exit(status);

		if (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
			_exit(WEXITSTATUS(status));

		_exit(SMF_EXIT_ERR_FATAL);
	}

	(void) setsid();
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	(void) chdir("/");
	(void) umask(002);
	(void) close(pfds[0]);

	return (pfds[1]);
}

static void
daemon_fini(int fd)
{
	(void) close(fd);
}

/*ARGSUSED*/
int
main(int argc, char **argv)
{
	struct sigaction restart;
	struct sigaction ignore;
	struct sigaction quit;
	struct sigaction act;
	list_t standingby;
	sigset_t set;
	pthread_t child[3];
	int status = SMF_EXIT_OK;
	int fd;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if (shadowd_init() != 0)
		exit(SMF_EXIT_ERR_CONFIG);

	if ((Libzfshdl = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "Unable to acquire libzfs handle\n");
		shadowd_fini();
		exit(SMF_EXIT_ERR_CONFIG);
	}
	libzfs_print_on_error(Libzfshdl, B_FALSE);

	(void) sigfillset(&ignore.sa_mask);
	ignore.sa_handler = ignorer;
	ignore.sa_flags = 0;
	(void) sigaction(SIGHUP, &ignore, NULL);
	(void) sigaction(SIGQUIT, &ignore, NULL);

	if ((fd = daemon_init()) < 0) {
		libzfs_fini(Libzfshdl);
		shadowd_fini();
		exit(SMF_EXIT_ERR_CONFIG);
	}

	/*
	 * When we are first starting up, we assume it is our job to convert
	 * any standby shadow mounts to active shadow mounts, as all mounts
	 * are standby initially to avoid attempting to mount source data via
	 * NFS or SMB when those services aren't yet available.
	 */
	list_create(&standingby, sizeof (struct remount_needed),
	    offsetof(struct remount_needed, next));

	/*
	 * We don't have to explicitly start the svc, the
	 * first time we add a shadowed dataset, that will
	 * happen for us
	 */
	(void) pthread_mutex_lock(&Shadowd_assess_mutex);
	shadowd_assess(&standingby);
	Shadowd_reassess = B_FALSE;
	(void) pthread_mutex_unlock(&Shadowd_assess_mutex);

	if (pthread_create(&child[0], NULL, shadowd_mntwaiter,
	    &standingby) != 0 ||
	    pthread_create(&child[1], NULL, shadowd_mntwatcher, NULL) != 0 ||
	    pthread_create(&child[2], NULL, shadowd_stat_publisher,
	    NULL) != 0) {
		libzfs_fini(Libzfshdl);
		shadowd_fini();
		exit(SMF_EXIT_ERR_FATAL);
	}

	(void) sigfillset(&restart.sa_mask);
	(void) sigfillset(&quit.sa_mask);
	(void) sigfillset(&act.sa_mask);
	restart.sa_handler = restarter;
	quit.sa_handler = quitter;
	act.sa_handler = handler;
	act.sa_flags = restart.sa_flags = quit.sa_flags = 0;
	(void) sigaction(SIGHUP, &restart, NULL);
	(void) sigaction(SIGQUIT, &quit, NULL);
	(void) sigaction(SIGTERM, &act, NULL);

	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT); /* always unblocked for ASSERT() */
	(void) sigdelset(&set, SIGTERM);
	(void) sigdelset(&set, SIGHUP);
	(void) sigdelset(&set, SIGQUIT);

	(void) write(fd, &status, sizeof (status));
	daemon_fini(fd);

	while (!Signal)
		(void) sigsuspend(&set);

	(void) sigaction(SIGHUP, &ignore, NULL);
	(void) sigaction(SIGQUIT, &ignore, NULL);

	(void) pthread_join(child[2], NULL);
	(void) pthread_join(child[1], NULL);
	(void) pthread_join(child[0], NULL);

	shadowd_free_remounts(&standingby);
	libzfs_fini(Libzfshdl);
	shadowd_fini();
	return (0);
}
