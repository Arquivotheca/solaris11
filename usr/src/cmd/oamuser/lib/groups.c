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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/




#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <grp.h>
#include <unistd.h>
#include <userdefs.h>
#include <errno.h>
#include <limits.h>
#include <auth_attr.h>
#include <auth_list.h>
#include "users.h"
#include <libintl.h>

#define	MYBUFSIZE (LINE_MAX)
	/* Corresponds to MYBUFSIZE in grpck.c, BUFCONST in nss_dbdefs.c */

#define	CMD_USERMOD	"/usr/sbin/usermod"
#define	CMD_ROLEMOD	"/usr/sbin/rolemod"
#define	MAXAUTHS	NSS_BUFLEN_AUTHATTR
#define	MAXARGS		10	/* max arguments used for usermod/rolemod */

int
files_edit_groups(char *login, char *new_login, struct group_entry *gids[],
    int overwrite)
{
	char **memptr;
	char t_name[] = "/etc/gtmp.XXXXXX";
	int fd;
	FILE *e_fptr, *t_fptr;
	struct group *g_ptr;	/* group structure from fgetgrent */
	int i;
	int modified = 0;
	struct stat sbuf;

	int bufsize, g_length, sav_errno;
	long g_curr = 0L;
	char *g_string, *new_g_string, *gstr_off;

	if ((e_fptr = fopen(GROUP, "r")) == NULL)
		return (EX_UPDATE);

	if (fstat(fileno(e_fptr), &sbuf) != 0) {
		(void) fclose(e_fptr);
		return (EX_UPDATE);
	}

	if ((fd = mkstemp(t_name)) == -1) {
		(void) fclose(e_fptr);
		return (EX_UPDATE);
	}

	if ((t_fptr = fdopen(fd, "w")) == NULL) {
		(void) close(fd);
		(void) unlink(t_name);
		(void) fclose(e_fptr);
		return (EX_UPDATE);
	}

	/*
	 * Get ownership and permissions correct
	 */

	if (fchmod(fd, sbuf.st_mode) != 0 ||
	    fchown(fd, sbuf.st_uid, sbuf.st_gid) != 0) {
		(void) fclose(t_fptr);
		(void) fclose(e_fptr);
		(void) unlink(t_name);
		return (EX_UPDATE);
	}

	g_curr = ftell(e_fptr);

	/* Make TMP file look like we want GROUP file to look */

	bufsize = MYBUFSIZE;
	if ((g_string = malloc(bufsize)) == NULL) {
		(void) fclose(t_fptr);
		(void) fclose(e_fptr);
		(void) unlink(t_name);
		return (EX_UPDATE);
	}
	/*
	 * bufsize contains the size of the currently allocated buffer
	 * buffer size, which is initially MYBUFSIZE but when a line
	 * greater than MYBUFSIZE is encountered then bufsize gets increased
	 * by MYBUFSIZE.
	 * g_string always points to the beginning of the buffer (even after
	 * realloc()).
	 * gstr_off = g_string + MYBUFSIZE * (n), where n >= 0.
	 */
	while (!feof(e_fptr) && !ferror(e_fptr)) {
		g_length = 0;
		gstr_off = g_string;
		while (fgets(gstr_off, (bufsize - g_length), e_fptr) != NULL) {
			g_length += strlen(gstr_off);
			if (g_string[g_length - 1] == '\n' || feof(e_fptr))
				break;
			new_g_string = realloc(g_string, (bufsize + MYBUFSIZE));
			if (new_g_string == NULL) {
				free(g_string);
				(void) fclose(t_fptr);
				(void) fclose(e_fptr);
				(void) unlink(t_name);
				return (EX_UPDATE);
			}
			bufsize += MYBUFSIZE;
			g_string = new_g_string;
			gstr_off = g_string + g_length;
		}
		if (g_length == 0) {
			continue;
		}

		/* While there is another group string */

		(void) fseek(e_fptr, g_curr, SEEK_SET);
		errno = 0;
		g_ptr = fgetgrent(e_fptr);
		sav_errno = errno;
		g_curr = ftell(e_fptr);

		if (g_ptr == NULL) {
			/* tried to parse a group string over MYBUFSIZ char */
			if (sav_errno == ERANGE) {
				(void) fprintf(stderr, gettext("WARNING: Group"
				    " entry exceeds 2048 char: "
				    "/etc/group entry truncated.\n"));

			} else {
				(void) fprintf(stderr, gettext("ERROR: Failed"
				    " to read /etc/group file due to invalid"
				    " entry or read error.\n"));
			}

			modified = 0; /* bad group file: cannot rebuild */
			break;
		}

		/* first delete the login from the group, if it's there */
		if (overwrite || gids == NULL) {
			if (g_ptr->gr_mem != NULL) {
				for (memptr = g_ptr->gr_mem; *memptr;
				    memptr++) {
					if (strcmp(*memptr, login) == 0) {
						/* Delete this one */
						char **from = memptr + 1;

						g_length -= (strlen(*memptr)+1);

						do {
							*(from - 1) = *from;
						} while (*from++);

						modified++;
						break;
					}
				}
			}
		}

		/* now check to see if group is one to add to */
		if (gids != NULL) {
			for (i = 0; gids[i] != NULL; i++) {
				if (g_ptr->gr_gid == gids[i]->gid) {
					/*
					 * If group name present, then check
					 * that it matches the one in the
					 * /etc/group entry.
					 */
					if (gids[i]->group_name &&
					    strcmp(gids[i]->group_name,
					    g_ptr->gr_name)) {
						continue;
					}

					/* Find end */
					for (memptr = g_ptr->gr_mem;
					    *memptr; memptr++)
					;
					g_length += strlen(new_login ?
					    new_login : login) + 1;

					*memptr++ = new_login ?
					    new_login : login;
					*memptr = NULL;

					modified++;
				}
			}
		}
		putgrent(g_ptr, t_fptr);
	}
	free(g_string);

	(void) fclose(e_fptr);

	if (fclose(t_fptr) != 0) {
		(void) unlink(t_name);
		return (EX_UPDATE);
	}

	/* Now, update GROUP file, if it was modified */
	if (modified) {
		if (rename(t_name, GROUP) != 0) {
			(void) unlink(t_name);
			return (EX_UPDATE);
		}
		return (EX_SUCCESS);
	} else {
		(void) unlink(t_name);
		return (EX_SUCCESS);
	}
}

