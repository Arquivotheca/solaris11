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
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/stat.h>
#include <sys/types.h>

/*
 * Dependent on types.h, but not including it...
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/dkio.h>
#include <sys/dktp/fdisk.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/sysmacros.h>
#include <sys/mkdev.h>
#include <sys/vfs.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <kstat.h>
#include <ctype.h>
#include <dirent.h>
#include <libdevinfo.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <devid.h>
#include <sys/scsi/adapters/scsi_vhci.h>

#include "dsr.h"
#include "statcommon.h"

/* where we get kstat name translation information from */
static di_node_t	di_root;	/* from di_init: for devid */
static di_dim_t		di_dim;		/* from di_dim_init: for /dev names */
static int		scsi_vhci_fd = -1; /* from scsi_vhci: for mpxio path */

/* disk/tape/misc info */
typedef struct {
	char		*minor_name;
	int		minor_isdisk;
} minor_match_t;
static minor_match_t	mm_disk = {"a", 1};
static minor_match_t	mm_tape	= {"", 0};
static minor_match_t	mm_misc	= {"0", 0};
static minor_match_t	mm_chng	= {"changer", 0};
static char		md_minor_name[MAXPATHLEN];
static minor_match_t	mm_md	= {md_minor_name, 0};
static minor_match_t	*mma_disk_tape_misc[]	=
			    {&mm_disk, &mm_tape, &mm_misc, &mm_chng, NULL};
static minor_match_t	*mma_md[]		= {&mm_md, NULL};
static char *mdsetno2name(int setno);
#define	DISKLIST_MOD	256		/* ^2 instunit mod hash */
static disk_list_t	*disklist[DISKLIST_MOD];
static char		*phci_minors[]		= {"fc", "scsi", NULL};

/* nfs info */
extern kstat_ctl_t	*kc;
extern mnt_t		*nfs;
static int		nfs_tried;
static char		*get_nfs_by_minor(uint_t);
static char		*cur_hostname(uint_t, kstat_ctl_t *);
static char		*cur_special(char *, char *);

/*
 * Clear the snapshot so a cache miss in lookup_ks_name() will cause a fresh
 * snapshot in drvinstunit2dev().
 */
void
cleanup_iodevs_snapshot()
{
	if (di_dim) {
		di_dim_fini(di_dim);
		di_dim = NULL;
	}

	if (di_root) {
		di_fini(di_root);
		di_root = DI_NODE_NIL;
	}

	nfs_tried = 0;
}

/*
 * Find information for (driver, instunit) device: return zero on failure.
 *
 * NOTE: Failure of drvinstunit2dev works out OK for the caller if the kstat
 * name is the same as public name: the caller will just use kstat name.
 */
