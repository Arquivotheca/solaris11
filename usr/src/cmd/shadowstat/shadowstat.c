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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <grp.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <pthread.h>
#include <libshadowfs.h>

char *Qpath;			/* Path to file representing IPC Queue */
key_t Qkey;			/* Key for Queue access */
int Timeout;			/* Non-zero if we ALARMed */
int Qid;			/* Queue ID for created IPC Queue */

static int
setup(const char *path)
{
	int fd;

	if ((fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0664)) < 0) {
		perror(path);
		return (1);
	}
	(void) close(fd);

	if ((Qpath = strdup(path)) == NULL) {
		(void) fprintf(stderr, gettext("Out of memory.\n"));
		return (1);
	}

	if ((Qkey = ftok(path, 0)) == -1) {
		perror("ftok");
		return (1);
	}

	if ((Qid = msgget(Qkey, 0664 | IPC_CREAT)) == -1) {
		perror("msgget");
		return (1);
	}

	return (0);
}

static void
teardown(void)
{
	/*
	 * attempt to throw away our message queue,
	 */
	if (Qid) {
		(void) msgctl(Qid, IPC_RMID, NULL);
	}
	if (Qpath) {
		(void) unlink(Qpath);
		free(Qpath);
	}
}

/*
 * Convert a number to an appropriately human-readable output.
 */
static void
nicenum(uint64_t num, char *buf, size_t buflen)
{
	uint64_t n = num;
	int index = 0;
	char u;

	while (n >= 1024) {
		n /= 1024;
		index++;
	}

	u = " KMGTPE"[index];

	if (index == 0) {
		(void) snprintf(buf, buflen, "%llu", n);
	} else if ((num & ((1ULL << 10 * index) - 1)) == 0) {
		/*
		 * If this is an even multiple of the base, always display
		 * without any decimal precision.
		 */
		(void) snprintf(buf, buflen, "%llu%c", n, u);
	} else {
		/*
		 * We want to choose a precision that reflects the
		 * best choice for fitting in 5 characters.  This can
		 * get rather tricky when we have numbers that are
		 * very close to an order of magnitude.  For example,
		 * when displaying 10239 (which is really 9.999K), we
		 * want only a single place of precision for 10.0K.
		 * We could develop some complex heuristics for this,
		 * but it's much easier just to try each combination
		 * in turn.
		 */
		int i;
		for (i = 2; i >= 0; i--) {
			if (snprintf(buf, buflen, "%.*f%c", i,
			    (double)num / (1ULL << 10 * index), u) <= 5)
				break;
		}
	}
}

/* seconds in a ... */
#define	MINUTE	60
#define	HOUR	(60 * MINUTE)
#define	DAY	(24 * HOUR)
#define	WEEK	(7 * DAY)

static void
nicetime(uint64_t secs, char *buf, size_t buflen)
{
	int wks, days, hrs, mins, s;
	int needed;
	size_t sofar;
	char *bufp;

	wks = secs / WEEK;
	days = (secs % WEEK) / DAY;
	hrs = ((secs % WEEK) % DAY) / HOUR;
	mins = (((secs % WEEK) % DAY) % HOUR) / MINUTE;
	s = (((secs % WEEK) % DAY) % HOUR) % MINUTE;

	buf[0] = '\0';
	sofar = strlen(buf);
	bufp = buf + sofar;

	if (wks) {
		needed = snprintf(NULL, 0, "%dw ", wks);
		if (needed >= buflen - sofar)
			return;
		(void) snprintf(bufp, buflen - sofar, "%dw ", wks);
	}
	sofar = strlen(buf);
	if (days) {
		needed = snprintf(NULL, 0, "%dd ", days);
		if (needed >= buflen - sofar)
			return;
		(void) snprintf(bufp, buflen - sofar, "%dd ", days);
	}
	sofar = strlen(buf);
	needed = snprintf(NULL, 0, "%02d:%02d:%02d", hrs, mins, s);
	if (needed >= buflen - sofar)
		return;
	(void) snprintf(bufp, buflen - sofar, "%02d:%02d:%02d", hrs, mins, s);
}

/*ARGSUSED*/
static void
timeout(int sig)
{
	Timeout++;
}

