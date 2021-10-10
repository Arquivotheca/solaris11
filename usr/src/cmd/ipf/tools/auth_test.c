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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stropts.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
/*
 * these two are needed in order to compile ip_compat.h
 */
#include <net/if.h>
#include <netinet/in.h>

#include <netinet/ip_fil.h>
#include <netinet/ip_auth.h>
#include <netinet/ipl.h>

#include "auth_match.h"

static int Quit = 0;
static int Ioctl_Fd = -1;

static void
sighndl(int signum)
{
	Quit = 1;
}

static int
open_ioctl(void)
{
	Ioctl_Fd = open(IPAUTH_NAME, O_RDWR);

	if (Ioctl_Fd == -1) {
		fprintf(stderr, "Unable to open %s (%s)\n",
		    IPAUTH_NAME,
		    strerror(errno));

		return (1);
	}

	return (0);
}

static void
close_ioctl(void)
{
	close(Ioctl_Fd);
}

static int
read_request(frauth_t *req)
{
	ipfobj_t	obj;

	bzero((char *)&obj, sizeof (obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = sizeof (frauth_t);
	obj.ipfo_ptr = req;
	obj.ipfo_type = IPFOBJ_FRAUTH;
	return (ioctl(Ioctl_Fd, SIOCAUTHW, &obj));
}

static int
write_response(const frauth_t *resp)
{
	ipfobj_t	obj;

	bzero((char *)&obj, sizeof (obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = sizeof (frauth_t);
	obj.ipfo_ptr = (void *)resp;
	obj.ipfo_type = IPFOBJ_FRAUTH;
	return (ioctl(Ioctl_Fd, SIOCAUTHR, &obj));
}

static void
usage(const char *prog)
{
	fprintf(stderr, "Usage:\t%s\n", prog);
	fprintf(stderr, "\t\t\t[-d] auth_rules.conf\n");
	exit(1);
}

extern int yydebug;

int
main(int argc, char *argv[], char *envp[])
{
	frauth_t fra_req;	/* Firewall request/response object */
	int	ret_val = 0;
	int opt;

	if (argc < 2) {
		usage(argv[0]);
		return (1);
	}

	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
			case 'd' : yydebug = 1; break;
			default	: usage(argv[0]);
		}
	}

	if ((optind >= argc) || (open_ioctl() == -1))
		return (1);

	if (auth_load_rules(argv[optind]) == -1) {
		close_ioctl();
		return (1);
	}

	sigset(SIGINT, sighndl);
	sigset(SIGTERM, sighndl);
	sigset(SIGQUIT, sighndl);

	while (Quit == 0) {
		bzero(&fra_req, sizeof (fra_req));

		if (read_request(&fra_req) == -1) {
			fprintf(stderr, "Failed to fetch request (%s)\n",
			    strerror(errno));
			Quit = 1;
			ret_val = 1;
			continue;
		}

		if (auth_check(&fra_req) == -1) {
			fprintf(stderr,
			    "auth_check() failed, going to block packet\n");
			fra_req.fra_pass = FR_BLOCK;
		}

		if (write_response(&fra_req) == -1) {
			fprintf(stderr, "Failed to write response (%s)\n",
			    strerror(errno));
			Quit = 1;
			ret_val = 1;
			continue;
		}
	}

	close_ioctl();
	printf("Going to unload rules");
	auth_unload_rules();

	return (ret_val);
}
