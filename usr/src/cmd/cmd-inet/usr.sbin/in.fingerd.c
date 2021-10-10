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
 * Copyright (c) 1986, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

/*
 * Finger server.
 */
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <ctype.h>
#include <syslog.h>
#include <libscf.h>
#include <libscf_priv.h>

#define	SMF_SNAPSHOT_RUNNING	"running"
#define	FINGERD_FMRI		"svc:/network/finger:default"

#define	MAXARGS 10

void fatal(char *prog, char *s);

static void read_conf_prop(boolean_t *);

int
main(argc, argv)
	int argc;
	char *argv[];
{
	register char *sp;
	char line[512];
	struct sockaddr_storage sin;
	socklen_t sinlen;
	pid_t pid, w;
	int i, p[2], status;
	FILE *fp;
	char *av[MAXARGS + 1];
	boolean_t can_forward = B_FALSE;

	openlog("fingerd", LOG_PID | LOG_ODELAY, LOG_DAEMON);

	/*
	 * Get the Finger forwarding switch state. If we're not
	 * successful, print an error message & disable forwarding.
	 */
	read_conf_prop(&can_forward);

	sinlen = sizeof (sin);
	if (getpeername(0, (struct sockaddr *)&sin, &sinlen) < 0)
		fatal(argv[0], "getpeername");
	line[0] = '\0';
	if (fgets(line, sizeof (line), stdin) == NULL)
		exit(1);
	sp = line;
	av[0] = "finger";
	i = 1;

	/* skip past leading white space */
	while (isspace(*sp))
		sp++;

	/*
	 * The finger protocol says a "/W" switch means verbose output.
	 * We explicitly set either the "long" or "short" output flags
	 * to the finger program so that we don't have to know what what
	 * the "finger" program's default is.
	 */
	if (*sp == '/' && (sp[1] == 'W' || sp[1] == 'w')) {
		sp += 2;
		av[i++] = "-l";
	} else {
		av[i++] = "-s";
	}

	/* look for username arguments */
	while (i < MAXARGS) {

		/* skip over leading white space */
		while (isspace(*sp))
			sp++;

		/* don't allow host forwarding unless specified */
		if ((can_forward == B_FALSE) && (strchr(sp, '@') != NULL)) {
			(void) printf("Finger forwarding service denied.\n");
			exit(0);
		}

		/* check for end of "command line" */
		if (*sp == '\0')
			break;

		/* pick up another name argument */
		av[i++] = sp;
		while ((*sp != '\0') && !isspace(*sp))
			sp++;

		/* check again for end of "command line" */
		if (*sp == '\0')
			break;
		else
			*sp++ = '\0';
	}

	av[i] = (char *)0;
	if (pipe(p) < 0)
		fatal(argv[0], "pipe");

	if ((pid = fork()) == 0) {
		close(p[0]);
		if (p[1] != 1) {
			dup2(p[1], 1);
			close(p[1]);
		}
		execv("/usr/bin/finger", av);
		printf("No local finger program found\n");
		fflush(stdout);
		_exit(1);
	}
	if (pid == (pid_t)-1)
		fatal(argv[0], "fork");
	close(p[1]);
	if ((fp = fdopen(p[0], "r")) == NULL)
		fatal(argv[0], "fdopen");
	while ((i = getc(fp)) != EOF) {
		if (i == '\n')
			putchar('\r');
		putchar(i);
	}
	fclose(fp);
	while ((w = wait(&status)) != pid && w != (pid_t)-1)
		;
	return (0);
}

/*
 * Read properties from SMF configuration repository using the
 * libscf(3LIB) APIs.
 *
 * Currently, svc:/network/finger only has one property,
 * 'config/forwarding'. This function gets the value of that property
 * from a snapshot of the SMF_SNAPSHOT_RUNNING instance and returns it
 * in "prop_val".
 */
static void
read_conf_prop(boolean_t *prop_val)
{
	scf_handle_t *scf_handle_p;
	scf_property_t *scf_prop_p = NULL;
	scf_instance_t *inst = NULL;
	scf_propertygroup_t *pg = NULL;
	scf_value_t *value = NULL;
	int errcode = 0;
	uint8_t boolvl;
	scf_snapshot_t *scf_snapshot_p = NULL;

	/* set-up communication with FINGERD_FMRI's repository (SCF) */
	if ((scf_handle_p = scf_handle_create(SCF_VERSION)) == NULL) {
		syslog(LOG_ERR, "scf_handle_create failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	if (scf_handle_bind(scf_handle_p) == -1) {
		syslog(LOG_ERR, "scf_handle_bind failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	inst = scf_instance_create(scf_handle_p);
	pg = scf_pg_create(scf_handle_p);
	scf_prop_p = scf_property_create(scf_handle_p);
	if (inst == NULL || pg == NULL || scf_prop_p == NULL) {
		syslog(LOG_ERR, "Initialization failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	errcode = scf_handle_decode_fmri(scf_handle_p, FINGERD_FMRI,
	    NULL, NULL, inst, pg, scf_prop_p, 0);
	if (errcode != 0) {
		syslog(LOG_ERR, "scf_handle_decode_fmri failed: "
		    "%s\n", scf_strerror(scf_error()));

		goto cleanup;
	}

	/* take a snapshot of the SMF_SNAPSHOT_RUNNING instance */
	scf_snapshot_p = scf_snapshot_create(scf_handle_p);
	if (scf_snapshot_p == NULL) {
		syslog(LOG_ERR, "scf_snapshot_create failed: "
		    "%s\n", scf_strerror(scf_error()));

		goto cleanup;
	}
	errcode = scf_instance_get_snapshot(inst, SMF_SNAPSHOT_RUNNING,
	    scf_snapshot_p);
	if (errcode == -1) {
		syslog(LOG_ERR, "scf_instance_get_snapshot failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}

	/* get 'config/forwarding' property */
	if (scf_instance_get_pg_composed(inst, scf_snapshot_p, "config", pg)
	    != 0) {

		syslog(LOG_ERR, "scf_instance_pg_composed failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	if (scf_pg_get_property(pg, "forwarding", scf_prop_p) != 0) {
		syslog(LOG_ERR, "scf_pg_get_property failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	value = scf_value_create(scf_handle_p);
	if (value == NULL) {
		syslog(LOG_ERR, "scf_value_create failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	if (scf_property_get_value(scf_prop_p, value) != 0) {
		syslog(LOG_ERR, "scf_property_get_value failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}
	if (scf_value_get_boolean(value, &boolvl) != 0) {
		syslog(LOG_ERR, "scf_value_get_boolean failed: %s\n",
		    scf_strerror(scf_error()));

		goto cleanup;
	}

	*prop_val = (boolean_t)boolvl;

cleanup:
	if (scf_handle_p != NULL) {
		(void) scf_handle_unbind(scf_handle_p);
		scf_handle_destroy(scf_handle_p);
	}
	if (inst != NULL)
		scf_instance_destroy(inst);
	if (pg != NULL)
		scf_pg_destroy(pg);
	if (scf_prop_p != NULL)
		scf_property_destroy(scf_prop_p);
	if (value != NULL)
		scf_value_destroy(value);
	if (scf_snapshot_p != NULL)
		scf_snapshot_destroy(scf_snapshot_p);
}

void
fatal(char *prog, char *s)
{
	fprintf(stderr, "%s: ", prog);
	perror(s);
	exit(1);
}
