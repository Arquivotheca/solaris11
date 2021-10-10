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

/*
 * This file contains the code that creates ASR audit messages.
 */
#include <errno.h>
#include <inttypes.h>
#include <libscf.h>
#include <string.h>
#include <sys/fm/protocol.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "asr.h"
#include "asr_buf.h"
#include "asr_mem.h"
#include "asr_err.h"
#include "asr_nvl.h"

#define	PKG_INFO_CMD	"/usr/bin/pkg info"
#define	PAD_COMPONENT	2
#define	PAD_ITEM	3

/* Default initial buffer sizes.  They will grow if needed. */
#define	ASR_AUDIT_BUFSIZE (1024*2048)
#define	ASR_AUDIT_LINESIZE 128
#define	ASR_AUDIT_PAYLOAD_GENERIC "generic"

#define	CHECK_FOR_ERR(e) if ((e)) goto finally
#define	CHECK_NOT_NULL(pointer) if ((pointer) == NULL) goto finally

/*
 * Logs a topology error to the ASR log.
 */
static int
asr_topo_error(asr_handle_t *ah, int err)
{
	asr_log_error(ah, EASR_TOPO,
	    "asr_topo: %s", topo_strerror(err));
	return (err);
}

/*
 * Gets a copy of an FMRI string from a name/value list.
 * The returned string must be freed when no longer needed.
 * If there is an error NULL will be returned and asr_errno will be set.
 */
static char *
asr_get_fmri_strcpy(asr_topo_enum_data_t *edata, nvlist_t *fmri)
{
	char *fmristr, *ret_fmri = NULL;
	int err;

	if (fmri == NULL)
		return (NULL);
	if (topo_fmri_nvl2str(edata->asr_topoh, fmri, &fmristr, &err) != 0) {
		(void) asr_topo_error(edata->asr_hdl, err);
		goto finally;
	}
	if (fmristr != NULL) {
		ret_fmri = asr_strdup(fmristr);
		if (ret_fmri == NULL)
			(void) asr_set_errno(EASR_NOMEM);
		topo_hdl_strfree(edata->asr_topoh, fmristr);
	}
finally:
	nvlist_free(fmri);
	return (ret_fmri);
}

/*
 * Gets the FRU from a topo node as a string.
 */
static char *
asr_get_fru(asr_topo_enum_data_t *edata, tnode_t *node)
{
	nvlist_t *fmri;
	int err;

	if (topo_node_fru(node, &fmri, NULL, &err) != 0) {
		return (NULL);
	}
	return (asr_get_fmri_strcpy(edata, fmri));
}

/*
 * Gets the ASRU from a topo node as a string.
 */
static char *
asr_get_asru(asr_topo_enum_data_t *edata, tnode_t *node)
{
	nvlist_t *fmri;
	int err;

	if (topo_node_asru(node, &fmri, NULL, &err) != 0) {
		return (NULL);
	}
	return (asr_get_fmri_strcpy(edata, fmri));
}

/*
 * Gets the resouce from a topo node as a string.
 */
static char *
asr_get_resource(asr_topo_enum_data_t *edata, tnode_t *node)
{
	nvlist_t *fmri;
	int err;

	if (topo_node_resource(node, &fmri, &err) != 0) {
		return (NULL);
	}
	return (asr_get_fmri_strcpy(edata, fmri));
}

/*
 * Prints the additional-information element filled with data from the given
 * topo node property.
 * The name of the property is returned for further processing of the
 * property.
 */
