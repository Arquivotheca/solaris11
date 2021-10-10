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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * zoneadm is a command interpreter for zone administration.  It is all in
 * C (i.e., no lex/yacc), and all the argument passing is argc/argv based.
 * main() calls parse_and_run() which calls cmd_match(), then invokes the
 * appropriate command's handler function.  The rest of the program is the
 * handler functions and their helper functions.
 *
 * Some of the helper functions are used largely to simplify I18N: reducing
 * the need for translation notes.  This is particularly true of many of
 * the zerror() calls: doing e.g. zerror(gettext("%s failed"), "foo") rather
 * than zerror(gettext("foo failed")) with a translation note indicating
 * that "foo" need not be translated.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <zone.h>
#include <priv.h>
#include <locale.h>
#include <libintl.h>
#include <libzonecfg.h>
#include <bsm/adt.h>
#include <sys/brand.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <assert.h>
#include <sys/sockio.h>
#include <sys/mntent.h>
#include <limits.h>
#include <dirent.h>
#include <uuid/uuid.h>
#include <fcntl.h>
#include <door.h>
#include <macros.h>
#include <libgen.h>
#include <fnmatch.h>
#include <sys/modctl.h>
#include <libbrand.h>
#include <libscf.h>
#include <procfs.h>
#include <strings.h>
#include <pool.h>
#include <sys/pool.h>
#include <sys/priocntl.h>
#include <sys/fsspriocntl.h>
#include <libdladm.h>
#include <libdllink.h>
#include <pwd.h>
#include <auth_list.h>
#include <auth_attr.h>
#include <secdb.h>
#include <sys/mkdev.h>

#include "zoneadm.h"

#define	MAXARGS	8
#define	SOURCE_ZONE (CMD_MAX + 1)

/* Reflects kernel zone entries */
typedef struct zone_entry {
	zoneid_t	zid;
	char		zname[ZONENAME_MAX];
	char		*zstate_str;
	zone_state_t	zstate_num;
	char		zbrand[MAXNAMELEN];
	char		zroot[MAXPATHLEN];
	char		zuuid[UUID_PRINTABLE_STRING_LENGTH];
	zone_iptype_t	ziptype;
	char		zreadonly;
	char		zmacprofile[MAXNAMELEN];
	uint64_t	zuniqid;
} zone_entry_t;

#define	CLUSTER_BRAND_NAME	"cluster"

static zone_entry_t *zents;
static size_t nzents;

#define	LOOPBACK_IF	"lo0"
#define	SOCKET_AF(af)	(((af) == AF_UNSPEC) ? AF_INET : (af))

struct net_if {
	char	*name;
	int	af;
};

/* 0755 is the default directory mode. */
#define	DEFAULT_DIR_MODE \
	(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

/* Default Owner, Group and permissions for zoneroot */
#define	DEFAULT_ZP_MODE	(S_IRWXU)
#define	DEFAULT_ZP_OWNER	0
#define	DEFAULT_ZP_GROUP	-1

struct cmd {
	uint_t	cmd_num;				/* command number */
	char	*cmd_name;				/* command name */
	char	*short_usage;				/* short form help */
	int	(*handler)(int argc, char *argv[]);	/* function to call */

};

/*
 * Callback data used with warn_nonbe_mounts()
 */
typedef struct nonbe_mount_info {
	char *rootpath;			/* GZ mountpoint of zone root */
	char rootds[MAXPATHLEN];	/* Dataset mounted at zone root */
	size_t len;			/* Length of rootds */
	int badmounts;			/* Mount cnt w/ dev not under rootds */
} nonbe_mount_info_t;

#define	SHELP_HELP	"help"
#define	SHELP_BOOT	"boot [-w|-W] [-- boot_arguments]"
#define	SHELP_HALT	"halt"
#define	SHELP_SHUTDOWN	"shutdown [-r [-- boot_arguments]"
#define	SHELP_READY	"ready"
#define	SHELP_REBOOT	"reboot [-w|-W] [-- boot_arguments]"
#define	SHELP_LIST	"list [-cipv]"
#define	SHELP_VERIFY	"verify"
#define	SHELP_INSTALL	"install [brand-specific args]"
#define	SHELP_UNINSTALL	"uninstall [-F] [brand-specific args]"
#define	SHELP_CLONE	"clone [-m method] [-s <ZFS snapshot>] "\
	"[brand-specific args] zonename"
#define	SHELP_MOVE	"move zonepath"
#define	SHELP_DETACH	"detach [-n] [brand-specific args]"
#define	SHELP_ATTACH	"attach [-F] [-n <path>] [brand-specific args]"
#define	SHELP_MARK	"mark incomplete"

#define	EXEC_PREFIX	"exec "
#define	EXEC_LEN	(strlen(EXEC_PREFIX))

static int cleanup_zonepath(char *, boolean_t);


static int help_func(int argc, char *argv[]);
static int ready_func(int argc, char *argv[]);
static int boot_func(int argc, char *argv[]);
static int shutdown_func(int argc, char *argv[]);
static int halt_func(int argc, char *argv[]);
static int reboot_func(int argc, char *argv[]);
static int list_func(int argc, char *argv[]);
static int verify_func(int argc, char *argv[]);
static int install_func(int argc, char *argv[]);
static int uninstall_func(int argc, char *argv[]);
static int mount_func(int argc, char *argv[]);
static int unmount_func(int argc, char *argv[]);
static int clone_func(int argc, char *argv[]);
static int move_func(int argc, char *argv[]);
static int detach_func(int argc, char *argv[]);
static int attach_func(int argc, char *argv[]);
static int mark_func(int argc, char *argv[]);
static int apply_func(int argc, char *argv[]);
static int sysboot_func(int argc, char *argv[]);
static int sanity_check(char *zone, int cmd_num, boolean_t running,
    boolean_t unsafe_when_running, boolean_t force);
static int cmd_match(char *cmd);
static int verify_details(int, char *argv[]);
static int verify_brand(zone_dochandle_t, int, char *argv[]);
static int invoke_brand_handler(int, char *argv[]);

static struct cmd cmdtab[] = {
	{ CMD_HELP,		"help",		SHELP_HELP,	help_func },
	{ CMD_BOOT,		"boot",		SHELP_BOOT,	boot_func },
	{ CMD_SHUTDOWN,		"shutdown",	SHELP_SHUTDOWN,	shutdown_func },
	{ CMD_HALT,		"halt",		SHELP_HALT,	halt_func },
	{ CMD_READY,		"ready",	SHELP_READY,	ready_func },
	{ CMD_REBOOT,		"reboot",	SHELP_REBOOT,	reboot_func },
	{ CMD_LIST,		"list",		SHELP_LIST,	list_func },
	{ CMD_VERIFY,		"verify",	SHELP_VERIFY,	verify_func },
	{ CMD_INSTALL,		"install",	SHELP_INSTALL,	install_func },
	{ CMD_UNINSTALL,	"uninstall",	SHELP_UNINSTALL,
	    uninstall_func },
	/* mount and unmount are private commands for admin/install */
	{ CMD_MOUNT,		"mount",	NULL,		mount_func },
	{ CMD_UNMOUNT,		"unmount",	NULL,		unmount_func },
	{ CMD_CLONE,		"clone",	SHELP_CLONE,	clone_func },
	{ CMD_MOVE,		"move",		SHELP_MOVE,	move_func },
	{ CMD_DETACH,		"detach",	SHELP_DETACH,	detach_func },
	{ CMD_ATTACH,		"attach",	SHELP_ATTACH,	attach_func },
	{ CMD_MARK,		"mark",		SHELP_MARK,	mark_func },
	{ CMD_APPLY,		"apply",	NULL,		apply_func },
	{ CMD_SYSBOOT,		"sysboot",	NULL,		sysboot_func }
};

/* global variables */

/* set early in main(), never modified thereafter, used all over the place */
static char *execname;
static char target_brand[MAXNAMELEN];
static char default_brand[MAXPATHLEN];
static char *locale;
char *target_zone;
static char *target_uuid;
char *username;

/* Used for dynamically generated help strings */
#define	LONG_HELP_SZ	(80*100)	/* 100 lines on a standard terminal */
#define	ADD_BRAND(b, m)	assert(snprintf((b), sizeof (b), gettext(m), \
	(target_brand[0] == '\0') ? "brands" : target_brand) <= sizeof (b))

char *
cmd_to_str(int cmd_num)
{
	assert(cmd_num >= CMD_MIN && cmd_num <= CMD_MAX);
	return (cmdtab[cmd_num].cmd_name);
}

