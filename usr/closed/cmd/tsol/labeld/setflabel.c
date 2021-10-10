/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */


/*
 *	File relabeling functions
 */


#include <priv.h>
#include <sys/priv.h>
#include <pwd.h>
#include <ucred.h>
#include <auth_attr.h>
#include <auth_list.h>
#include <syslog.h>

#include <bsm/adt.h>
#include <bsm/adt_event.h>
#include <thread.h>
#include <synch.h>
#include <tsol/label.h>
#include <sys/tsol/label_macro.h>
#include "labeld.h"
#include "impl.h"

#include <zone.h>
#include <fcntl.h>
#include <sys/zone.h>
#include <sys/param.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wait.h>
#include <locale.h>
#include <stdarg.h>
#include <errno.h>

static m_label_t *findmntlabel(char *, char *, size_t, const m_label_t *);

#define	TSOLRELABEL	"relabel"
#define	TSOLRELABELPROG	"/etc/security/tsol/relabel"


/*
 * This function is just a wrapper around getpathbylabel().
 */
static m_label_t *
findmntlabel(char *path, char *globalpath, size_t gblpathsize,
    const m_label_t *sl)
{
	m_label_t	*slabel;

	if (getpathbylabel((const char *)path, globalpath, gblpathsize,
	    sl) == 0)
		return (NULL);
	/*
	 * get label of mount point
	 */
	if ((slabel = m_label_alloc(MAC_LABEL)) == NULL)
		return (NULL);

	if (getlabel(globalpath, slabel) == -1) {
		m_label_free(slabel);
		return (NULL);
	}
	return (slabel);
}

#define	DGSL		1
#define	EQSL		2
#define	UGSL		3
#define	DJSL		4

static int
check_policy(m_label_t *src_label, m_label_t *dst_label,
    zoneid_t zoneid, char *user) {
	priv_set_t	*zoneprivs = NULL;
	const priv_impl_info_t	*ip = NULL;
	int		sl_relationship;
	int		result = 0;
	int		auth_chk = 1;

	if (user == NULL)
		auth_chk = 0;

	/*
	 *
	 * Get privileges of caller's zone to use for MAC checks
	 */
	if ((zoneprivs = priv_allocset()) == NULL) {
		result = EINVAL;
		goto chk_done;
	}

	/*
	 * This function is private to libc. Instead of using
	 * it, we could simply allocate something much bigger
	 * than a priv_set, but that would still be ugly.
	 */
	ip = getprivimplinfo();
	if (zone_getattr(zoneid, ZONE_ATTR_PRIVSET, zoneprivs,
	    sizeof (priv_chunk_t) * ip->priv_setsize) == -1) {
		result = EINVAL;
		goto chk_done;
	}
	/*
	 * Determine how labels are related
	 */

	if (blstrictdom(src_label, dst_label))
		sl_relationship = DGSL;
	else if (blequal(src_label, dst_label))
		sl_relationship = EQSL;
	else if (blstrictdom(dst_label, src_label))
		sl_relationship = UGSL;
	else
		sl_relationship = DJSL;

	switch (sl_relationship) {
	case DJSL:
	case DGSL:
		/*
		 * Verify that downgrade is allowed in this zone
		 */
		if (!priv_ismember(zoneprivs, PRIV_FILE_DOWNGRADE_SL)) {
			/*
			 * Zone not authorized to downgrade
			 */
			result = EACCES;
			goto chk_done;
		}
		/*
		 * Check if user is authorized to downgrade
		 */

		if (auth_chk && !chkauthattr(FILE_DOWNGRADE_SL_AUTH, user)) {
			/*
			 * User not authorized to downgrade
			 */
			result = EACCES;
			goto chk_done;
		}
		break;

	case UGSL:
		/*
		 * Verify that upgrade is allowed in this zone
		 */
		if (!priv_ismember(zoneprivs, PRIV_FILE_UPGRADE_SL)) {
			/*
			 * Zone not authorized to upgrade
			 */
			result = EACCES;
			goto chk_done;
		}
		/*
		 * Check if user is authorized to upgrade
		 */
		if (auth_chk && !chkauthattr(FILE_UPGRADE_SL_AUTH, user)) {
			/*
			 * User not authorized to upgrade
			 */
			result = EACCES;
			goto chk_done;
		}
		break;

	case EQSL:
		result = EEXIST;
		goto chk_done;
	}
chk_done:
	if (zoneprivs != NULL)
		priv_freeset(zoneprivs);
	return (result);
}

#define	clcall call->cargs.setfbcl_arg
#define	clret ret->rvals.setfbcl_ret

/*
 *	setflbl - Set SL of pathname
 *
 *	Entry	pathname = pathname of file to set
 *		sl = label of file to set
 *
 *	Exit	None.
 *
 *	Returns	err = 	-1, If invalid pathname
 *			0, If successful.
 *			Character offset in string of error.
 */