static int
add_topo_ai_prop(
    asr_topo_enum_data_t *edata, nvlist_t *prop, char **name, asr_buf_t *value)
{
	int err = 0;
	nvpair_t *nvp;
	uint_t nelem;
	int i;
	char *type;

	if (prop == NULL || (nvp = nvlist_next_nvpair(prop, NULL)) == NULL)
		return (ASR_FAILURE);

	if ((nvp = nvlist_next_nvpair(prop, NULL)) == NULL ||
	    nvpair_name(nvp) == NULL ||
	    strcmp(TOPO_PROP_VAL_NAME, nvpair_name(nvp)) != 0)
		return (ASR_FAILURE);
	else
		(void) nvpair_value_string(nvp, name);

	if ((nvp = nvlist_next_nvpair(prop, nvp)) == NULL ||
	    nvpair_name(nvp) == NULL ||
	    strcmp(nvpair_name(nvp), TOPO_PROP_VAL_TYPE) != 0 ||
	    nvpair_type(nvp) != DATA_TYPE_UINT32)
		return (ASR_FAILURE);
	else
		(void) nvpair_value_uint32(nvp, (uint32_t *)&type);

	if (nvpair_name(nvp) == NULL ||
	    (nvp = nvlist_next_nvpair(prop, nvp)) == NULL)
		return (ASR_FAILURE);

	switch (nvpair_type(nvp)) {
	case DATA_TYPE_INT32:
	{
		int32_t val;
		(void) nvpair_value_int32(nvp, &val);
		(void) asr_buf_append(value, " %d", val);
		break;
	}
	case DATA_TYPE_UINT32:
	{
		uint32_t val;
		(void) nvpair_value_uint32(nvp, &val);
		(void) asr_buf_append(value, " 0x%x", val);
		break;
	}
	case DATA_TYPE_INT64:
	{
		int64_t val;
		(void) nvpair_value_int64(nvp, &val);
		(void) asr_buf_append(value, " %lld", (longlong_t)val);
		break;
	}
	case DATA_TYPE_UINT64:
	{
		uint64_t val;
		(void) nvpair_value_uint64(nvp, &val);
		(void) asr_buf_append(value, " 0x%llx", (u_longlong_t)val);
		break;
	}
	case DATA_TYPE_DOUBLE:
	{
		double val;
		(void) nvpair_value_double(nvp, &val);
		(void) asr_buf_append(value, " %lf", (double)val);
		break;
	}
	case DATA_TYPE_STRING:
	{
		char *val;
		(void) nvpair_value_string(nvp, &val);
		(void) asr_buf_append(value, " %s", val);
		break;
	}
	case DATA_TYPE_NVLIST:
	{
		nvlist_t *val;
		char *fmri;
		(void) nvpair_value_nvlist(nvp, &val);
		if (topo_fmri_nvl2str(
		    edata->asr_topoh, val, &fmri, &err) != 0) {
			asr_nvl_tostringi(value, prop, 1, '"', " : ");
		} else {
			(void) asr_buf_append(value, " %s", fmri);
			topo_hdl_strfree(edata->asr_topoh, fmri);
		}
		break;
	}
	case DATA_TYPE_INT32_ARRAY:
	{
		int32_t *val;
		(void) nvpair_value_int32_array(nvp, &val, &nelem);
		(void) asr_buf_append_str(value, " [ ");
		for (i = 0; i < nelem; i++)
			(void) asr_buf_append(value, "%d ", val[i]);
		(void) asr_buf_append_str(value, "]");
		break;
	}
	case DATA_TYPE_UINT32_ARRAY:
	{
		uint32_t *val;
		(void) nvpair_value_uint32_array(nvp, &val, &nelem);
		(void) asr_buf_append_str(value, " [ ");
		for (i = 0; i < nelem; i++)
			(void) asr_buf_append(value, "%u ", val[i]);
		(void) asr_buf_append(value, "]");
		break;
	}
	case DATA_TYPE_INT64_ARRAY:
	{
		int64_t *val;
		(void) nvpair_value_int64_array(nvp, &val, &nelem);
		(void) asr_buf_append_str(value, " [ ");
		for (i = 0; i < nelem; i++)
			(void) asr_buf_append(value, "%"PRId64" ", val[i]);
		(void) asr_buf_append(value, "]");
		break;
	}
	case DATA_TYPE_UINT64_ARRAY:
	{
		uint64_t *val;
		(void) nvpair_value_uint64_array(nvp, &val, &nelem);
		(void) asr_buf_append_str(value, " [ ");
		for (i = 0; i < nelem; i++)
			(void) asr_buf_append(value, "%"PRIu64" ", val[i]);
		(void) asr_buf_append_str(value, "]");
		break;
	}
	case DATA_TYPE_STRING_ARRAY:
	{
		char **val;
		(void) nvpair_value_string_array(nvp, &val, &nelem);
		(void) asr_buf_append_str(value, " [ ");
		for (i = 0; i < nelem; i++)
			(void) asr_buf_append(value, "\"%s\" ", val[i]);
		(void) asr_buf_append_str(value, "]");
		break;
	}
	default:
		(void) asr_buf_append(value, " unknown data type (%d)",
		    nvpair_type(nvp));
		break;
	}

	asr_buf_trim(value);
	return (err);
}

