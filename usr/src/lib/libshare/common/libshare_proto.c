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

#include <strings.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>
#include <libintl.h>
#include <libnvpair.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>

#include "libshare.h"
#include "libshare_impl.h"

#define	CHUNK_SIZE	256

/*
 * sa_get_protocols
 *
 * Return list of installed protocol plugins
 */
int
sa_get_protocols(sa_proto_t **protos)
{
	return (saplugin_get_protos(protos));
}

/*
 * saproto_share_parse
 *
 * call the protocol plugin op sap_share_parse
 * for the specified protocol.
 */
int
saproto_share_parse(sa_proto_t proto, const char *propstr, int unset,
    nvlist_t **prot_nvl, char *errbuf, size_t buflen)
{
	sa_proto_ops_t *ops;
	int ret;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_parse == NULL)) {
		ret = SA_NOT_SUPPORTED;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(ret), sa_proto_to_val(proto));
		return (ret);
	}

	return (ops->sap_share_parse(propstr, unset, prot_nvl, errbuf, buflen));
}

/*
 * saproto_share_merge
 *
 * call the protocol plugin op sap_share_merge
 * for the specified protocol.
 */
int
saproto_share_merge(sa_proto_t proto, nvlist_t *dst_prot_nvl,
    nvlist_t *src_prot_nvl, int unset, char *errbuf, size_t buflen)
{
	sa_proto_ops_t *ops;
	int ret;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_merge == NULL)) {
		ret = SA_NOT_SUPPORTED;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(ret), sa_proto_to_val(proto));
		return (ret);
	}

	return (ops->sap_share_merge(dst_prot_nvl, src_prot_nvl, unset,
	    errbuf, buflen));
}

/*
 * saproto_share_set_def_proto
 *
 * call the protocol plugin op sap_share_set_def_proto
 * to add the default protocol properties to the share.
 */
int
saproto_share_set_def_proto(sa_proto_t proto, nvlist_t *share)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_set_def_proto == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_share_set_def_proto(share));
}

int
saproto_share_validate_name(const char *sh_name, sa_proto_t proto,
    boolean_t new, char *errbuf, size_t buflen)
{
	sa_proto_ops_t *ops;
	int ret;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_validate_name == NULL)) {
		ret = SA_NOT_SUPPORTED;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(ret), sa_proto_to_val(proto));
		return (ret);
	}

	return (ops->sap_share_validate_name(sh_name, new, errbuf, buflen));
}

int
saproto_share_validate(nvlist_t *share, sa_proto_t proto,
    boolean_t new, char *errbuf, size_t buflen)
{
	sa_proto_ops_t *ops;
	int ret;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_validate == NULL)) {
		ret = SA_NOT_SUPPORTED;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(ret), sa_proto_to_val(proto));
		return (ret);
	}

	return (ops->sap_share_validate(share, new, errbuf, buflen));
}

/*
 * saproto_share_format_props(nvlist_t *props, char **retbuf)
 *
 * format the props into the protocol dependent string that is needed
 * by sharetab.
 */
int
saproto_share_format_props(nvlist_t *props, sa_proto_t proto, char **retbuf)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_prop_format == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_share_prop_format(props, retbuf));
}

/*
 * saproto_share_publish
 *
 * Publish the share for the specified protocol. proto must be
 * a specific protocol (SA_PROT_NFS, SA_PROT_SMB). SA_PROT_ALL
 * is handled by sa_share_publish.
 *
 * It is up to the caller to determine if sharing is enabled for
 * the path / dataset
 */
int
saproto_share_publish(nvlist_t *share, sa_proto_t proto, int wait)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_publish == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_share_publish(share, wait));
}

/*
 * saproto_share_unpublish
 *
 * Unpublish the share for the specified protocol. proto must be
 * a specific protocol (SA_PROT_NFS, SA_PROT_SMB). SA_PROT_ALL
 * is handled by the caller (sa_share_unpublish).
 *
 */
int
saproto_share_unpublish(nvlist_t *share, sa_proto_t proto, int wait)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_unpublish == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_share_unpublish(share, wait));
}