int
displayer(int count)
{
	struct sigaction act;
	shadstatus_t ssb;
	char *firstend, *endds;
	int mid;
	int idx = 0;

	if ((mid = msgget(Qkey, 0664)) == -1) {
		perror(gettext("failed msgget() after setup"));
		return (1);
	}

	(void) sigfillset(&act.sa_mask);
	act.sa_handler = timeout;
	act.sa_flags = SA_RESETHAND;
	(void) sigaction(SIGALRM, &act, NULL);

	(void) printf(gettext("\t\t\t\t\tEST\t\t\n"));
	(void) printf(gettext("\t\t\t\tBYTES\tBYTES\t\tELAPSED\n"));
	(void) printf(gettext("DATASET\t\t\t\tXFRD\tLEFT\tERRORS\tTIME\n"));

	do {
		(void) alarm(15);
		while (msgrcv(mid, (struct msgbuf *)&ssb, sizeof (ssb),
		    0, 0) == -1) {
			switch (errno) {
			case EAGAIN:
			case EBUSY:
				/*
				 * We see this errno even though it isn't
				 * documented.  Try again.  If this causes
				 * a busy loop then grab a trace otherwise
				 * it's a brace 'til we can figure out why it
				 * happens.
				 */
				continue;
			case EINTR:
			default:
				(void) alarm(0);
				return (1);
			}
		}
		(void) alarm(0);

		/*
		 * The dataset name should be surrounded on either side by a ^.
		 * That's not a legal character in the name.
		 */
		if ((firstend = strrchr(ssb.ss_buf, '^')) == NULL) {
			(void) fprintf(stderr,
			    gettext("Bogus statistics messsage received\n"));
			return (1);
		}
		endds = firstend;

		while (endds) {
			uint64_t sofar, left, elapsed;
			char *beginds;
			char finished;
			char timebuf[48];
			char numbuf[32];
			int dsnmlen;
			int errors;
			int fields;

			beginds = endds - 1;
			while (beginds >= ssb.ss_buf) {
				if (*beginds == '^')
					break;
				beginds--;
			}
			if (beginds < ssb.ss_buf || *beginds != '^')
				return (1);
			beginds++;

			dsnmlen = endds - beginds;

			if (strncmp(beginds,
			    SHADOWD_STATUS_NOMIGRATIONS, dsnmlen) == 0) {
				(void) printf(
				    gettext("No migrations in progress\n"));
				return (0);
			}

			fields = sscanf(endds + 1, "%llu %llu %d %llu %c",
			    &sofar, &left, &errors, &elapsed, &finished);

			if (fields != 5) {
				(void) printf("%.*s\n", dsnmlen, beginds);
				(void) printf(
				    gettext("received %d data fields, "));
				(void) printf(gettext("expected 5.\n"));
				return (1);
			}

			*endds = '\0';
			if (strlen(beginds) < 31) {
				(void) printf("%-32s", beginds);
			} else {
				(void) printf("..");
				(void) printf("%-30s", endds - 29);
			}
			if (sofar == 0) {
				(void) printf("-\t");
			} else {
				nicenum(sofar, numbuf, 32);
				(void) printf("%s\t", numbuf);
			}
			if (left == -1ULL) {
				(void) printf("-\t");
			} else {
				nicenum(left, numbuf, 32);
				(void) printf("%s\t", numbuf);
			}
			if (errors)
				(void) printf("%d\t", errors);
			else
				(void) printf("-\t");
			if (finished == 'Y') {
				(void) printf("\t(completed)\n");
			} else {
				nicetime(elapsed, timebuf, 48);
				(void) printf("%s\n", timebuf);
			}

			endds = beginds - 2;
			while (endds > ssb.ss_buf) {
				if (*endds == '^')
					break;
				endds--;
			}
			if (endds <= ssb.ss_buf || *endds != '^')
				break;
		}

		idx++;

		if (ssb.ss_type == SHADOWD_STATUS_PARTIAL)
			continue;

		if (!Timeout)
			(void) sleep(3);

	} while (Timeout == 0 && (count < 0 || idx < count));

	return (0);
}

/*ARGSUSED*/
static void
handler(int sig)
{
	teardown();
	_exit(2);
}

static int
numfromstr(const char *str, const char *errmsg, int *val)
{
	char *end;

	*val = strtol(str, &end, 0);
	if (*end != '\0' || *val <= 0) {
		(void) fprintf(stderr,
		    gettext(
"invalid %s '%s': must be a non-zero positive integer\n"),
		    errmsg, str);
		return (-1);
	}
	return (0);
}

static int
Usage(char *prog)
{
	(void) fprintf(stderr,
	    "Usage:\t%s [count]\n\t%s [-f] -c path\n", prog, prog);
	return (1);
}

static void
dump_path(const char *path, void *data)
{
	const char *root = data;
	size_t len = strlen(root);

	if (strcmp(path, root) == 0) {
		(void) fprintf(stderr, "<root>\n");
		return;
	}

	/*
	 * assert(strlen(path) > len);
	 * assert(strncmp(path, root, len) == 0);
	 * assert(path[len] == '/');
	 */

	(void) fprintf(stderr, "%s\n", path + len + 1);
}

