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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <kstat.h>
#include <limits.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <topo_error.h>
#include <fm/topo_mod.h>
#include <sys/fm/protocol.h>

#include <topo_method.h>
#include <topo_subr.h>
#include <mem.h>

/*
 * platform specific mem module
 */
#define	PLATFORM_MEM_VERSION	MEM_VERSION
#define	PLATFORM_MEM_NAME	"platform-mem"

/*
 * Flags for mem_fmri_str2nvl
 */
#define	MEMFMRI_PA		0x0001	/* Valid physical address */
#define	MEMFMRI_OFFSET		0x0002	/* Valid offset */

static int mem_enum(topo_mod_t *, tnode_t *, const char *, topo_instance_t,
    topo_instance_t, void *, void *);
static void mem_release(topo_mod_t *, tnode_t *);
static int mem_fmri_nvl2str(topo_mod_t *, tnode_t *, topo_version_t, nvlist_t *,
    nvlist_t **);
static int mem_fmri_str2nvl(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);

static const topo_method_t mem_methods[] = {
	{ TOPO_METH_NVL2STR, TOPO_METH_NVL2STR_DESC, TOPO_METH_NVL2STR_VERSION,
	    TOPO_STABILITY_INTERNAL, mem_fmri_nvl2str },
	{ TOPO_METH_STR2NVL, TOPO_METH_STR2NVL_DESC, TOPO_METH_STR2NVL_VERSION,
	    TOPO_STABILITY_INTERNAL, mem_fmri_str2nvl },
	{ NULL }
};

static const topo_modops_t mem_ops =
	{ mem_enum, mem_release };
static const topo_modinfo_t mem_info =
	{ "mem", FM_FMRI_SCHEME_MEM, MEM_VERSION, &mem_ops };

