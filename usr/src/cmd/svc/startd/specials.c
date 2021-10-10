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
 * specials.c - knowledge of special services
 *
 * svc.startd(1M) has duties that cannot be carried out without knowledge of the
 * transition of various services, such as the milestones, to their online
 * states.  Hooks are called with the restarter instance's ri_lock held, so
 * operations on all instances (or on the graph) should be performed
 * asynchronously.
 */

#include <sys/statvfs.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <libintl.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zone.h>

#include "startd.h"

void
special_null_transition()
{
}

static void
special_fsroot_post_online()
{
	static int once;
	char *locale;

	/*
	 * /usr, with timezone and locale data, is now available.
	 */
	if (!st->st_log_timezone_known) {
		tzset();
		st->st_log_timezone_known = 1;
	}

	if (!st->st_log_locale_known) {
		locale = st->st_locale;

		(void) setlocale(LC_ALL, "");
		st->st_locale = setlocale(LC_MESSAGES, "");
		if (st->st_locale) {
			st->st_locale = safe_strdup(st->st_locale);
			xstr_sanitize(st->st_locale);
			free(locale);
		} else {
			st->st_locale = locale;
		}

		(void) textdomain(TEXT_DOMAIN);
		st->st_log_locale_known = 1;
	}

	if (once)
		return;

	/*
	 * ctime(3C) ends with '\n\0'.
	 */
	once++;
	log_framework(LOG_INFO, "system start time was %s",
	    ctime(&st->st_start_time.tv_sec));
}

static void
special_fsminimal_post_online(void)
{
	ulong_t rfsid, fsid;
	int ret;

	log_framework(LOG_DEBUG, "special_fsminimal_post_online hook "
	    "executed\n");

	/*
	 * If /var is still read-only, and it is on a separate filesystem, then
	 * attempt to mount it read-write now.
	 */
	if ((ret = fs_is_read_only("/var", &fsid)) == 1) {
		(void) fs_is_read_only("/", &rfsid);

		if (rfsid != fsid) {
			log_framework(LOG_WARNING, "/var filesystem "
			    "read-only after system/filesystem/minimal\n");
			if (fs_remount("/var"))
				log_framework(LOG_WARNING, "/var "
				    "filesystem remount failed\n");
		}
	}

	if ((ret = fs_is_read_only("/var", &fsid)) != 1) {
		if (ret != 0)
			log_error(LOG_WARNING, gettext("couldn't check status "
			    "of /var filesystem: %s\n"), strerror(errno));

		/*
		 * Record boot time.
		 */
		utmpx_write_boottime();

		/*
		 * Reinitialize the logs to point to LOG_PREFIX_NORMAL.
		 */
		log_init();

	}

	if ((ret = fs_is_read_only("/etc/svc", &fsid)) != 1) {
		if (ret != 0)
			log_error(LOG_WARNING, gettext("couldn't check status "
			    "of /etc/svc filesystem: %s\n"), strerror(errno));

		/*
		 * Take pending snapshots and create a svc.startd instance.
		 */
		(void) startd_thread_create(restarter_post_fsminimal_thread,
		    NULL);
	}
}

static void
special_single_post_online(void)
{
	int r;

	log_framework(LOG_DEBUG, "special_single_post_online hook executed\n");

	/*
	 * Un-set the special reconfig reboot property.
	 */
	r = libscf_set_reconfig(0);
	switch (r) {
	case 0:
	case ENOENT:
		break;

	case EPERM:
	case EACCES:
	case EROFS:
		log_error(LOG_WARNING, "Could not clear reconfiguration "
		    "property: %s.\n", strerror(r));
		break;

	default:
		bad_error("libscf_set_reconfig", r);
	}

	if (booting_to_single_user)
		(void) startd_thread_create(single_user_thread, NULL);
}

static void
special_unconfigure_post_online(void)
{
	extern boolean_t go_to_unconfigure;

	log_framework(LOG_DEBUG,
	    "special_unconfigure_post_online hook executed\n");

	go_to_unconfigure = B_FALSE;
}

static service_hook_assn_t special_svcs[] = {
	{ "svc:/system/filesystem/root:default",
		special_null_transition,
		special_fsroot_post_online,
		special_null_transition },
	{ "svc:/system/filesystem/minimal:default",
		special_null_transition,
		special_fsminimal_post_online,
		special_null_transition },
	{ "svc:/milestone/single-user:default",
		special_null_transition,
		special_single_post_online,
		special_null_transition },
	{ "svc:/milestone/unconfig:default",
		special_null_transition,
		special_unconfigure_post_online,
		special_null_transition },
};

void
special_online_hooks_get(const char *fmri, instance_hook_t *pre_onp,
    instance_hook_t *post_onp, instance_hook_t *post_offp)
{
	int i;

	for (i = 0; i < sizeof (special_svcs) / sizeof (service_hook_assn_t);
	    i++)
		if (strcmp(fmri, special_svcs[i].sh_fmri) == 0) {
			*pre_onp = special_svcs[i].sh_pre_online_hook;
			*post_onp = special_svcs[i].sh_post_online_hook;
			*post_offp = special_svcs[i].sh_post_offline_hook;
			return;
		}

	*pre_onp = *post_onp = *post_offp = special_null_transition;
}