static int
completion_check(char *prog, boolean_t finalize, boolean_t debug, char *path)
{
	shadow_handle_t *shp;
	int cnt;
	int err;

	if ((shp = shadow_open(path)) == NULL) {
		(void) fprintf(stderr,
		    gettext("failed to open shadow handle for %s: %s\n"),
		    path, shadow_errmsg());
		return (1);
	}

	/*
	 * Do a few migrations, if we are close to done, this should
	 * trigger the pending file updates that let us really know we
	 * are done.
	 */
	cnt = 0;
	while (cnt < 4) {
		err = shadow_migrate_one(shp);
		if (err && shadow_errno() != ESHADOW_MIGRATE_DONE) {
			(void) fprintf(stderr,
			    gettext("A file migration failed: %s\n"),
			    shadow_errmsg());
			return (1);
		} else if (err) {
			break;
		}
		cnt++;
	}

	if (shadow_migrate_done(shp)) {
		(void) fprintf(stderr,
		    gettext("Migration complete for %s.\n"), path);
		if (finalize) {
			if (shadow_migrate_finalize(shp)) {
				(void) fprintf(stderr,
				    gettext("Trouble finalizing "
				    "migration: %s\n"), shadow_errmsg());
			} else {
				(void) fprintf(stderr,
				    gettext("Migration finalized.\n"));
			}
		} else {
			(void) fprintf(stderr,
			    gettext("Run '%s -fc %s to finalize migration.\n"),
			    prog, path);
		}
		return (0);
	} else if (shadow_migrate_only_errors(shp)) {
		(void) fprintf(stderr,
		    gettext("Only files where we had an error "
		    "when migrating remain for %s.\n"
		    "Correct errors and re-try migration.\n"), path);
		return (1);
	}

	if (debug) {
		(void) fprintf(stderr, gettext("Remaining:\n"));
		(void) shadow_migrate_iter(shp, dump_path, path);
	}

	shadow_close(shp);

	(void) fprintf(stderr, gettext("Migration NOT complete for %s.\n"),
	    path);
	return (1);
}

int
main(int argc, char **argv)
{
	struct sigaction act;
	struct group *ge;
	boolean_t quickcheck = B_FALSE;
	boolean_t finalize = B_FALSE;
	boolean_t debug = B_FALSE;
	gid_t gid;
	char path[MAXPATHLEN];
	char *state;
	int count;
	int rv;
	int c;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "c:df")) != -1) {
		switch (c) {
		case 'c':
			quickcheck = B_TRUE;
			(void) strlcpy(path, optarg, MAXPATHLEN);
			break;
		case 'f':
			finalize = B_TRUE;
			break;
		case 'd':
			debug = B_TRUE;
			break;
		case '?':
		default:
			return (Usage(argv[0]));
		}
	}

	if (!quickcheck && (debug || finalize))
		quickcheck = B_TRUE;

	if (quickcheck) {
		return (completion_check(argv[0], finalize, debug, path));
	}

	/* We can only provide certain information if the shadowd is running */
	state = smf_get_state(SHADOWD_INST_FMRI);
	if (state == NULL || strcmp(state, SCF_STATE_STRING_ONLINE) != 0) {
		(void) fprintf(stderr,
		    gettext("Stats unavailable, service \"%s\" not enabled.\n"),
		    SHADOWD_INST_FMRI);
		return (1);
	}

	if (argc > 2)
		return (Usage(argv[0]));

	if (argc == 2) {
		if (numfromstr(argv[1], "count", &count) < 0)
			return (1);
	} else {
		count = -1;
	}

	(void) snprintf(path, MAXPATHLEN, "%s/%s.%d",
	    SHADOWD_STATUS_REPORTS_DIR, SHADOWD_STATUS_PREFIX, getpid());

	(void) umask(002);

	/*
	 * The IPC endpoint needs to be group daemon even if running
	 * as root.  Ordinary users must run under the appropriate
	 * profile, doing so will run this program as gid daemon.
	 */
	errno = 0;
	if ((ge = getgrnam("daemon")) == NULL) {
		(void) fprintf(stderr,
		    gettext("Unable to determine gid of daemon: %s\n"),
		    strerror(errno));
		return (1);
	}
	gid = ge->gr_gid;

	if (getegid() != gid) {
		if (setegid(gid) != 0) {
			(void) fprintf(stderr,
			    gettext("%s must run as group daemon\n    restart "
			    "in the Shadow Migration Monitor profile,\n    or "
			    "with privilege to reset group id.\n"), argv[0]);
			return (1);
		}
	}

	if (setup(path))
		return (1);

	if (atexit(teardown) != 0) {
		(void) fprintf(stderr, gettext("Out of memory"));
		return (1);
	}

	(void) sigfillset(&act.sa_mask);
	act.sa_handler = handler;
	act.sa_flags = 0;
	(void) sigaction(SIGHUP, &act, NULL);
	(void) sigaction(SIGINT, &act, NULL);
	(void) sigaction(SIGQUIT, &act, NULL);
	(void) sigaction(SIGTERM, &act, NULL);

	rv = displayer(count);

	if (Timeout)
		(void) fprintf(stderr,
		    gettext("No response from \"%s\"\n"), SHADOWD_INST_FMRI);

	return (rv);
}