static int
drvinstunitpart2dev(char *driver, int instunit, char *part,
    char **devpathp, char **adevpathp, char **devidp)
{
	int		instance;
	minor_match_t	**mma;
	minor_match_t	*mm;
	char		*devpath;
	size_t		devpathl, slen;
	char		*devid;
	char		*a, *s, *t;
	int		mdsetno;
	char		*mdsetname = NULL;
	char		amdsetname[MAXPATHLEN];
	char		*devicespath;
	di_node_t	node;

	/* setup "no result" return values */
	if (devpathp)
		*devpathp = NULL;
	if (adevpathp)
		*adevpathp = NULL;
	if (devidp)
		*devidp = NULL;

	/* take <driver><instance><minor_name> snapshot if not established */
	if (di_dim == NULL) {
		di_dim = di_dim_init();
		if (di_dim == NULL)
			return (0);
	}

	/*
	 * Determine if 'instunit' is an 'instance' or 'unit' based on the
	 * 'driver'.  The current code only detects 'md' metadevice 'units',
	 * and defaults to 'instance' for everything else.
	 *
	 * For a metadevice, 'driver' is either "md" or "<setno>/md".
	 */
	s = strstr(driver, "/md");
	if ((strcmp(driver, "md") == 0) ||
	    (s && isdigit(*driver) && (strcmp(s, "/md") == 0))) {
		/*
		 * "md" unit: Special case translation of "md" kstat names.
		 * For the local set the kstat name is "md<unit>", and for
		 * a shared set the kstat name is "<setno>/md<unit>": we map
		 * these to the minor paths "/pseudo/md@0:<unit>,blk" and
		 * "/pseudo/md@0:<set>,<unit>,blk" respectively.
		 */
		if (isdigit(*driver)) {
			mdsetno = atoi(driver);

			/* convert setno to setname */
			mdsetname = mdsetno2name(mdsetno);
		} else
			mdsetno = 0;

		driver = "md";
		instance = 0;
		mma = mma_md;			/* metadevice dynamic minor */
		(void) snprintf(md_minor_name, sizeof (md_minor_name),
		    "%d,%d,blk", mdsetno, instunit);
	} else {
		instance = instunit;
		mma = mma_disk_tape_misc;	/* disk/tape/misc minors */
	}

	if (part) {
		devpath = di_dim_path_dev(di_dim, driver, instance, part);
	} else  {
		/* Try to find a minor_match that works */
		for (mm = *mma++; mm; mm = *mma++)  {
			if ((devpath = di_dim_path_dev(di_dim,
			    driver, instance, mm->minor_name)) != NULL)
				break;
		}
	}
	if (devpath == NULL)
		return (0);

	/*
	 * At this point we have a devpath result. Return the information about
	 * the result that the caller is asking for.
	 */
	if (devpathp)			/* devpath */
		*devpathp = safe_strdup(devpath);

	if (adevpathp) {		/* abbreviated devpath */
		if ((part == NULL) && mm->minor_isdisk) {
			/*
			 * For disk kstats without a partition we return the
			 * last component with trailing "s#" or "p#" stripped
			 * off (i.e. partition/slice information is removed).
			 * For example for devpath of "/dev/dsk/c0t0d0s0" the
			 * abbreviated devpath would be "c0t0d0".
			 */
			a = strrchr(devpath, '/');
			if (a == NULL) {
				SAFE_FREE(devpath, 1);
				return (0);
			}
			a++;
			s = strrchr(a, 's');
			if (s == NULL) {
				s = strrchr(a, 'p');
				if (s == NULL) {
					SAFE_FREE(devpath, 1);
					return (0);
				}
			}
			/* don't include slice information in devpath */
			*s = '\0';
		} else {
			/*
			 * remove "/dev/", and "/dsk/", from 'devpath' (like
			 * "/dev/md/dsk/d0") to form the abbreviated devpath
			 * (like "md/d0").
			 */
			devpathl = strlen(devpath) + 1;
			t = safe_alloc(devpathl);
			if ((s = strstr(devpath, "/dev/")) != NULL) {
				(void) strlcpy(t, s + 5, devpathl);
				slen = strlen(s + 1) + 1;
				(void) strlcpy(s + 1, t, slen);
			}
			if ((s = strstr(devpath, "/dsk/")) != NULL) {
				(void) strlcpy(t, s + 5, devpathl);
				slen = strlen(s + 1) + 1;
				(void) strlcpy(s + 1, t, slen);
			}
			if ((s = strstr(devpath, "/scsi/")) != NULL) {
				(void) strlcpy(t, s + 6, devpathl);
				slen = strlen(s + 1) + 1;
				(void) strlcpy(s + 1, t, slen);
			}
			SAFE_FREE(t, 1);
			/*
			 * If we have an mdsetname, convert abbreviated setno
			 * notation (like "md/shared/1/d0" to abbreviated
			 * setname notation (like "md/red/d0").
			 */
			if (mdsetname) {
				a = strrchr(devpath, '/');
				(void) snprintf(amdsetname, sizeof (amdsetname),
				    "md/%s%s", mdsetname, a);
				SAFE_FREE(mdsetname, 1);
				a = amdsetname;
			} else {
				if (*devpath == '/')
					a = devpath + 1;
				else
					a = devpath;
			}
		}
		*adevpathp = safe_strdup(a);
	}

	if (devidp) {			/* lookup the devid */
		/* take snapshot if not established */
		if (di_root == DI_NODE_NIL) {
			di_root = di_init("/", DINFOCACHE);
		}
		if (di_root) {
			/* get path to /devices devinfo node */
			devicespath = di_dim_path_devices(di_dim,
			    driver, instance, NULL);
			if (devicespath) {
				/* find the node in the snapshot */
				node = di_lookup_node(di_root, devicespath);
				SAFE_FREE(devicespath, 1);

				/* and lookup devid property on the node */
				if (di_prop_lookup_strings(DDI_DEV_T_ANY, node,
				    DEVID_PROP_NAME, &devid) != -1)
					*devidp = devid;
			}
		}
	}

	SAFE_FREE(devpath, 1);
	return (1);				/* success */
}