/*
 * Prints additional information for an entire topo node group.
 * Any properties that are part of the standard ASR audit message
 * will be returned in the std name value list.
 */
static int
add_topo_ai_grp(asr_topo_enum_data_t *edata, nvlist_t *grp,
		nvlist_t *std, asr_buf_t *out)
{
	char *gname, *pname;
	nvpair_t *nvp;
	nvlist_t *prop;
	int err = 0;
	asr_buf_t *val, *buf;
	char *skip = "|protocol|authority|";
	char *stdvals = "|serial|part|state|status|";

	nvp = nvlist_next_nvpair(grp, NULL);
	if (nvp == NULL)
		return (ASR_FAILURE);
	pname = nvpair_name(nvp);
	if (pname == NULL)
		return (ASR_FAILURE);
	if (strcmp(TOPO_PROP_GROUP_NAME, pname) != 0)
		return (ASR_FAILURE);
	else
		if (nvpair_value_string(nvp, &gname) != 0)
			return (ASR_FAILURE);

	buf = asr_buf_alloc(ASR_AUDIT_LINESIZE);
	if (buf == NULL)
		return (ASR_FAILURE);

	err = asr_buf_append(buf, "|%s|", gname);
	if (err != 0) {
		asr_buf_free(buf);
		return (ASR_FAILURE);
	}

	/*
	 * If this is a value we should skip then stop now.
	 */
	if (strstr(skip, buf->asrb_data) != NULL) {
		asr_buf_free(buf);
		return (ASR_OK);
	}

	val = asr_buf_alloc(ASR_AUDIT_LINESIZE);
	if (val == NULL) {
		asr_buf_free(buf);
		return (ASR_FAILURE);
	}

	for (nvp = nvlist_next_nvpair(grp, NULL); nvp != NULL && err == 0;
	    nvp = nvlist_next_nvpair(grp, nvp)) {
		if ((strcmp(TOPO_PROP_VAL, nvpair_name(nvp)) == 0) &&
		    nvpair_type(nvp) == DATA_TYPE_NVLIST) {
			if ((err = nvpair_value_nvlist(nvp, &prop)) != 0)
				break;
			asr_buf_reset(val);
			if ((err = add_topo_ai_prop(
			    edata, prop, &pname, val)) != 0)
				break;
			if ((err = asr_buf_append(buf, "|%s|", pname)) != 0)
				break;
			if (strstr(stdvals, buf->asrb_data))
				err = nvlist_add_string(std, pname,
				    asr_buf_data(val));
			if (err == 0)
				err = asr_buf_append_xml_ai(
				    out, 3, pname, asr_buf_data(val));
		}
	}

	asr_buf_free(buf);
	asr_buf_free(val);

	return (err);
}

/*
 * Prints out additional information for a topology node.
 */