void
setflbl(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	m_label_t	*src_label = NULL;
	m_label_t	*dst_label = NULL;
	m_label_t	*new_label;
	m_label_t	*ucred_label;
	m_label_t	admin_low;
	m_label_t	admin_high;
	blrange_t	*range;
	set_id		blinset_id;
	uid_t		uid = ucred_getruid(uc);
	uid_t		euid = ucred_geteuid(uc);
	gid_t		gid = ucred_getrgid(uc);
	gid_t		egid = ucred_getegid(uc);
	char		*src_path;
	char		*dst_file;
	char		dst_path[MAXPATHLEN];
	char		global_src_path[MAXPATHLEN];
	char		global_dst_path[MAXPATHLEN];
	zoneid_t	zoneid = ucred_getzoneid(uc);
	struct passwd	*pwd, pwds;
	char		buf_pwd[1024];
	struct stat	stbuf;
	int		err;
	pid_t		pid;
	int		rc;
	adt_session_data_t	*ah;
	adt_event_data_t	*event;

	*len = RET_SIZE(setfbcl_ret_t, 0);

	if (debug > 1) {
		(void) fprintf(stderr, "labeld op=setfbcl:\n");
		(void) fprintf(stderr, "\tstring = %s\n", clcall.pathname);
	}

	src_path = clcall.pathname;	/* pathname needing label */
	new_label = &clcall.sl; 	/* new label */

	if ((ucred_label = ucred_getlabel(uc)) == NULL) {
		clret.status = EINVAL;
		goto setflabel_done;
	}

	pwd = getpwuid_r(uid, &pwds, buf_pwd, sizeof (buf_pwd));
	if (pwd == NULL) {
		clret.status = EACCES;
		goto setflabel_done;
	}

	if ((src_label = findmntlabel(src_path, global_src_path,
	    sizeof (global_src_path), ucred_label)) == NULL) {
		/* shouldn't happen */
		clret.status = errno;
		goto setflabel_done;
	}

	bsllow(&admin_low);
	bslhigh(&admin_high);
	if (zoneid == GLOBAL_ZONEID) {
		char *prefix;

		/*
		 * Don't change the integrity of global zone
		 */
		if (blequal(src_label, &admin_low) ||
		    blequal(src_label, &admin_high)) {
			clret.status = EROFS;
			goto setflabel_done;
		}

		/*
		 * Src file isn't in GLOBAL_ZONE
		 * Remove zone prefix so we can replace it with
		 * zone prefix of target.
		 */

		prefix = getzonerootbylabel(src_label);
		(void) strlcpy(dst_path, global_src_path + strlen(prefix),
		    MAXPATHLEN);
		free(prefix);

		/*
		 * Treat this as if the ucred label is the
		 * label of the src path
		 */

	} else { /* Not GLOBAL_ZONEID */

		/*
		 * Don't let labeled zones affect the global zone
		 */
		if (blequal(new_label, &admin_low) ||
		    blequal(new_label, &admin_high)) {
			clret.status = EACCES;
			goto setflabel_done;
		}

		blinset_id.type = USER_ACCREDITATION_RANGE;
		if ((blinset(new_label, &blinset_id)) != 1) {
			if (!chkauthattr(SYS_ACCRED_SET_AUTH, pwd->pw_name)) {
				/* Label out of range */
				clret.status = EACCES;
				goto setflabel_done;
			}
		}

		/*
		 * If caller is not in global zone then
		 * label of file must match label of caller
		 */
		if (!blequal(src_label, ucred_label)) {
			clret.status = EACCES;
			goto setflabel_done;
		}
		(void) strlcpy(dst_path, src_path, MAXPATHLEN);
	}
	/*
	 * Extract file name from full path
	 */
	dst_file = strrchr(dst_path, '/');
	if (dst_file == NULL) {
		clret.status = EINVAL;
		goto setflabel_done;
	}
	*dst_file = '\0';
	dst_file++;


	/*
	 * Verify that new_label is dominated by
	 * user's clearance and dominates user's min label
	 */

	if ((range = getuserrange(pwd->pw_name)) == NULL) {
		/* Label out of range */
		clret.status = EACCES;
		goto setflabel_done;
	}
	if (!blinrange(new_label, range)) {
		if (!chkauthattr(SYS_ACCRED_SET_AUTH, pwd->pw_name)) {
			/* Label out of range */
			m_label_free(range->lower_bound);
			m_label_free(range->upper_bound);
			free(range);
			clret.status = EACCES;
			goto setflabel_done;
		}
	}
	m_label_free(range->lower_bound);
	m_label_free(range->upper_bound);
	free(range);

	if (rc = check_policy(src_label, new_label, zoneid, pwd->pw_name)) {
		clret.status = rc;
		goto setflabel_done;
	}

	/*
	 * File must be writable by caller
	 * Parent directory must be writable by caller
	 *
	 * Let the relabel command check the access
	 * Capture stderr in pipe, and return it
	 * as a door return value
	 */

	/*
	 * Stat the file
	 * Make sure that it is a regular file
	 * Not a hard link
	 * Not a symlink
	 * Not a directory
	 */

	if ((lstat(global_src_path,  &stbuf) == -1)) {
		/* Can't find file */
		/* get errno */
		clret.status = errno;
		goto setflabel_done;
	}
	if ((stbuf.st_mode & S_IFMT) != S_IFREG) {
		/* Can only relabel regular files */
		clret.status = EISDIR;
		goto setflabel_done;
	}
	if (stbuf.st_nlink > 1) {
		/* Can't relabel files with hard links */
		clret.status = EMLINK;
		goto setflabel_done;
	}

	if ((dst_label = findmntlabel(dst_path, global_dst_path,
	    sizeof (global_dst_path), new_label)) == NULL) {
		/* shouldn't happen */
		clret.status = errno;
		goto setflabel_done;
	}
	/*
	 * Append filename to target directory
	 */
	(void) strlcat(global_dst_path, "/", MAXPATHLEN);
	(void) strlcat(global_dst_path, dst_file, MAXPATHLEN);

	/*
	 * Verify that destination path is owned by proper zone
	 */
	if (!blequal(new_label, dst_label)) {
		clret.status = EACCES;
		goto setflabel_audit;

	}
	/*
	 * Verify that destination doesn't exist
	 */

	if ((lstat(global_dst_path,  &stbuf) == 0)) {
		/* File already exists at that label */
		clret.status = EEXIST;
		goto setflabel_audit;
	}
	/*
	 * Fork
	 * Set up inheritable privileges for "relabel" command
	 * Set real and effective uid and gid from caller
	 */

	/* fork the process that does the actual work */
	pid = fork();
	if (pid == -1) {
		clret.status = errno;
		goto setflabel_audit;
	} else if (pid == 0) {
		const priv_set_t	*pset;

		if ((pset = ucred_getprivset(uc, PRIV_EFFECTIVE)) == NULL) {
			exit(1);
		}

		if ((priv_addset((priv_set_t *)pset,
		    PRIV_FILE_DAC_SEARCH)) == -1) {
			exit(1);
		}

		(void) setpflags(PRIV_AWARE, 1);
		if (setppriv(PRIV_SET, PRIV_INHERITABLE, pset) != 0) {
			if (debug > 1)
				(void) fprintf(stderr, "labeld: "
				    "setppriv(defaultpriv) failed\n");
			exit(1);
		}

		(void) setregid(gid, egid);
		(void) setreuid(uid, euid);
		/*
		 * close extra file descriptors?
		 */

		/* child process */

		if (debug > 1)
			(void) fprintf(stderr, "setflabel: exec relabel\n");
		(void) execlp(TSOLRELABELPROG, TSOLRELABEL, global_src_path,
		    global_dst_path,
		    NULL);
		if (debug > 1)
			(void) fprintf(stderr,
		    "setflabel: exec relabel failed\n");
		_exit(1);
	} else {

		/* parent */
		if ((waitpid(pid, &err, 0)) == -1) {
			if (errno == ECHILD)
				clret.status = 0;
			else
				clret.status = errno;
			goto setflabel_audit;
		} else {
			if (WEXITSTATUS(err) != 0)
				clret.status = EACCES;
			else
				clret.status = 0;
			goto setflabel_audit;
		}
	}

setflabel_audit:
	if (adt_start_session(&ah, NULL, 0) != 0) {
		syslog(LOG_ERR, "setflabel adt_start_session: %m",
		    errno);
	}
	if (adt_set_from_ucred(ah, uc, ADT_NEW) != 0) {
		syslog(LOG_ERR, "setflabel adt_set_from_ucred: %m",
		    errno);
	}
	if ((event = adt_alloc_event(ah, ADT_file_relabel)) == NULL) {
		syslog(LOG_ERR,
		    "setflabel adt_alloc_event(ADT_file_relabel): %m",
		    errno);
	}
	event->adt_file_relabel.file = global_src_path;
	event->adt_file_relabel.src_label = src_label;
	event->adt_file_relabel.dst_label = dst_label;
	if (clret.status == 0) {
		if (adt_put_event(event, ADT_SUCCESS, ADT_SUCCESS) != 0) {
			syslog(LOG_ERR, "setflabel "
			    "adt_put_event(ADT_file_relabel, success): %m",
			    errno);
		}
	} else {
		if (adt_put_event(event, ADT_FAILURE, clret.status) != 0) {
			syslog(LOG_ERR, "setflabel "
			    "adt_put_event(ADT_file_relabel, failure): %m",
			    errno);
		}
	}
	adt_free_event(event);
	(void) adt_end_session(ah);

setflabel_done:
	m_label_free(src_label);
	m_label_free(dst_label);
}