/* This is a separate function because of gettext() wrapping. */
static char *
long_help(int cmd_num)
{
	/*
	 * If the help message varies based on the zone, this buffer will
	 * contain the variable text that is returned.
	 */
	static char buf[LONG_HELP_SZ];

	assert(cmd_num >= CMD_MIN && cmd_num <= CMD_MAX);
	switch (cmd_num) {
	case CMD_HELP:
		return (gettext("Print usage message."));
	case CMD_BOOT:
		return (gettext("Activates (boots) specified zone.  See "
		    "zoneadm(1m) for valid boot\n\targuments."));
	case CMD_HALT:
		return (gettext("Halts specified zone, bypassing shutdown "
		    "scripts and removing runtime\n\tresources of the zone."));
	case CMD_SHUTDOWN:
		return (gettext("Cleanly shut down (or reboot) the zone.\n\t"
		    "See zoneadm(1m) for valid boot arguments."));
	case CMD_READY:
		return (gettext("Prepares a zone for running applications but "
		    "does not start any user\n\tprocesses in the zone."));
	case CMD_REBOOT:
		return (gettext("Restarts the zone (equivalent to a halt / "
		    "boot sequence).\n\tSee zoneadm(1m) for valid boot "
		    "arguments."));
	case CMD_LIST:
		return (gettext("Lists the current zones, or a "
		    "specific zone if indicated.  By default,\n\tall "
		    "running zones are listed, though this can be "
		    "expanded to all\n\tinstalled zones with the -i "
		    "option or all configured zones with the\n\t-c "
		    "option.  When used with the general -z <zone> and/or -u "
		    "<uuid-match>\n\toptions, lists only the specified "
		    "matching zone, but lists it\n\tregardless of its state, "
		    "and the -i and -c options are disallowed.  The\n\t-v "
		    "option can be used to display verbose information: zone "
		    "name, id,\n\tcurrent state, root directory and options.  "
		    "The -p option can be used\n\tto request machine-parsable "
		    "output.  The -v and -p options are mutually\n\texclusive."
		    "  If neither -v nor -p is used, just the zone name is "
		    "listed."));
	case CMD_VERIFY:
		return (gettext("Check to make sure the configuration "
		    "can safely be instantiated\n\ton the machine: "
		    "physical network interfaces exist, etc."));
	case CMD_INSTALL:
		ADD_BRAND(buf, "Install the configuration on to the "
		    "system.\n\tAll arguments are passed to the brand "
		    "installation function;\n\tsee %s(5) for more "
		    "information.");
		return (buf);
	case CMD_UNINSTALL:
		ADD_BRAND(buf, "Uninstall the configuration from the system.  "
		    "The -F flag can be used\n\tto force the action.  All "
		    "other arguments are passed to the brand\n\tuninstall "
		    "function; see %s(5) for more information.");
		return (buf);
	case CMD_CLONE:
		ADD_BRAND(buf, "Clone the installation of another zone.  "
		    "The -m option can be used to\n\tspecify 'copy' which "
		    "forces a copy of the source zone.  The -s option\n\t"
		    "can be used to specify the name of a ZFS snapshot "
		    "that was taken from\n\ta previous clone command.  The "
		    "snapshot will be used as the source\n\tinstead of "
		    "creating a new ZFS snapshot.  All other arguments are "
		    "passed\n\tto the brand clone function; see "
		    "%s(5) for more information.");
		return (buf);
	case CMD_MOVE:
		return (gettext("Move the zone to a new zonepath."));
	case CMD_DETACH:
		ADD_BRAND(buf, "Detach the zone from the system. The zone "
		    "state is changed to\n\t'configured' (but the files under "
		    "the zonepath are untouched).\n\tThe zone can subsequently "
		    "be attached, or can be moved to another\n\tsystem and "
		    "attached there.  The -n option can be used to specify\n\t"
		    "'no-execute' mode.  When -n is used, the information "
		    "needed to attach\n\tthe zone is sent to standard output "
		    "but the zone is not actually\n\tdetached.  All other "
		    "arguments are passed to the brand detach function;\n\tsee "
		    "%s(5) for more information.");
		return (buf);
	case CMD_ATTACH:
		ADD_BRAND(buf, "Attach the zone to the system.  The zone "
		    "state must be 'configured'\n\tprior to attach; upon "
		    "successful completion, the zone state will be\n\t"
		    "'installed'.  The system software on the current "
		    "system must be\n\tcompatible with the software on the "
		    "zone's original system.\n\tSpecify -F "
		    "to force the attach and skip software compatibility "
		    "tests.\n\tThe -n option can be used to specify "
		    "'no-execute' mode.  When -n is\n\tused, the information "
		    "needed to attach the zone is read from the\n\tspecified "
		    "path and the configuration is only validated.  The path "
		    "can\n\tbe '-' to specify standard input.  The -F and -n "
		    "options are mutually\n\texclusive.  All other arguments "
		    "are passed to the brand attach\n\tfunction; see "
		    "%s(5) for more information.");
		return (buf);
	case CMD_MARK:
		return (gettext("Set the state of the zone.  This can be used "
		    "to force the zone\n\tstate to 'incomplete' "
		    "administratively if some activity has rendered\n\tthe "
		    "zone permanently unusable.  The only valid state that "
		    "may be\n\tspecified is 'incomplete'."));
	default:
		return ("");
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Called with explicit B_TRUE when help is explicitly requested, B_FALSE for
 * unexpected errors.
 */

static int
usage(boolean_t explicit)
{
	int i;
	FILE *fd = explicit ? stdout : stderr;

	(void) fprintf(fd, "%s:\t%s help\n", gettext("usage"), execname);
	(void) fprintf(fd, "\t%s [-z <zone>] [-u <uuid-match>] list\n",
	    execname);
	(void) fprintf(fd, "\t%s {-z <zone>|-u <uuid-match>} <%s>\n", execname,
	    gettext("subcommand"));
	(void) fprintf(fd, "\n%s:\n\n", gettext("Subcommands"));
	for (i = CMD_MIN; i <= CMD_MAX; i++) {
		if (cmdtab[i].short_usage == NULL)
			continue;
		(void) fprintf(fd, "%s\n", cmdtab[i].short_usage);
		if (explicit)
			(void) fprintf(fd, "\t%s\n\n", long_help(i));
	}
	if (!explicit)
		(void) fputs("\n", fd);
	return (Z_USAGE);
}

static void
sub_usage(char *short_usage, int cmd_num)
{
	(void) fprintf(stderr, "%s:\t%s\n", gettext("usage"), short_usage);
	(void) fprintf(stderr, "\t%s\n", long_help(cmd_num));
}

/*
 * zperror() is like perror(3c) except that this also prints the executable
 * name at the start of the message, and takes a boolean indicating whether
 * to call libzonecfg's zonecfg_strerror() or libc'c strerror().  That is:
 *
 *    zonecfg_error == B_TRUE:	call zonecfg_strerror()
 *    zonecfg_error == B_FALSE:	call strerror()
 *
 */

void
zperror(const char *str, boolean_t zonecfg_error)
{
	(void) fprintf(stderr, "%s: %s: %s\n", execname, str,
	    zonecfg_error ? zonecfg_strerror(errno) : strerror(errno));
}

/*
 * zperror2() is very similar to zperror() above, except it also prints a
 * supplied zone name after the executable.
 *
 * All current consumers of this function want libzonecfg's strerror() rather
 * than libc's; if this ever changes, this function can be made more generic
 * like zperror() above.
 */

void
zperror2(const char *zone, const char *str)
{
	(void) fprintf(stderr, "%s: %s: %s: %s\n", execname, zone, str,
	    zonecfg_strerror(errno));
}

/* PRINTFLIKE1 */
void
zerror(const char *fmt, ...)
{
	va_list alist;

	va_start(alist, fmt);
	(void) fprintf(stderr, "%s: ", execname);
	if (target_zone != NULL)
		(void) fprintf(stderr, "zone '%s': ", target_zone);
	(void) vfprintf(stderr, fmt, alist);
	(void) fprintf(stderr, "\n");
	va_end(alist);
}

static void *
safe_calloc(size_t nelem, size_t elsize)
{
	void *r = calloc(nelem, elsize);

	if (r == NULL) {
		zerror(gettext("failed to allocate %lu bytes: %s"),
		    (ulong_t)nelem * elsize, strerror(errno));
		exit(Z_ERR);
	}
	return (r);
}

static void
zone_print(zone_entry_t *zent, boolean_t verbose, boolean_t parsable)
{
	static boolean_t firsttime = B_TRUE;
	char *ip_type_str;

	/* Skip a zone that shutdown while we were collecting data. */
	if (zent->zname[0] == '\0')
		return;

	if (zent->ziptype == ZS_EXCLUSIVE)
		ip_type_str = "excl";
	else
		ip_type_str = "shared";

	assert(!(verbose && parsable));
	if (firsttime && verbose) {
		firsttime = B_FALSE;
		(void) printf("%*s %-16s %-10s %-30s %-8s %-6s\n",
		    ZONEID_WIDTH, "ID", "NAME", "STATUS", "PATH", "BRAND",
		    "IP");
	}
	if (!verbose) {
		char *cp, *clim;

		if (!parsable) {
			(void) printf("%s\n", zent->zname);
			return;
		}
		if (zent->zid == ZONE_ID_UNDEFINED) {
			if (zent->zuniqid == (uint64_t)ZONE_ID_UNDEFINED)
				(void) printf("-");
			else
				(void) printf("%llu", zent->zuniqid);
		} else {
			(void) printf("%lu", zent->zid);
		}
		(void) printf(":%s:%s:", zent->zname, zent->zstate_str);
		cp = zent->zroot;
		while ((clim = strchr(cp, ':')) != NULL) {
			(void) printf("%.*s\\:", clim - cp, cp);
			cp = clim + 1;
		}
		(void) printf("%s:%s:%s:%s:%c:%s\n", cp, zent->zuuid,
		    zent->zbrand, ip_type_str, zent->zreadonly,
		    zent->zmacprofile);
		return;
	}
	if (zent->zstate_str != NULL) {
		if (zent->zid == ZONE_ID_UNDEFINED) {
			if (zent->zuniqid == (uint64_t)ZONE_ID_UNDEFINED)
				(void) printf("%*s", ZONEID_WIDTH, "-");
			else
				(void) printf("%*llu", ZONEID_WIDTH,
				    zent->zuniqid);
		} else {
			(void) printf("%*lu", ZONEID_WIDTH, zent->zid);
		}
		(void) printf(" %-16s %-10s %-30s %-8s %-6s\n", zent->zname,
		    zent->zstate_str, zent->zroot, zent->zbrand, ip_type_str);
	}
}

static int
lookup_zone_info(const char *zone_name, zoneid_t zid, zone_entry_t *zent)
{
	char root[MAXPATHLEN], *cp;
	int err;
	uuid_t uuid;
	zone_dochandle_t handle;

	(void) strlcpy(zent->zname, zone_name, sizeof (zent->zname));
	(void) strlcpy(zent->zroot, "???", sizeof (zent->zroot));
	(void) strlcpy(zent->zbrand, "???", sizeof (zent->zbrand));
	zent->zstate_str = "???";

	zent->zid = zid;
	zent->zuniqid = (uint64_t)ZONE_ID_UNDEFINED;

	if (zonecfg_get_uuid(zone_name, uuid) == Z_OK &&
	    !uuid_is_null(uuid))
		uuid_unparse(uuid, zent->zuuid);
	else
		zent->zuuid[0] = '\0';

	/*
	 * For labeled zones which query the zone path of lower-level
	 * zones, the path needs to be adjusted to drop the final
	 * "/root" component. This adjusted path is then useful
	 * for reading down any exported directories from the
	 * lower-level zone.
	 */
	if (is_system_labeled() && zent->zid != ZONE_ID_UNDEFINED) {
		if (zone_getattr(zent->zid, ZONE_ATTR_ROOT, zent->zroot,
		    sizeof (zent->zroot)) == -1) {
			zperror2(zent->zname,
			    gettext("could not get zone path."));
			return (Z_ERR);
		}
		cp = zent->zroot + strlen(zent->zroot) - 5;
		if (cp > zent->zroot && strcmp(cp, "/root") == 0)
			*cp = '\0';
	} else {
		if ((err = zone_get_zonepath(zent->zname, root,
		    sizeof (root))) != Z_OK) {
			errno = err;
			zperror2(zent->zname,
			    gettext("could not get zone path."));
			return (Z_ERR);
		}
		(void) strlcpy(zent->zroot, root, sizeof (zent->zroot));
	}

	if ((err = zone_get_state(zent->zname, &zent->zstate_num)) != Z_OK) {
		errno = err;
		zperror2(zent->zname, gettext("could not get state"));
		return (Z_ERR);
	}
	zent->zstate_str = zone_state_str(zent->zstate_num);

	/*
	 * A zone's brand is only available in the .xml file describing it,
	 * which is only visible to the global zone.  This causes
	 * zone_get_brand() to fail when called from within a non-global
	 * zone.  Fortunately we only do this on labeled systems, where we
	 * know all zones are native.
	 */
	if (getzoneid() != GLOBAL_ZONEID) {
		assert(is_system_labeled() != 0);
		(void) strlcpy(zent->zbrand, default_brand,
		    sizeof (zent->zbrand));
	} else if (zone_get_brand(zent->zname, zent->zbrand,
	    sizeof (zent->zbrand)) != Z_OK) {
		zperror2(zent->zname, gettext("could not get brand name"));
		return (Z_ERR);
	}

	/*
	 * Get ip type of the zone.
	 * Note for global zone, ZS_SHARED is set always.
	 */
	if (zid == GLOBAL_ZONEID) {
		zent->ziptype = ZS_SHARED;
		zent->zreadonly = '-';
		(void) strcpy(zent->zmacprofile, "none");
		return (Z_OK);
	}

	/*
	 * There is a race condition where the zone could boot while
	 * we're walking the index file.  In this case the zone state
	 * could be seen as running from the call above, but the zoneid
	 * would be undefined.
	 *
	 * There is also a race condition where the zone could shutdown after
	 * we got its running state above.  This is also not an error and
	 * we fall back to getting the ziptype from the zone configuration.
	 */
	if (zent->zstate_num == ZONE_STATE_RUNNING &&
	    zid != ZONE_ID_UNDEFINED) {
		ushort_t flags;

		if (zone_getattr(zid, ZONE_ATTR_FLAGS, &flags,
		    sizeof (flags)) >= 0 &&
		    zone_getattr(zid, ZONE_ATTR_MAC_PROFILE, zent->zmacprofile,
		    sizeof (zent->zmacprofile)) >= 0) {
			if (flags & ZF_NET_EXCL)
				zent->ziptype = ZS_EXCLUSIVE;
			else
				zent->ziptype = ZS_SHARED;

			if (ZONECFG_READ_WRITE_PROFNAME(zent->zmacprofile))
				zent->zreadonly = '-';
			else if (flags & ZF_LOCKDOWN)
				zent->zreadonly = 'R';
			else
				zent->zreadonly = 'W';
			return (Z_OK);
		}
	}

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror2(zent->zname, gettext("could not init handle"));
		return (Z_ERR);
	}
	if ((err = zonecfg_get_handle(zent->zname, handle)) != Z_OK) {
		zperror2(zent->zname, gettext("could not get handle"));
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}

	if ((err = zonecfg_get_iptype(handle, &zent->ziptype)) != Z_OK) {
		zperror2(zent->zname, gettext("could not get ip-type"));
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}

	if ((err = zonecfg_get_mac_profile(handle, zent->zmacprofile,
	    sizeof (zent->zmacprofile))) != Z_OK) {
		zperror2(zent->zname, gettext("could not get mac_profile"));
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}

	zent->zreadonly = '-';

	zonecfg_fini_handle(handle);

	return (Z_OK);
}

/*
 * fetch_zents() calls zone_get_zoneids(libc-private) to find out how many
 * and which zones are running (the former is stored in the global nzents).
 * This function may be called multiple times, so if zents is already set,
 * we return immediately to save work.
 *
 * Note that the data about running zones can change while this function
 * is running, so its possible that the list of zones will have empty slots
 * at the end.
 */

static int
fetch_zents(void)
{
	zoneid_t *zids = NULL;
	int i, retv;
	FILE *fp;
	boolean_t inaltroot;
	zone_entry_t *zentp;
	const char *altroot;

	if (nzents > 0)
		return (Z_OK);

	if (zone_get_zoneids(&zids, &nzents) != 0) {
		zperror(gettext("failed to get zoneid list"), B_FALSE);
		return (Z_ERR);
	}

	zents = safe_calloc(nzents, sizeof (zone_entry_t));

	inaltroot = zonecfg_in_alt_root();
	if (inaltroot) {
		fp = zonecfg_open_scratch("", B_FALSE);
		altroot = zonecfg_get_root();
	} else {
		fp = NULL;
	}
	zentp = zents;
	retv = Z_OK;
	for (i = 0; i < nzents; i++) {
		char name[ZONENAME_MAX];
		char altname[ZONENAME_MAX];
		char rev_altroot[MAXPATHLEN];

		if (getzonenamebyid(zids[i], name, sizeof (name)) < 0) {
			/*
			 * There is a race condition where the zone may have
			 * shutdown since we retrieved the number of running
			 * zones above.  This is not an error, there will be
			 * an empty slot at the end of the list.
			 */
			continue;
		}
		if (zonecfg_is_scratch(name)) {
			/* Ignore scratch zones by default */
			if (!inaltroot)
				continue;
			if (fp == NULL ||
			    zonecfg_reverse_scratch(fp, name, altname,
			    sizeof (altname), rev_altroot,
			    sizeof (rev_altroot)) == -1) {
				zerror(gettext("could not resolve scratch "
				    "zone %s"), name);
				retv = Z_ERR;
				continue;
			}
			/* Ignore zones in other alternate roots */
			if (strcmp(rev_altroot, altroot) != 0)
				continue;
			(void) strcpy(name, altname);
		} else {
			/* Ignore non-scratch when in an alternate root */
			if (inaltroot && strcmp(name, GLOBAL_ZONENAME) != 0)
				continue;
		}
		if (lookup_zone_info(name, zids[i], zentp) != Z_OK) {
			/*
			 * There is a race condition where the zone may have
			 * shutdown since we retrieved the number of running
			 * zones above.  This is not an error, there will be
			 * an empty slot at the end of the list.
			 */
			continue;
		}
		zentp++;
	}
	nzents = zentp - zents;
	if (fp != NULL)
		zonecfg_close_scratch(fp);

	free(zids);
	return (retv);
}

static void
zone_print_defunct(boolean_t verbose, boolean_t parsable)
{
	uint64_t *uniqids = NULL;
	uint_t oldcount = 0, count = 0;
	zone_entry_t dzent;
	char *cp;
	int i;

again:
	if (zone_list_defunct(uniqids, &count) != 0) {
		zperror(gettext("failed to get defunct zone list"), B_FALSE);
		return;
	}
	if (count == 0)
		return;
	if (count > oldcount) {
		if (oldcount != 0)
			free(uniqids);
		uniqids = safe_calloc(count, sizeof (*uniqids));
		oldcount = count;
		goto again;
	}

	/*
	 * Loop through and print the relevant information for the defunct
	 * zones.  If one of the zone_getattr_defunct()s fails, it probably
	 * means that the defunct zone has gone away (in which case the
	 * subsequent calls will also fail), so we can just silently ignore
	 * it, as it clearly is no longer a defunct zone.
	 */
	for (i = 0; i < count; i++) {
		zone_entry_t *zent = &dzent;
		ushort_t flags;

		zent->zuniqid = uniqids[i];
		zent->zid = ZONE_ID_UNDEFINED;
		zent->zstate_num = 0; /* doesn't matter */
		zent->zstate_str = "<defunct>";
		zent->zuuid[0] = '-';
		zent->zuuid[1] = '\0';

		if (zone_getattr_defunct(zent->zuniqid, ZONE_ATTR_NAME,
		    zent->zname, sizeof (zent->zname)) == -1)
			continue;
		/*
		 * Avoid using the zones index to lookup the root path,
		 * since that may have changed since the zone was
		 * halted.
		 */
		if (zone_getattr_defunct(zent->zuniqid, ZONE_ATTR_ROOT,
		    zent->zroot, sizeof (zent->zroot)) == -1)
			continue;
		/* strip off the trailing "/root" */
		cp = zent->zroot + strlen(zent->zroot) - 5;
		if (cp > zent->zroot && strcmp(cp, "/root") == 0)
			*cp = '\0';

		if (zone_getattr_defunct(zent->zuniqid, ZONE_ATTR_BRAND,
		    zent->zbrand, sizeof (zent->zbrand)) == -1)
			continue;

		if (zone_getattr_defunct(zent->zuniqid, ZONE_ATTR_FLAGS,
		    &flags, sizeof (flags)) == -1)
			continue;

		if (flags & ZF_NET_EXCL)
			zent->ziptype = ZS_EXCLUSIVE;
		else
			zent->ziptype = ZS_SHARED;

		if (zone_getattr_defunct(zent->zuniqid, ZONE_ATTR_MAC_PROFILE,
		    zent->zmacprofile, sizeof (zent->zmacprofile)) == -1)
			continue;

		if (ZONECFG_READ_WRITE_PROFNAME(zent->zmacprofile))
			zent->zreadonly = '-';
		else if (flags & ZF_LOCKDOWN)
			zent->zreadonly = 'R';
		else
			zent->zreadonly = 'W';
		zone_print(zent, verbose, parsable);
	}

	free(uniqids);
}

static int
zone_print_list(zone_state_t min_state, boolean_t verbose, boolean_t parsable,
    boolean_t defunct)
{
	int i;
	zone_entry_t zent;
	FILE *cookie;
	char *name;

	/*
	 * First get the list of running zones from the kernel and print them.
	 * If that is all we need, then return.
	 */
	if ((i = fetch_zents()) != Z_OK) {
		/*
		 * No need for error messages; fetch_zents() has already taken
		 * care of this.
		 */
		return (i);
	}
	for (i = 0; i < nzents; i++)
		zone_print(&zents[i], verbose, parsable);
	if (defunct)
		zone_print_defunct(verbose, parsable);
	if (min_state >= ZONE_STATE_RUNNING)
		return (Z_OK);
	/*
	 * Next, get the full list of zones from the configuration, skipping
	 * any we have already printed.
	 */
	cookie = setzoneent();
	while ((name = getzoneent(cookie)) != NULL) {
		for (i = 0; i < nzents; i++) {
			if (strcmp(zents[i].zname, name) == 0)
				break;
		}
		if (i < nzents) {
			free(name);
			continue;
		}
		if (lookup_zone_info(name, ZONE_ID_UNDEFINED, &zent) != Z_OK) {
			free(name);
			continue;
		}
		free(name);
		if (zent.zstate_num >= min_state)
			zone_print(&zent, verbose, parsable);
	}
	endzoneent(cookie);
	return (Z_OK);
}

/*
 * Retrieve a zone entry by name.  Returns NULL if no such zone exists.
 */
static zone_entry_t *
lookup_running_zone(const char *str)
{
	int i;

	if (fetch_zents() != Z_OK)
		return (NULL);

	for (i = 0; i < nzents; i++) {
		if (strcmp(str, zents[i].zname) == 0)
			return (&zents[i]);
	}
	return (NULL);
}

/*
 * Check a bit in a mode_t: if on is B_TRUE, that bit should be on; if
 * B_FALSE, it should be off.  Return B_TRUE if the mode is bad (incorrect).
 */
static boolean_t
bad_mode_bit(mode_t mode, mode_t bit, boolean_t on, char *file)
{
	char *str;

	assert(bit == S_IRUSR || bit == S_IWUSR || bit == S_IXUSR ||
	    bit == S_IRGRP || bit == S_IWGRP || bit == S_IXGRP ||
	    bit == S_IROTH || bit == S_IWOTH || bit == S_IXOTH);
	/*
	 * TRANSLATION_NOTE
	 * The strings below will be used as part of a larger message,
	 * either:
	 * (file name) must be (owner|group|world) (read|writ|execut)able
	 * or
	 * (file name) must not be (owner|group|world) (read|writ|execut)able
	 */
	switch (bit) {
	case S_IRUSR:
		str = gettext("owner readable");
		break;
	case S_IWUSR:
		str = gettext("owner writable");
		break;
	case S_IXUSR:
		str = gettext("owner executable");
		break;
	case S_IRGRP:
		str = gettext("group readable");
		break;
	case S_IWGRP:
		str = gettext("group writable");
		break;
	case S_IXGRP:
		str = gettext("group executable");
		break;
	case S_IROTH:
		str = gettext("world readable");
		break;
	case S_IWOTH:
		str = gettext("world writable");
		break;
	case S_IXOTH:
		str = gettext("world executable");
		break;
	}
	if ((mode & bit) == (on ? 0 : bit)) {
		/*
		 * TRANSLATION_NOTE
		 * The first parameter below is a file name; the second
		 * is one of the "(owner|group|world) (read|writ|execut)able"
		 * strings from above.
		 */
		/*
		 * The code below could be simplified but not in a way
		 * that would easily translate to non-English locales.
		 */
		if (on) {
			(void) fprintf(stderr, gettext("%s must be %s.\n"),
			    file, str);
		} else {
			(void) fprintf(stderr, gettext("%s must not be %s.\n"),
			    file, str);
		}
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * We want to make sure that no zone has its zone path as a child node
 * (in the directory sense) of any other.  We do that by comparing this
 * zone's path to the path of all other (non-global) zones.  The comparison
 * in each case is simple: add '/' to the end of the path, then do a
 * strncmp() of the two paths, using the length of the shorter one.
 */

static int
crosscheck_zonepaths(char *path)
{
	char rpath[MAXPATHLEN];		/* resolved path */
	char path_copy[MAXPATHLEN];	/* copy of original path */
	char rpath_copy[MAXPATHLEN];	/* copy of original rpath */
	struct zoneent *ze;
	int res, err;
	FILE *cookie;

	cookie = setzoneent();
	while ((ze = getzoneent_private(cookie)) != NULL) {
		/* Skip zones which are not installed. */
		if (ze->zone_state < ZONE_STATE_INSTALLED) {
			free(ze);
			continue;
		}
		/* Skip the global zone and the current target zone. */
		if (strcmp(ze->zone_name, GLOBAL_ZONENAME) == 0 ||
		    strcmp(ze->zone_name, target_zone) == 0) {
			free(ze);
			continue;
		}
		if (strlen(ze->zone_path) == 0) {
			/* old index file without path, fall back */
			if ((err = zone_get_zonepath(ze->zone_name,
			    ze->zone_path, sizeof (ze->zone_path))) != Z_OK) {
				errno = err;
				zperror2(ze->zone_name,
				    gettext("could not get zone path"));
				free(ze);
				continue;
			}
		}
		(void) snprintf(path_copy, sizeof (path_copy), "%s%s",
		    zonecfg_get_root(), ze->zone_path);
		res = resolvepath(path_copy, rpath, sizeof (rpath));
		if (res == -1) {
			if (errno != ENOENT) {
				zperror(path_copy, B_FALSE);
				free(ze);
				return (Z_ERR);
			}
			(void) printf(gettext("WARNING: zone %s is installed, "
			    "but its %s %s does not exist.\n"), ze->zone_name,
			    "zonepath", path_copy);
			free(ze);
			continue;
		}
		rpath[res] = '\0';
		(void) snprintf(path_copy, sizeof (path_copy), "%s/", path);
		(void) snprintf(rpath_copy, sizeof (rpath_copy), "%s/", rpath);
		if (strncmp(path_copy, rpath_copy,
		    min(strlen(path_copy), strlen(rpath_copy))) == 0) {
			/*
			 * TRANSLATION_NOTE
			 * zonepath is a literal that should not be translated.
			 */
			(void) fprintf(stderr, gettext("%s zonepath (%s) and "
			    "%s zonepath (%s) overlap.\n"),
			    target_zone, path, ze->zone_name, rpath);
			free(ze);
			return (Z_ERR);
		}
		free(ze);
	}
	endzoneent(cookie);
	return (Z_OK);
}

static int
validate_zonepath(char *path, int cmd_num)
{
	int res;			/* result of last library/system call */
	boolean_t err = B_FALSE;	/* have we run into an error? */
	struct stat stbuf;
	struct statvfs64 vfsbuf;
	char rpath[MAXPATHLEN];		/* resolved path */
	char ppath[MAXPATHLEN];		/* parent path */
	char rppath[MAXPATHLEN];	/* resolved parent path */
	char rootpath[MAXPATHLEN];	/* root path */
	zone_state_t state;

	if (path[0] != '/') {
		zerror("zonepath %s is not an absolute path.", path);
		return (Z_ERR);
	}

	/*
	 * Only try to create a dataset if being called by install or clone.
	 * Don't try to create it with move because move just changes the
	 * mountpoint of the dataset.  Ideally, move would rename the dataset
	 * and change the mountpoint property accordingly.  If attach is
	 * not running in dry-run mode, it may call create_zfs_zonepath()
	 * before calling validate_zonepath().
	 */
	if (cmd_num == CMD_INSTALL || cmd_num == CMD_CLONE) {
		if (create_zfs_zonepath(path, B_FALSE) != Z_OK)
			return (Z_ERR);
	}
	if ((res = resolvepath(path, rpath, sizeof (rpath) - 1)) == -1) {
		if (cmd_num != CMD_VERIFY || errno != ENOENT) {
			zerror(gettext("cannot resolve zonepath %s: %s"),
			    path, strerror(errno));
			return (Z_ERR);
		}
		/*
		 * In some cases, verify will fall through.  Set rpath[0]
		 * both as a flag and to ensure that garbage data is not used.
		 */
		rpath[0] = '\0';
	} else {
		rpath[res] = '\0';
	}

	/*
	 * Verify is special in that it can return 0 even if the zonepath
	 * is not set up yet.
	 */
	if (cmd_num == CMD_VERIFY) {
		if ((rpath[0] == '\0') || !is_mountpnt(path, MNTTYPE_ZFS)) {
			/*
			 * TRANSLATION_NOTE
			 * zoneadm is a literal that should not be translated.
			 */
			(void) fprintf(stderr, gettext("WARNING: %s does not "
			    "exist, so it could not be verified.\nWhen "
			    "'zoneadm %s' is run, '%s' will try to create\n%s, "
			    "and '%s' will be tried again,\nbut the '%s' may "
			    "fail if:\nthe parent directory of %s is group- or "
			    "other-writable\nor\n%s overlaps with any other "
			    "installed zones\nor\n%s is not a mountpoint for "
			    "a zfs file system.\n"), path,
			    cmd_to_str(CMD_INSTALL), cmd_to_str(CMD_INSTALL),
			    path, cmd_to_str(CMD_VERIFY),
			    cmd_to_str(CMD_VERIFY), path, path, path);
			return (Z_OK);
		}
	} else if (!is_mountpnt(rpath, MNTTYPE_ZFS)) {
		zerror(gettext("zonepath is not a mountpoint for a zfs file "
		    "system"));
		return (Z_ERR);
	}

	if ((res = lstat(rpath, &stbuf)) != 0) {
		zperror(rpath, B_FALSE);
		return (Z_ERR);
	}
	if (S_ISLNK(stbuf.st_mode)) {
		zerror(gettext("zonepath cannot be a symbolic link"));
		return (Z_ERR);
	}
	if (!S_ISDIR(stbuf.st_mode)) {
		(void) fprintf(stderr, gettext("%s is not a directory.\n"),
		    rpath);
		return (Z_ERR);
	}
	/*
	 * This is a rather generic message, so put it below the more
	 * specific ones above.
	 */
	if (strcmp(path, rpath) != 0) {
		errno = Z_RESOLVED_PATH;
		zperror(path, B_TRUE);
		return (Z_ERR);
	}
	if (verify_zonepath_datasets(rpath, cmd_to_str(cmd_num)) != Z_OK)
		return (Z_ERR);

	/*
	 * It looks like a zonepath dataset, so let the detach proceed without
	 * fixing anything.
	 */
	if (cmd_num == CMD_DETACH)
		return (Z_OK);

	if (crosscheck_zonepaths(rpath) != Z_OK)
		return (Z_ERR);
	/*
	 * Try to collect and report as many minor errors as possible
	 * and fix them automatically corresponding to our requirements
	 * if we are not in strict CMD_VERIFY mode.
	 */
	if (stbuf.st_uid != DEFAULT_ZP_OWNER) {
		(void) fprintf(stderr,
		    gettext("zonepath %s is not owned by root.\n"), rpath);
		if (cmd_num != CMD_VERIFY) {
			(void) fprintf(stdout, gettext(
			    "changing owner to root.\n"));
			if (chown(rpath, DEFAULT_ZP_OWNER,
			    DEFAULT_ZP_GROUP) != 0) {
				(void) fprintf(stderr, gettext("cannot "
				    "chown zonepath, reason: %s.\n"),
				    strerror(errno));
				err = B_TRUE;
			}
		} else {
			err = B_TRUE;
		}
	}
	err |= bad_mode_bit(stbuf.st_mode, S_IRUSR, B_TRUE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IWUSR, B_TRUE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IXUSR, B_TRUE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IRGRP, B_FALSE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IWGRP, B_FALSE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IXGRP, B_FALSE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IROTH, B_FALSE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IWOTH, B_FALSE, rpath);
	err |= bad_mode_bit(stbuf.st_mode, S_IXOTH, B_FALSE, rpath);

	if (err && (cmd_num != CMD_VERIFY)) {
		(void) fprintf(stdout, gettext("changing zonepath "
		    "permissions to 0700.\n"));
		if (chmod(rpath, DEFAULT_ZP_MODE) != 0) {
			(void) fprintf(stderr, gettext("cannot chmod "
			    "zonepath, reason: %s.\n"), strerror(errno));
		} else {
			err = B_FALSE;
		}
	}
	(void) snprintf(ppath, sizeof (ppath), "%s/..", path);
	if ((res = resolvepath(ppath, rppath, sizeof (rppath) - 1)) == -1) {
		zperror(ppath, B_FALSE);
		return (Z_ERR);
	}
	rppath[res] = '\0';
	if ((res = stat(rppath, &stbuf)) != 0) {
		zperror(rppath, B_FALSE);
		return (Z_ERR);
	}
	/* theoretically impossible */
	if (!S_ISDIR(stbuf.st_mode)) {
		(void) fprintf(stderr, gettext("%s is not a directory.\n"),
		    rppath);
		return (Z_ERR);
	}
	if (stbuf.st_uid != 0) {
		(void) fprintf(stderr, gettext("%s is not owned by root.\n"),
		    rppath);
		err = B_TRUE;
	}
	err |= bad_mode_bit(stbuf.st_mode, S_IRUSR, B_TRUE, rppath);
	err |= bad_mode_bit(stbuf.st_mode, S_IWUSR, B_TRUE, rppath);
	err |= bad_mode_bit(stbuf.st_mode, S_IXUSR, B_TRUE, rppath);
	err |= bad_mode_bit(stbuf.st_mode, S_IWGRP, B_FALSE, rppath);
	err |= bad_mode_bit(stbuf.st_mode, S_IWOTH, B_FALSE, rppath);
	if (strcmp(rpath, rppath) == 0) {
		(void) fprintf(stderr, gettext("%s is its own parent.\n"),
		    rppath);
		err = B_TRUE;
	}

	if (statvfs64(rpath, &vfsbuf) != 0) {
		zperror(rpath, B_FALSE);
		return (Z_ERR);
	}
	if (strcmp(vfsbuf.f_basetype, MNTTYPE_NFS) == 0) {
		/*
		 * TRANSLATION_NOTE
		 * Zonepath and NFS are literals that should not be translated.
		 */
		(void) fprintf(stderr, gettext("Zonepath %s is on an NFS "
		    "mounted file system.\n"
		    "\tA local file system must be used.\n"), rpath);
		return (Z_ERR);
	}
	if (vfsbuf.f_flag & ST_NOSUID) {
		/*
		 * TRANSLATION_NOTE
		 * Zonepath and nosuid are literals that should not be
		 * translated.
		 */
		(void) fprintf(stderr, gettext("Zonepath %s is on a nosuid "
		    "file system.\n"), rpath);
		return (Z_ERR);
	}

	if ((res = zone_get_state(target_zone, &state)) != Z_OK) {
		errno = res;
		zperror2(target_zone, gettext("could not get state"));
		return (Z_ERR);
	}
	/*
	 * The existence of the root path is only bad in the configured state,
	 * as it is *supposed* to be there at the installed and later states.
	 * However, the root path is expected to be there if the zone is
	 * detached.
	 * State/command mismatches are caught earlier in verify_details().
	 */
	if (state == ZONE_STATE_CONFIGURED && cmd_num != CMD_ATTACH) {
		if (snprintf(rootpath, sizeof (rootpath), "%s/root", rpath) >=
		    sizeof (rootpath)) {
			/*
			 * TRANSLATION_NOTE
			 * Zonepath is a literal that should not be translated.
			 */
			(void) fprintf(stderr,
			    gettext("Zonepath %s is too long.\n"), rpath);
			return (Z_ERR);
		}
		if ((res = stat(rootpath, &stbuf)) == 0) {
			if (!S_ISDIR(stbuf.st_mode)) {
				(void) fprintf(stderr, gettext("%s is not a "
				    "directory.\n"), rootpath);
				return (Z_ERR);
			}

			if (stbuf.st_uid != 0) {
				(void) fprintf(stderr, gettext("%s is not "
				    "owned by root.\n"), rootpath);
				return (Z_ERR);
			}

			if ((stbuf.st_mode & 0777) != 0755) {
				(void) fprintf(stderr, gettext("%s mode is not "
				    "0755.\n"), rootpath);
				return (Z_ERR);
			}
		}
	}

	return (err ? Z_ERR : Z_OK);
}

static int
invoke_brand_handler(int cmd_num, char *argv[])
{
	zone_dochandle_t handle;
	int err;

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(cmd_num), B_TRUE);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(cmd_num), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	if (verify_brand(handle, cmd_num, argv) != Z_OK) {
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	zonecfg_fini_handle(handle);
	return (Z_OK);
}

static int
call_zoneadmd(zone_cmd_t cmd, uint32_t flags, boolean_t quiet)
{
	zone_cmd_arg_t zarg;

	zarg.cmd = cmd;
	zarg.flags = flags;
	zarg.bootbuf[0] = '\0';

	if (zonecfg_call_zoneadmd(target_zone, &zarg, locale) == 0)
		return (Z_OK);

	if (quiet == B_FALSE)
		zerror(gettext("call to zoneadmd failed"));

	return (Z_ERR);
}

static int
ready_func(int argc, char *argv[])
{
	int arg;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot ready zone in alternate root"));
		return (Z_ERR);
	}

	optind = 0;
	if ((arg = getopt(argc, argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_READY, CMD_READY);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_READY, CMD_READY);
			return (Z_USAGE);
		}
	}
	if (argc > optind) {
		sub_usage(SHELP_READY, CMD_READY);
		return (Z_USAGE);
	}
	if (sanity_check(target_zone, CMD_READY, B_FALSE, B_FALSE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);
	if (verify_details(CMD_READY, argv) != Z_OK)
		return (Z_ERR);

	return (call_zoneadmd(Z_READY, Z_CMD_FLAG_NONE, B_FALSE));
}

static int
boot_func(int argc, char *argv[])
{
	zone_cmd_arg_t zarg;
	int arg;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot boot zone in alternate root"));
		return (Z_ERR);
	}

	zarg.cmd = Z_BOOT;
	zarg.flags = Z_CMD_FLAG_NONE;
	zarg.bootbuf[0] = '\0';

	/*
	 * The following getopt processes arguments to zone boot; that
	 * is to say, the [here] portion of the argument string:
	 *
	 *	zoneadm -z myzone boot [here] -- -v -m verbose
	 *
	 * Where [here] can either be nothing, -? (in which case we bail
	 * and print usage), -f (a private option to indicate that the
	 * boot operation should be 'forced'), -w/-W (to signal a read-only
	 * zone to boot read-write resp. boot read-write transiently) or -s.
	 * Support for -s is vestigal and obsolete, but is retained because it
	 * was a documented interface and there are known consumers including
	 * admin/install; the proper way to specify boot arguments like -s
	 * is:
	 *
	 *	zoneadm -z myzone boot -- -s -v -m verbose.
	 */
	optind = 0;
	while ((arg = getopt(argc, argv, "?fswW")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_BOOT, CMD_BOOT);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		case 's':
			(void) strlcpy(zarg.bootbuf, "-s",
			    sizeof (zarg.bootbuf));
			break;
		case 'f':
			zarg.flags |= Z_CMD_FLAG_FORCE;
			break;
		case 'W':
			zarg.flags |= Z_CMD_FLAG_TRANSIENT_RW;
			/*FALLTHROUGH*/
		case 'w':
			zarg.flags |= Z_CMD_FLAG_RW;
			break;
		default:
			sub_usage(SHELP_BOOT, CMD_BOOT);
			return (Z_USAGE);
		}
	}

	for (; optind < argc; optind++) {
		if (strlcat(zarg.bootbuf, argv[optind],
		    sizeof (zarg.bootbuf)) >= sizeof (zarg.bootbuf)) {
			zerror(gettext("Boot argument list too long"));
			return (Z_ERR);
		}
		if (optind < argc - 1)
			if (strlcat(zarg.bootbuf, " ", sizeof (zarg.bootbuf)) >=
			    sizeof (zarg.bootbuf)) {
				zerror(gettext("Boot argument list too long"));
				return (Z_ERR);
			}
	}
	if (sanity_check(target_zone, CMD_BOOT, B_FALSE, B_FALSE,
	    zarg.flags & Z_CMD_FLAG_FORCE) != Z_OK)
		return (Z_ERR);
	if (verify_details(CMD_BOOT, argv) != Z_OK)
		return (Z_ERR);
	if (zonecfg_call_zoneadmd(target_zone, &zarg, locale) != 0) {
		zerror(gettext("call to %s failed"), "zoneadmd");
		return (Z_ERR);
	}

	return (Z_OK);
}