static int
add_topo_ai(asr_topo_enum_data_t *edata, tnode_t *node,
	    nvlist_t *std, asr_buf_t *out)
{
	int err = 0;
	nvpair_t *nvp;
	nvlist_t *grps = topo_prop_getprops(node, &err);
	nvlist_t *grp;

	for (nvp = nvlist_next_nvpair(grps, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(grps, nvp)) {
		if (strcmp(TOPO_PROP_GROUP, nvpair_name(nvp)) != 0 ||
		    nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		(void) nvpair_value_nvlist(nvp, &grp);
		err |= add_topo_ai_grp(edata, grp, std, out);
	}
	asr_nvl_free(grps);
	return (err);
}

/*
 * Converts an FMRI string to an ASR name.
 * The string will contain the FMRI data after the <authority>/
 */
char *
asr_fmri_str_to_name(char *fmri)
{
	char *id;

	for (id = fmri; *id != '\0'; id++)
		if (*id == '/' && id != fmri &&
		    *(id + 1) != ':' && *(id + 1) != '/')
			break;
	if (*id == '/')
		id++;
	return (id);
}

/*
 * Contains the definition of a hardware component, inclusive of a path to this
 * hardware component
 * Adds name, id, serial, part, revision, path, state, status &
 * additional-information
 */
static int
add_hardware_component(asr_topo_enum_data_t *edata, tnode_t *node)
{
	asr_buf_t *out = edata->asr_data;
	int err = 0;
	int rollback_index = out->asrb_length;
	char *serial = NULL, *part = NULL, *revision = NULL;
	char *state = NULL, *status = NULL;

	char *fru = asr_get_fru(edata, node);
	char *resource = asr_get_resource(edata, node);
	char *asru = asr_get_asru(edata, node);
	char *name;

	nvlist_t *std = asr_nvl_alloc();
	asr_buf_t *ai = asr_buf_alloc(64);

	CHECK_NOT_NULL(std);
	CHECK_NOT_NULL(ai);

	if (fru != NULL)
		err |= asr_buf_append_xml_ai(ai, 3, "fru", fru);
	if (resource != NULL)
		err |= asr_buf_append_xml_ai(ai, 3, "resource", resource);
	if (asru != NULL)
		err |= asr_buf_append_xml_ai(ai, 3, "asru", asru);

	err |= add_topo_ai(edata, node, std, ai);
	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out, "<component><hardware-component>\n");
	CHECK_FOR_ERR(err);

	if (resource != NULL)
		name = asr_fmri_str_to_name(resource);
	else if (fru != NULL)
		name = asr_fmri_str_to_name(fru);
	else
		name = topo_node_name(node);

	err |= asr_buf_append_xml_nvtoken(out, PAD_ITEM, "name", name);
	CHECK_FOR_ERR(err);

	(void) nvlist_lookup_string(std, "serial", &serial);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "serial", serial);
	CHECK_FOR_ERR(err);

	(void) nvlist_lookup_string(std, "part", &part);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "part", part);
	CHECK_FOR_ERR(err);

	(void) nvlist_lookup_string(std, "revision", &revision);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "revision", revision);
	CHECK_FOR_ERR(err);

	(void) nvlist_lookup_string(std, "state", &state);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "state", state);
	CHECK_FOR_ERR(err);

	(void) nvlist_lookup_string(std, "status", &status);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "status", status);
	CHECK_FOR_ERR(err);

	err |= asr_buf_append_buf(out, ai);
	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out, "</hardware-component></component>\n");

finally:
	if (fru != NULL)
		free(fru);
	if (resource != NULL)
		free(resource);
	if (asru != NULL)
		free(asru);
	if (err && rollback_index) {
		out->asrb_length = rollback_index;
	}
	if (std != NULL)
		asr_nvl_free(std);
	if (ai != NULL)
		asr_buf_free(ai);
	return (err);
}

/*
 * Walks the topology. Used as a callback function to the topology walker.
 */
/* ARGSUSED */
static int
asr_audit_topo_enum(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	asr_topo_enum_data_t *edata = arg;

	(void) add_hardware_component(edata, node);

	return (TOPO_WALK_NEXT);
}

/*
 * Prints all the hardware-component ASR elements.
 * Searches the FMA topology and creates a hardware-component for each
 * topology node found.
 */
static int
add_hardware_components(asr_handle_t *ah, asr_buf_t *out)
{
	return (asr_topo_walk(ah, asr_audit_topo_enum, out));
}

static char *
add_pkg_info(nvlist_t *pkg, char *name1, char *name2)
{
	char *value = asr_nvl_str(pkg, name1);
	if (value == NULL)
		value = asr_nvl_str(pkg, name2);
	return (value);
}

/*
 * A description of a software package reminiscent of pkginfo
 * Adds all values in an uncategorized element so all info can be sent.
 * Newer verisons of message.xsd should include these new values.
 */