/*
 * Validate a list of users.
 * Returns
 *	NULL - if all users are valid. users is filled in
 *	pointer to bad user - if user is invalid
 */

char *
check_users(char *userlist, sec_repository_t *repo,
	nss_XbyY_buf_t *nssbuf, char **users)
{
	char *username;
	struct passwd *pw;
	char *lasts;

	username = strtok_r(userlist, ",", &lasts);
	while (username != NULL) {
		if (repo->rops->get_pwnam(username, &pw, nssbuf) != 0) {
			return (username); /* invalid user */
		}
		*users++ = username;
		username = strtok_r(NULL, ",", &lasts);
	}

	*users = NULL; /* last entry */

	return (NULL);
}

/*
 * Append group to the auth name
 */
char *
appendedauth(char *group)
{
	static char authname[MAXAUTHS];

	(void) snprintf(authname, sizeof (authname), "%s/%s",
	    GROUP_ASSIGN_AUTH, group);

	return (authname);
}

/*
 * exec command
 */
static int
exec_cmd(char *nargv[])
{
	int rc;
	int pid;
	int wpid;

	switch (pid = fork()) {
	case 0:
		/* Child */
		if (getuid() != 0) {
			(void) setuid(0);
		}

		if (execvp(nargv[0], nargv) == -1) {
			exit(EX_FAILURE);
		}
		break;

	case -1:
		/* ERROR */
		return (EX_FAILURE);

	default:
		/* PARENT */

		while ((wpid = wait(&rc)) != pid) {
			if (wpid == -1)
				return (EX_FAILURE);
		}
		rc = (rc >> 8) & 0xff;
	}

	return (rc);
}
/*
 * Update the solaris.group.assign/<groupname>
 * authorization for the caller. Uses passmgmt
 * to do the operation.
 *
 * if authorize
 *	B_TRUE - Add solaris.group.assign/groupname auth
 *	B_FALSE - Remove solaris.group.assign/groupname auth
 * Returns
 *	0	   - success
 *	EX_FAILURE - failure
 */
int
group_authorize(char *username, char *group, boolean_t authorize)
{
	char *nargv[MAXARGS];
	char *authname;
	int argindex;
	char *cur_auths;
	char *usertype;
	userattr_t *u;
	char old_auths[MAXAUTHS];
	char new_auths[MAXAUTHS];
	char *cmdname;
	char *authstr;

	authname = appendedauth(group);
	/* Some redundancy checks */
	if (authorize) {
		/* No need to add auth to an already authorized user */
		if (chkauthattr(authname, username) != 0) {
			return (EX_SUCCESS);
		}
	} else {
		/* We don't delete if the user has the assign auth */
		if (chkauthattr(GROUP_ASSIGN_AUTH, username) != 0) {
			return (EX_SUCCESS);
		}
	}

	if ((u = getusernam(username)) == NULL) {
		return (EX_FAILURE);
	}

	bzero(&old_auths, sizeof (old_auths));
	bzero(&new_auths, sizeof (new_auths));

	/* Determine which cmd to user: usermod or rolemod? */
	usertype = kva_match(u->attr, USERATTR_TYPE_KW);
	if (usertype != NULL &&
	    strcmp(usertype, USERATTR_TYPE_NONADMIN_KW) == 0) {
		cmdname = CMD_ROLEMOD;
	} else {
		cmdname = CMD_USERMOD;
	}

	/* Construct new authorization list */
	cur_auths = kva_match(u->attr, USERATTR_AUTHS_KW);
	if (authorize) {
		authstr = attr_add(cur_auths, authname, new_auths,
		    sizeof (new_auths), ",");
	} else {
		/* Remove the authorization */
		authstr = attr_remove(cur_auths, authname, new_auths,
		    sizeof (new_auths), ",");
	}

	free_userattr(u);

	if (authstr == NULL) {
		return (EX_FAILURE);
	}

	/* Setup the command & its arguments */
	argindex = 0;
	nargv[argindex++] = cmdname;

	nargv[argindex++] = "-A"; /* Authorization */
	nargv[argindex++] = new_auths;
	nargv[argindex++] = username;
	nargv[argindex++] = NULL; /* for last arg */

	return (exec_cmd(nargv));
}