static void
fake_up_local_zone(zoneid_t zid, zone_entry_t *zeptr)
{
	ssize_t result;
	uuid_t uuid;
	FILE *fp;
	ushort_t flags;

	(void) memset(zeptr, 0, sizeof (*zeptr));

	zeptr->zid = zid;

	/*
	 * Since we're looking up our own (non-global) zone name,
	 * we can be assured that it will succeed.
	 */
	result = getzonenamebyid(zid, zeptr->zname, sizeof (zeptr->zname));
	assert(result >= 0);
	if (zonecfg_is_scratch(zeptr->zname) &&
	    (fp = zonecfg_open_scratch("", B_FALSE)) != NULL) {
		(void) zonecfg_reverse_scratch(fp, zeptr->zname, zeptr->zname,
		    sizeof (zeptr->zname), NULL, 0);
		zonecfg_close_scratch(fp);
	}

	if (is_system_labeled()) {
		(void) zone_getattr(zid, ZONE_ATTR_ROOT, zeptr->zroot,
		    sizeof (zeptr->zroot));
		(void) strlcpy(zeptr->zbrand, SOLARIS_BRAND_NAME,
		    sizeof (zeptr->zbrand));
	} else {
		(void) strlcpy(zeptr->zroot, "/", sizeof (zeptr->zroot));
		(void) zone_getattr(zid, ZONE_ATTR_BRAND, zeptr->zbrand,
		    sizeof (zeptr->zbrand));
	}

	zeptr->zstate_str = "running";
	if (zonecfg_get_uuid(zeptr->zname, uuid) == Z_OK &&
	    !uuid_is_null(uuid))
		uuid_unparse(uuid, zeptr->zuuid);

	if (zone_getattr(zid, ZONE_ATTR_FLAGS, &flags, sizeof (flags)) < 0) {
		zperror2(zeptr->zname, gettext("could not get zone flags"));
		exit(Z_ERR);
	}
	if (flags & ZF_NET_EXCL)
		zeptr->ziptype = ZS_EXCLUSIVE;
	else
		zeptr->ziptype = ZS_SHARED;

	if (zone_getattr(zid, ZONE_ATTR_MAC_PROFILE, zeptr->zmacprofile,
	    sizeof (zeptr->zmacprofile)) < 0) {
		zperror2(zeptr->zname,
		    gettext("could not get file-mac-profile"));
		exit(Z_ERR);
	}
	if (ZONECFG_READ_WRITE_PROFNAME(zeptr->zmacprofile))
		zeptr->zreadonly = '-';
	else if (flags & ZF_LOCKDOWN)
		zeptr->zreadonly = 'R';
	else
		zeptr->zreadonly = 'W';
}

static int
list_func(int argc, char *argv[])
{
	zone_entry_t *zentp, zent;
	int arg, retv;
	boolean_t output = B_FALSE, verbose = B_FALSE, parsable = B_FALSE,
	    defunct = B_FALSE;
	zone_state_t min_state = ZONE_STATE_RUNNING;
	zoneid_t zone_id = getzoneid();

	if (target_zone == NULL) {
		/* all zones: default view to running but allow override */
		optind = 0;
		while ((arg = getopt(argc, argv, "?cdipv")) != EOF) {
			switch (arg) {
			case '?':
				sub_usage(SHELP_LIST, CMD_LIST);
				return (optopt == '?' ? Z_OK : Z_USAGE);
				/*
				 * The 'i' and 'c' options are not mutually
				 * exclusive so if 'c' is given, then min_state
				 * is set to 0 (ZONE_STATE_CONFIGURED) which is
				 * the lowest possible state.  If 'i' is given,
				 * then min_state is set to be the lowest state
				 * so far.
				 */
			case 'c':
				min_state = ZONE_STATE_CONFIGURED;
				break;
			case 'd':
				defunct = B_TRUE;
				break;
			case 'i':
				min_state = min(ZONE_STATE_INSTALLED,
				    min_state);

				break;
			case 'p':
				parsable = B_TRUE;
				break;
			case 'v':
				verbose = B_TRUE;
				break;
			default:
				sub_usage(SHELP_LIST, CMD_LIST);
				return (Z_USAGE);
			}
		}
		if (parsable && verbose) {
			zerror(gettext("%s -p and -v are mutually exclusive."),
			    cmd_to_str(CMD_LIST));
			return (Z_ERR);
		}
		if (zone_id == GLOBAL_ZONEID || is_system_labeled()) {
			retv = zone_print_list(min_state, verbose, parsable,
			    defunct);
		} else {
			fake_up_local_zone(zone_id, &zent);
			retv = Z_OK;
			zone_print(&zent, verbose, parsable);
		}
		return (retv);
	}

	/*
	 * Specific target zone: disallow -i/-c suboptions.
	 */
	optind = 0;
	while ((arg = getopt(argc, argv, "?pv")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_LIST, CMD_LIST);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		case 'p':
			parsable = B_TRUE;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		default:
			sub_usage(SHELP_LIST, CMD_LIST);
			return (Z_USAGE);
		}
	}
	if (parsable && verbose) {
		zerror(gettext("%s -p and -v are mutually exclusive."),
		    cmd_to_str(CMD_LIST));
		return (Z_ERR);
	}
	if (argc > optind) {
		sub_usage(SHELP_LIST, CMD_LIST);
		return (Z_USAGE);
	}
	if (zone_id != GLOBAL_ZONEID && !is_system_labeled()) {
		fake_up_local_zone(zone_id, &zent);
		/*
		 * main() will issue a Z_NO_ZONE error if it cannot get an
		 * id for target_zone, which in a non-global zone should
		 * happen for any zone name except `zonename`.  Thus we
		 * assert() that here but don't otherwise check.
		 */
		assert(strcmp(zent.zname, target_zone) == 0);
		zone_print(&zent, verbose, parsable);
		output = B_TRUE;
	} else if ((zentp = lookup_running_zone(target_zone)) != NULL) {
		zone_print(zentp, verbose, parsable);
		output = B_TRUE;
	} else if (lookup_zone_info(target_zone, ZONE_ID_UNDEFINED,
	    &zent) == Z_OK) {
		zone_print(&zent, verbose, parsable);
		output = B_TRUE;
	}

	/*
	 * Invoke brand-specific handler. Note that we do this
	 * only if we're in the global zone, and target_zone is specified
	 * and it is not the global zone.
	 */
	if (zone_id == GLOBAL_ZONEID && target_zone != NULL &&
	    strcmp(target_zone, GLOBAL_ZONENAME) != 0)
		if (invoke_brand_handler(CMD_LIST, argv) != Z_OK)
			return (Z_ERR);

	return (output ? Z_OK : Z_ERR);
}

int
do_subproc(char *cmdbuf)
{
	void (*saveint)(int);
	void (*saveterm)(int);
	void (*savequit)(int);
	void (*savehup)(int);
	int pid, child, status;

	if ((child = vfork()) == 0) {
		(void) execl("/bin/sh", "sh", "-c", cmdbuf, (char *)NULL);
	}

	if (child == -1)
		return (-1);

	saveint = sigset(SIGINT, SIG_IGN);
	saveterm = sigset(SIGTERM, SIG_IGN);
	savequit = sigset(SIGQUIT, SIG_IGN);
	savehup = sigset(SIGHUP, SIG_IGN);

	while ((pid = waitpid(child, &status, 0)) != child && pid != -1)
		;

	(void) sigset(SIGINT, saveint);
	(void) sigset(SIGTERM, saveterm);
	(void) sigset(SIGQUIT, savequit);
	(void) sigset(SIGHUP, savehup);

	return (pid == -1 ? -1 : status);
}

int
subproc_status(const char *cmd, int status, boolean_t verbose_failure)
{
	if (WIFEXITED(status)) {
		int exit_code = WEXITSTATUS(status);

		if ((verbose_failure) && (exit_code != ZONE_SUBPROC_OK))
			zerror(gettext("'%s' failed with exit code %d."), cmd,
			    exit_code);

		return (exit_code);
	} else if (WIFSIGNALED(status)) {
		int signal = WTERMSIG(status);
		char sigstr[SIG2STR_MAX];

		if (sig2str(signal, sigstr) == 0) {
			zerror(gettext("'%s' terminated by signal SIG%s."), cmd,
			    sigstr);
		} else {
			zerror(gettext("'%s' terminated by an unknown signal."),
			    cmd);
		}
	} else {
		zerror(gettext("'%s' failed for unknown reasons."), cmd);
	}

	/*
	 * Assume a subprocess that died due to a signal or an unknown error
	 * should be considered an exit code of ZONE_SUBPROC_FATAL, as the
	 * user will likely need to do some manual cleanup.
	 */
	return (ZONE_SUBPROC_FATAL);
}

static int
auth_check(char *user, char *zone, int cmd_num)
{
	char authname[MAXAUTHS];

	switch (cmd_num) {
	case CMD_LIST:
	case CMD_HELP:
		return (Z_OK);
	case SOURCE_ZONE:
		(void) strlcpy(authname, ZONE_CLONEFROM_AUTH, MAXAUTHS);
		break;
	case CMD_BOOT:
	case CMD_HALT:
	case CMD_READY:
	case CMD_REBOOT:
	case CMD_SYSBOOT:
	case CMD_VERIFY:
	case CMD_INSTALL:
	case CMD_UNINSTALL:
	case CMD_MOUNT:
	case CMD_UNMOUNT:
	case CMD_CLONE:
	case CMD_MOVE:
	case CMD_DETACH:
	case CMD_ATTACH:
	case CMD_MARK:
	case CMD_APPLY:
	default:
		(void) strlcpy(authname, ZONE_MANAGE_AUTH, MAXAUTHS);
		break;
	}
	(void) strlcat(authname, KV_OBJECT, MAXAUTHS);
	(void) strlcat(authname, zone, MAXAUTHS);
	if (chkauthattr(authname, user) == 0) {
		return (Z_ERR);
	} else {
		/*
		 * Some subcommands, e.g. install, run subcommands,
		 * e.g. sysidcfg, that require a real uid of root,
		 *  so switch to root, here.
		 */
		if (setuid(0) == -1) {
			zperror(gettext("insufficient privilege"), B_TRUE);
			return (Z_ERR);
		}
		return (Z_OK);
	}
}

/*
 * Various sanity checks; make sure:
 * 1. We're in the global zone.
 * 2. The calling user has sufficient privilege.
 * 3. The target zone is neither the global zone nor anything starting with
 *    the system-reserved zone name prefix.
 * 4a. If we're looking for a 'not running' (i.e., configured or installed)
 *     zone, the name service knows about it.
 * 4b. For some operations which expect a zone not to be running, that it is
 *     not already running (or ready).
 */
static int
sanity_check(char *zone, int cmd_num, boolean_t running,
    boolean_t unsafe_when_running, boolean_t force)
{
	zone_entry_t *zent;
	priv_set_t *privset;
	zone_state_t state, min_state;
	char kernzone[ZONENAME_MAX];
	FILE *fp;

	if (getzoneid() != GLOBAL_ZONEID) {
		switch (cmd_num) {
		case CMD_HALT:
			zerror(gettext("use %s to %s this zone."), "halt(1M)",
			    cmd_to_str(cmd_num));
			break;
		case CMD_REBOOT:
			zerror(gettext("use %s to %s this zone."),
			    "reboot(1M)", cmd_to_str(cmd_num));
			break;
		default:
			zerror(gettext("must be in the global zone to %s a "
			    "zone."), cmd_to_str(cmd_num));
			break;
		}
		return (Z_ERR);
	}

	if ((privset = priv_allocset()) == NULL) {
		zerror(gettext("%s failed"), "priv_allocset");
		return (Z_ERR);
	}

	if (getppriv(PRIV_EFFECTIVE, privset) != 0) {
		zerror(gettext("%s failed"), "getppriv");
		priv_freeset(privset);
		return (Z_ERR);
	}

	if (priv_isfullset(privset) == B_FALSE) {
		zerror(gettext("only a privileged user may %s a zone."),
		    cmd_to_str(cmd_num));
		priv_freeset(privset);
		return (Z_ERR);
	}
	priv_freeset(privset);

	if (zone == NULL) {
		zerror(gettext("no zone specified"));
		return (Z_ERR);
	}

	if (auth_check(username, zone, cmd_num) == Z_ERR) {
		zerror(gettext("User %s is not authorized to %s this zone."),
		    username, cmd_to_str(cmd_num));
		return (Z_ERR);
	}

	if (strcmp(zone, GLOBAL_ZONENAME) == 0) {
		zerror(gettext("%s operation is invalid for the global zone."),
		    cmd_to_str(cmd_num));
		return (Z_ERR);
	}

	if (strncmp(zone, SYS_ZONE_PREFIX, strlen(SYS_ZONE_PREFIX)) == 0) {
		zerror(gettext("%s operation is invalid for zones starting "
		    "with %s."), cmd_to_str(cmd_num), SYS_ZONE_PREFIX);
		return (Z_ERR);
	}

	if (!zonecfg_in_alt_root()) {
		zent = lookup_running_zone(zone);
	} else if ((fp = zonecfg_open_scratch("", B_FALSE)) == NULL) {
		zent = NULL;
	} else {
		if (zonecfg_find_scratch(fp, zone, zonecfg_get_root(),
		    kernzone, sizeof (kernzone)) == 0)
			zent = lookup_running_zone(kernzone);
		else
			zent = NULL;
		zonecfg_close_scratch(fp);
	}

	/*
	 * Look up from the kernel for 'running' zones.
	 */
	if (running && !force) {
		if (zent == NULL) {
			zerror(gettext("not running"));
			return (Z_ERR);
		}
	} else {
		int err;

		if (unsafe_when_running && zent != NULL) {
			/* check whether the zone is ready or running */
			if ((err = zone_get_state(zent->zname,
			    &zent->zstate_num)) != Z_OK) {
				errno = err;
				zperror2(zent->zname,
				    gettext("could not get state"));
				/* can't tell, so hedge */
				zent->zstate_str = "ready/running";
			} else {
				zent->zstate_str =
				    zone_state_str(zent->zstate_num);
			}
			zerror(gettext("%s operation is invalid for %s zones."),
			    cmd_to_str(cmd_num), zent->zstate_str);
			return (Z_ERR);
		}
		if ((err = zone_get_state(zone, &state)) != Z_OK) {
			errno = err;
			zperror2(zone, gettext("could not get state"));
			return (Z_ERR);
		}
		switch (cmd_num) {
		case CMD_UNINSTALL:
			if (state == ZONE_STATE_CONFIGURED) {
				zerror(gettext("is already in state '%s'."),
				    zone_state_str(ZONE_STATE_CONFIGURED));
				return (Z_ERR);
			}
			break;
		case CMD_ATTACH:
			if (state == ZONE_STATE_INSTALLED) {
				zerror(gettext("is already %s."),
				    zone_state_str(ZONE_STATE_INSTALLED));
				return (Z_ERR);
			} else if (state == ZONE_STATE_INCOMPLETE && !force) {
				zerror(gettext("zone is %s; %s required."),
				    zone_state_str(ZONE_STATE_INCOMPLETE),
				    cmd_to_str(CMD_UNINSTALL));
				return (Z_ERR);
			}
			break;
		case CMD_CLONE:
		case CMD_INSTALL:
			if (state == ZONE_STATE_INSTALLED) {
				zerror(gettext("is already %s."),
				    zone_state_str(ZONE_STATE_INSTALLED));
				return (Z_ERR);
			} else if (state == ZONE_STATE_INCOMPLETE) {
				zerror(gettext("zone is %s; %s required."),
				    zone_state_str(ZONE_STATE_INCOMPLETE),
				    cmd_to_str(CMD_UNINSTALL));
				return (Z_ERR);
			}
			break;
		case CMD_DETACH:
		case CMD_MOVE:
		case CMD_READY:
		case CMD_BOOT:
		case CMD_MOUNT:
		case CMD_MARK:
			if ((cmd_num == CMD_BOOT || cmd_num == CMD_MOUNT) &&
			    force)
				min_state = ZONE_STATE_INCOMPLETE;
			else if (cmd_num == CMD_MARK)
				min_state = ZONE_STATE_CONFIGURED;
			else
				min_state = ZONE_STATE_INSTALLED;

			if (state < min_state) {
				zerror(gettext("must be %s before %s."),
				    zone_state_str(min_state),
				    cmd_to_str(cmd_num));
				return (Z_ERR);
			}
			break;
		case CMD_VERIFY:
			if (state == ZONE_STATE_INCOMPLETE) {
				zerror(gettext("zone is %s; %s required."),
				    zone_state_str(ZONE_STATE_INCOMPLETE),
				    cmd_to_str(CMD_UNINSTALL));
				return (Z_ERR);
			}
			break;
		case CMD_UNMOUNT:
			if (state != ZONE_STATE_MOUNTED) {
				zerror(gettext("must be %s before %s."),
				    zone_state_str(ZONE_STATE_MOUNTED),
				    cmd_to_str(cmd_num));
				return (Z_ERR);
			}
			break;
		case CMD_SYSBOOT:
			if (state != ZONE_STATE_INSTALLED) {
				zerror(gettext("%s operation is invalid for %s "
				    "zones."), cmd_to_str(cmd_num),
				    zone_state_str(state));
				return (Z_ERR);
			}
			break;
		}
	}
	return (Z_OK);
}

static int
shutdown_func(int argc, char *argv[])
{
	zone_cmd_arg_t zarg;
	int arg;

	zarg.cmd = Z_SHUTDOWN;
	zarg.flags = Z_CMD_FLAG_NONE;
	zarg.bootbuf[0] = '\0';

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot shut down zone in alternate root"));
		return (Z_ERR);
	}

	optind = 0;
	while ((arg = getopt(argc, argv, "r?")) != EOF) {
		switch (arg) {
		case 'r':
			zarg.flags = Z_CMD_FLAG_REBOOT;
			break;
		case '?':
			sub_usage(SHELP_SHUTDOWN, CMD_SHUTDOWN);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_SHUTDOWN, CMD_SHUTDOWN);
			return (Z_USAGE);
		}
	}

	for (; optind < argc; optind++) {
		if (strlcat(zarg.bootbuf, argv[optind],
		    sizeof (zarg.bootbuf)) >= sizeof (zarg.bootbuf)) {
			zerror(gettext("Boot argument list too long"));
			return (Z_ERR);
		}
		if (optind < argc - 1)
			if (strlcat(zarg.bootbuf, " ", sizeof (zarg.bootbuf)) >=
			    sizeof (zarg.bootbuf)) {
				zerror(gettext("Boot argument list too long"));
				return (Z_ERR);
			}
	}

	/*
	 * zoneadmd should be the one to decide whether or not to proceed,
	 * so even though it seems that the third parameter below should
	 * perhaps be B_TRUE, it really shouldn't be.
	 */
	if (sanity_check(target_zone, CMD_SHUTDOWN, B_TRUE, B_FALSE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);

	return ((zonecfg_call_zoneadmd(target_zone, &zarg, locale) == 0)
	    ? Z_OK : Z_ERR);
}

static int
halt_func(int argc, char *argv[])
{
	int arg;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot halt zone in alternate root"));
		return (Z_ERR);
	}

	optind = 0;
	if ((arg = getopt(argc, argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_HALT, CMD_HALT);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_HALT, CMD_HALT);
			return (Z_USAGE);
		}
	}
	if (argc > optind) {
		sub_usage(SHELP_HALT, CMD_HALT);
		return (Z_USAGE);
	}

	/*
	 * zoneadmd should be the one to decide whether or not to proceed,
	 * so even though it seems that the third parameter below should
	 * perhaps be B_TRUE, it really shouldn't be.
	 */
	if (sanity_check(target_zone, CMD_HALT, B_FALSE, B_FALSE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);

	/*
	 * Invoke brand-specific handler.
	 */
	if (invoke_brand_handler(CMD_HALT, argv) != Z_OK)
		return (Z_ERR);

	return (call_zoneadmd(Z_HALT, Z_CMD_FLAG_NONE, B_FALSE));
}