static int
add_software_package(asr_buf_t *out, nvlist_t *pkg)
{
	int err = 0;
	int rollback_index = out->asrb_length;

	char *name = add_pkg_info(pkg, "Name", "PKGINST");
	char *summary = add_pkg_info(pkg, "Summary", "NAME");
	char *category = add_pkg_info(pkg, "Category", "CATEGORY");
	char *state = add_pkg_info(pkg, "State", "STATUS");
	char *arch = asr_nvl_str(pkg, "ARCH");
	char *publisher = asr_nvl_str(pkg, "Publisher");
	char *version = add_pkg_info(pkg, "Version", "VERSION");
	char *release = asr_nvl_str(pkg, "Build Release");
	char *branch = asr_nvl_str(pkg, "Branch");
	char *basedir = asr_nvl_str(pkg, "BASEDIR");
	char *vendor = asr_nvl_str(pkg, "VENDOR");
	char *desc = add_pkg_info(pkg, "Description", "DESC");
	char *instdate = add_pkg_info(pkg, "Packaging Date", "INSTDATE");
	char *size = asr_nvl_str(pkg, "Size");
	char *fmri = asr_nvl_str(pkg, "FMRI");

	if (name == NULL || version == NULL)
		return (0);

	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out,
	    "<component>"
	    "<uncategorized name='software-package'>"
	    "<software-package>\n");
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "name", name);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "summary", summary);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "description", desc);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "category", category);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "state", state);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "publisher", publisher);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "arch", arch);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "version", version);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "release", release);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "branch", branch);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "basedir", basedir);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "vendor", vendor);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "instdate", instdate);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "size", size);
	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "fmri", fmri);

	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out,
	    "</software-package>"
	    "</uncategorized>"
	    "</component>\n");

	if (err && rollback_index) {
		out->asrb_length = rollback_index;
	}

	return (err);
}

/*
 * Reads a file containing pkginfo output and prints the results to the out
 * buffer as ASR audit software-package elements.
 */
static int
add_pkginfo_data(asr_handle_t *ah, asr_buf_t *out, FILE *in)
{
	int err = 0;
	char *buf, *name, *value, *sep;
	nvlist_t *pkg = NULL;
	asr_buf_t *line = NULL;

	if (in == NULL)
		return (ASR_FAILURE);

	if ((line = asr_buf_alloc(ASR_AUDIT_LINESIZE)) == NULL)
		return (ASR_FAILURE);

	do {
		asr_buf_reset(line);
		if (asr_buf_readln(line, in) == 0)
			break;
		buf = asr_buf_data(line);
		if ((sep = strstr(buf, ":")) == NULL) {
			if ((buf == NULL || buf[0] == '\n' || buf[0] == '\0') &&
			    pkg != NULL) {
				if (add_software_package(out, pkg) != 0)
					asr_log_warn(ah, "failed to add pkg "
					    "info to audit event");
				asr_nvl_free(pkg);
				pkg = NULL;
			}
			continue;
		}
		if (pkg == NULL) {
			pkg = asr_nvl_alloc();
			if (pkg == NULL) {
				err = 1;
				break;
			}
		}

		for (name = buf; *name == ' ' && *name != '\0'; name++)
			;

		*sep = '\0';
		value = sep + 1;

		for (value = sep + 1; *value == ' ' && *value != '\0'; value++)
			;

		sep = strstr(value, "\n");
		if (sep != NULL)
			*sep = '\0';
		(void) asr_nvl_add_str(pkg, name, value);
	} while (err == 0);

	if (pkg != NULL)
		asr_nvl_free(pkg);
	asr_buf_free(line);
	return (err);
}

static char *
asr_tmpfile(asr_handle_t *ah)
{
	asr_buf_t *file;
	char *rootdir = asr_getprop_strd(ah, ASR_PROP_ROOTDIR, "/");
	char *datadir = asr_get_datadir(ah);
	uuid_t uuid;
	char uuidbuf[UUID_PRINTABLE_STRING_LENGTH];

	if (rootdir == NULL || datadir == NULL)
		return (NULL);

	uuid_generate(uuid);
	uuid_unparse(uuid, uuidbuf);

	file = asr_buf_alloc(PATH_MAX);
	if (asr_buf_append(file, "%s%s/%s", rootdir, datadir, uuidbuf) != 0) {
		asr_buf_free(file);
		return (NULL);
	}
	return (asr_buf_free_struct(file));
}

/*
 * Collects software package information and print it out as ASR
 * software-package elements.
 */
static int
add_software_packages(asr_handle_t *ah, asr_buf_t *out)
{
	int err = 0;
	FILE *outfile;

	if ((outfile = popen(PKG_INFO_CMD, "r")) == NULL) {
		(void) asr_error(EASR_SYSTEM,
		    "Error calling %s (%s) ", PKG_INFO_CMD, strerror(errno));
		return (ASR_FAILURE);
	}

	err = add_pkginfo_data(ah, out, outfile);
	(void) pclose(outfile);

	return (err);
}