/*
 * Do <pid> to 'target-port' translation
 */
static int
drvpid2port(uint_t pid, char **target_portp)
{
	sv_iocdata_t	ioc;
	char		target_port[MAXNAMELEN];

	/* setup "no result" return values */
	*target_portp = NULL;

	/* open scsi_vhci if not already done */
	if (scsi_vhci_fd == -1) {
		scsi_vhci_fd = open("/devices/scsi_vhci:devctl", O_RDONLY);
		if (scsi_vhci_fd == -1)
			return (0);		/* failure */
	}

	/*
	 * Perform ioctl for <pid> -> 'target-port' translation.
	 *
	 * NOTE: it is legimite for this ioctl to fail for transports
	 * that use mpxio, but don't set a 'target-port' pathinfo property.
	 * On failure we return the the "<pid>" as the target port string.
	 */
	bzero(&ioc, sizeof (sv_iocdata_t));
	ioc.buf_elem = pid;
	ioc.addr = target_port;
	if (ioctl(scsi_vhci_fd, SCSI_VHCI_GET_TARGET_LONGNAME, &ioc) < 0) {
		(void) snprintf(target_port, sizeof (target_port), "%d", pid);
	}

	*target_portp = safe_strdup(target_port);
	return (1);				/* success */
}

/*
 * Do phci driver instance to cfgadm controler number c# translation
 * If translation is not possible, the original initiator name is returned
 */
static void
phcidrvinst2cntl(const char *initiator, char ** cntlp)
{
	const char *p;
	char drv[KSTAT_STRLEN];
	int inst, len;
	char **minor = NULL;
	char *cntlpath = NULL;

	p = &initiator[strlen(initiator) - 1];	/* last char */

	while ((p >= initiator) && isdigit(*p))
		p--;				/* backwards over digits */
	p++;					/* beginning of instance */

	len = p - initiator;
	if (len == strlen(initiator))		/* no instance number */
		goto fail;

	inst = atoi(p);
	/*
	 * strlcpy is not appropriate here - we're copying substring only.
	 *
	 * 'initiator' is a substring of kstat name so len < KSTAT_STRLEN
	 */
	(void) strncpy(drv, initiator, len);
	drv[len] = '\0';

	if (di_dim == NULL) {
		di_dim = di_dim_init();
		if (di_dim == NULL)
			goto fail;
	}

	for (minor = phci_minors; *minor; ++minor) {
		cntlpath = di_dim_path_dev(di_dim, drv, inst, *minor);
		if (cntlpath != NULL)
			break;
	}

	if (cntlpath == NULL)
		goto fail;

	p = strrchr(cntlpath, '/');	/* we want the 'file' part */
	p++;				/* skip '/' */
	*cntlp = safe_strdup(p);

	SAFE_FREE(cntlpath, 1);
	return;

fail:
	*cntlp = safe_strdup(initiator); /* failsafe */
}

/*
 * Find/create a disk_list entry for given a kstat name.
 * The basic format of a kstat name is
 *
 *	"<driver><instunit>.<pid>.<phci-driver><instance>,<partition>".
 *
 * The <instunit> is a decimal number. The ".<pid>.<phci-driver><instance>",
 * which describes mpxio path stat information, and ",<partition>" parts are
 * optional. The <pid> consists of the letter 't' followed by a decimal number.
 * When available, we use the <pid> to find the 'target-port' via ioctls to
 * the scsi_vhci driver.
 *
 * NOTE: In the case of non-local metadevices, the format of "<driver>" in
 * a kstat name is acutally "<setno>/md".
 */