static int
reboot_func(int argc, char *argv[])
{
	zone_cmd_arg_t zarg;
	int arg;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot reboot zone in alternate root"));
		return (Z_ERR);
	}

	zarg.cmd = Z_REBOOT;
	zarg.flags = Z_CMD_FLAG_NONE;
	zarg.bootbuf[0] = '\0';

	optind = 0;
	while ((arg = getopt(argc, argv, "Ww?")) != EOF) {
		switch (arg) {
		case 'W':
			zarg.flags |= Z_CMD_FLAG_TRANSIENT_RW;
			/*FALLTHROUGH*/
		case 'w':
			zarg.flags |= Z_CMD_FLAG_RW;
			break;
		case '?':
			sub_usage(SHELP_REBOOT, CMD_REBOOT);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_REBOOT, CMD_REBOOT);
			return (Z_USAGE);
		}
	}

	for (; optind < argc; optind++) {
		if (strlcat(zarg.bootbuf, argv[optind],
		    sizeof (zarg.bootbuf)) >= sizeof (zarg.bootbuf)) {
			zerror(gettext("Boot argument list too long"));
			return (Z_ERR);
		}
		if (optind < argc - 1)
			if (strlcat(zarg.bootbuf, " ", sizeof (zarg.bootbuf)) >=
			    sizeof (zarg.bootbuf)) {
				zerror(gettext("Boot argument list too long"));
				return (Z_ERR);
			}
	}

	if (sanity_check(target_zone, CMD_REBOOT, B_TRUE, B_FALSE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);
	if (verify_details(CMD_REBOOT, argv) != Z_OK)
		return (Z_ERR);

	return ((zonecfg_call_zoneadmd(target_zone, &zarg, locale) == 0)
	    ? Z_OK : Z_ERR);
}

static int
get_hook(brand_handle_t bh, char *cmd, size_t len, int (*bp)(brand_handle_t,
    const char *, const char *, char *, size_t), char *zonename, char *zonepath)
{
	if (strlcpy(cmd, EXEC_PREFIX, len) >= len)
		return (Z_ERR);

	if (bp(bh, zonename, zonepath, cmd + EXEC_LEN, len - EXEC_LEN) != 0)
		return (Z_ERR);

	if (strlen(cmd) <= EXEC_LEN)
		cmd[0] = '\0';

	return (Z_OK);
}

static int
verify_brand(zone_dochandle_t handle, int cmd_num, char *argv[])
{
	char cmdbuf[MAXPATHLEN];
	int err;
	char zonepath[MAXPATHLEN];
	brand_handle_t bh = NULL;
	int status, i;

	/*
	 * Fetch the verify command from the brand configuration.
	 * "exec" the command so that the returned status is that of
	 * the command and not the shell.
	 */
	if (handle == NULL) {
		(void) strlcpy(zonepath, "-", sizeof (zonepath));
	} else if ((err = zonecfg_get_zonepath(handle, zonepath,
	    sizeof (zonepath))) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(cmd_num), B_TRUE);
		return (Z_ERR);
	}
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		return (Z_ERR);
	}

	/*
	 * If the brand has its own verification routine, execute it now.
	 * The verification routine validates the intended zoneadm
	 * operation for the specific brand. The zoneadm subcommand and
	 * all its arguments are passed to the routine.
	 */
	err = get_hook(bh, cmdbuf, sizeof (cmdbuf), brand_get_verify_adm,
	    target_zone, zonepath);
	brand_close(bh);
	if (err != Z_OK)
		return (Z_BRAND_ERROR);
	if (cmdbuf[0] == '\0')
		return (Z_OK);

	if ((strlcat(cmdbuf, " ", sizeof (cmdbuf)) >= sizeof (cmdbuf)) ||
	    (strlcat(cmdbuf, cmd_to_str(cmd_num),
	    sizeof (cmdbuf)) >= sizeof (cmdbuf)))
		return (Z_ERR);

	/* Build the argv string */
	i = 0;
	while (argv[i] != NULL) {
		if ((strlcat(cmdbuf, " ",
		    sizeof (cmdbuf)) >= sizeof (cmdbuf)) ||
		    (strlcat(cmdbuf, argv[i++],
		    sizeof (cmdbuf)) >= sizeof (cmdbuf)))
			return (Z_ERR);
	}

	status = do_subproc(cmdbuf);
	err = subproc_status(gettext("brand-specific verification"),
	    status, B_FALSE);

	return ((err == ZONE_SUBPROC_OK) ? Z_OK : Z_BRAND_ERROR);
}

static int
verify_rctls(zone_dochandle_t handle)
{
	struct zone_rctltab rctltab;
	size_t rbs = rctlblk_size();
	rctlblk_t *rctlblk;
	int error = Z_INVAL;

	if ((rctlblk = malloc(rbs)) == NULL) {
		zerror(gettext("failed to allocate %lu bytes: %s"), rbs,
		    strerror(errno));
		return (Z_NOMEM);
	}

	if (zonecfg_setrctlent(handle) != Z_OK) {
		zerror(gettext("zonecfg_setrctlent failed"));
		free(rctlblk);
		return (error);
	}

	rctltab.zone_rctl_valptr = NULL;
	while (zonecfg_getrctlent(handle, &rctltab) == Z_OK) {
		struct zone_rctlvaltab *rctlval;
		const char *name = rctltab.zone_rctl_name;

		if (!zonecfg_is_rctl(name)) {
			zerror(gettext("WARNING: Ignoring unrecognized rctl "
			    "'%s'."),  name);
			zonecfg_free_rctl_value_list(rctltab.zone_rctl_valptr);
			rctltab.zone_rctl_valptr = NULL;
			continue;
		}

		for (rctlval = rctltab.zone_rctl_valptr; rctlval != NULL;
		    rctlval = rctlval->zone_rctlval_next) {
			if (zonecfg_construct_rctlblk(rctlval, rctlblk)
			    != Z_OK) {
				zerror(gettext("invalid rctl value: "
				    "(priv=%s,limit=%s,action%s)"),
				    rctlval->zone_rctlval_priv,
				    rctlval->zone_rctlval_limit,
				    rctlval->zone_rctlval_action);
				goto out;
			}
			if (!zonecfg_valid_rctl(name, rctlblk)) {
				zerror(gettext("(priv=%s,limit=%s,action=%s) "
				    "is not a valid value for rctl '%s'"),
				    rctlval->zone_rctlval_priv,
				    rctlval->zone_rctlval_limit,
				    rctlval->zone_rctlval_action,
				    name);
				goto out;
			}
		}
		zonecfg_free_rctl_value_list(rctltab.zone_rctl_valptr);
	}
	rctltab.zone_rctl_valptr = NULL;
	error = Z_OK;
out:
	zonecfg_free_rctl_value_list(rctltab.zone_rctl_valptr);
	(void) zonecfg_endrctlent(handle);
	free(rctlblk);
	return (error);
}

static int
verify_pool(zone_dochandle_t handle)
{
	char poolname[MAXPATHLEN];
	pool_conf_t *poolconf;
	pool_t *pool;
	int status;
	int error;

	/*
	 * This ends up being very similar to the check done in zoneadmd.
	 */
	error = zonecfg_get_pool(handle, poolname, sizeof (poolname));
	if (error == Z_NO_ENTRY || (error == Z_OK && strlen(poolname) == 0)) {
		/*
		 * No pool specified.
		 */
		return (0);
	}
	if (error != Z_OK) {
		zperror(gettext("Unable to retrieve pool name from "
		    "configuration"), B_TRUE);
		return (error);
	}
	/*
	 * Don't do anything if pools aren't enabled.
	 */
	if (pool_get_status(&status) != PO_SUCCESS || status != POOL_ENABLED) {
		zerror(gettext("WARNING: pools facility not active; "
		    "zone will not be bound to pool '%s'."), poolname);
		return (Z_OK);
	}
	/*
	 * Try to provide a sane error message if the requested pool doesn't
	 * exist.  It isn't clear that pools-related failures should
	 * necessarily translate to a failure to verify the zone configuration,
	 * hence they are not considered errors.
	 */
	if ((poolconf = pool_conf_alloc()) == NULL) {
		zerror(gettext("WARNING: pool_conf_alloc failed; "
		    "using default pool"));
		return (Z_OK);
	}
	if (pool_conf_open(poolconf, pool_dynamic_location(), PO_RDONLY) !=
	    PO_SUCCESS) {
		zerror(gettext("WARNING: pool_conf_open failed; "
		    "using default pool"));
		pool_conf_free(poolconf);
		return (Z_OK);
	}
	pool = pool_get_pool(poolconf, poolname);
	(void) pool_conf_close(poolconf);
	pool_conf_free(poolconf);
	if (pool == NULL) {
		zerror(gettext("WARNING: pool '%s' not found. "
		    "using default pool"), poolname);
	}

	return (Z_OK);
}

/*
 * Verify that the special device/file system exists and is valid.
 */
static int
verify_fs_special(struct zone_fstab *fstab)
{
	struct stat64 st;

	/*
	 * This validation is really intended for standard zone administration.
	 * If we are in a mini-root or some other upgrade situation where
	 * we are using the scratch zone, just by-pass this.
	 */
	if (zonecfg_in_alt_root())
		return (Z_OK);

	if (strcmp(fstab->zone_fs_type, MNTTYPE_ZFS) == 0)
		return (verify_fs_zfs(fstab));

	if (stat64(fstab->zone_fs_special, &st) != 0) {
		(void) fprintf(stderr, gettext("could not verify fs "
		    "%s: could not access %s: %s\n"), fstab->zone_fs_dir,
		    fstab->zone_fs_special, strerror(errno));
		return (Z_ERR);
	}

	if (strcmp(st.st_fstype, MNTTYPE_NFS) == 0) {
		/*
		 * TRANSLATION_NOTE
		 * fs and NFS are literals that should
		 * not be translated.
		 */
		(void) fprintf(stderr, gettext("cannot verify "
		    "fs %s: NFS mounted file system.\n"
		    "\tA local file system must be used.\n"),
		    fstab->zone_fs_special);
		return (Z_ERR);
	}

	return (Z_OK);
}

static int
isregfile(const char *path)
{
	struct stat64 st;

	if (stat64(path, &st) == -1)
		return (-1);

	return (S_ISREG(st.st_mode));
}

static int
verify_filesystems(zone_dochandle_t handle)
{
	int return_code = Z_OK;
	struct zone_fstab fstab;
	char cmdbuf[MAXPATHLEN];
	struct stat st;

	/*
	 * Since the actual mount point is not known until the dependent mounts
	 * are performed, we don't attempt any path validation here: that will
	 * happen later when zoneadmd actually does the mounts.
	 */
	if (zonecfg_setfsent(handle) != Z_OK) {
		(void) fprintf(stderr, gettext("could not verify file systems: "
		    "unable to enumerate mounts\n"));
		return (Z_ERR);
	}
	while (zonecfg_getfsent(handle, &fstab) == Z_OK) {
		if (!zonecfg_valid_fs_type(fstab.zone_fs_type)) {
			(void) fprintf(stderr, gettext("cannot verify fs %s: "
			    "type %s is not allowed.\n"), fstab.zone_fs_dir,
			    fstab.zone_fs_type);
			return_code = Z_ERR;
			goto next_fs;
		}
		/*
		 * Verify /usr/lib/fs/<fstype>/mount exists.
		 */
		if (snprintf(cmdbuf, sizeof (cmdbuf), "/usr/lib/fs/%s/mount",
		    fstab.zone_fs_type) > sizeof (cmdbuf)) {
			(void) fprintf(stderr, gettext("cannot verify fs %s: "
			    "type %s is too long.\n"), fstab.zone_fs_dir,
			    fstab.zone_fs_type);
			return_code = Z_ERR;
			goto next_fs;
		}
		if (stat(cmdbuf, &st) != 0) {
			(void) fprintf(stderr, gettext("could not verify fs "
			    "%s: could not access %s: %s\n"), fstab.zone_fs_dir,
			    cmdbuf, strerror(errno));
			return_code = Z_ERR;
			goto next_fs;
		}
		if (!S_ISREG(st.st_mode)) {
			(void) fprintf(stderr, gettext("could not verify fs "
			    "%s: %s is not a regular file\n"),
			    fstab.zone_fs_dir, cmdbuf);
			return_code = Z_ERR;
			goto next_fs;
		}
		/*
		 * If zone_fs_raw is set, verify that there's an fsck
		 * binary for it.  If zone_fs_raw is not set, and it's
		 * not a regular file (lofi mount), and there's an fsck
		 * binary for it, complain.
		 */
		if (snprintf(cmdbuf, sizeof (cmdbuf), "/usr/lib/fs/%s/fsck",
		    fstab.zone_fs_type) > sizeof (cmdbuf)) {
			(void) fprintf(stderr, gettext("cannot verify fs %s: "
			    "type %s is too long.\n"), fstab.zone_fs_dir,
			    fstab.zone_fs_type);
			return_code = Z_ERR;
			goto next_fs;
		}
		if (fstab.zone_fs_raw[0] != '\0' &&
		    (stat(cmdbuf, &st) != 0 || !S_ISREG(st.st_mode))) {
			(void) fprintf(stderr, gettext("cannot verify fs %s: "
			    "'raw' device specified but "
			    "no fsck executable exists for %s\n"),
			    fstab.zone_fs_dir, fstab.zone_fs_type);
			return_code = Z_ERR;
			goto next_fs;
		} else if (fstab.zone_fs_raw[0] == '\0' &&
		    stat(cmdbuf, &st) == 0 &&
		    isregfile(fstab.zone_fs_special) != 1) {
			(void) fprintf(stderr, gettext("could not verify fs "
			    "%s: must specify 'raw' device for %s "
			    "file systems\n"),
			    fstab.zone_fs_dir, fstab.zone_fs_type);
			return_code = Z_ERR;
			goto next_fs;
		}

		/* Verify fs_special. */
		if ((return_code = verify_fs_special(&fstab)) != Z_OK)
			goto next_fs;

		/* Verify fs_raw. */
		if (fstab.zone_fs_raw[0] != '\0' &&
		    stat(fstab.zone_fs_raw, &st) != 0) {
			/*
			 * TRANSLATION_NOTE
			 * fs is a literal that should not be translated.
			 */
			(void) fprintf(stderr, gettext("could not verify fs "
			    "%s: could not access %s: %s\n"), fstab.zone_fs_dir,
			    fstab.zone_fs_raw, strerror(errno));
			return_code = Z_ERR;
			goto next_fs;
		}
next_fs:
		zonecfg_free_fs_option_list(fstab.zone_fs_options);
	}
	(void) zonecfg_endfsent(handle);

	return (return_code);
}

static int
verify_limitpriv(zone_dochandle_t handle)
{
	char *privname = NULL;
	int err;
	priv_set_t *privs;

	if ((privs = priv_allocset()) == NULL) {
		zperror(gettext("failed to allocate privilege set"), B_FALSE);
		return (Z_NOMEM);
	}
	err = zonecfg_get_privset(handle, privs, &privname);
	switch (err) {
	case Z_OK:
		break;
	case Z_PRIV_PROHIBITED:
		(void) fprintf(stderr, gettext("privilege \"%s\" is not "
		    "permitted within the zone's privilege set\n"), privname);
		break;
	case Z_PRIV_REQUIRED:
		(void) fprintf(stderr, gettext("required privilege \"%s\" is "
		    "missing from the zone's privilege set\n"), privname);
		break;
	case Z_PRIV_UNKNOWN:
		(void) fprintf(stderr, gettext("unknown privilege \"%s\" "
		    "specified in the zone's privilege set\n"), privname);
		break;
	default:
		zperror(
		    gettext("failed to determine the zone's privilege set"),
		    B_TRUE);
		break;
	}
	free(privname);
	priv_freeset(privs);
	return (err);
}

static void
free_local_netifs(int if_cnt, struct net_if **if_list)
{
	int		i;

	for (i = 0; i < if_cnt; i++) {
		free(if_list[i]->name);
		free(if_list[i]);
	}
	free(if_list);
}

/*
 * Get a list of the network interfaces, along with their address families,
 * that are plumbed in the global zone.  See if_tcp(7p) for a description
 * of the ioctls used here.
 */
static int
get_local_netifs(int *if_cnt, struct net_if ***if_list)
{
	int		s;
	int		i;
	int		res = Z_OK;
	int		space_needed;
	int		cnt = 0;
	struct		lifnum if_num;
	struct		lifconf if_conf;
	struct		lifreq *if_reqp;
	char		*if_buf;
	struct net_if	**local_ifs = NULL;

	*if_cnt = 0;
	*if_list = NULL;

	if ((s = socket(SOCKET_AF(AF_INET), SOCK_DGRAM, 0)) < 0)
		return (Z_ERR);

	/*
	 * Come back here in the unlikely event that the number of interfaces
	 * increases between the time we get the count and the time we do the
	 * SIOCGLIFCONF ioctl.
	 */
retry:
	/* Get the number of interfaces. */
	if_num.lifn_family = AF_UNSPEC;
	if_num.lifn_flags = LIFC_NOXMIT;
	if (ioctl(s, SIOCGLIFNUM, &if_num) < 0) {
		(void) close(s);
		return (Z_ERR);
	}

	/* Get the interface configuration list. */
	space_needed = if_num.lifn_count * sizeof (struct lifreq);
	if ((if_buf = malloc(space_needed)) == NULL) {
		(void) close(s);
		return (Z_ERR);
	}
	if_conf.lifc_family = AF_UNSPEC;
	if_conf.lifc_flags = LIFC_NOXMIT;
	if_conf.lifc_len = space_needed;
	if_conf.lifc_buf = if_buf;
	if (ioctl(s, SIOCGLIFCONF, &if_conf) < 0) {
		free(if_buf);
		/*
		 * SIOCGLIFCONF returns EINVAL if the buffer we passed in is
		 * too small.  In this case go back and get the new if cnt.
		 */
		if (errno == EINVAL)
			goto retry;

		(void) close(s);
		return (Z_ERR);
	}
	(void) close(s);

	/* Get the name and address family for each interface. */
	if_reqp = if_conf.lifc_req;
	for (i = 0; i < (if_conf.lifc_len / sizeof (struct lifreq)); i++) {
		struct net_if	**p;
		struct lifreq	req;

		if (strcmp(LOOPBACK_IF, if_reqp->lifr_name) == 0) {
			if_reqp++;
			continue;
		}

		if ((s = socket(SOCKET_AF(if_reqp->lifr_addr.ss_family),
		    SOCK_DGRAM, 0)) == -1) {
			res = Z_ERR;
			break;
		}

		(void) strncpy(req.lifr_name, if_reqp->lifr_name,
		    sizeof (req.lifr_name));
		if (ioctl(s, SIOCGLIFADDR, &req) < 0) {
			(void) close(s);
			if_reqp++;
			continue;
		}

		if ((p = (struct net_if **)realloc(local_ifs,
		    sizeof (struct net_if *) * (cnt + 1))) == NULL) {
			res = Z_ERR;
			break;
		}
		local_ifs = p;

		if ((local_ifs[cnt] = malloc(sizeof (struct net_if))) == NULL) {
			res = Z_ERR;
			break;
		}

		if ((local_ifs[cnt]->name = strdup(if_reqp->lifr_name))
		    == NULL) {
			free(local_ifs[cnt]);
			res = Z_ERR;
			break;
		}
		local_ifs[cnt]->af = req.lifr_addr.ss_family;
		cnt++;

		(void) close(s);
		if_reqp++;
	}

	free(if_buf);

	if (res != Z_OK) {
		free_local_netifs(cnt, local_ifs);
	} else {
		*if_cnt = cnt;
		*if_list = local_ifs;
	}

	return (res);
}

static char *
af2str(int af)
{
	switch (af) {
	case AF_INET:
		return ("IPv4");
	case AF_INET6:
		return ("IPv6");
	default:
		return ("Unknown");
	}
}

/*
 * Cross check the network interface name and address family with the
 * interfaces that are set up in the global zone so that we can print the
 * appropriate error message.
 */
static void
print_net_err(char *phys, char *addr, int af, char *msg)
{
	int		i;
	int		local_if_cnt = 0;
	struct net_if	**local_ifs = NULL;
	boolean_t	found_if = B_FALSE;
	boolean_t	found_af = B_FALSE;

	if (get_local_netifs(&local_if_cnt, &local_ifs) != Z_OK) {
		(void) fprintf(stderr,
		    gettext("could not verify %s %s=%s %s=%s\n\t%s\n"),
		    "net", "address", addr, "physical", phys, msg);
		return;
	}

	for (i = 0; i < local_if_cnt; i++) {
		if (strcmp(phys, local_ifs[i]->name) == 0) {
			found_if = B_TRUE;
			if (af == local_ifs[i]->af) {
				found_af = B_TRUE;
				break;
			}
		}
	}

	free_local_netifs(local_if_cnt, local_ifs);

	if (!found_if) {
		(void) fprintf(stderr,
		    gettext("could not verify %s %s=%s\n\t"
		    "network interface %s is not plumbed in the global zone\n"),
		    "net", "physical", phys, phys);
		return;
	}

	/*
	 * Print this error if we were unable to find the address family
	 * for this interface.  If the af variable is not initialized to
	 * to something meaningful by the caller (not AF_UNSPEC) then we
	 * also skip this message since it wouldn't be informative.
	 */
	if (!found_af && af != AF_UNSPEC) {
		(void) fprintf(stderr,
		    gettext("could not verify %s %s=%s %s=%s\n\tthe %s address "
		    "family is not configured on this network interface in "
		    "the\n\tglobal zone\n"),
		    "net", "address", addr, "physical", phys, af2str(af));
		return;
	}

	(void) fprintf(stderr,
	    gettext("could not verify %s %s=%s %s=%s\n\t%s\n"),
	    "net", "address", addr, "physical", phys, msg);
}