/*
 * Prints out a SCF service as an ASR software-module element containing
 * name & description
 *     name=FMRI
 *     description = STATE STIME
 */
static int
add_software_module(asr_buf_t *out, char *fmri, scf_service_t *svc)
{
	int err = 0;
	char *state = NULL;
	char *desc = "";
	int rollback_index = 0;

	CHECK_NOT_NULL(out);
	CHECK_NOT_NULL(fmri);
	CHECK_NOT_NULL(svc);

	state = smf_get_state(fmri);
	if (state != NULL) {
		desc = state;
	}

	rollback_index = out->asrb_length;
	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out, "<component><software-module>\n");
	CHECK_FOR_ERR(err);

	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "name", fmri);
	CHECK_FOR_ERR(err);

	err |= asr_buf_append_xml_nv(out, PAD_ITEM, "description", desc);
	CHECK_FOR_ERR(err);

	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out, "</software-module></component>\n");

finally:
	if (state != NULL) {
		free(state);
	}
	if (err && rollback_index) {
		out->asrb_length = rollback_index;
	}
	return (err);
}

/*
 * Collects all SCF services and prints them out as ASR software-module
 * elements
 */
static int
add_software_modules(asr_buf_t *out)
{
	int r;
	int err = 1;
	char *fmri;
	scf_handle_t *handle = NULL;
	scf_scope_t *scope = NULL;
	scf_service_t *svc = NULL;
	scf_instance_t *inst = NULL;
	scf_iter_t *svc_iter = NULL;
	scf_iter_t *inst_iter = NULL;
	size_t sz = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH) + 1;

	fmri = malloc(sz + 1);
	CHECK_NOT_NULL(fmri);

	handle = scf_handle_create(SCF_VERSION);
	CHECK_NOT_NULL(handle);

	scope = scf_scope_create(handle);
	CHECK_NOT_NULL(scope);

	svc = scf_service_create(handle);
	CHECK_NOT_NULL(svc);

	inst = scf_instance_create(handle);
	CHECK_NOT_NULL(inst);

	svc_iter = scf_iter_create(handle);
	CHECK_NOT_NULL(svc_iter);

	inst_iter = scf_iter_create(handle);
	CHECK_NOT_NULL(inst_iter);

	err = scf_handle_bind(handle);
	CHECK_FOR_ERR(err == -1);

	err = scf_handle_get_scope(handle, SCF_SCOPE_LOCAL, scope);
	CHECK_FOR_ERR(err == -1);

	err = scf_iter_scope_services(svc_iter, scope);
	CHECK_FOR_ERR(err == -1);

	while ((r = scf_iter_next_service(svc_iter, svc)) > 0) {
		err = scf_iter_service_instances(inst_iter, svc);
		CHECK_FOR_ERR(err == -1);

		while ((r = scf_iter_next_instance(inst_iter, inst)) > 0) {
			int frmi_len = scf_instance_to_fmri(inst, fmri, sz);
			if (frmi_len < 0) {
				err++;
				goto finally;
			}
			(void) add_software_module(out, fmri, svc);
		}
		if (r < 0)
			break;
	}
	if (r < 0) {
		err++;
	}

finally:
	if (inst_iter != NULL)
		scf_iter_destroy(inst_iter);
	if (svc_iter != NULL)
		scf_iter_destroy(svc_iter);
	if (inst != NULL)
		scf_instance_destroy(inst);
	if (svc != NULL)
		scf_service_destroy(svc);
	if (scope != NULL)
		scf_scope_destroy(scope);
	if (handle != NULL)
		scf_handle_destroy(handle);
	if (fmri != NULL)
		free(fmri);
	return (err);
}