int
mem_init(topo_mod_t *mod, topo_version_t version)
{

	topo_mod_setdebug(mod);
	topo_mod_dprintf(mod, "initializing mem builtin\n");

	if (version != MEM_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (topo_mod_register(mod, &mem_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "failed to register mem_info: "
		    "%s\n", topo_mod_errmsg(mod));
		return (-1); /* mod errno already set */
	}

	return (0);
}

void
mem_fini(topo_mod_t *mod)
{
	topo_mod_unregister(mod);
}

/*ARGSUSED*/
static int
mem_enum(topo_mod_t *mod, tnode_t *pnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *notused1, void *notused2)
{
	int isglobal = (getzoneid() == GLOBAL_ZONEID);
	topo_mod_t *nmp;

	if (isglobal && (nmp = topo_mod_load(mod, PLATFORM_MEM_NAME,
	    PLATFORM_MEM_VERSION)) == NULL) {
		if (topo_mod_errno(mod) == ETOPO_MOD_NOENT) {
			/*
			 * There is no platform specific mem module.
			 */
			(void) topo_method_register(mod, pnode, mem_methods);
			return (0);
		} else {
			/* Fail to load the module */
			topo_mod_dprintf(mod, "Failed to load module %s: %s",
			    PLATFORM_MEM_NAME, topo_mod_errmsg(mod));
			return (-1);
		}
	}

	if (isglobal && topo_mod_enumerate(nmp, pnode, PLATFORM_MEM_NAME, name,
	    min, max, NULL) < 0) {
		topo_mod_dprintf(mod, "%s failed to enumerate: %s",
		    PLATFORM_MEM_NAME, topo_mod_errmsg(mod));
		return (-1);
	}
	(void) topo_method_register(mod, pnode, mem_methods);

	return (0);
}

static void
mem_release(topo_mod_t *mod, tnode_t *node)
{
	topo_method_unregister_all(mod, node);
}

/*
 * Convert an input string to a URI escaped string and return the new string.
 * RFC2396 Section 2.4 says that data must be escaped if it does not have a
 * representation using an unreserved character, where an unreserved character
 * is one that is either alphanumeric or one of the marks defined in S2.3.
 */
static size_t
mem_fmri_uriescape(const char *s, const char *xmark, char *buf, size_t len)
{
	static const char rfc2396_mark[] = "-_.!~*'()";
	static const char hex_digits[] = "0123456789ABCDEF";
	static const char empty_str[] = "";

	const char *p;
	char c, *q;
	size_t n = 0;

	if (s == NULL)
		s = empty_str;

	if (xmark == NULL)
		xmark = empty_str;

	for (p = s; (c = *p) != '\0'; p++) {
		if (isalnum(c) || strchr(rfc2396_mark, c) || strchr(xmark, c))
			n++;    /* represent c as itself */
		else
			n += 3; /* represent c as escape */
	}

	if (buf == NULL)
		return (n);

	for (p = s, q = buf; (c = *p) != '\0' && q < buf + len; p++) {
		if (isalnum(c) || strchr(rfc2396_mark, c) || strchr(xmark, c)) {
			*q++ = c;
		} else {
			*q++ = '%';
			*q++ = hex_digits[((uchar_t)c & 0xf0) >> 4];
			*q++ = hex_digits[(uchar_t)c & 0xf];
		}
	}

	if (q == buf + len)
		q--; /* len is too small: truncate output string */

	*q = '\0';
	return (n);
}

/*ARGSUSED*/
static int
mem_fmri_nvl2str(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	const char *format;
	nvlist_t *nvl;
	uint64_t val;
	char *buf, *unum;
	size_t len;
	int err;
	char *preunum, *escunum, *prefix;
	ssize_t presz;
	int i;

	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));

	if (nvlist_lookup_string(in, FM_FMRI_MEM_UNUM, &unum) != 0) {
		nvlist_free(nvl);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	/*
	 * If we have a DIMM offset, include it in the string.  If we have a
	 * PA then use that.  Otherwise just format the unum element.
	 */
	if (nvlist_lookup_uint64(in, FM_FMRI_MEM_OFFSET, &val) == 0) {
		format = FM_FMRI_SCHEME_MEM ":///%1$s%2$s/"
		    FM_FMRI_MEM_OFFSET "=%3$llx";
	} else if (nvlist_lookup_uint64(in, FM_FMRI_MEM_PHYSADDR, &val) == 0) {
		format = FM_FMRI_SCHEME_MEM ":///%1$s%2$s/"
		    FM_FMRI_MEM_PHYSADDR "=%3$llx";
	} else
		format = FM_FMRI_SCHEME_MEM ":///%1$s%2$s";

	/*
	 * If we have a well-formed unum we step over the hc:// and
	 * authority prefix
	 */
	if (strncmp(unum, "hc://", 5) == 0) {
		unum += 5;
		unum = strchr(unum, '/');
		++unum;
		prefix = "";
		escunum = unum;
	} else {
		prefix = FM_FMRI_MEM_UNUM "=";
		preunum = topo_mod_strdup(mod, unum);
		presz = strlen(preunum) + 1;

		for (i = 0; i < presz - 1; i++) {
			if (preunum[i] == ':' && preunum[i + 1] == ' ') {
				bcopy(preunum + i + 2, preunum + i + 1,
				    presz - (i + 2));
			} else if (preunum[i] == ' ') {
				preunum[i] = ',';
			}
		}

		i = mem_fmri_uriescape(preunum, ":,/", NULL, 0);
		escunum = topo_mod_alloc(mod, i + 1);
		(void) mem_fmri_uriescape(preunum, ":,/", escunum, i + 1);
		topo_mod_free(mod, preunum, presz);
	}

	len = snprintf(NULL, 0, format, prefix, escunum, val) + 1;
	buf = topo_mod_zalloc(mod, len);

	if (buf == NULL) {
		nvlist_free(nvl);
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	(void) snprintf(buf, len, format, prefix, escunum, val);
	if (escunum != unum)
		topo_mod_strfree(mod, escunum);
	err = nvlist_add_string(nvl, "fmri-string", buf);
	topo_mod_free(mod, buf, len);

	if (err != 0) {
		nvlist_free(nvl);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	*out = nvl;
	return (0);
}

/*ARGSUSED*/
static int
mem_fmri_str2nvl(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int err, len1, len2, flags = 0;
	uint64_t offset, paddr;
	boolean_t done = B_FALSE;
	char *fmristr, *str1, *str2, *estr,  buf[PATH_MAX];
	char *bufp = buf;
	char *unum = NULL;
	nvlist_t *fmri;

	if (version > TOPO_METH_STR2NVL_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_string(in, "fmri-string", &fmristr) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));

	/* We're expecting a string version of a mem scheme FMRI. */
	str1 = FM_FMRI_SCHEME_MEM":///";
	len1 = strlen(str1);
	if (strncmp(fmristr, str1, len1) != 0) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
		    "%s: bad scheme\n", __func__);
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
	}

	/* Check that first component is "unum=". */
	str2 = FM_FMRI_MEM_UNUM"=";
	len2 = strlen(str2);
	if (strncmp(fmristr + len1, str2, len2) != 0) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
		    "%s: no unum\n", __func__);
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
	}

	if (strlcpy(bufp, fmristr, PATH_MAX) >= PATH_MAX) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
		    "%s: fmri string to long\n", __func__);
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
	}

	/* Skip past "mem:///unum=" part". */
	unum = bufp + len1 + len2;

	/* Check that there is a unum string. */
	if (*unum == NULL) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
		    "%s: null unum\n", __func__);
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
	}

	/*
	 * Look for "offset=<offset>" and/or "paddr=<paddr>" component(s).
	 * i.e. "mem:///unum=a/b/c/paddr=<addr>/offset=<addr>
	 */
	while (done == B_FALSE) {
		/* Look for last '/'. */
		if ((bufp = strrchr(unum, '/')) == NULL)
			break;
		topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
		    "%s: bufp=0x%p=%s\n", __func__, (void *)bufp, bufp);
		str1 = FM_FMRI_MEM_PHYSADDR"=";
		len1 = strlen(str1);
		str2 = FM_FMRI_MEM_OFFSET"=";
		len2 = strlen(str2);
		if (strncmp(bufp + 1, str1, len1) == 0) {
			*bufp++ = '\0'; /* get rid of last '/' */
			flags |= MEMFMRI_PA;
			bufp += len1;
			errno = 0;
			paddr = strtoul(bufp, &estr, 0);
			if (errno != 0 || estr == bufp) {
				topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
				    "%s: bad paddr\n", __func__);
				return (topo_mod_seterrno(mod,
				    EMOD_FMRI_MALFORM));
			}
		} else if (strncmp(bufp + 1, str2, len2) == 0) {
			*bufp++ = '\0'; /* get rid of last '/' */
			flags |= MEMFMRI_OFFSET;
			bufp += len2;
			errno = 0;
			offset = strtoul(bufp, &estr, 0);
			if (errno != 0 || estr == bufp) {
				topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
				    "%s: bad offset\n", __func__);
				return (topo_mod_seterrno(mod,
				    EMOD_FMRI_MALFORM));
			}
		} else {
			topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
			    "%s: unrecognized component=%s\n",
			    __func__, bufp);
			done = B_TRUE;
			break;
		}
	}

	if (topo_mod_nvalloc(mod, &fmri, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));

	topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
	    "%s: unum=0x%p=%s\n", __func__, (void *)unum, unum);

	err = nvlist_add_uint8(fmri, FM_VERSION, FM_MEM_SCHEME_VERSION);
	err |= nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_MEM);
	err |= nvlist_add_string(fmri, FM_FMRI_MEM_UNUM, unum);
	if (flags & MEMFMRI_PA) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
		    "%s: paddr=0x%llx\n", __func__, (u_longlong_t)paddr);
		err |= nvlist_add_uint64(fmri, FM_FMRI_MEM_PHYSADDR, paddr);
	}
	if (flags & MEMFMRI_OFFSET) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
		    "%s: offset=0x%llx\n", __func__, (u_longlong_t)offset);
		err |= nvlist_add_uint64(fmri, FM_FMRI_MEM_OFFSET, offset);
	}

	if (err != 0) {
		nvlist_free(fmri);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	*out = fmri;

	return (0);
}