static int
verify_handle(int cmd_num, zone_dochandle_t handle, char *argv[])
{
	struct zone_nettab nettab;
	struct zone_anettab anettab;
	int return_code = Z_OK;
	int err;
	boolean_t in_alt_root;
	zone_iptype_t iptype;
	dladm_handle_t dh;
	dladm_status_t status;
	datalink_id_t linkid;
	char errmsg[DLADM_STRSIZE];

	in_alt_root = zonecfg_in_alt_root();
	if (in_alt_root)
		goto no_net;

	if ((err = zonecfg_get_iptype(handle, &iptype)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(cmd_num), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}

	/*
	 * Verification for net resource.
	 */
	if ((err = zonecfg_setnetent(handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(cmd_num), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	while (zonecfg_getnetent(handle, &nettab) == Z_OK) {
		struct lifreq lifr;
		sa_family_t af = AF_UNSPEC;
		char dl_owner_zname[ZONENAME_MAX];
		zoneid_t dl_owner_zid;
		zoneid_t target_zid;
		int res;

		/* skip any loopback interfaces */
		if (strcmp(nettab.zone_net_physical, "lo0") == 0)
			continue;
		switch (iptype) {
		case ZS_SHARED:
			if ((res = zonecfg_valid_net_address(
			    nettab.zone_net_address, &lifr)) != Z_OK) {
				print_net_err(nettab.zone_net_physical,
				    nettab.zone_net_address, af,
				    zonecfg_strerror(res));
				return_code = Z_ERR;
				continue;
			}
			af = lifr.lifr_addr.ss_family;
			if (!zonecfg_ifname_exists(af,
			    nettab.zone_net_physical)) {
				/*
				 * The interface failed to come up. We continue
				 * on anyway for the sake of consistency: a
				 * zone is not shut down if the interface fails
				 * any time after boot, nor does the global zone
				 * fail to boot if an interface fails.
				 */
				(void) fprintf(stderr,
				    gettext("WARNING: skipping network "
				    "interface '%s' which may not be "
				    "present/plumbed in the global "
				    "zone.\n"),
				    nettab.zone_net_physical);
			}
			break;
		case ZS_EXCLUSIVE:
			/* Warning if it exists for either IPv4 or IPv6 */

			if (zonecfg_ifname_exists(AF_INET,
			    nettab.zone_net_physical) ||
			    zonecfg_ifname_exists(AF_INET6,
			    nettab.zone_net_physical)) {
				(void) fprintf(stderr,
				    gettext("WARNING: skipping network "
				    "interface '%s' which is used in the "
				    "global zone.\n"),
				    nettab.zone_net_physical);
				break;
			}

			/*
			 * Verify that the datalink exists and that it isn't
			 * already assigned to a zone.
			 */
			if ((status = dladm_open(&dh)) == DLADM_STATUS_OK) {
				status = dladm_name2info(dh,
				    nettab.zone_net_physical, &linkid, NULL,
				    NULL, NULL);
				dladm_close(dh);
			}
			if (status != DLADM_STATUS_OK) {
				(void) fprintf(stderr,
				    gettext("WARNING: skipping network "
				    "interface '%s': %s\n"),
				    nettab.zone_net_physical,
				    dladm_status2str(status, errmsg));
				break;
			}
			dl_owner_zid = ALL_ZONES;
			if (zone_check_datalink(&dl_owner_zid, linkid) != 0)
				break;

			/*
			 * If the zone being verified is
			 * running and owns the interface
			 */
			target_zid = getzoneidbyname(target_zone);
			if (target_zid == dl_owner_zid)
				break;

			/* Zone id match failed, use name to check */
			if (getzonenamebyid(dl_owner_zid, dl_owner_zname,
			    ZONENAME_MAX) < 0) {
				/* No name, show ID instead */
				(void) snprintf(dl_owner_zname, ZONENAME_MAX,
				    "<%d>", dl_owner_zid);
			} else if (strcmp(dl_owner_zname, target_zone) == 0)
				break;

			/*
			 * Note here we only report a warning that
			 * the interface is already in use by another
			 * running zone, and the verify process just
			 * goes on, if the interface is still in use
			 * when this zone really boots up, zoneadmd
			 * will find it. If the name of the zone which
			 * owns this interface cannot be determined,
			 * then it is not possible to determine if there
			 * is a conflict so just report it as a warning.
			 */
			(void) fprintf(stderr,
			    gettext("WARNING: skipping network interface "
			    "'%s' which is used by the non-global zone "
			    "'%s'.\n"), nettab.zone_net_physical,
			    dl_owner_zname);
			break;
		}
	}
	(void) zonecfg_endnetent(handle);

	if (iptype == ZS_EXCLUSIVE) {
		datalink_class_t class;
		uint32_t media;

		/*
		 * Verification for anet resource.
		 */
		if ((err = zonecfg_setanetent(handle)) != Z_OK) {
			errno = err;
			zperror(cmd_to_str(cmd_num), B_TRUE);
			zonecfg_fini_handle(handle);
			return (Z_ERR);
		}
		while (zonecfg_getanetent(handle, &anettab) == Z_OK) {
			/*
			 * Verify that the lower-link exists
			 */
			if (anettab.zone_anet_lower_link[0] == '\0') {
				(void) fprintf(stderr,
				    gettext("%s resource for '%s': "
				    "%s not specified.\n"),
				    "anet", anettab.zone_anet_linkname,
				    "lower-link");
				return_code = Z_ERR;
				continue;
			}
			if (strcasecmp(anettab.zone_anet_lower_link,
			    "auto") == 0)
				continue;
			if ((status = dladm_open(&dh)) == DLADM_STATUS_OK) {
				status = dladm_name2info(dh,
				    anettab.zone_anet_lower_link, NULL, NULL,
				    &class, &media);
				dladm_close(dh);
			}
			if (status != DLADM_STATUS_OK) {
				(void) fprintf(stderr,
				    gettext("%s resource for '%s': "
				    "unable to find %s '%s': %s.\n"),
				    "anet", anettab.zone_anet_linkname,
				    "lower-link", anettab.zone_anet_lower_link,
				    dladm_status2str(status, errmsg));
				return_code = Z_ERR;
				continue;
			}

			if (class == DATALINK_CLASS_ETHERSTUB ||
			    media == DL_ETHER)
				continue;

			(void) fprintf(stderr,
			    gettext("%s resource for '%s': "
			    "%s '%s' is not %s.\n"),
			    "anet", anettab.zone_anet_linkname,
			    "lower-link", anettab.zone_anet_lower_link,
			    "Ethernet");
			return_code = Z_ERR;
		}
		(void) zonecfg_endanetent(handle);
	}

no_net:

	/* verify that lofs has not been excluded from the kernel */
	if (!(cmd_num == CMD_CLONE || cmd_num == CMD_ATTACH ||
	    cmd_num == CMD_MOVE) &&
	    modctl(MODLOAD, 1, "fs/lofs", NULL) != 0) {
		if (errno == ENXIO)
			(void) fprintf(stderr, gettext("could not verify "
			    "lofs(7FS): possibly excluded in /etc/system\n"));
		else
			(void) fprintf(stderr, gettext("could not verify "
			    "lofs(7FS): %s\n"), strerror(errno));
		return_code = Z_ERR;
	}

	if (verify_filesystems(handle) != Z_OK)
		return_code = Z_ERR;
	if (!in_alt_root && verify_rctls(handle) != Z_OK)
		return_code = Z_ERR;
	if (!in_alt_root && verify_pool(handle) != Z_OK)
		return_code = Z_ERR;
	if (!in_alt_root && verify_brand(handle, cmd_num, argv) != Z_OK)
		return_code = Z_ERR;
	if (!in_alt_root && verify_datasets(handle) != Z_OK)
		return_code = Z_ERR;

	/*
	 * As the "mount" command is used for patching/upgrading of zones
	 * or other maintenance processes, the zone's privilege set is not
	 * checked in this case.  Instead, the default, safe set of
	 * privileges will be used when this zone is created in the
	 * kernel.
	 */
	if (!in_alt_root && cmd_num != CMD_MOUNT &&
	    verify_limitpriv(handle) != Z_OK)
		return_code = Z_ERR;

	return (return_code);
}

static int
verify_details(int cmd_num, char *argv[])
{
	zone_dochandle_t handle;
	char zonepath[MAXPATHLEN], checkpath[MAXPATHLEN];
	int return_code = Z_OK;
	int err;

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(cmd_num), B_TRUE);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(cmd_num), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_zonepath(handle, zonepath, sizeof (zonepath))) !=
	    Z_OK) {
		errno = err;
		zperror(cmd_to_str(cmd_num), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	/*
	 * zonecfg_get_zonepath() gets its data from the XML repository.
	 * Verify this against the index file, which is checked first by
	 * zone_get_zonepath().  If they don't match, bail out.
	 */
	if ((err = zone_get_zonepath(target_zone, checkpath,
	    sizeof (checkpath))) != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not get zone path"));
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	if (strcmp(zonepath, checkpath) != 0) {
		/*
		 * TRANSLATION_NOTE
		 * XML and zonepath are literals that should not be translated.
		 */
		(void) fprintf(stderr, gettext("The XML repository has "
		    "zonepath '%s',\nbut the index file has zonepath '%s'.\n"
		    "These must match, so fix the incorrect entry.\n"),
		    zonepath, checkpath);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}
	if (cmd_num != CMD_ATTACH &&
	    validate_zonepath(zonepath, cmd_num) != Z_OK) {
		(void) fprintf(stderr, gettext("could not verify zonepath %s "
		    "because of the above errors.\n"), zonepath);
		return_code = Z_ERR;
	}
	/*
	 * There's no more reasonable required work to do here for a detach op.
	 */
	if (cmd_num == CMD_DETACH)
		return (return_code);
	if (verify_handle(cmd_num, handle, argv) != Z_OK)
		return_code = Z_ERR;

	zonecfg_fini_handle(handle);
	if (return_code == Z_ERR)
		(void) fprintf(stderr,
		    gettext("%s: zone %s failed to verify\n"),
		    execname, target_zone);
	return (return_code);
}

static int
verify_func(int argc, char *argv[])
{
	int arg;

	optind = 0;
	if ((arg = getopt(argc, argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_VERIFY, CMD_VERIFY);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_VERIFY, CMD_VERIFY);
			return (Z_USAGE);
		}
	}
	if (argc > optind) {
		sub_usage(SHELP_VERIFY, CMD_VERIFY);
		return (Z_USAGE);
	}
	if (sanity_check(target_zone, CMD_VERIFY, B_FALSE, B_FALSE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);
	return (verify_details(CMD_VERIFY, argv));
}

static int
addoptions(char *buf, char *argv[], size_t len)
{
	int i = 0;

	if (buf[0] == '\0')
		return (Z_OK);

	while (argv[i] != NULL) {
		if (strlcat(buf, " ", len) >= len ||
		    strlcat(buf, argv[i++], len) >= len) {
			zerror("Command line too long");
			return (Z_ERR);
		}
	}

	return (Z_OK);
}

static int
addopt(char *buf, int opt, char *optarg, size_t bufsize)
{
	char optstring[4];

	if (opt > 0)
		(void) sprintf(optstring, " -%c", opt);
	else
		(void) strcpy(optstring, " ");

	if ((strlcat(buf, optstring, bufsize) > bufsize))
		return (Z_ERR);

	if ((optarg != NULL) && (strlcat(buf, optarg, bufsize) > bufsize))
		return (Z_ERR);

	return (Z_OK);
}

/*
 * If a subproc fails with ZONE_SUBPROC_DETACHED, the zone needs to be
 * put back into the configured state and an appropriate error message
 * is displayed indicating how a zone can be attached or uninstalled.
 */
static void
set_detached()
{
	if ((errno = zone_set_state(target_zone, ZONE_STATE_CONFIGURED)) !=
	    Z_OK) {
		zperror2(target_zone, gettext("could not reset state: "
		    "uninstall required"));
		return;
	}
	zerror(gettext("manually correct the problem[s] reported above, then "
	    "run"));
	/* Literal command: not translated */
	zerror("zoneadm -z %s " SHELP_INSTALL, target_zone);
	zerror(gettext("or uninstall the zone with"));
	/* Literal commands: not translated */
	zerror("zoneadm -z %s " SHELP_MARK, target_zone);
	zerror("zoneadm -z %s " SHELP_UNINSTALL, target_zone);
}

/* ARGSUSED */
static int
install_func(int argc, char *argv[])
{
	char cmdbuf[MAXPATHLEN];
	char postcmdbuf[MAXPATHLEN];
	int lockfd;
	int arg, err, subproc_err;
	char zonepath[MAXPATHLEN];
	brand_handle_t bh = NULL;
	int status;
	boolean_t do_postinstall = B_FALSE;
	boolean_t brand_help = B_FALSE;
	char opts[128];

	if (target_zone == NULL) {
		sub_usage(SHELP_INSTALL, CMD_INSTALL);
		return (Z_USAGE);
	}

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot install zone in alternate root"));
		return (Z_ERR);
	}

	if ((err = zone_get_zonepath(target_zone, zonepath,
	    sizeof (zonepath))) != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not get zone path"));
		return (Z_ERR);
	}

	/* Fetch the install command from the brand configuration.  */
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		return (Z_ERR);
	}

	if (get_hook(bh, cmdbuf, sizeof (cmdbuf), brand_get_install,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing install resource");
		brand_close(bh);
		return (Z_ERR);
	}

	if (get_hook(bh, postcmdbuf, sizeof (postcmdbuf), brand_get_postinstall,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing postinstall "
		    "resource");
		brand_close(bh);
		return (Z_ERR);
	}

	if (postcmdbuf[0] != '\0')
		do_postinstall = B_TRUE;

	(void) strcpy(opts, "?x:");
	/*
	 * Fetch the list of recognized command-line options from
	 * the brand configuration file.
	 */
	if (brand_get_installopts(bh, opts + strlen(opts),
	    sizeof (opts) - strlen(opts)) != 0) {
		zerror("invalid brand configuration: missing "
		    "install options resource");
		brand_close(bh);
		return (Z_ERR);
	}

	brand_close(bh);

	if (cmdbuf[0] == '\0') {
		zerror("Missing brand install command");
		return (Z_ERR);
	}

	/* Check the argv string for args we handle internally */
	optind = 0;
	opterr = 0;
	while ((arg = getopt(argc, argv, opts)) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?') {
				sub_usage(SHELP_INSTALL, CMD_INSTALL);
				brand_help = B_TRUE;
			}
			/* Ignore unknown options - may be brand specific. */
			break;
		default:
			/* Ignore unknown options - may be brand specific. */
			break;
		}

		/*
		 * Append the option to the command line passed to the
		 * brand-specific install and postinstall routines.
		 */
		if (addopt(cmdbuf, optopt, optarg, sizeof (cmdbuf)) != Z_OK) {
			zerror("Install command line too long");
			return (Z_ERR);
		}
		if (addopt(postcmdbuf, optopt, optarg, sizeof (postcmdbuf))
		    != Z_OK) {
			zerror("Post-Install command line too long");
			return (Z_ERR);
		}
	}

	for (; optind < argc; optind++) {
		if (addopt(cmdbuf, 0, argv[optind], sizeof (cmdbuf)) != Z_OK) {
			zerror("Install command line too long");
			return (Z_ERR);
		}

		if (addopt(postcmdbuf, 0, argv[optind], sizeof (postcmdbuf))
		    != Z_OK) {
			zerror("Post-Install command line too long");
			return (Z_ERR);
		}
	}

	if (!brand_help) {
		if (sanity_check(target_zone, CMD_INSTALL, B_FALSE, B_TRUE,
		    B_FALSE) != Z_OK)
			return (Z_ERR);
		if (verify_details(CMD_INSTALL, argv) != Z_OK)
			return (Z_ERR);

		if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
			zerror(gettext("another %s may have an operation in "
			    "progress."), "zoneadm");
			return (Z_ERR);
		}
		err = zone_set_state(target_zone, ZONE_STATE_INCOMPLETE);
		if (err != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not set state"));
			goto done;
		}

		if (create_zfs_zonepath(zonepath, B_FALSE) != Z_OK) {
			zerror(gettext("failed to create zone path"));
			return (Z_ERR);
		}
	}

	status = do_subproc(cmdbuf);
	if ((subproc_err =
	    subproc_status(gettext("brand-specific installation"), status,
	    B_FALSE)) != ZONE_SUBPROC_OK) {
		if (subproc_err == ZONE_SUBPROC_USAGE && !brand_help) {
			sub_usage(SHELP_INSTALL, CMD_INSTALL);
		}
		errno = subproc_err;
		err = Z_ERR;
		goto done;
	}

	if (brand_help)
		return (Z_OK);

	if ((err = zone_set_state(target_zone, ZONE_STATE_INSTALLED)) != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not set state"));
		goto done;
	}

	if (do_postinstall) {
		status = do_subproc(postcmdbuf);

		if ((subproc_err =
		    subproc_status(gettext("brand-specific post-install"),
		    status, B_FALSE)) != ZONE_SUBPROC_OK) {
			errno = subproc_err;
			err = Z_ERR;
			(void) zone_set_state(target_zone,
			    ZONE_STATE_INCOMPLETE);
		}
	}

done:
	/*
	 * If the install script exited with ZONE_SUBPROC_DETACHED, it
	 * indicates that an image now exists but it is not yet bootable.
	 * The admin has two options at this point:
	 *
	 *  - Fix whatever went wrong and then run attach [-u].
	 *  - Mark the zone as incomplete and then uninstall.
	 */
	if (subproc_err == ZONE_SUBPROC_DETACHED) {
		zerror(gettext("NOTICE: installation partially succeeded"));
		set_detached();
	}
	if (subproc_err == ZONE_SUBPROC_USAGE) {
		if ((err = zone_set_state(target_zone,
		    ZONE_STATE_CONFIGURED)) != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not set state: "
			    "uninstall required"));
		}
	}

	if (!brand_help)
		zonecfg_release_lock_file(target_zone, lockfd);
	return ((err == Z_OK) ? Z_OK : Z_ERR);
}

static void
warn_dev_match(zone_dochandle_t s_handle, char *source_zone,
    zone_dochandle_t t_handle, char *target_zone)
{
	int err;
	struct zone_devtab s_devtab;
	struct zone_devtab t_devtab;

	if ((err = zonecfg_setdevent(t_handle)) != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not enumerate devices"));
		return;
	}

	while (zonecfg_getdevent(t_handle, &t_devtab) == Z_OK) {
		if ((err = zonecfg_setdevent(s_handle)) != Z_OK) {
			errno = err;
			zperror2(source_zone,
			    gettext("could not enumerate devices"));
			(void) zonecfg_enddevent(t_handle);
			return;
		}

		while (zonecfg_getdevent(s_handle, &s_devtab) == Z_OK) {
			/*
			 * Use fnmatch to catch the case where wildcards
			 * were used in one zone and the other has an
			 * explicit entry (e.g. /dev/dsk/c0t0d0s6 vs.
			 * /dev/\*dsk/c0t0d0s6).
			 */
			if (fnmatch(t_devtab.zone_dev_match,
			    s_devtab.zone_dev_match, FNM_PATHNAME) == 0 ||
			    fnmatch(s_devtab.zone_dev_match,
			    t_devtab.zone_dev_match, FNM_PATHNAME) == 0) {
				(void) fprintf(stderr,
				    gettext("WARNING: device '%s' "
				    "is configured in both zones.\n"),
				    t_devtab.zone_dev_match);
				break;
			}
		}
		(void) zonecfg_enddevent(s_handle);
	}

	(void) zonecfg_enddevent(t_handle);
}

/*
 * Check if the specified mount option (opt) is contained within the
 * options string.
 */
static boolean_t
opt_match(char *opt, char *options)
{
	char *p;
	char *lastp;

	if ((p = strtok_r(options, ",", &lastp)) != NULL) {
		if (strcmp(p, opt) == 0)
			return (B_TRUE);
		while ((p = strtok_r(NULL, ",", &lastp)) != NULL) {
			if (strcmp(p, opt) == 0)
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}

#define	RW_LOFS	"WARNING: read-write lofs file system on '%s' is configured " \
	"in both zones.\n"

static void
print_fs_warnings(struct zone_fstab *s_fstab, struct zone_fstab *t_fstab)
{
	/*
	 * It is ok to have shared lofs mounted fs but we want to warn if
	 * either is rw since this will effect the other zone.
	 */
	if (strcmp(t_fstab->zone_fs_type, "lofs") == 0) {
		zone_fsopt_t *optp;

		/* The default is rw so no options means rw */
		if (t_fstab->zone_fs_options == NULL ||
		    s_fstab->zone_fs_options == NULL) {
			(void) fprintf(stderr, gettext(RW_LOFS),
			    t_fstab->zone_fs_special);
			return;
		}

		for (optp = s_fstab->zone_fs_options; optp != NULL;
		    optp = optp->zone_fsopt_next) {
			if (opt_match("rw", optp->zone_fsopt_opt)) {
				(void) fprintf(stderr, gettext(RW_LOFS),
				    s_fstab->zone_fs_special);
				return;
			}
		}

		for (optp = t_fstab->zone_fs_options; optp != NULL;
		    optp = optp->zone_fsopt_next) {
			if (opt_match("rw", optp->zone_fsopt_opt)) {
				(void) fprintf(stderr, gettext(RW_LOFS),
				    t_fstab->zone_fs_special);
				return;
			}
		}

		return;
	}

	/*
	 * TRANSLATION_NOTE
	 * The first variable is the file system type and the second is
	 * the file system special device.  For example,
	 * WARNING: ufs file system on '/dev/dsk/c0t0d0s0' ...
	 */
	(void) fprintf(stderr, gettext("WARNING: %s file system on '%s' "
	    "is configured in both zones.\n"), t_fstab->zone_fs_type,
	    t_fstab->zone_fs_special);
}

static void
warn_fs_match(zone_dochandle_t s_handle, char *source_zone,
    zone_dochandle_t t_handle, char *target_zone)
{
	int err;
	struct zone_fstab s_fstab;
	struct zone_fstab t_fstab;

	if ((err = zonecfg_setfsent(t_handle)) != Z_OK) {
		errno = err;
		zperror2(target_zone,
		    gettext("could not enumerate file systems"));
		return;
	}

	while (zonecfg_getfsent(t_handle, &t_fstab) == Z_OK) {
		if ((err = zonecfg_setfsent(s_handle)) != Z_OK) {
			errno = err;
			zperror2(source_zone,
			    gettext("could not enumerate file systems"));
			(void) zonecfg_endfsent(t_handle);
			return;
		}

		while (zonecfg_getfsent(s_handle, &s_fstab) == Z_OK) {
			if (strcmp(t_fstab.zone_fs_special,
			    s_fstab.zone_fs_special) == 0) {
				print_fs_warnings(&s_fstab, &t_fstab);
				break;
			}
		}
		(void) zonecfg_endfsent(s_handle);
	}

	(void) zonecfg_endfsent(t_handle);
}

/*
 * We don't catch the case where you used the same IP address but
 * it is not an exact string match.  For example, 192.9.0.128 vs. 192.09.0.128.
 * However, we're not going to worry about that but we will check for
 * a possible netmask on one of the addresses (e.g. 10.0.0.1 and 10.0.0.1/24)
 * and handle that case as a match.
 */
static void
warn_ip_match(zone_dochandle_t s_handle, char *source_zone,
    zone_dochandle_t t_handle, char *target_zone)
{
	int err;
	struct zone_nettab s_nettab;
	struct zone_nettab t_nettab;

	if ((err = zonecfg_setnetent(t_handle)) != Z_OK) {
		errno = err;
		zperror2(target_zone,
		    gettext("could not enumerate network interfaces"));
		return;
	}

	while (zonecfg_getnetent(t_handle, &t_nettab) == Z_OK) {
		char *p;

		/* remove an (optional) netmask from the address */
		if ((p = strchr(t_nettab.zone_net_address, '/')) != NULL)
			*p = '\0';

		if ((err = zonecfg_setnetent(s_handle)) != Z_OK) {
			errno = err;
			zperror2(source_zone,
			    gettext("could not enumerate network interfaces"));
			(void) zonecfg_endnetent(t_handle);
			return;
		}

		while (zonecfg_getnetent(s_handle, &s_nettab) == Z_OK) {
			/* remove an (optional) netmask from the address */
			if ((p = strchr(s_nettab.zone_net_address, '/'))
			    != NULL)
				*p = '\0';

			/* For exclusive-IP zones, address is not specified. */
			if (strlen(s_nettab.zone_net_address) == 0)
				continue;

			if (strcmp(t_nettab.zone_net_address,
			    s_nettab.zone_net_address) == 0) {
				(void) fprintf(stderr,
				    gettext("WARNING: network address '%s' "
				    "is configured in both zones.\n"),
				    t_nettab.zone_net_address);
				break;
			}
		}
		(void) zonecfg_endnetent(s_handle);
	}

	(void) zonecfg_endnetent(t_handle);
}

static void
warn_dataset_match(zone_dochandle_t s_handle, char *source,
    zone_dochandle_t t_handle, char *target)
{
	int err;
	struct zone_dstab s_dstab;
	struct zone_dstab t_dstab;

	if ((err = zonecfg_setdsent(t_handle)) != Z_OK) {
		errno = err;
		zperror2(target, gettext("could not enumerate datasets"));
		return;
	}

	while (zonecfg_getdsent(t_handle, &t_dstab) == Z_OK) {
		if ((err = zonecfg_setdsent(s_handle)) != Z_OK) {
			errno = err;
			zperror2(source,
			    gettext("could not enumerate datasets"));
			(void) zonecfg_enddsent(t_handle);
			return;
		}

		while (zonecfg_getdsent(s_handle, &s_dstab) == Z_OK) {
			if (strcmp(t_dstab.zone_dataset_name,
			    s_dstab.zone_dataset_name) == 0) {
				zerror(gettext("WARNING: dataset '%s' "
				    "is configured in both zones.\n"),
				    t_dstab.zone_dataset_name);
				break;
			}
		}
		(void) zonecfg_enddsent(s_handle);
	}

	(void) zonecfg_enddsent(t_handle);
}

/*
 * Callback used with warn_nonbe_mounts()
 */
static int
nonbe_mount_cb(const struct mnttab *p, void *r) {
	nonbe_mount_info_t *data = r;

	if (strncmp(p->mnt_special, data->rootds, data->len) == 0 &&
	    (p->mnt_special[data->len] == '\0' ||
	    p->mnt_special[data->len] == '/')) {
		return (0);
	}

	if (data->badmounts == 0) {
		zerror(gettext("These file systems are mounted on "
		    "subdirectories of %s."), data->rootpath);
	}
	zerror("  %s", p->mnt_mountp);
	data->badmounts++;
	return (0);
}

/*
 * Look for mounts under rootpath that are not part of the boot environment.
 * If non-BE mounts are found, they are reported via zerror().
 * Returns -1 if there was an error, else the number of non-BE mounts.
 */
static int
warn_nonbe_mounts(char *rootpath)
{
	FILE *mnttab = NULL;
	struct stat stbuf;
	struct extmnttab mnt;
	int err;
	nonbe_mount_info_t data;

	/*
	 * Find the dataset that contains rootpath and store it in the
	 * callback data.
	 */
	if (stat(rootpath, &stbuf) != 0) {
		return (0);
	}
	if ((mnttab = fopen(MNTTAB, "r")) == NULL) {
		(void) fprintf(stderr, gettext("%s: unable to read %s: %s\n"),
		    execname, MNTTAB, strerror(errno));
		goto errorout;
	}
	while (getextmntent(mnttab, &mnt, sizeof (mnt)) == 0) {
		if (makedev(mnt.mnt_major, mnt.mnt_minor) == stbuf.st_dev) {
			(void) fclose(mnttab);
			mnttab = NULL;
			break;
		}
	}
	if (mnttab != NULL) {
		(void) fprintf(stderr, gettext("%s: device for root path %s "
		    "not found in %s\n"), execname, rootpath, MNTTAB);
		goto errorout;
	}
	if ((data.len = strlcpy(data.rootds, mnt.mnt_special,
	    sizeof (data.rootds))) >= sizeof (data.rootds)) {
		(void) fprintf(stderr, gettext("%s: block device for root "
		    "path %s too long\n"), execname, rootpath);
		goto errorout;
	}

	/*
	 * Iterate through zone's mounts.
	 */
	data.badmounts = 0;
	data.rootpath = rootpath;
	err = zonecfg_find_mounts(rootpath, nonbe_mount_cb, (void*)&data);
	if (err == -1) {
		(void) fprintf(stderr, gettext("%s: unable to walk mount "
		    "points under %s\n"), execname, rootpath);
		goto errorout;
	}
	return (data.badmounts);

errorout:
	if (mnttab != NULL) {
		(void) fclose(mnttab);
	}
	zerror(gettext("Unable to examine mount points under root path %s.\n"),
	    rootpath);
	return (-1);
}

/*
 * Check that the clone and its source have the same brand type.
 */
static int
valid_brand_clone(char *source_zone, char *target_zone)
{
	brand_handle_t bh;
	char source_brand[MAXNAMELEN];

	if ((zone_get_brand(source_zone, source_brand,
	    sizeof (source_brand))) != Z_OK) {
		(void) fprintf(stderr, "%s: zone '%s': %s\n",
		    execname, source_zone, gettext("missing or invalid brand"));
		return (Z_ERR);
	}

	if (strcmp(source_brand, target_brand) != 0) {
		(void) fprintf(stderr,
		    gettext("%s: Zones '%s' and '%s' have different brand "
		    "types.\n"), execname, source_zone, target_zone);
		return (Z_ERR);
	}

	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		return (Z_ERR);
	}
	brand_close(bh);
	return (Z_OK);
}

static int
validate_clone(char *source_zone, char *target_zone)
{
	int err = Z_OK;
	zone_dochandle_t s_handle;
	zone_dochandle_t t_handle;

	if ((t_handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_CLONE), B_TRUE);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_handle(target_zone, t_handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_CLONE), B_TRUE);
		zonecfg_fini_handle(t_handle);
		return (Z_ERR);
	}

	if ((s_handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_CLONE), B_TRUE);
		zonecfg_fini_handle(t_handle);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_handle(source_zone, s_handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_CLONE), B_TRUE);
		goto done;
	}

	/* verify new zone has same brand type */
	err = valid_brand_clone(source_zone, target_zone);
	if (err != Z_OK)
		goto done;

	/* warn about imported fs's which are the same */
	warn_fs_match(s_handle, source_zone, t_handle, target_zone);

	/* warn about imported IP addresses which are the same */
	warn_ip_match(s_handle, source_zone, t_handle, target_zone);

	/* warn about imported devices which are the same */
	warn_dev_match(s_handle, source_zone, t_handle, target_zone);

	/* warn about imported datasets which are the same */
	warn_dataset_match(s_handle, source_zone, t_handle, target_zone);