static int
add_payload_data(asr_buf_t *out, char *fname, char *name)
{
	int err = 0;
	int line;
	FILE *in = fopen(fname, "r");

	if (in == NULL)
		return (1);
	if (name == NULL)
		name = ASR_AUDIT_PAYLOAD_GENERIC;

	if (asr_buf_append_pad(out, PAD_COMPONENT) != 0 ||
	    asr_buf_append(out, "<payload name='%s'><![CDATA[\n", name) != 0) {
		(void) fclose(in);
		return (ASR_FAILURE);
	}

	do {
		err = asr_buf_append_pad(out, PAD_COMPONENT);
		line = asr_buf_readln(out, in);
	} while (line != 0 && err == 0);

	err |= asr_buf_append_str(out, "]]>\n");
	err |= asr_buf_append_pad(out, PAD_COMPONENT);
	err |= asr_buf_append_str(out, "</payload>\n");
	err |= fclose(in);
	return (err);
}

static int
add_payload_cmd(asr_handle_t *ah, asr_buf_t *out, char *name, char *payload)
{
	int err;
	asr_buf_t *cmd;
	char *outfile = asr_tmpfile(ah);

	if (outfile == NULL)
		return (ASR_FAILURE);

	if ((cmd = asr_buf_alloc(ASR_AUDIT_LINESIZE)) == NULL) {
		free(outfile);
		return (ASR_FAILURE);
	}

	if (asr_buf_append(cmd, "%s > %s", payload, outfile) != 0) {
		asr_buf_free(cmd);
		free(outfile);
		return (ASR_FAILURE);
	}

	if ((err = system(asr_buf_data(cmd))) != 0) {
		(void) asr_error(EASR_SYSTEM,
		    "Error (%d) calling audit payload command (%s)",
		    err, asr_buf_data(cmd));
		asr_buf_free(cmd);
		(void) unlink(outfile);
		free(outfile);
		return (ASR_FAILURE);
	}
	asr_buf_free(cmd);

	err = add_payload_data(out, outfile, name);
	(void) unlink(outfile);
	free(outfile);

	return (err);
}

/*
 * Collects software package information and print it out as ASR
 * software-package elements.
 */
static int
add_payload(asr_handle_t *ah, asr_buf_t *out)
{
	int result = 0;
	int rollback_index = out->asrb_length;
	char *name = asr_getprop_str(ah, ASR_PROP_AUDIT_PAYLOAD_NAME);
	char *cmd = asr_getprop_str(ah, ASR_PROP_AUDIT_PAYLOAD_CMD);

	if (cmd == NULL || cmd[0] == '\0')
		return (result);

	result = add_payload_cmd(ah, out, name, cmd);

	if (result != ASR_OK && rollback_index > 0) {
		out->asrb_length = rollback_index;
	}
	return (result);
}

/*
 * Creates an ASR audit message and places it in out_msg.  The message must
 * be released when finished using it.
 * Returns non-zero, sets out_msg to NULL and sets asr_errno
 * if there is an error.
 */
int
asr_audit(asr_handle_t *ah, asr_message_t **out_msg)
{
	int err;
	asr_message_t *msg = NULL;
	asr_buf_t *buf = asr_buf_alloc(ASR_AUDIT_BUFSIZE);

	if (buf == NULL)
		return (asr_set_errno(EASR_NOMEM));

	err = asr_msg_start(ah, buf);
	err |= asr_buf_append_xml_elem(buf, 1, "component-list");
	CHECK_FOR_ERR(err);

	if (asr_getprop_bool(ah, ASR_PROP_AUDIT_SEND_FRU, B_TRUE)) {
		err = add_hardware_components(ah, buf);
		CHECK_FOR_ERR(err);
	}

	if (asr_getprop_bool(ah, ASR_PROP_AUDIT_SEND_PKG, B_TRUE)) {
		err = add_software_packages(ah, buf);
		CHECK_FOR_ERR(err);
	}

	if (asr_getprop_bool(ah, ASR_PROP_AUDIT_SEND_SVC, B_TRUE)) {
		err = add_software_modules(buf);
		CHECK_FOR_ERR(err);
	}

	err = asr_buf_append_xml_end(buf, 1, "component-list");
	CHECK_FOR_ERR(err);

	if (asr_getprop_bool(ah, ASR_PROP_AUDIT_SEND_PAYLOAD, B_TRUE)) {
		if (add_payload(ah, buf) != 0)
			asr_log_warn(ah, "Unable to add audit payload.");
	}

	err = asr_msg_end(buf);

finally:
	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_AUDIT)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}

	*out_msg = msg;
	return (err);
}