disk_list_t *
lookup_ks_name(char *ks_name, int want_devid)
{
	char		*pidp;		/* ".<pid>... */
	char		*part;		/* ",partition... */
	char		*initiator;	/* ".<phci-driver>... */
	char		*p;
	int		len;
	char		driver[KSTAT_STRLEN];
	int		instunit;
	disk_list_t	**dlhp;		/* disklist head */
	disk_list_t	*entry;
	char		*devpath = NULL;
	char		*adevpath = NULL;
	char		*cntl = NULL;
	char		*devid = NULL;
	int		pid;
	char		*target_port = NULL;
	char		portform[MAXPATHLEN];

	/* Filter out illegal forms (like all digits). */
	if ((ks_name == NULL) || (*ks_name == 0) ||
	    (strspn(ks_name, "0123456789") == strlen(ks_name)))
		goto fail;

	/* parse ks_name to create new entry */
	pidp = strchr(ks_name, '.');		/* start of ".<pid>" */
	initiator = strrchr(ks_name, '.');	/* start of ".<pHCI-driver>" */
	if (pidp && (pidp == initiator))	/* can't have same start */
		goto fail;

	part = strchr(ks_name, ',');		/* start of ",<partition>" */
	p = strchr(ks_name, ':');		/* start of ":<partition>" */
	if (part && p)
		goto fail;			/* can't have both */
	if (p)
		part = p;
	if (part && pidp)
		goto fail;			/* <pid> and partition: bad */

	p = part ? part : pidp;
	if (p == NULL)
		p = &ks_name[strlen(ks_name) - 1];	/* last char */
	else
		p--;				/* before ',' or '.' */

	while ((p >= ks_name) && isdigit(*p))
		p--;				/* backwards over digits */
	p++;					/* start of instunit */
	if ((*p == '\0') || (*p == ',') || (*p == '.') || (*p == ':'))
		goto fail;			/* no <instunit> */
	len = p - ks_name;
	(void) strncpy(driver, ks_name, len);
	driver[len] = '\0';
	instunit = atoi(p);
	if (part)
		part++;				/* skip ',' */

	/* hash by instunit and search for existing entry */
	dlhp = &disklist[instunit & (DISKLIST_MOD - 1)];
	for (entry = *dlhp; entry; entry = entry->next) {
		if (strcmp(entry->ks_name, ks_name) == 0) {
			return (entry);
		}
	}

	/* not found, translate kstat_name components and create new entry */

	/* translate kstat_name dev information */
	if (drvinstunitpart2dev(driver, instunit, part,
	    &devpath, &adevpath, want_devid ? &devid : NULL) == 0) {
		goto fail;
	}

	/* parse and translate path information */
	if (pidp) {
		/* parse path information: ".t#.<phci-driver><instance>" */
		pidp++;				/* skip '.' */
		initiator++;			/* skip '.' */
		if ((*pidp != 't') || !isdigit(pidp[1]))
			goto fail;		/* not ".t#" */
		pid = atoi(&pidp[1]);

		/* translate <pid> to 'target-port' */
		if (drvpid2port(pid, &target_port) == 0)
			goto fail;

		/* translate initiator to controller */
		phcidrvinst2cntl(initiator, &cntl);

		/* Establish 'target-port' form. */
		(void) snprintf(portform, sizeof (portform),
		    "%s.t%s.%s", adevpath, target_port, cntl);
		SAFE_FREE(target_port, 1);
		SAFE_FREE(adevpath, 1);
		SAFE_FREE(cntl, 1);
		adevpath = strdup(portform);
	}

	/* make a new entry ... */
	entry = safe_alloc(sizeof (disk_list_t));
	entry->ks_name = safe_strdup(ks_name);
	entry->dname = devpath;
	entry->dsk = adevpath;
	entry->devidstr = devid;

#ifdef	DEBUG
	(void) printf("lookup_ks_name:    new: %s	%s\n",
	    ks_name, entry->dsk ? entry->dsk : "NULL");
#endif	/* DEBUG */

	/* add new entry to head of hashed list */
	entry->next = *dlhp;
	*dlhp = entry;
	return (entry);

fail:
	SAFE_FREE(devpath, 1);
	SAFE_FREE(adevpath, 1);
	SAFE_FREE(devid, 1);
#ifdef	DEBUG
	(void) printf("lookup_ks_name: failed: %s\n", ks_name);
#endif	/* DEBUG */
	return (NULL);
}

/*
 * Convert metadevice setno to setname by looking in /dev/md for symlinks
 * that point to "shared/setno" - the name of such a symlink is the setname.
 * The caller is responsible for freeing the returned string.
 */