done:
	zonecfg_fini_handle(t_handle);
	zonecfg_fini_handle(s_handle);

	return ((err == Z_OK) ? Z_OK : Z_ERR);
}

static int
copy_zone(char *src, char *dst)
{
	boolean_t out_null = B_FALSE;
	int status;
	char *outfile;
	char cmdbuf[MAXPATHLEN * 2 + 128];

	if ((outfile = tempnam("/var/log", "zone")) == NULL) {
		outfile = "/dev/null";
		out_null = B_TRUE;
	}

	/*
	 * Use find to get the list of files to copy.  We need to skip
	 * files of type "socket" since cpio can't handle those but that
	 * should be ok since the app will recreate the socket when it runs.
	 * We also need to filter out anything under the .zfs subdir.  Since
	 * find is running depth-first, we need the extra egrep to filter .zfs.
	 */
	(void) snprintf(cmdbuf, sizeof (cmdbuf),
	    "cd %s && /usr/bin/find . -type s -prune -o -depth -print | "
	    "/usr/bin/egrep -v '^\\./\\.zfs$|^\\./\\.zfs/' | "
	    "/usr/bin/cpio -pdmuP@ %s > %s 2>&1",
	    src, dst, outfile);

	status = do_subproc(cmdbuf);

	if (subproc_status("copy", status, B_TRUE) != ZONE_SUBPROC_OK) {
		if (!out_null)
			(void) fprintf(stderr, gettext("\nThe copy failed.\n"
			    "More information can be found in %s\n"), outfile);
		return (Z_ERR);
	}

	if (!out_null)
		(void) unlink(outfile);

	return (Z_OK);
}

/* ARGSUSED */
int
zfm_print(const struct mnttab *p, void *r) {
	zerror("  %s\n", p->mnt_mountp);
	return (0);
}

int
clone_copy(char *source_zonepath, char *zonepath)
{
	int err;

	/* Don't clone the zone if anything is still mounted there */
	if (zonecfg_find_mounts(source_zonepath, NULL, NULL)) {
		zerror(gettext("These file systems are mounted on "
		    "subdirectories of %s.\n"), source_zonepath);
		(void) zonecfg_find_mounts(source_zonepath, zfm_print, NULL);
		return (Z_ERR);
	}

	/*
	 * Attempt to create a ZFS fs for the zonepath.
	 */
	if (create_zfs_zonepath(zonepath, B_FALSE) != Z_OK) {
		zerror(gettext("failed to create zone path"));
		return (Z_ERR);
	}

	(void) printf(gettext("Copying %s..."), source_zonepath);
	(void) fflush(stdout);

	err = copy_zone(source_zonepath, zonepath);

	(void) printf("\n");

	return (err);
}

static int
clone_func(int argc, char *argv[])
{
	char *source_zone = NULL;
	int lockfd;
	int err, arg;
	char zonepath[MAXPATHLEN];
	char source_zonepath[MAXPATHLEN];
	zone_state_t state;
	zone_entry_t *zent;
	char *method = NULL;
	char *snapshot = NULL;
	char cmdbuf[MAXPATHLEN];
	char postcmdbuf[MAXPATHLEN];
	char presnapbuf[MAXPATHLEN];
	char postsnapbuf[MAXPATHLEN];
	char validsnapbuf[MAXPATHLEN];
	brand_handle_t bh = NULL;
	int status;
	boolean_t brand_help = B_FALSE;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot clone zone in alternate root"));
		return (Z_ERR);
	}

	/* Check the argv string for args we handle internally */
	optind = 0;
	opterr = 0;
	while ((arg = getopt(argc, argv, "?m:s:")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?') {
				sub_usage(SHELP_CLONE, CMD_CLONE);
				brand_help = B_TRUE;
			}
			/* Ignore unknown options - may be brand specific. */
			break;
		case 'm':
			method = optarg;
			break;
		case 's':
			snapshot = optarg;
			break;
		default:
			/* Ignore unknown options - may be brand specific. */
			break;
		}
	}

	if (optind < argc)
		source_zone = argv[argc - 1];
	else {
		sub_usage(SHELP_CLONE, CMD_CLONE);
		return (Z_USAGE);
	}

	if (!brand_help) {
		if (sanity_check(target_zone, CMD_CLONE, B_FALSE, B_TRUE,
		    B_FALSE) != Z_OK)
			return (Z_ERR);
		if (verify_details(CMD_CLONE, argv) != Z_OK)
			return (Z_ERR);

		/*
		 * We also need to do some extra validation on the source zone.
		 */
		if (strcmp(source_zone, GLOBAL_ZONENAME) == 0) {
			zerror(gettext("%s operation is invalid for the "
			    "global zone."), cmd_to_str(CMD_CLONE));
			return (Z_ERR);
		}

		if (strncmp(source_zone, SYS_ZONE_PREFIX,
		    strlen(SYS_ZONE_PREFIX)) == 0) {
			zerror(gettext("%s operation is invalid for zones "
			    "starting with %s."), cmd_to_str(CMD_CLONE),
			    SYS_ZONE_PREFIX);
			return (Z_ERR);
		}

		if (auth_check(username, source_zone, SOURCE_ZONE) == Z_ERR) {
			zerror(gettext("%s operation is invalid because "
			    "user is not authorized to read source zone."),
			    cmd_to_str(CMD_CLONE));
			return (Z_ERR);
		}

		zent = lookup_running_zone(source_zone);
		if (zent != NULL) {
			/* check whether the zone is ready or running */
			if ((err = zone_get_state(zent->zname,
			    &zent->zstate_num)) != Z_OK) {
				errno = err;
				zperror2(zent->zname, gettext("could not get "
				    "state"));
				/* can't tell, so hedge */
				zent->zstate_str = "ready/running";
			} else {
				zent->zstate_str =
				    zone_state_str(zent->zstate_num);
			}
			zerror(gettext("%s operation is invalid for %s zones."),
			    cmd_to_str(CMD_CLONE), zent->zstate_str);
			return (Z_ERR);
		}

		if ((err = zone_get_state(source_zone, &state)) != Z_OK) {
			errno = err;
			zperror2(source_zone, gettext("could not get state"));
			return (Z_ERR);
		}
		if (state != ZONE_STATE_INSTALLED) {
			(void) fprintf(stderr,
			    gettext("%s: zone %s is %s; %s is required.\n"),
			    execname, source_zone, zone_state_str(state),
			    zone_state_str(ZONE_STATE_INSTALLED));
			return (Z_ERR);
		}

		/*
		 * The source zone checks out ok, continue with the clone.
		 */

		if (validate_clone(source_zone, target_zone) != Z_OK)
			return (Z_ERR);

		if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
			zerror(gettext("another %s may have an operation in "
			    "progress."), "zoneadm");
			return (Z_ERR);
		}
	}

	if ((err = zone_get_zonepath(source_zone, source_zonepath,
	    sizeof (source_zonepath))) != Z_OK) {
		errno = err;
		zperror2(source_zone, gettext("could not get zone path"));
		goto done;
	}

	if ((err = zone_get_zonepath(target_zone, zonepath, sizeof (zonepath)))
	    != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not get zone path"));
		goto done;
	}

	/*
	 * Fetch the clone and postclone hooks from the brand configuration.
	 */
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		err = Z_ERR;
		goto done;
	}

	if (get_hook(bh, cmdbuf, sizeof (cmdbuf), brand_get_clone, target_zone,
	    zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing clone resource");
		brand_close(bh);
		err = Z_ERR;
		goto done;
	}

	if (get_hook(bh, postcmdbuf, sizeof (postcmdbuf), brand_get_postclone,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing postclone "
		    "resource");
		brand_close(bh);
		err = Z_ERR;
		goto done;
	}

	if (get_hook(bh, presnapbuf, sizeof (presnapbuf), brand_get_presnap,
	    source_zone, source_zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing presnap "
		    "resource");
		brand_close(bh);
		err = Z_ERR;
		goto done;
	}

	if (get_hook(bh, postsnapbuf, sizeof (postsnapbuf), brand_get_postsnap,
	    source_zone, source_zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing postsnap "
		    "resource");
		brand_close(bh);
		err = Z_ERR;
		goto done;
	}

	if (get_hook(bh, validsnapbuf, sizeof (validsnapbuf),
	    brand_get_validatesnap, target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing validatesnap "
		    "resource");
		brand_close(bh);
		err = Z_ERR;
		goto done;
	}
	brand_close(bh);

	/* Append all options to clone hook. */
	if (addoptions(cmdbuf, argv, sizeof (cmdbuf)) != Z_OK) {
		err = Z_ERR;
		goto done;
	}

	/* Append all options to postclone hook. */
	if (addoptions(postcmdbuf, argv, sizeof (postcmdbuf)) != Z_OK) {
		err = Z_ERR;
		goto done;
	}

	if (!brand_help) {
		if ((err = zone_set_state(target_zone, ZONE_STATE_INCOMPLETE))
		    != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not set state"));
			goto done;
		}
	}

	/* Create the target zone zonepath if it doesn't already exist */
	if (create_zfs_zonepath(zonepath, B_FALSE) != Z_OK) {
		zerror(gettext("failed to create zone path"));
		return (Z_ERR);
	}

	/*
	 * The clone hook is optional.  If it exists, use the hook for
	 * cloning, otherwise use the built-in clone support
	 */
	if (cmdbuf[0] != '\0') {
		/* Run the clone hook */
		status = do_subproc(cmdbuf);
		if ((status = subproc_status(gettext("brand-specific clone"),
		    status, B_FALSE)) != ZONE_SUBPROC_OK) {
			if (status == ZONE_SUBPROC_USAGE && !brand_help)
				sub_usage(SHELP_CLONE, CMD_CLONE);
			err = Z_ERR;
			goto done;
		}

		if (brand_help)
			return (Z_OK);

	} else {
		/* If just help, we're done since there is no brand help. */
		if (brand_help)
			return (Z_OK);

		/* Run the built-in clone support. */

		/* The only explicit built-in method is "copy". */
		if (method != NULL && strcmp(method, "copy") != 0) {
			sub_usage(SHELP_CLONE, CMD_CLONE);
			err = Z_USAGE;
			goto done;
		}

		if (snapshot != NULL) {
			err = clone_snapshot_zfs(snapshot, zonepath,
			    validsnapbuf);
		} else {
			/*
			 * We always copy the clone unless the source is ZFS
			 * and a ZFS clone worked.  We fallback to copying if
			 * the ZFS clone fails for some reason.
			 */
			err = Z_ERR;
			if (method == NULL && is_zonepath_zfs(source_zonepath))
				err = clone_zfs(source_zonepath, zonepath,
				    presnapbuf, postsnapbuf);

			if (err != Z_OK)
				err = clone_copy(source_zonepath, zonepath);
		}
	}

	if (err == Z_OK && postcmdbuf[0] != '\0') {
		status = do_subproc(postcmdbuf);
		if ((err = subproc_status("postclone", status, B_FALSE))
		    != ZONE_SUBPROC_OK) {
			zerror(gettext("post-clone configuration failed."));
			err = Z_ERR;
		}
	}

done:
	/*
	 * If everything went well, we mark the zone as installed.
	 */
	if (err == Z_OK) {
		err = zone_set_state(target_zone, ZONE_STATE_INSTALLED);
		if (err != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not set state"));
		}
	}
	if (!brand_help && status == ZONE_SUBPROC_USAGE) {
		if ((err = zone_set_state(target_zone,
		    ZONE_STATE_CONFIGURED)) != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not set state: "
			    "uninstall required"));
		}
	}
	if (!brand_help)
		zonecfg_release_lock_file(target_zone, lockfd);
	return ((err == Z_OK) ? Z_OK : Z_ERR);
}

/*
 * Used when removing a zonepath after uninstalling or cleaning up after
 * the move subcommand.  This handles a zonepath that has non-standard
 * contents so that we will only cleanup the stuff we know about and leave
 * any user data alone.
 *
 * If the "all" parameter is true then we should remove the whole zonepath
 * even if it has non-standard files/directories in it.  This can be used when
 * we need to cleanup after moving the zonepath across file systems.
 *
 * We "exec" the RMCOMMAND so that the returned status is that of RMCOMMAND
 * and not the shell.
 */
static int
cleanup_zonepath(char *zonepath, boolean_t all)
{
	int		status;
	int		i;
	boolean_t	non_std = B_FALSE;
	struct dirent	*dp;
	DIR		*dirp;
			/*
			 * The SUNWattached.xml file is expected since it might
			 * exist if the zone was force-attached after a
			 * migration.
			 */
	char		*std_entries[] = {"dev", "lu", "root",
			    "SUNWattached.xml", NULL};
			/* (MAXPATHLEN * 3) is for the 3 std_entries dirs */
	char		cmdbuf[sizeof (RMCOMMAND) + (MAXPATHLEN * 3) + 64];
	int		retval = Z_OK;

	/*
	 * We shouldn't need these checks but lets be paranoid since we
	 * could blow away the whole system here if we got the wrong zonepath.
	 */
	if (*zonepath == NULL || strcmp(zonepath, "/") == 0) {
		(void) fprintf(stderr, "invalid zonepath '%s'\n", zonepath);
		return (Z_INVAL);
	}

	/*
	 * If the dirpath is already gone (maybe it was manually removed) then
	 * we just return Z_OK so that the cleanup is successful.
	 */
	if ((dirp = opendir(zonepath)) == NULL)
		return (Z_OK);

	/*
	 * Look through the zonepath directory to see if there are any
	 * non-standard files/dirs.  Also skip .zfs since that might be
	 * there but we'll handle ZFS file systems as a special case.
	 */
	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0 ||
		    strcmp(dp->d_name, ".zfs") == 0)
			continue;

		for (i = 0; std_entries[i] != NULL; i++)
			if (strcmp(dp->d_name, std_entries[i]) == 0)
				break;

		if (std_entries[i] == NULL)
			non_std = B_TRUE;
	}
	(void) closedir(dirp);

	if (!all && non_std) {
		/*
		 * There are extra, non-standard directories/files in the
		 * zonepath so we don't want to remove the zonepath.  We
		 * just want to remove the standard directories and leave
		 * the user data alone.
		 */
		(void) snprintf(cmdbuf, sizeof (cmdbuf), "exec " RMCOMMAND);

		for (i = 0; std_entries[i] != NULL; i++) {
			char tmpbuf[MAXPATHLEN];

			if (snprintf(tmpbuf, sizeof (tmpbuf), " %s/%s",
			    zonepath, std_entries[i]) >= sizeof (tmpbuf) ||
			    strlcat(cmdbuf, tmpbuf, sizeof (cmdbuf)) >=
			    sizeof (cmdbuf)) {
				(void) fprintf(stderr,
				    gettext("path is too long\n"));
				return (Z_INVAL);
			}
		}

		status = do_subproc(cmdbuf);

		(void) fprintf(stderr, gettext("WARNING: Unable to completely "
		    "remove %s\nbecause it contains additional user data.  "
		    "Only the standard directory\nentries have been "
		    "removed.\n"),
		    zonepath);

		return ((subproc_status(RMCOMMAND, status, B_TRUE) ==
		    ZONE_SUBPROC_OK) ? Z_OK : Z_ERR);
	}

	/*
	 * There is nothing unexpected in the zonepath, try to get rid of the
	 * whole zonepath dataset.
	 */
	if (is_zonepath_zfs(zonepath)) {
		if (destroy_zfs_zonepath(zonepath) != Z_OK) {
			zerror(gettext("could not destroy zone datasets"));
			retval = Z_ERR;
		}
		/* Perhaps it failed because the dataset didn't exist. */
		if (rmdir(zonepath)) {
			zperror(gettext("could not remove zonepath"), B_FALSE);
			retval = Z_ERR;
		}
	} else {
		zerror(gettext("zonepath is not on ZFS"));
		retval = Z_ERR;
	}

	return (retval);
}

static int
move_func(int argc, char *argv[])
{
	char new_zonepath[MAXPATHLEN];
	int lockfd;
	int err = Z_ERR;
	int arg;
	char zonepath[MAXPATHLEN];
	zone_dochandle_t handle = NULL;
	struct dirent *dp;
	struct stat new_zonepath_buf;
	brand_handle_t bh = NULL;
	char postcmdbuf[MAXPATHLEN];

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot move zone in alternate root"));
		return (Z_ERR);
	}

	optind = 0;
	if ((arg = getopt(argc, argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_MOVE, CMD_MOVE);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_MOVE, CMD_MOVE);
			return (Z_USAGE);
		}
	}
	if (argc != (optind + 1)) {
		sub_usage(SHELP_MOVE, CMD_MOVE);
		return (Z_USAGE);
	}

	if ((errno = zonecfg_simplify_path(argv[optind], new_zonepath,
	    sizeof (new_zonepath))) != Z_OK) {
		zperror(argv[optind], B_TRUE);
		return (Z_ERR);
	}

	/*
	 * Grab the lock file so nothing about the zone changes between the
	 * time that sanity checks occur and we eventually do something to
	 * the zone.
	 */
	if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
		zerror(gettext("another %s may have an operation in progress."),
		    "zoneadm");
		return (Z_ERR);
	}

	if (sanity_check(target_zone, CMD_MOVE, B_FALSE, B_TRUE, B_FALSE)
	    != Z_OK) {
		goto errout;
	}
	if (verify_details(CMD_MOVE, argv) != Z_OK) {
		goto errout;
	}

	if ((errno = zone_get_zonepath(target_zone, zonepath,
	    sizeof (zonepath))) != Z_OK) {
		zperror2(target_zone, gettext("could not get zone path"));
		goto errout;
	}

	if (!is_zonepath_zfs(zonepath)) {
		zerror(gettext("zonepath '%s' is not the root of a mounted "
		    "zfs file system."), zonepath);
		goto errout;
	}

	/* Create the ancestor of the new zonepath, if needed */
	if (create_zfs_zonepath(new_zonepath, B_TRUE) != Z_OK) {
		zerror(gettext("could not create parent dataset of new "
		    "zonepath"));
		goto errout;
	}

	/*
	 * If the new zonepath already exists, be sure it is an empty
	 * directory and not a mount point.  If it doesn't exist, create it
	 * as an empty directory - move_zfs() relies on this.
	 */
	if (lstat(new_zonepath, &new_zonepath_buf) == -1) {
		if (errno != ENOENT) {
			zperror(gettext("could not stat new zone path"),
			    B_FALSE);
			goto errout;
		}
		if (mkdir(new_zonepath, 0755) != 0) {
			zperror(gettext("could not create new zone path"),
			    B_FALSE);
			goto errout;
		}
	} else {
		DIR *dirp;
		boolean_t empty = B_TRUE;

		if (S_ISLNK(new_zonepath_buf.st_mode)) {
			zerror(gettext("zonepath cannot be a symbolic link"));
			goto errout;
		}
		if (!S_ISDIR(new_zonepath_buf.st_mode)) {
			zerror(gettext("zonepath must be a directory"));
			goto errout;
		}

		if ((dirp = opendir(new_zonepath)) == NULL) {
			zperror(gettext("could not open new zone path"),
			    B_FALSE);
			goto errout;
		}
		while ((dp = readdir(dirp)) != (struct dirent *)0) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0 ||
			    strcmp(dp->d_name, ".zfs") == 0)
				continue;
			empty = B_FALSE;
			break;
		}
		(void) closedir(dirp);

		/* Error if there is anything in the destination directory. */
		if (!empty) {
			zerror(gettext("could not move zone to %s: directory "
			    "not empty"), new_zonepath);
			goto errout;
		}
	}

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_MOVE), B_TRUE);
		goto errout;
	}

	if ((errno = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
		zperror(cmd_to_str(CMD_MOVE), B_TRUE);
		goto errout;
	}

	/*
	 * Fetch the postmove hook from the brand configuration.
	 */
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		goto errout;
	}

	if (get_hook(bh, postcmdbuf, sizeof (postcmdbuf), brand_get_postmove,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing postmove "
		    "resource");
		goto errout;
	}
	brand_close(bh);
	bh = NULL;

	if ((err = move_zfs(zonepath, new_zonepath)) != Z_OK) {
		/* Error message already printed */
		goto errout;
	}

	if ((err = zonecfg_set_zonepath(handle, new_zonepath)) != Z_OK) {
		errno = err;
		zperror(gettext("could not set new zonepath"), B_TRUE);
		goto errout;
	}

	if ((err = zonecfg_save(handle)) != Z_OK) {
		errno = err;
		zperror(gettext("zonecfg save failed"), B_TRUE);
		goto errout;
	}

	if (err == Z_OK && postcmdbuf[0] != '\0') {
		int status = do_subproc(postcmdbuf);
		if ((err = subproc_status("postmove", status, B_FALSE))
		    != ZONE_SUBPROC_OK)
			zerror(gettext("post-move configuration failed."),
			    B_TRUE);
	}