/*
 * saproto_share_unpublish_byname
 *
 * Unpublish the share for the specified protocol. proto must be
 * a specific protocol (SA_PROT_NFS, SA_PROT_SMB). SA_PROT_ALL
 * is handled by the caller (sa_share_unpublish).
 *
 */
int
saproto_share_unpublish_byname(const char *sh_name, const char *sh_path,
    sa_proto_t proto, int wait)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_unpublish_byname == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_share_unpublish_byname(sh_name, sh_path, wait));
}

int
saproto_share_publish_admin(const char *mntpnt, sa_proto_t proto)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_share_publish_admin == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_share_publish_admin(mntpnt));
}

/*
 * sa_fs_publish will call with specific protocol, no need
 * to worry about SA_PROT_ALL
 */
int
saproto_fs_publish(nvlist_t *sh_list, sa_proto_t proto, int wait)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_fs_publish == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_fs_publish(sh_list, wait));
}

/*
 * saproto_fs_unpublish will call with specific protocol, no need
 * to worry about SA_PROT_ALL
 */
int
saproto_fs_unpublish(nvlist_t *sh_list, sa_proto_t proto, int wait)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if ((ops == NULL) || (ops->sap_fs_unpublish == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sap_fs_unpublish(sh_list, wait));
}

/*
 * sharectl protocol property management routines
 */

/*
 * sa_proto_get_features
 *
 * Return a mask of the protocol features supported.
 */
uint64_t
sa_proto_get_featureset(sa_proto_t proto)
{
	sa_proto_ops_t *ops;
	uint64_t features;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if (ops == NULL)
		return (0);

	if (ops->sap_proto_get_features == NULL)
		return (0);

	if (ops->sap_proto_get_features(&features) == SA_OK)
		return (features);
	else
		return (0);
}

/*
 * sa_proto_get_proplist
 *
 * return nvlist of protocol properties
 */
nvlist_t *
sa_proto_get_proplist(sa_proto_t proto)
{
	sa_proto_ops_t *ops;
	nvlist_t *nvl;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if (ops == NULL)
		return (NULL);

	if (ops->sap_proto_get_proplist == NULL)
		return (NULL);

	if (ops->sap_proto_get_proplist(&nvl) == SA_OK)
		return (nvl);
	else
		return (NULL);
}

char *
sa_proto_get_status(sa_proto_t proto)
{
	sa_proto_ops_t *ops;
	char *status_str = NULL;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if (ops == NULL)
		return (NULL);

	if (ops->sap_proto_get_status == NULL)
		return (NULL);

	if (ops->sap_proto_get_status(&status_str) == SA_OK)
		return (status_str);
	else
		return (NULL);
}

char *
sa_proto_get_property(sa_proto_t proto, const char *sectname,
    const char *propname)
{
	sa_proto_ops_t *ops;
	char *propval = NULL;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if (ops == NULL)
		return (NULL);

	if (ops->sap_proto_get_property == NULL)
		return (NULL);

	if (ops->sap_proto_get_property(sectname, propname, &propval) == SA_OK)
		return (propval);
	else
		return (NULL);
}

int
sa_proto_set_property(sa_proto_t proto, const char *sectname,
    const char *propname, const char *propval)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if (ops == NULL)
		return (SA_INTERNAL_ERR);

	if (ops->sap_proto_set_property == NULL)
		return (SA_NOT_IMPLEMENTED);

	return (ops->sap_proto_set_property(sectname, propname, propval));
}

int
sa_proto_rem_section(sa_proto_t proto, const char *sectname)
{
	sa_proto_ops_t *ops;

	ops = (sa_proto_ops_t *)saplugin_find_ops(SA_PLUGIN_PROTO, proto);
	if (ops == NULL)
		return (SA_INTERNAL_ERR);

	if (ops->sap_proto_rem_section == NULL)
		return (SA_NOT_IMPLEMENTED);

	return (ops->sap_proto_rem_section(sectname));
}