static char *
mdsetno2name(int setno)
{
	char		setlink[MAXPATHLEN + 1];
	char		link[MAXPATHLEN + 1];
	char		path[MAXPATHLEN + 1];
	char		*p;
	DIR		*dirp;
	struct dirent	*dp;
	size_t		len;
	char		*mdsetname = NULL;

	/* we are looking for a link to setlink */
	(void) snprintf(setlink, MAXPATHLEN, "shared/%d", setno);

	/* in the directory /dev/md */
	(void) strcpy(path, "/dev/md/");
	p = path + strlen(path);
	dirp = opendir(path);
	if (dirp == NULL)
		return (NULL);

	/* loop through /dev/md directory entries */
	while ((dp = readdir(dirp)) != NULL) {

		/* doing a readlink of entry (fails for non-symlinks) */
		*p = '\0';
		(void) strcpy(p, dp->d_name);
		if ((len = readlink(path, link, MAXPATHLEN)) == (size_t)-1)
			continue;

		/* and looking for a link to setlink */
		link[len] = '\0';
		if (strcmp(setlink, link))
			continue;

		/* found- name of link is the setname */
		mdsetname = safe_strdup(dp->d_name);
		break;
	}

	(void) closedir(dirp);
	return (mdsetname);
}

char *
lookup_nfs_name(char *ks, kstat_ctl_t *kc)
{
	uint_t minor;
	char *host, *path;
	char *cp;
	char *rstr = 0;
	size_t len;

	if (sscanf(ks, "nfs%u", &minor) == 1) {
retry:
		cp = get_nfs_by_minor(minor);
		if (cp) {
			if (strchr(cp, ',') == NULL) {
				rstr = safe_strdup(cp);
				return (rstr);
			}
			host = cur_hostname(minor, kc);
			if (host) {
				if (*host) {
					path = cur_special(host, cp);
					if (path) {
						len = strlen(host);
						len += strlen(path);
						len += 2;
						rstr = safe_alloc(len);
						(void) snprintf(rstr, len,
						    "%s:%s", host, path);
					} else {
						rstr = safe_strdup(cp);
					}
				} else {
					rstr = safe_strdup(ks);
				}
				SAFE_FREE(host, 1);
			} else {
				rstr = safe_strdup(cp);
			}
		} else if (nfs_tried == 0) {
			nfs_tried = 1;
			do_mnttab();
			goto retry;
		}
	}
	return (rstr);
}

static char *
get_nfs_by_minor(uint_t minor)
{
	mnt_t *localnfs;

	localnfs = nfs;
	while (localnfs) {
		if (localnfs->minor == minor) {
			return (localnfs->device_name);
		}
		localnfs = localnfs->next;
	}
	return (0);
}

/*
 * Read the cur_hostname from the mntinfo kstat
 */
static char *
cur_hostname(uint_t minor, kstat_ctl_t *kc)
{
	kstat_t *ksp;
	static struct mntinfo_kstat mik;
	char *rstr;

	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if (ksp->ks_type != KSTAT_TYPE_RAW)
			continue;
		if (ksp->ks_instance != minor)
			continue;
		if (strcmp(ksp->ks_module, "nfs"))
			continue;
		if (strcmp(ksp->ks_name, "mntinfo"))
			continue;
		if (ksp->ks_flags & KSTAT_FLAG_INVALID)
			return (NULL);
		if (kstat_read(kc, ksp, &mik) == -1)
			return (NULL);
		rstr = safe_strdup(mik.mik_curserver);
		return (rstr);
	}
	return (NULL);
}

/*
 * Given the hostname of the mounted server, extract the server
 * mount point from the mnttab string.
 *
 * Common forms:
 *	server1,server2,server3:/path
 *	server1:/path,server2:/path
 * or a hybrid of the two
 */
static char *
cur_special(char *hostname, char *special)
{
	char *cp;
	char *path;
	size_t hlen = strlen(hostname);

	/*
	 * find hostname in string
	 */
again:
	if ((cp = strstr(special, hostname)) == NULL)
		return (NULL);

	/*
	 * hostname must be followed by ',' or ':'
	 */
	if (cp[hlen] != ',' && cp[hlen] != ':') {
		special = &cp[hlen];
		goto again;
	}

	/*
	 * If hostname is followed by a ',' eat all characters until a ':'
	 */
	cp = &cp[hlen];
	if (*cp == ',') {
		cp++;
		while (*cp != ':') {
			if (*cp == NULL)
				return (NULL);
			cp++;
		}
	}
	path = ++cp;			/* skip ':' */

	/*
	 * path is terminated by either 0, or space or ','
	 */
	while (*cp) {
		if (isspace(*cp) || *cp == ',') {
			*cp = NULL;
			return (path);
		}
		cp++;
	}
	return (path);
}