errout:
	zonecfg_release_lock_file(target_zone, lockfd);
	if (handle != NULL)
		zonecfg_fini_handle(handle);
	return ((err == Z_OK) ? Z_OK : Z_ERR);
}

/* ARGSUSED */
static int
detach_func(int argc, char *argv[])
{
	int lockfd = -1;
	int err, arg, status;
	char zonepath[MAXPATHLEN];
	char cmdbuf[MAXPATHLEN];
	char precmdbuf[MAXPATHLEN];
	boolean_t force = B_FALSE;
	boolean_t brand_specific = B_FALSE;
	boolean_t execute = B_TRUE;
	boolean_t brand_help = B_FALSE;
	brand_handle_t bh = NULL;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot detach zone in alternate root"));
		return (Z_ERR);
	}

	/* Check the argv string for args we handle internally */
	optind = 0;
	opterr = 0;
	while ((arg = getopt(argc, argv, "?Fn")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?') {
				sub_usage(SHELP_DETACH, CMD_DETACH);
				brand_help = B_TRUE;
			}
			/*
			 * If brand scripts will be called, ignore unknown
			 * options as they may be brand specific.
			 */
			brand_specific = B_TRUE;
			break;
		case 'F':
			force = B_TRUE;
			break;
		case 'n':
			execute = B_FALSE;
			break;
		default:
			/*
			 * If brand scripts will be called, ignore unknown
			 * options as they may be brand specific.
			 */
			brand_specific = B_TRUE;
			break;
		}
	}

	if (brand_help)
		execute = B_FALSE;

	/*
	 * A force detach does not call any of the brand scripts - it just
	 * sets the zone state to configured.
	 */
	if (force) {
		if (brand_specific) {
			zerror(gettext("-F option and brand-specific options "
			    "are incompatible"));
			return (Z_ERR);
		}
		if (!execute) {
			zerror(gettext("-F option is incompatible with -n and "
			    "-? options"));
			return (Z_ERR);
		}
		if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
			zerror(gettext("another %s may have an operation in "
			    "progress."), "zoneadm");
			return (Z_ERR);
		}
		if ((err = sanity_check(target_zone, CMD_DETACH, B_FALSE,
		    B_TRUE, B_FALSE)) != Z_OK)
			goto done;
		if ((err = zone_set_state(target_zone,
		    ZONE_STATE_CONFIGURED)) != Z_OK) {
			errno = err;
			zperror(gettext("could not reset state"), B_TRUE);
		}
		goto done;
	}

	if (execute) {
		if (sanity_check(target_zone, CMD_DETACH, B_FALSE, B_TRUE,
		    B_FALSE) != Z_OK)
			return (Z_ERR);
		if (verify_details(CMD_DETACH, argv) != Z_OK) {
			/*
			 * Just before this the message was displayed:
			 * "could not verify zonepath <path> because of the
			 * above errors."
			 */
			zerror(gettext("the -F option may be used to force "
			    "detach"));
			return (Z_ERR);
		}
		/* Invoke brand-specific callback. */
		if (invoke_brand_handler(CMD_DETACH, argv) != Z_OK)
			return (Z_ERR);
	} else {
		/*
		 * We want a dry-run to work for a non-privileged user so we
		 * only do minimal validation.
		 */
		if (target_zone == NULL) {
			zerror(gettext("no zone specified"));
			return (Z_ERR);
		}

		if (strcmp(target_zone, GLOBAL_ZONENAME) == 0) {
			zerror(gettext("%s operation is invalid for the "
			    "global zone."), cmd_to_str(CMD_DETACH));
			return (Z_ERR);
		}
	}

	if ((err = zone_get_zonepath(target_zone, zonepath, sizeof (zonepath)))
	    != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not get zone path"));
		return (Z_ERR);
	}

	/* Fetch the detach and predetach hooks from the brand configuration. */
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		return (Z_ERR);
	}

	if (get_hook(bh, cmdbuf, sizeof (cmdbuf), brand_get_detach, target_zone,
	    zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing detach resource");
		brand_close(bh);
		return (Z_ERR);
	}

	if (get_hook(bh, precmdbuf, sizeof (precmdbuf), brand_get_predetach,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing predetach "
		    "resource");
		brand_close(bh);
		return (Z_ERR);
	}
	brand_close(bh);

	/* Append all options to predetach hook. */
	if (addoptions(precmdbuf, argv, sizeof (precmdbuf)) != Z_OK)
		return (Z_ERR);

	/* Append all options to detach hook. */
	if (addoptions(cmdbuf, argv, sizeof (cmdbuf)) != Z_OK)
		return (Z_ERR);

	if (execute && zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
		zerror(gettext("another %s may have an operation in progress."),
		    "zoneadm");
		return (Z_ERR);
	}

	/* If we have a brand predetach hook, run it. */
	if (!brand_help && precmdbuf[0] != '\0') {
		status = do_subproc(precmdbuf);
		if (subproc_status(gettext("brand-specific predetach"),
		    status, B_FALSE) != ZONE_SUBPROC_OK) {

			if (execute) {
				assert(lockfd >= 0);
				zonecfg_release_lock_file(target_zone, lockfd);
				lockfd = -1;
			}

			assert(lockfd == -1);
			return (Z_ERR);
		}
	}

	if (cmdbuf[0] != '\0') {
		/* Run the detach hook */
		status = do_subproc(cmdbuf);
		if ((status = subproc_status(gettext("brand-specific detach"),
		    status, B_FALSE)) != ZONE_SUBPROC_OK) {
			if (status == ZONE_SUBPROC_USAGE && !brand_help)
				sub_usage(SHELP_DETACH, CMD_DETACH);

			if (execute) {
				assert(lockfd >= 0);
				zonecfg_release_lock_file(target_zone, lockfd);
				lockfd = -1;
			}

			assert(lockfd == -1);
			return (Z_ERR);
		}

	} else {
		zone_dochandle_t handle;

		/* If just help, we're done since there is no brand help. */
		if (brand_help) {
			assert(lockfd == -1);
			return (Z_OK);
		}

		/*
		 * Run the built-in detach support.  Just generate a simple
		 * zone definition XML file and detach.
		 */

		/* Don't detach the zone if anything is still mounted there */
		if (execute && zonecfg_find_mounts(zonepath, NULL, NULL)) {
			(void) fprintf(stderr, gettext("These file systems are "
			    "mounted on subdirectories of %s.\n"), zonepath);
			(void) zonecfg_find_mounts(zonepath, zfm_print, NULL);
			err = ZONE_SUBPROC_NOTCOMPLETE;
			goto done;
		}

		if ((handle = zonecfg_init_handle()) == NULL) {
			zperror(cmd_to_str(CMD_DETACH), B_TRUE);
			err = ZONE_SUBPROC_NOTCOMPLETE;
			goto done;
		}

		if ((err = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
			errno = err;
			zperror(cmd_to_str(CMD_DETACH), B_TRUE);

		} else if ((err = zonecfg_detach_save(handle,
		    (execute ? 0 : ZONE_DRY_RUN))) != Z_OK) {
			errno = err;
			zperror(gettext("saving the detach manifest failed"),
			    B_TRUE);
		}

		zonecfg_fini_handle(handle);
		if (err != Z_OK)
			goto done;
	}

	/*
	 * Set the zone state back to configured unless we are running with the
	 * no-execute option.
	 */
	if (execute && (err = zone_set_state(target_zone,
	    ZONE_STATE_CONFIGURED)) != Z_OK) {
		errno = err;
		zperror(gettext("could not reset state"), B_TRUE);
	}

done:
	if (execute) {
		assert(lockfd >= 0);
		zonecfg_release_lock_file(target_zone, lockfd);
		lockfd = -1;
	}

	assert(lockfd == -1);
	return ((err == Z_OK) ? Z_OK : Z_ERR);
}

/*
 * Determine the brand when doing a dry-run attach.  The zone does not have to
 * exist, so we have to read the incoming manifest to determine the zone's
 * brand.
 *
 * Because the manifest has to be processed twice; once to determine the brand
 * and once to do the brand-specific attach logic, we always read it into a tmp
 * file.  This handles the manifest coming from stdin or a regular file.  The
 * tmpname parameter returns the name of the temporary file that the manifest
 * was read into.
 */
static int
dryrun_get_brand(char *manifest_path, char *tmpname, int size)
{
	int fd;
	int err;
	int res = Z_OK;
	zone_dochandle_t local_handle;
	zone_dochandle_t rem_handle = NULL;
	int len;
	int ofd;
	char buf[512];

	if (strcmp(manifest_path, "-") == 0) {
		fd = STDIN_FILENO;
	} else {
		if ((fd = open(manifest_path, O_RDONLY)) < 0) {
			if (getcwd(buf, sizeof (buf)) == NULL)
				(void) strlcpy(buf, "/", sizeof (buf));
			zerror(gettext("could not open manifest path %s%s: %s"),
			    (*manifest_path == '/' ? "" : buf), manifest_path,
			    strerror(errno));
			return (Z_ERR);
		}
	}

	(void) snprintf(tmpname, size, _PATH_SYSVOL "/zone.%d", getpid());

	if ((ofd = open(tmpname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		zperror(gettext("could not save manifest"), B_FALSE);
		(void) close(fd);
		return (Z_ERR);
	}

	while ((len = read(fd, buf, sizeof (buf))) > 0) {
		if (write(ofd, buf, len) == -1) {
			zperror(gettext("could not save manifest"), B_FALSE);
			(void) close(ofd);
			(void) close(fd);
			return (Z_ERR);
		}
	}

	if (close(ofd) != 0) {
		zperror(gettext("could not save manifest"), B_FALSE);
		(void) close(fd);
		return (Z_ERR);
	}

	(void) close(fd);

	if ((fd = open(tmpname, O_RDONLY)) < 0) {
		zperror(gettext("could not open manifest path"), B_FALSE);
		return (Z_ERR);
	}

	if ((local_handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_ATTACH), B_TRUE);
		res = Z_ERR;
		goto done;
	}

	if ((rem_handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_ATTACH), B_TRUE);
		res = Z_ERR;
		goto done;
	}

	if ((err = zonecfg_attach_manifest(fd, local_handle, rem_handle))
	    != Z_OK) {
		res = Z_ERR;

		if (err == Z_INVALID_DOCUMENT) {
			struct stat st;
			char buf[6];

			if (strcmp(manifest_path, "-") == 0) {
				zerror(gettext("Input is not a valid XML "
				    "file"));
				goto done;
			}

			if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
				zerror(gettext("%s is not an XML file"),
				    manifest_path);
				goto done;
			}

			bzero(buf, sizeof (buf));
			(void) lseek(fd, 0L, SEEK_SET);
			if (read(fd, buf, sizeof (buf) - 1) < 0 ||
			    strncmp(buf, "<?xml", 5) != 0)
				zerror(gettext("%s is not an XML file"),
				    manifest_path);
			else
				zerror(gettext("Cannot attach to an earlier "
				    "release of the operating system"));
		} else {
			zperror(cmd_to_str(CMD_ATTACH), B_TRUE);
		}
		goto done;
	}

	/* Retrieve remote handle brand type. */
	if (zonecfg_get_brand(rem_handle, target_brand, sizeof (target_brand))
	    != Z_OK) {
		zerror(gettext("missing or invalid brand"));
		exit(Z_ERR);
	}

done:
	zonecfg_fini_handle(local_handle);
	zonecfg_fini_handle(rem_handle);
	(void) close(fd);

	return ((res == Z_OK) ? Z_OK : Z_ERR);
}

/* ARGSUSED */
static int
attach_func(int argc, char *argv[])
{
	int lockfd = -1;
	int err, arg;
	boolean_t force = B_FALSE;
	zone_dochandle_t handle;
	char zonepath[MAXPATHLEN];
	char cmdbuf[MAXPATHLEN];
	char postcmdbuf[MAXPATHLEN];
	boolean_t execute = B_TRUE;
	boolean_t brand_help = B_FALSE;
	char *manifest_path;
	char tmpmanifest[80];
	int manifest_pos;
	brand_handle_t bh = NULL;
	int status;
	int last_index = 0;
	int offset;
	char *up;
	boolean_t forced_update = B_FALSE;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot attach zone in alternate root"));
		return (Z_ERR);
	}

	/* Check the argv string for args we handle internally */
	optind = 0;
	opterr = 0;
	while ((arg = getopt(argc, argv, "?Fn:X")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?') {
				sub_usage(SHELP_ATTACH, CMD_ATTACH);
				brand_help = B_TRUE;
			}
			/* Ignore unknown options - may be brand specific. */
			break;
		case 'F':
			force = B_TRUE;
			break;
		case 'n':
			execute = B_FALSE;
			manifest_path = optarg;
			manifest_pos = optind - 1;
			break;
		case 'X':
			/*
			 * Undocumented 'force update' option for p2v update on
			 * attach when zone is in the incomplete state.  Change
			 * the option back to 'u' and set forced_update flag.
			 */
			if (optind == last_index)
				offset = optind;
			else
				offset = optind - 1;
			if ((up = index(argv[offset], 'X')) != NULL)
				*up = 'u';
			forced_update = B_TRUE;
			break;
		default:
			/* Ignore unknown options - may be brand specific. */
			break;
		}
		last_index = optind;
	}

	if (brand_help) {
		force = B_FALSE;
		execute = B_TRUE;
	}

	/* dry-run and force flags are mutually exclusive */
	if (!execute && force) {
		zerror(gettext("-F and -n flags are mutually exclusive"));
		return (Z_ERR);
	}

	/*
	 * If the no-execute option was specified, we don't do validation and
	 * need to figure out the brand, since there is no zone required to be
	 * configured for this option.
	 */
	if (execute) {
		if (!brand_help) {
			if (sanity_check(target_zone, CMD_ATTACH, B_FALSE,
			    B_TRUE, forced_update) != Z_OK)
				return (Z_ERR);
			if (verify_details(CMD_ATTACH, argv) != Z_OK)
				return (Z_ERR);
		}

		if ((err = zone_get_zonepath(target_zone, zonepath,
		    sizeof (zonepath))) != Z_OK) {
			errno = err;
			zperror2(target_zone,
			    gettext("could not get zone path"));
			return (Z_ERR);
		}
	} else {
		if (dryrun_get_brand(manifest_path, tmpmanifest,
		    sizeof (tmpmanifest)) != Z_OK)
			return (Z_ERR);

		argv[manifest_pos] = tmpmanifest;
		target_zone = "-";
		(void) strlcpy(zonepath, "-", sizeof (zonepath));

		/* Run the brand's verify_adm hook. */
		if (verify_brand(NULL, CMD_ATTACH, argv) != Z_OK)
			return (Z_ERR);
	}

	/*
	 * Fetch the attach and postattach hooks from the brand configuration.
	 */
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		return (Z_ERR);
	}

	if (get_hook(bh, cmdbuf, sizeof (cmdbuf), brand_get_attach, target_zone,
	    zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing attach resource");
		brand_close(bh);
		return (Z_ERR);
	}

	if (get_hook(bh, postcmdbuf, sizeof (postcmdbuf), brand_get_postattach,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing postattach "
		    "resource");
		brand_close(bh);
		return (Z_ERR);
	}
	brand_close(bh);

	/* Append all options to attach hook. */
	if (addoptions(cmdbuf, argv, sizeof (cmdbuf)) != Z_OK)
		return (Z_ERR);

	/* Append all options to postattach hook. */
	if (addoptions(postcmdbuf, argv, sizeof (postcmdbuf)) != Z_OK)
		return (Z_ERR);

	if (execute && !brand_help) {
		if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
			zerror(gettext("another %s may have an operation in "
			    "progress."), "zoneadm");
			return (Z_ERR);
		}
		if (create_zfs_zonepath(zonepath, B_FALSE) != Z_OK) {
			zerror(gettext("failed to create zone path"));
			return (Z_ERR);
		}

		/*
		 * Now we can validate that the zonepath exists.  If it is
		 * a dry run, the zonepath was set to "-" above, so there
		 * is no way to validate it.
		 */
		if (validate_zonepath(zonepath, CMD_ATTACH) != Z_OK) {
			(void) fprintf(stderr, gettext("could not verify "
			    "zonepath %s because of the above errors.\n"),
			    zonepath);

			assert(zonecfg_lock_file_held(&lockfd));
			zonecfg_release_lock_file(target_zone, lockfd);
			return (Z_ERR);
		}
	}

	if (!force) {
		/*
		 * Not a force-attach, so we need to actually do the work.
		 */
		if (cmdbuf[0] != '\0') {
			/* Run the attach hook */
			status = do_subproc(cmdbuf);
			if ((status = subproc_status(gettext("brand-specific "
			    "attach"), status, B_FALSE)) != ZONE_SUBPROC_OK) {
				if (status == ZONE_SUBPROC_USAGE &&
				    !brand_help) {
					sub_usage(SHELP_ATTACH, CMD_ATTACH);
					if ((err = zone_set_state(target_zone,
					    ZONE_STATE_CONFIGURED)) != Z_OK) {
						errno = err;
						zperror2(target_zone,
						    gettext("could not set "
						    "state: uninstall "
						    "required"));
					}
				}
				if (status == ZONE_SUBPROC_DETACHED &&
				    !brand_help) {
					zerror(gettext("NOTICE: attach "
					    "partially succeeded"));
					set_detached();
				}

				if (execute && !brand_help) {
					assert(zonecfg_lock_file_held(&lockfd));
					zonecfg_release_lock_file(target_zone,
					    lockfd);
					lockfd = -1;
				}

				assert(lockfd == -1);
				return (Z_ERR);
			}
		}

		/*
		 * Else run the built-in attach support.
		 * This is a no-op since there is nothing to validate.
		 */

		/* If dry-run or help, then we're done. */
		if (!execute || brand_help) {
			if (!execute)
				(void) unlink(tmpmanifest);
			assert(lockfd == -1);
			return (Z_OK);
		}
	}

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_ATTACH), B_TRUE);
		err = Z_ERR;
	} else if ((err = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_ATTACH), B_TRUE);
		zonecfg_fini_handle(handle);
	} else {
		zonecfg_rm_detached(handle, force);
		zonecfg_fini_handle(handle);
	}

	if (err == Z_OK &&
	    (err = zone_set_state(target_zone, ZONE_STATE_INSTALLED)) != Z_OK) {
		errno = err;
		zperror(gettext("could not reset state"), B_TRUE);
	}

	assert(zonecfg_lock_file_held(&lockfd));
	zonecfg_release_lock_file(target_zone, lockfd);
	lockfd = -1;

	/* If we have a brand postattach hook, run it. */
	if (err == Z_OK && !force && postcmdbuf[0] != '\0') {
		status = do_subproc(postcmdbuf);
		if (subproc_status(gettext("brand-specific postattach"),
		    status, B_FALSE) != ZONE_SUBPROC_OK) {
			if ((err = zone_set_state(target_zone,
			    ZONE_STATE_CONFIGURED)) != Z_OK) {
				errno = err;
				zperror(gettext("could not reset state"),
				    B_TRUE);
			}
		}
	}

	assert(lockfd == -1);
	return ((err == Z_OK) ? Z_OK : Z_ERR);
}

/*
 * On input, TRUE => yes, FALSE => no.
 * On return, TRUE => 1, FALSE => 0, could not ask => -1.
 */

static int
ask_yesno(boolean_t default_answer, const char *question)
{
	char line[64];	/* should be large enough to answer yes or no */

	if (!isatty(STDIN_FILENO))
		return (-1);
	for (;;) {
		(void) printf("%s (%s)? ", question,
		    default_answer ? "[y]/n" : "y/[n]");
		if (fgets(line, sizeof (line), stdin) == NULL ||
		    line[0] == '\n')
			return (default_answer ? 1 : 0);
		if (tolower(line[0]) == 'y')
			return (1);
		if (tolower(line[0]) == 'n')
			return (0);
	}
}

