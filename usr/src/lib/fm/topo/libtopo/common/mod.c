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

#include <limits.h>
#include <strings.h>
#include <unistd.h>
#include <alloca.h>
#include <libnvpair.h>
#include <fm/topo_mod.h>
#include <sys/fm/protocol.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/objfs.h>
#include <sys/modctl.h>
#include <libelf.h>
#include <gelf.h>

#include <topo_method.h>
#include <topo_module.h>
#include <topo_subr.h>
#include <mod.h>

static int mod_enum(topo_mod_t *, tnode_t *, const char *, topo_instance_t,
    topo_instance_t, void *, void *);
static void mod_release(topo_mod_t *, tnode_t *);
static int mod_fmri_create_meth(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int mod_fmri_nvl2str(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int mod_fmri_str2nvl(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);

static const topo_method_t mod_methods[] = {
	{ TOPO_METH_FMRI, TOPO_METH_FMRI_DESC, TOPO_METH_FMRI_VERSION,
	    TOPO_STABILITY_INTERNAL, mod_fmri_create_meth },
	{ TOPO_METH_NVL2STR, TOPO_METH_NVL2STR_DESC, TOPO_METH_NVL2STR_VERSION,
	    TOPO_STABILITY_INTERNAL, mod_fmri_nvl2str },
	{ TOPO_METH_STR2NVL, TOPO_METH_STR2NVL_DESC, TOPO_METH_STR2NVL_VERSION,
	    TOPO_STABILITY_INTERNAL, mod_fmri_str2nvl },
	{ NULL }
};

static const topo_modops_t mod_modops =
	{ mod_enum, mod_release };
static const topo_modinfo_t mod_info =
	{ "mod", FM_FMRI_SCHEME_MOD, MOD_VERSION, &mod_modops };

int
mod_init(topo_mod_t *mod, topo_version_t version)
{
	if (getenv("TOPOMODDEBUG"))
		topo_mod_setdebug(mod);
	topo_mod_dprintf(mod, "initializing mod builtin\n");

	if (version != MOD_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (topo_mod_register(mod, &mod_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "failed to register mod_info: "
		    "%s\n", topo_mod_errmsg(mod));
		return (-1); /* mod errno already set */
	}

	return (0);
}

void
mod_fini(topo_mod_t *mod)
{
	topo_mod_unregister(mod);
}

/*ARGSUSED*/
static int
mod_enum(topo_mod_t *mod, tnode_t *pnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *notused1, void *notused2)
{
	/*
	 * Methods are registered, but there is no enumeration.  Should
	 * enumeration be added be sure to cater for global vs non-global
	 * zones.
	 */
	(void) topo_method_register(mod, pnode, mod_methods);
	return (0);
}

static void
mod_release(topo_mod_t *mod, tnode_t *node)
{
	topo_method_unregister_all(mod, node);
}

static int
mod_binary_path_get(topo_mod_t *mp, const char *objpath)
{
	Elf *elf = NULL;
	Elf_Scn *scn = NULL;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	int fd;

	if ((fd = open(objpath, O_RDONLY)) < 0) {
		topo_mod_dprintf(mp, "unable to open %s\n", objpath);
		return (-1);
	}

	if (elf_version(EV_CURRENT) == EV_NONE) {
		topo_mod_dprintf(mp, "Elf version out of whack\n");
		goto mbpg_bail;
	}
	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		topo_mod_dprintf(mp, "elf_begin failed\n");
		goto mbpg_bail;
	}
	if ((gelf_getehdr(elf, &ehdr)) == NULL) {
		topo_mod_dprintf(mp, "gelf_getehdr failed\n");
		goto mbpg_bail;
	}
	scn = elf_getscn(elf, 0);	/* "seek" to start of sections */
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		const char *sh_name;
		if (gelf_getshdr(scn, &shdr) == NULL) {
			topo_mod_dprintf(mp, "gelf_getshdr failed\n");
			goto mbpg_bail;
		}
		if (shdr.sh_type != SHT_PROGBITS)
			continue;
		sh_name = elf_strptr(elf,
		    ehdr.e_shstrndx, (size_t)shdr.sh_name);
		if (strcmp(sh_name, ".filename") != 0)
			continue;
		if (elf_getdata(scn, NULL) == NULL) {
			topo_mod_dprintf(mp, "no filename data");
			break;
		}
		break;
	}
	(void) elf_end(elf);
	(void) close(fd);
	return (0);

mbpg_bail:
	if (elf != NULL)
		(void) elf_end(elf);
	if (fd >= 0)
		(void) close(fd);
	(void) topo_mod_seterrno(mp, EMOD_METHOD_INVAL);
	return (-1);
}

static int
mod_nvl_data(topo_mod_t *mp, nvlist_t *out, const char *path)
{
	struct modinfo mi;
	struct stat64 s;
	int id, e;

	if (stat64(path, &s) < 0) {
		topo_mod_dprintf(mp,
		    "No system object file for driver %s", path);
		return (topo_mod_seterrno(mp, EMOD_METHOD_INVAL));
	}

	id = OBJFS_MODID(s.st_ino);
	mi.mi_id = mi.mi_nextid = id;
	mi.mi_info = MI_INFO_ONE | MI_INFO_NOBASE;
	if (modctl(MODINFO, id, &mi) < 0) {
		return (topo_mod_seterrno(mp, EMOD_METHOD_INVAL));
	}
	mi.mi_name[MODMAXNAMELEN - 1] = '\0';
	mi.mi_msinfo[0].msi_linkinfo[MODMAXNAMELEN - 1] = '\0';
	e = nvlist_add_string(out, FM_FMRI_SCHEME, FM_FMRI_SCHEME_MOD);
	e |= nvlist_add_uint8(out, FM_VERSION, FM_MOD_SCHEME_VERSION);
	e |= nvlist_add_string(out, FM_FMRI_MOD_NAME, mi.mi_name);
	e |= nvlist_add_string(out,
	    FM_FMRI_MOD_DESC, mi.mi_msinfo[0].msi_linkinfo);
	if (e != 0)
		return (topo_mod_seterrno(mp, EMOD_FMRI_NVL));

	return (0);
}

static nvlist_t *
mod_fmri_create(topo_mod_t *mp, const char *driver)
{
	nvlist_t *out = NULL;
	char objpath[PATH_MAX];

	if (topo_mod_nvalloc(mp, &out, NV_UNIQUE_NAME) != 0) {
		(void) topo_mod_seterrno(mp, EMOD_FMRI_NVL);
		goto mfc_bail;
	}

	(void) snprintf(objpath, PATH_MAX, "%s/%s/object", OBJFS_ROOT, driver);

	/*
	 * Validate the module object ELF header if possible
	 */
	if (mod_binary_path_get(mp, objpath) < 0)
		goto mfc_bail;

	if (mod_nvl_data(mp, out, objpath) < 0) {
		topo_mod_dprintf(mp, "failed to get modinfo for %s", driver);
		goto mfc_bail;
	}

	return (out);

mfc_bail:
	nvlist_free(out);
	return (NULL);
}

/*ARGSUSED*/
static int
mod_fmri_create_meth(topo_mod_t *mp, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	nvlist_t *args;
	nvlist_t *modnvl;
	char *driver;

	if (version > TOPO_METH_FMRI_VERSION)
		return (topo_mod_seterrno(mp, EMOD_VER_NEW));

	if (nvlist_lookup_nvlist(in, TOPO_METH_FMRI_ARG_NVL, &args) != 0 ||
	    nvlist_lookup_string(args, "DRIVER", &driver) != 0) {
		topo_mod_dprintf(mp, "no DRIVER string in method argument\n");
		return (topo_mod_seterrno(mp, EMOD_METHOD_INVAL));
	}

	modnvl = mod_fmri_create(mp, driver);
	if (modnvl == NULL) {
		*out = NULL;
		topo_mod_dprintf(mp, "failed to create contained mod FMRI\n");
		return (-1);
	}
	*out = modnvl;
	return (0);
}

#define	MAXINTSTR	11

static ssize_t
fmri_nvl2str(nvlist_t *nvl, char *buf, size_t buflen)
{
	nvlist_t *anvl = NULL;
	nvpair_t *apair;
	uint8_t version;
	ssize_t size = 0;
	char *modname = NULL, *aname, *aval;
	int err;

	if (nvlist_lookup_uint8(nvl, FM_VERSION, &version) != 0 ||
	    version > FM_MOD_SCHEME_VERSION)
		return (-1);

	/* Get authority, if present */
	err = nvlist_lookup_nvlist(nvl, FM_FMRI_AUTHORITY, &anvl);
	if (err != 0 && err != ENOENT)
		return (-1);

	/*
	 *  For brevity, we only include the module name and id
	 *  present in the FMRI in our output string.  The FMRI
	 *  also has data on the package containing the module.
	 */

	/* There must be a module name */
	err = nvlist_lookup_string(nvl, FM_FMRI_MOD_NAME, &modname);
	if (err != 0 || modname == NULL)
		return (-1);

	/* mod:// */
	topo_fmristr_build(&size, buf, buflen, FM_FMRI_SCHEME_MOD, NULL, "://");

	/* authority, if any */
	if (anvl != NULL) {
		for (apair = nvlist_next_nvpair(anvl, NULL);
		    apair != NULL; apair = nvlist_next_nvpair(anvl, apair)) {
			if (nvpair_type(apair) != DATA_TYPE_STRING ||
			    nvpair_value_string(apair, &aval) != 0)
				continue;
			aname = nvpair_name(apair);
			topo_fmristr_build(&size, buf, buflen, ":", NULL, NULL);
			topo_fmristr_build(&size, buf, buflen, "=",
			    aname, aval);
		}
	}

	/* module parts */
	topo_fmristr_build(&size, buf, buflen, modname,
	    "/" FM_FMRI_MOD_NAME "=", "/");

	return (size);
}

/*ARGSUSED*/
static int
mod_fmri_nvl2str(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *nvl, nvlist_t **out)
{
	ssize_t len;
	char *name = NULL;
	nvlist_t *fmristr;

	if (version > TOPO_METH_NVL2STR_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if ((len = fmri_nvl2str(nvl, NULL, 0)) == 0 ||
	    (name = topo_mod_alloc(mod, len + 1)) == NULL ||
	    fmri_nvl2str(nvl, name, len + 1) == 0) {
		if (name != NULL)
			topo_mod_free(mod, name, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	if (topo_mod_nvalloc(mod, &fmristr, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	if (nvlist_add_string(fmristr, "fmri-string", name) != 0) {
		topo_mod_free(mod, name, len + 1);
		nvlist_free(fmristr);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	topo_mod_free(mod, name, len + 1);
	*out = fmristr;

	return (0);
}

/*ARGSUSED*/
static int
mod_fmri_str2nvl(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	nvlist_t *fmri;
	char *mod_name = NULL;
	char *str, *tmpstr;
	int err, len;

	if (version > TOPO_METH_STR2NVL_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_string(in, "fmri-string", &str) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));

	/* We're expecting a string version of a mod scheme FMRI */
	if (strncmp(str, "mod:///", 7) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	/*
	 * Try to parse the module name.
	 */
	tmpstr = str + 7;
	if (strncmp(tmpstr, FM_FMRI_MOD_NAME "=", 9) == 0) {
		char *n;

		n = strchr(tmpstr + 9, '/');
		if (n == NULL)
			return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
		len = n - (tmpstr + 9);
		mod_name = alloca(len + 1);
		(void) memcpy(mod_name, tmpstr + 9, len);
		mod_name[len] = 0;
		tmpstr = n + 1;
	}

	if (topo_mod_nvalloc(mod, &fmri, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));

	err = nvlist_add_uint8(fmri, FM_VERSION, FM_DEV_SCHEME_VERSION);
	err |= nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_MOD);
	if (mod_name)
		err |= nvlist_add_string(fmri, FM_FMRI_MOD_NAME, mod_name);

	if (err != 0) {
		nvlist_free(fmri);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	*out = fmri;

	return (0);
}