/* ARGSUSED */
static int
uninstall_func(int argc, char *argv[])
{
	char line[ZONENAME_MAX + 128];	/* Enough for "Are you sure ..." */
	char rootpath[MAXPATHLEN], zonepath[MAXPATHLEN];
	char cmdbuf[MAXPATHLEN];
	char precmdbuf[MAXPATHLEN];
	boolean_t force = B_FALSE;
	int lockfd, answer;
	int err, arg;
	boolean_t brand_help = B_FALSE;
	brand_handle_t bh = NULL;
	int status;

	if (zonecfg_in_alt_root()) {
		zerror(gettext("cannot uninstall zone in alternate root"));
		return (Z_ERR);
	}

	/* Check the argv string for args we handle internally */
	optind = 0;
	opterr = 0;
	while ((arg = getopt(argc, argv, "?F")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?') {
				sub_usage(SHELP_UNINSTALL, CMD_UNINSTALL);
				brand_help = B_TRUE;
			}
			/* Ignore unknown options - may be brand specific. */
			break;
		case 'F':
			force = B_TRUE;
			break;
		default:
			/* Ignore unknown options - may be brand specific. */
			break;
		}
	}

	if (!brand_help) {
		if (sanity_check(target_zone, CMD_UNINSTALL, B_FALSE, B_TRUE,
		    B_FALSE) != Z_OK)
			return (Z_ERR);

		/*
		 * Invoke brand-specific handler.
		 */
		if (invoke_brand_handler(CMD_UNINSTALL, argv) != Z_OK)
			return (Z_ERR);

		if (!force) {
			(void) snprintf(line, sizeof (line),
			    gettext("Are you sure you want to %s zone %s"),
			    cmd_to_str(CMD_UNINSTALL), target_zone);
			if ((answer = ask_yesno(B_FALSE, line)) == 0) {
				return (Z_OK);
			} else if (answer == -1) {
				zerror(gettext("Input not from terminal and -F "
				    "not specified: %s not done."),
				    cmd_to_str(CMD_UNINSTALL));
				return (Z_ERR);
			}
		}
	}

	if ((err = zone_get_zonepath(target_zone, zonepath,
	    sizeof (zonepath))) != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not get zone path"));
		return (Z_ERR);
	}

	/*
	 * Fetch the uninstall and preuninstall hooks from the brand
	 * configuration.
	 */
	if ((bh = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand"));
		return (Z_ERR);
	}

	if (get_hook(bh, cmdbuf, sizeof (cmdbuf), brand_get_uninstall,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing uninstall "
		    "resource");
		brand_close(bh);
		return (Z_ERR);
	}

	if (get_hook(bh, precmdbuf, sizeof (precmdbuf), brand_get_preuninstall,
	    target_zone, zonepath) != Z_OK) {
		zerror("invalid brand configuration: missing preuninstall "
		    "resource");
		brand_close(bh);
		return (Z_ERR);
	}
	brand_close(bh);

	/* Append all options to preuninstall hook. */
	if (addoptions(precmdbuf, argv, sizeof (precmdbuf)) != Z_OK)
		return (Z_ERR);

	/* Append all options to uninstall hook. */
	if (addoptions(cmdbuf, argv, sizeof (cmdbuf)) != Z_OK)
		return (Z_ERR);

	if (!brand_help) {
		if ((err = zone_get_rootpath(target_zone, rootpath,
		    sizeof (rootpath))) != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not get root "
			    "path"));
			return (Z_ERR);
		}

		/*
		 * If there seems to be a zoneadmd running for this zone, call
		 * it to tell it that an uninstall is happening; if all goes
		 * well it will then shut itself down.
		 */
		if (zonecfg_ping_zoneadmd(target_zone) == Z_OK) {
			/* we don't care too much if this fails, just plow on */
			(void) call_zoneadmd(Z_NOTE_UNINSTALLING,
			    Z_CMD_FLAG_NONE, B_TRUE);
		}

		if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
			zerror(gettext("another %s may have an operation in "
			    "progress."), "zoneadm");
			return (Z_ERR);
		}

		/*
		 * Don't uninstall the zone if anything outside of the boot
		 * environment is mounted there.
		 */
		if ((err = warn_nonbe_mounts(rootpath)) != 0) {
			zonecfg_release_lock_file(target_zone, lockfd);
			return (Z_ERR);
		}
	}

	/* If we have a brand preuninstall hook, run it. */
	if (!brand_help && precmdbuf[0] != '\0') {
		status = do_subproc(precmdbuf);
		if (subproc_status(gettext("brand-specific preuninstall"),
		    status, B_FALSE) != ZONE_SUBPROC_OK) {
			zonecfg_release_lock_file(target_zone, lockfd);
			return (Z_ERR);
		}
	}

	if (!brand_help) {
		err = zone_set_state(target_zone, ZONE_STATE_INCOMPLETE);
		if (err != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("could not set state"));
			goto bad;
		}
	}

	/*
	 * If there is a brand uninstall hook, use it, otherwise use the
	 * built-in uninstall code.
	 */
	if (cmdbuf[0] != '\0') {
		/* Run the uninstall hook */
		status = do_subproc(cmdbuf);
		if ((status = subproc_status(gettext("brand-specific "
		    "uninstall"), status, B_FALSE)) != ZONE_SUBPROC_OK) {
			if (status == ZONE_SUBPROC_USAGE && !brand_help)
				sub_usage(SHELP_UNINSTALL, CMD_UNINSTALL);
			if (!brand_help)
				zonecfg_release_lock_file(target_zone, lockfd);
			return (Z_ERR);
		}

		if (brand_help)
			return (Z_OK);
	} else {
		/* If just help, we're done since there is no brand help. */
		if (brand_help)
			return (Z_OK);

		/* Run the built-in uninstall support. */
		if ((err = cleanup_zonepath(zonepath, B_FALSE)) != Z_OK) {
			errno = err;
			zperror2(target_zone, gettext("cleaning up zonepath "
			    "failed"));
			goto bad;
		}
	}

	err = zone_set_state(target_zone, ZONE_STATE_CONFIGURED);
	if (err != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not reset state"));
	}
bad:
	zonecfg_release_lock_file(target_zone, lockfd);
	return (err);
}

/* ARGSUSED */
static int
mount_func(int argc, char *argv[])
{
	uint32_t flags = Z_CMD_FLAG_NONE;
	int arg;

	/*
	 * The only supported subargument to the "mount" subcommand is
	 * "-f", which forces us to mount a zone in the INCOMPLETE state.
	 */
	optind = 0;
	while ((arg = getopt(argc, argv, "f")) != EOF) {
		switch (arg) {
		case 'f':
			flags |= Z_CMD_FLAG_FORCE;
			break;
		default:
			return (Z_USAGE);
		}
	}
	if (argc > optind)
		return (Z_USAGE);

	if (sanity_check(target_zone, CMD_MOUNT, B_FALSE, B_FALSE,
	    flags & Z_CMD_FLAG_FORCE) != Z_OK)
		return (Z_ERR);
	if (verify_details(CMD_MOUNT, argv) != Z_OK)
		return (Z_ERR);

	return (call_zoneadmd(Z_MOUNT, flags, B_FALSE));
}

/* ARGSUSED */
static int
unmount_func(int argc, char *argv[])
{
	if (argc > 0)
		return (Z_USAGE);
	if (sanity_check(target_zone, CMD_UNMOUNT, B_FALSE, B_FALSE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);

	return (call_zoneadmd(Z_UNMOUNT, Z_CMD_FLAG_NONE, B_FALSE));
}

static int
mark_func(int argc, char *argv[])
{
	int err, lockfd;
	int arg;
	boolean_t force = B_FALSE;
	int state;

	optind = 0;
	opterr = 0;
	while ((arg = getopt(argc, argv, "F")) != EOF) {
		switch (arg) {
		case 'F':
			force = B_TRUE;
			break;
		default:
			return (Z_USAGE);
		}
	}

	if (argc != (optind + 1))
		return (Z_USAGE);

	if (strcmp(argv[optind], "configured") == 0)
		state = ZONE_STATE_CONFIGURED;
	else if (strcmp(argv[optind], "incomplete") == 0)
		state = ZONE_STATE_INCOMPLETE;
	else if (strcmp(argv[optind], "installed") == 0)
		state = ZONE_STATE_INSTALLED;
	else
		return (Z_USAGE);

	if (state != ZONE_STATE_INCOMPLETE && !force)
		return (Z_USAGE);

	if (sanity_check(target_zone, CMD_MARK, B_FALSE, B_TRUE, B_FALSE)
	    != Z_OK)
		return (Z_ERR);

	/*
	 * Invoke brand-specific handler.
	 */
	if (invoke_brand_handler(CMD_MARK, argv) != Z_OK)
		return (Z_ERR);

	if (zonecfg_grab_lock_file(target_zone, &lockfd) != Z_OK) {
		zerror(gettext("another %s may have an operation in progress."),
		    "zoneadm");
		return (Z_ERR);
	}

	err = zone_set_state(target_zone, state);
	if (err != Z_OK) {
		errno = err;
		zperror2(target_zone, gettext("could not set state"));
	}
	zonecfg_release_lock_file(target_zone, lockfd);

	return (err);
}

/*
 * Check what scheduling class we're running under and print a warning if
 * we're not using FSS.
 */
static int
check_sched_fss(zone_dochandle_t handle)
{
	char class_name[PC_CLNMSZ];

	if (zonecfg_get_dflt_sched_class(handle, class_name,
	    sizeof (class_name)) != Z_OK) {
		zerror(gettext("WARNING: unable to determine the zone's "
		    "scheduling class"));
	} else if (strcmp("FSS", class_name) != 0) {
		zerror(gettext("WARNING: The zone.cpu-shares rctl is set but\n"
		    "FSS is not the default scheduling class for this zone.  "
		    "FSS will be\nused for processes in the zone but to get "
		    "the full benefit of FSS,\nit should be the default "
		    "scheduling class.  See dispadmin(1M) for\nmore details."));
		return (Z_SYSTEM);
	}

	return (Z_OK);
}

static int
check_cpu_shares_sched(zone_dochandle_t handle)
{
	int err;
	int res = Z_OK;
	struct zone_rctltab rctl;

	if ((err = zonecfg_setrctlent(handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_APPLY), B_TRUE);
		return (err);
	}

	while (zonecfg_getrctlent(handle, &rctl) == Z_OK) {
		if (strcmp(rctl.zone_rctl_name, "zone.cpu-shares") == 0) {
			if (check_sched_fss(handle) != Z_OK)
				res = Z_SYSTEM;
			break;
		}
	}

	(void) zonecfg_endrctlent(handle);

	return (res);
}

/*
 * Check if there is a mix of processes running in different pools within the
 * zone.  This is currently only going to be called for the global zone from
 * apply_func but that could be generalized in the future.
 */
static boolean_t
mixed_pools(zoneid_t zoneid)
{
	DIR *dirp;
	dirent_t *dent;
	boolean_t mixed = B_FALSE;
	boolean_t poolid_set = B_FALSE;
	poolid_t last_poolid = 0;

	if ((dirp = opendir("/proc")) == NULL) {
		zerror(gettext("could not open /proc"));
		return (B_FALSE);
	}

	while ((dent = readdir(dirp)) != NULL) {
		int procfd;
		psinfo_t ps;
		char procpath[MAXPATHLEN];

		if (dent->d_name[0] == '.')
			continue;

		(void) snprintf(procpath, sizeof (procpath), "/proc/%s/psinfo",
		    dent->d_name);

		if ((procfd = open(procpath, O_RDONLY)) == -1)
			continue;

		if (read(procfd, &ps, sizeof (ps)) == sizeof (psinfo_t)) {
			/* skip processes in other zones and system processes */
			if (zoneid != ps.pr_zoneid || ps.pr_flag & SSYS) {
				(void) close(procfd);
				continue;
			}

			if (poolid_set) {
				if (ps.pr_poolid != last_poolid)
					mixed = B_TRUE;
			} else {
				last_poolid = ps.pr_poolid;
				poolid_set = B_TRUE;
			}
		}

		(void) close(procfd);

		if (mixed)
			break;
	}

	(void) closedir(dirp);

	return (mixed);
}

/*
 * Check if a persistent or temporary pool is configured for the zone.
 * This is currently only going to be called for the global zone from
 * apply_func but that could be generalized in the future.
 */
static boolean_t
pool_configured(zone_dochandle_t handle)
{
	int err1, err2;
	struct zone_psettab pset_tab;
	char poolname[MAXPATHLEN];

	err1 = zonecfg_lookup_pset(handle, &pset_tab);
	err2 = zonecfg_get_pool(handle, poolname, sizeof (poolname));

	if (err1 == Z_NO_ENTRY &&
	    (err2 == Z_NO_ENTRY || (err2 == Z_OK && strlen(poolname) == 0)))
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * This is an undocumented interface which is currently only used to apply
 * the global zone resource management settings when the system boots.
 * This function does not yet properly handle updating a running system so
 * any projects running in the zone would be trashed if this function
 * were to run after the zone had booted.  It also does not reset any
 * rctl settings that were removed from zonecfg.  There is still work to be
 * done before we can properly support dynamically updating the resource
 * management settings for a running zone (global or non-global).  Thus, this
 * functionality is undocumented for now.
 */
/* ARGSUSED */
static int
apply_func(int argc, char *argv[])
{
	int err;
	int res = Z_OK;
	priv_set_t *privset;
	zoneid_t zoneid;
	zone_dochandle_t handle;
	struct zone_mcaptab mcap;
	char pool_err[128];

	zoneid = getzoneid();

	if (zonecfg_in_alt_root() || zoneid != GLOBAL_ZONEID ||
	    target_zone == NULL || strcmp(target_zone, GLOBAL_ZONENAME) != 0)
		return (usage(B_FALSE));

	if ((privset = priv_allocset()) == NULL) {
		zerror(gettext("%s failed"), "priv_allocset");
		return (Z_ERR);
	}

	if (getppriv(PRIV_EFFECTIVE, privset) != 0) {
		zerror(gettext("%s failed"), "getppriv");
		priv_freeset(privset);
		return (Z_ERR);
	}

	if (priv_isfullset(privset) == B_FALSE) {
		(void) usage(B_FALSE);
		priv_freeset(privset);
		return (Z_ERR);
	}
	priv_freeset(privset);

	if ((handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_APPLY), B_TRUE);
		return (Z_ERR);
	}

	if ((err = zonecfg_get_handle(target_zone, handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_APPLY), B_TRUE);
		zonecfg_fini_handle(handle);
		return (Z_ERR);
	}

	/* specific error msgs are printed within apply_rctls */
	if ((err = zonecfg_apply_rctls(target_zone, handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_APPLY), B_TRUE);
		res = Z_ERR;
	}

	if ((err = check_cpu_shares_sched(handle)) != Z_OK)
		res = Z_ERR;

	if (pool_configured(handle)) {
		if (mixed_pools(zoneid)) {
			zerror(gettext("Zone is using multiple resource "
			    "pools.  The pool\nconfiguration cannot be "
			    "applied without rebooting."));
			res = Z_ERR;
		} else {

			/*
			 * The next two blocks of code attempt to set up
			 * temporary pools as well as persistent pools.  In
			 * both cases we call the functions unconditionally.
			 * Within each funtion the code will check if the zone
			 * is actually configured for a temporary pool or
			 * persistent pool and just return if there is nothing
			 * to do.
			 */
			if ((err = zonecfg_bind_tmp_pool(handle, zoneid,
			    pool_err, sizeof (pool_err))) != Z_OK) {
				if (err == Z_POOL || err == Z_POOL_CREATE ||
				    err == Z_POOL_BIND)
					zerror("%s: %s", zonecfg_strerror(err),
					    pool_err);
				else
					zerror(gettext("could not bind zone to "
					    "temporary pool: %s"),
					    zonecfg_strerror(err));
				res = Z_ERR;
			}

			if ((err = zonecfg_bind_pool(handle, zoneid, pool_err,
			    sizeof (pool_err))) != Z_OK) {
				if (err == Z_POOL || err == Z_POOL_BIND)
					zerror("%s: %s", zonecfg_strerror(err),
					    pool_err);
				else
					zerror("%s", zonecfg_strerror(err));
			}
		}
	}

	/*
	 * If a memory cap is configured, set the cap in the kernel using
	 * zone_setattr() and make sure the rcapd SMF service is enabled.
	 */
	if (zonecfg_getmcapent(handle, &mcap) == Z_OK) {
		uint64_t num;
		char smf_err[128];

		num = (uint64_t)strtoll(mcap.zone_physmem_cap, NULL, 10);
		if (zone_setattr(zoneid, ZONE_ATTR_PHYS_MCAP, &num, 0) == -1) {
			zerror(gettext("could not set zone memory cap"));
			res = Z_ERR;
		}

		if (zonecfg_enable_rcapd(smf_err, sizeof (smf_err)) != Z_OK) {
			zerror(gettext("enabling system/rcap service failed: "
			    "%s"), smf_err);
			res = Z_ERR;
		}
	}

	zonecfg_fini_handle(handle);

	return (res);
}

/*
 * This is an undocumented interface that is invoked by the zones SMF service
 * for installed zones that won't automatically boot.
 */
/* ARGSUSED */
static int
sysboot_func(int argc, char *argv[])
{
	int err;
	zone_dochandle_t zone_handle;
	brand_handle_t brand_handle;
	char cmdbuf[MAXPATHLEN];
	char zonepath[MAXPATHLEN];

	/*
	 * This subcommand can only be executed in the global zone on non-global
	 * zones.
	 */
	if (zonecfg_in_alt_root())
		return (usage(B_FALSE));
	if (sanity_check(target_zone, CMD_SYSBOOT, B_FALSE, B_TRUE, B_FALSE) !=
	    Z_OK)
		return (Z_ERR);

	/*
	 * Fetch the sysboot hook from the target zone's brand.
	 */
	if ((zone_handle = zonecfg_init_handle()) == NULL) {
		zperror(cmd_to_str(CMD_SYSBOOT), B_TRUE);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_handle(target_zone, zone_handle)) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_SYSBOOT), B_TRUE);
		zonecfg_fini_handle(zone_handle);
		return (Z_ERR);
	}
	if ((err = zonecfg_get_zonepath(zone_handle, zonepath,
	    sizeof (zonepath))) != Z_OK) {
		errno = err;
		zperror(cmd_to_str(CMD_SYSBOOT), B_TRUE);
		zonecfg_fini_handle(zone_handle);
		return (Z_ERR);
	}
	if ((err = validate_zonepath(zonepath, CMD_SYSBOOT)) != Z_OK) {
		/* Useful message already printed */
		return (Z_ERR);
	}
	if ((brand_handle = brand_open(target_brand)) == NULL) {
		zerror(gettext("missing or invalid brand during %s operation: "
		    "%s"), cmd_to_str(CMD_SYSBOOT), target_brand);
		zonecfg_fini_handle(zone_handle);
		return (Z_ERR);
	}
	err = get_hook(brand_handle, cmdbuf, sizeof (cmdbuf), brand_get_sysboot,
	    target_zone, zonepath);
	brand_close(brand_handle);
	zonecfg_fini_handle(zone_handle);
	if (err != Z_OK) {
		zerror(gettext("unable to get brand hook from brand %s for %s "
		    "operation"), target_brand, cmd_to_str(CMD_SYSBOOT));
		return (Z_ERR);
	}

	/*
	 * If the hook wasn't defined (which is OK), then indicate success and
	 * return.  Otherwise, execute the hook.
	 */
	if (cmdbuf[0] != '\0')
		return ((subproc_status(gettext("brand sysboot operation"),
		    do_subproc(cmdbuf), B_FALSE) == ZONE_SUBPROC_OK) ? Z_OK :
		    Z_BRAND_ERROR);
	return (Z_OK);
}

static int
help_func(int argc, char *argv[])
{
	int arg, cmd_num;

	if (argc == 0) {
		(void) usage(B_TRUE);
		return (Z_OK);
	}
	optind = 0;
	if ((arg = getopt(argc, argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			sub_usage(SHELP_HELP, CMD_HELP);
			return (optopt == '?' ? Z_OK : Z_USAGE);
		default:
			sub_usage(SHELP_HELP, CMD_HELP);
			return (Z_USAGE);
		}
	}
	while (optind < argc) {
		/* Private commands have NULL short_usage; omit them */
		if ((cmd_num = cmd_match(argv[optind])) < 0 ||
		    cmdtab[cmd_num].short_usage == NULL) {
			sub_usage(SHELP_HELP, CMD_HELP);
			return (Z_USAGE);
		}
		sub_usage(cmdtab[cmd_num].short_usage, cmd_num);
		optind++;
	}
	return (Z_OK);
}

/*
 * Returns: CMD_MIN thru CMD_MAX on success, -1 on error
 */

static int
cmd_match(char *cmd)
{
	int i;

	for (i = CMD_MIN; i <= CMD_MAX; i++) {
		/* return only if there is an exact match */
		if (strcmp(cmd, cmdtab[i].cmd_name) == 0)
			return (cmdtab[i].cmd_num);
	}
	return (-1);
}

static int
parse_and_run(int argc, char *argv[])
{
	int i = cmd_match(argv[0]);

	if (i < 0)
		return (usage(B_FALSE));
	return (cmdtab[i].handler(argc - 1, &(argv[1])));
}

static char *
get_execbasename(char *execfullname)
{
	char *last_slash, *execbasename;

	/* guard against '/' at end of command invocation */
	for (;;) {
		last_slash = strrchr(execfullname, '/');
		if (last_slash == NULL) {
			execbasename = execfullname;
			break;
		} else {
			execbasename = last_slash + 1;
			if (*execbasename == '\0') {
				*last_slash = '\0';
				continue;
			}
			break;
		}
	}
	return (execbasename);
}

static char *
get_username()
{
	uid_t uid;
	struct passwd *nptr;


	/*
	 * Authorizations are checked to restrict access based on the
	 * requested operation and zone name, It is assumed that the
	 * program is running with all privileges, but that the real
	 * user ID is that of the user or role on whose behalf we are
	 * operating. So we start by getting the username that will be
	 * used for subsequent authorization checks.
	 */

	uid = getuid();
	if ((nptr = getpwuid(uid)) == NULL) {
		zerror(gettext("could not get user name."));
		exit(Z_ERR);
	}
	return (nptr->pw_name);
}

int
main(int argc, char **argv)
{
	int arg;
	zoneid_t zid;
	struct stat st;
	char *zone_lock_env;
	int err;

	if ((locale = setlocale(LC_ALL, "")) == NULL)
		locale = "C";
	(void) textdomain(TEXT_DOMAIN);
	setbuf(stdout, NULL);
	(void) sigset(SIGHUP, SIG_IGN);
	execname = get_execbasename(argv[0]);
	username = get_username();
	target_zone = NULL;
	target_brand[0] = '\0';
	if (chdir("/") != 0) {
		zerror(gettext("could not change directory to /."));
		exit(Z_ERR);
	}

	/*
	 * Use the default system mask rather than anything that may have been
	 * set by the caller.
	 */
	(void) umask(CMASK);

	if (init_zfs() != Z_OK)
		exit(Z_ERR);

	while ((arg = getopt(argc, argv, "?u:z:R:")) != EOF) {
		switch (arg) {
		case '?':
			return (usage(B_TRUE));
		case 'u':
			target_uuid = optarg;
			break;
		case 'z':
			target_zone = optarg;
			break;
		case 'R':	/* private option for admin/install use */
			if (*optarg != '/') {
				zerror(gettext("root path must be absolute."));
				exit(Z_ERR);
			}
			if (stat(optarg, &st) == -1 || !S_ISDIR(st.st_mode)) {
				zerror(
				    gettext("root path must be a directory."));
				exit(Z_ERR);
			}
			zonecfg_set_root(optarg);
			break;
		default:
			return (usage(B_FALSE));
		}
	}

	if (optind >= argc)
		return (usage(B_FALSE));

	if (target_uuid != NULL && *target_uuid != '\0') {
		uuid_t uuid;
		static char newtarget[ZONENAME_MAX];

		if (uuid_parse(target_uuid, uuid) == -1) {
			zerror(gettext("illegal UUID value specified"));
			exit(Z_ERR);
		}
		if (zonecfg_get_name_by_uuid(uuid, newtarget,
		    sizeof (newtarget)) == Z_OK)
			target_zone = newtarget;
	}

	if (target_zone != NULL && zone_get_id(target_zone, &zid) != 0) {
		errno = Z_NO_ZONE;
		zperror(target_zone, B_TRUE);
		exit(Z_ERR);
	}

	/*
	 * See if we have inherited the right to manipulate this zone from
	 * a zoneadm instance in our ancestry.  If so, set zone_lock_cnt to
	 * indicate it.  If not, make that explicit in our environment.
	 */
	zonecfg_init_lock_file(target_zone, &zone_lock_env);

	/* Figure out what the system's default brand is */
	if (zonecfg_default_brand(default_brand,
	    sizeof (default_brand)) != Z_OK) {
		zerror(gettext("unable to determine default brand"));
		return (Z_ERR);
	}

	/*
	 * If we are going to be operating on a single zone, retrieve its
	 * brand type and determine whether it is native or not.
	 */
	if ((target_zone != NULL) &&
	    (strcmp(target_zone, GLOBAL_ZONENAME) != 0)) {
		if (zone_get_brand(target_zone, target_brand,
		    sizeof (target_brand)) != Z_OK) {
			zerror(gettext("missing or invalid brand"));
			exit(Z_ERR);
		}
		/*
		 * In the alternate root environment, the only supported
		 * operations are mount and unmount.  In this case, just treat
		 * the zone as native if it is cluster.  Cluster zones can be
		 * native for the purpose of LU or upgrade, and the cluster
		 * brand may not exist in the miniroot (such as in net install
		 * upgrade).
		 */
		if (strcmp(target_brand, CLUSTER_BRAND_NAME) == 0) {
			if (zonecfg_in_alt_root()) {
				(void) strlcpy(target_brand, default_brand,
				    sizeof (target_brand));
			}
		}
	}

	err = parse_and_run(argc - optind, &argv[optind]);

	return (err);
}
