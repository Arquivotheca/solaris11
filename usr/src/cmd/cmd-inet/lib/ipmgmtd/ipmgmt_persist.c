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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Contains DB walker functions, which are of type `db_wfunc_t';
 *
 * typedef boolean_t db_wfunc_t(void *cbarg, nvlist_t *db_nvl, char *buf,
 *				size_t bufsize, int *errp);
 *
 * ipadm_rw_db() walks through the data store, one line at a time and calls
 * these call back functions with:
 *	`cbarg'  - callback argument
 *	`db_nvl' - representing a line from DB in nvlist_t form
 *	`buf'	 - character buffer to hold modified line
 *	`bufsize'- size of the buffer
 *	`errp' - captures any error inside the walker function.
 *
 * All the 'write' callback functions modify `db_nvl' based on `cbarg' and
 * copy string representation of `db_nvl' (using ipadm_nvlist2str()) into `buf'.
 * To delete a line from the DB, buf[0] is set to `\0'. Inside ipadm_rw_db(),
 * the modified `buf' is written back into DB.
 *
 * All the 'read' callback functions, retrieve the information from the DB, by
 * reading `db_nvl' and then populate the `cbarg'.
 */

#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <inet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "ipmgmt_impl.h"

/* SCF related property group names and property names */
#define	IPMGMTD_APP_PG		"ipmgmtd"
#define	IPMGMTD_PROP_DBVER	"datastore_version"

#define	ATYPE	"_atype"		/* name of the address type nvpair */
#define	FLAGS	"_flags"		/* name of the flags nvpair */

/*
 * flag used by ipmgmt_persist_aobjmap() to indicate address type is
 * IPADM_ADDR_IPV6_ADDRCONF.
 */
#define	IPMGMT_ATYPE_V6ACONF	0x1

extern pthread_rwlock_t ipmgmt_dbconf_lock;

/* signifies whether volatile copy of data store is in use */
static boolean_t ipmgmt_rdonly_root = B_FALSE;

/*
 * Checks if the database nvl, `db_nvl' contains and matches ALL of the passed
 * in private nvpairs `proto', `ifname' & `aobjname'.
 */
static boolean_t
ipmgmt_nvlist_match(nvlist_t *db_nvl, const char *proto, const char *ifname,
    const char *aobjname)
{
	char		*db_proto = NULL, *db_ifname = NULL;
	char		*db_aobjname = NULL;
	nvpair_t	*nvp;
	char		*name;

	/* walk through db_nvl and retrieve all its private nvpairs */
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(IPADM_NVP_PROTONAME, name) == 0)
			(void) nvpair_value_string(nvp, &db_proto);
		else if (strcmp(IPADM_NVP_IFNAME, name) == 0)
			(void) nvpair_value_string(nvp, &db_ifname);
		else if (strcmp(IPADM_NVP_AOBJNAME, name) == 0)
			(void) nvpair_value_string(nvp, &db_aobjname);
	}

	if (proto != NULL && proto[0] == '\0')
		proto = NULL;
	if (ifname != NULL && ifname[0] == '\0')
		ifname = NULL;
	if (aobjname != NULL && aobjname[0] == '\0')
		aobjname = NULL;

	if ((proto == NULL && db_proto != NULL) ||
	    (proto != NULL && db_proto == NULL) ||
	    strcmp(proto, db_proto) != 0) {
		/* no intersection - different protocols. */
		return (B_FALSE);
	}
	if ((ifname == NULL && db_ifname != NULL) ||
	    (ifname != NULL && db_ifname == NULL) ||
	    strcmp(ifname, db_ifname) != 0) {
		/* no intersection - different interfaces. */
		return (B_FALSE);
	}
	if ((aobjname == NULL && db_aobjname != NULL) ||
	    (aobjname != NULL && db_aobjname == NULL) ||
	    strcmp(aobjname, db_aobjname) != 0) {
		/* no intersection - different address objects */
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Checks if the database nvl, `db_nvl' and the input nvl, `in_nvl' intersects.
 */
static boolean_t
ipmgmt_nvlist_intersects(nvlist_t *db_nvl, nvlist_t *in_nvl)
{
	nvpair_t	*nvp;
	char		*name;
	char		*proto = NULL, *ifname = NULL, *aobjname = NULL;

	/* walk through in_nvl and retrieve all its private nvpairs */
	for (nvp = nvlist_next_nvpair(in_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(in_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(IPADM_NVP_PROTONAME, name) == 0)
			(void) nvpair_value_string(nvp, &proto);
		else if (strcmp(IPADM_NVP_IFNAME, name) == 0)
			(void) nvpair_value_string(nvp, &ifname);
		else if (strcmp(IPADM_NVP_AOBJNAME, name) == 0)
			(void) nvpair_value_string(nvp, &aobjname);
	}

	return (ipmgmt_nvlist_match(db_nvl, proto, ifname, aobjname));
}

/*
 * Checks if the database nvl, `db_nvl', contains and matches ANY of the passed
 * in private nvpairs `proto', `ifname' & `aobjname'.
 */
static boolean_t
ipmgmt_nvlist_contains(nvlist_t *db_nvl, const char *proto,
    const char *ifname, char *aobjname)
{
	char		*db_ifname = NULL, *db_proto = NULL;
	char		*db_aobjname = NULL;
	nvpair_t	*nvp;
	char		*name;

	/* walk through db_nvl and retrieve all private nvpairs */
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(IPADM_NVP_PROTONAME, name) == 0)
			(void) nvpair_value_string(nvp, &db_proto);
		else if (strcmp(IPADM_NVP_IFNAME, name) == 0)
			(void) nvpair_value_string(nvp, &db_ifname);
		else if (strcmp(IPADM_NVP_AOBJNAME, name) == 0)
			(void) nvpair_value_string(nvp, &db_aobjname);
	}

	if (proto != NULL && proto[0] != '\0') {
		if ((db_proto == NULL || strcmp(proto, db_proto) != 0))
			return (B_FALSE);
	}
	if (ifname != NULL && ifname[0] != '\0') {
		if ((db_ifname == NULL || strcmp(ifname, db_ifname) != 0))
			return (B_FALSE);
	}
	if (aobjname != NULL && aobjname[0] != '\0') {
		if ((db_aobjname == NULL || strcmp(aobjname, db_aobjname) != 0))
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Retrieves the property value from the DB. The property whose value is to be
 * retrieved is in `pargp->ia_pname'.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_getprop(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_prop_arg_t	*pargp = arg;
	boolean_t		cont = B_TRUE;
	char			*pval;
	int			err = 0;

	*errp = 0;

	if (!ipmgmt_nvlist_match(db_nvl, pargp->ia_module,
	    pargp->ia_ifname, pargp->ia_aobjname))
		return (B_TRUE);

	if ((err = nvlist_lookup_string(db_nvl, pargp->ia_pname,
	    &pval)) == 0) {
		(void) strlcpy(pargp->ia_pval, pval, sizeof (pargp->ia_pval));
		/*
		 * We have retrieved what we are looking for.
		 * Stop the walker.
		 */
		cont = B_FALSE;
	} else {
		if (err == ENOENT)
			err = 0;
		*errp = err;
	}

	return (cont);
}

/*
 * Removes the property value from the DB. The property whose value is to be
 * removed is in `pargp->ia_pname'.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_resetprop(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_prop_arg_t	*pargp = arg;

	*errp = 0;
	if (!ipmgmt_nvlist_match(db_nvl, pargp->ia_module,
	    pargp->ia_ifname, pargp->ia_aobjname))
		return (B_TRUE);

	if (!nvlist_exists(db_nvl, pargp->ia_pname))
		return (B_TRUE);

	/*
	 * We found the property in the DB. If IPMGMT_REMOVE is not set then
	 * delete the entry from the db. If it is set, then the property is a
	 * multi-valued property so just remove the specified values from DB.
	 */
	if (pargp->ia_flags & IPMGMT_REMOVE) {
		char	*dbpval = NULL;
		char	*inpval = pargp->ia_pval;
		char	pval[MAXPROPVALLEN];
		char	*val, *lasts;

		*errp = nvlist_lookup_string(db_nvl, pargp->ia_pname, &dbpval);
		if (*errp != 0)
			return (B_FALSE);

		/*
		 * multi-valued properties are represented as comma separated
		 * values. Use string tokenizer functions to split them and
		 * search for the value to be removed.
		 */
		bzero(pval, sizeof (pval));
		if ((val = strtok_r(dbpval, ",", &lasts)) != NULL) {
			if (strcmp(val, inpval) != 0)
				(void) strlcat(pval, val, MAXPROPVALLEN);
			while ((val = strtok_r(NULL, ",", &lasts)) != NULL) {
				if (strcmp(val, inpval) != 0) {
					if (pval[0] != '\0')
						(void) strlcat(pval, ",",
						    MAXPROPVALLEN);
					(void) strlcat(pval, val,
					    MAXPROPVALLEN);
				}
			}
		} else {
			if (strcmp(dbpval, inpval) != 0)
				*errp = ENOENT;
			else
				buf[0] =  '\0';
			return (B_FALSE);
		}
		*errp = nvlist_add_string(db_nvl, pargp->ia_pname, pval);
		if (*errp != 0)
			return (B_FALSE);

		(void) memset(buf, 0, buflen);
		if (ipadm_nvlist2str(db_nvl, buf, buflen) == 0) {
			/* buffer overflow */
			*errp = ENOBUFS;
		}
	} else {
		buf[0] = '\0';
	}

	/* stop the search */
	return (B_FALSE);
}

/*
 * Input arguments can have IPADM_NVP_AOBJNAME or IPADM_NVP_IFNAME. A match is
 * found, when one of the following occurs first.
 * - the input aobjname matches the db aobjname. Return the db address.
 * - the input interface matches the db interface. Return all the
 *   matching db lines with addresses.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_getaddr(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_getaddr_cbarg_t	*cbarg = arg;
	char		*db_aobjname = NULL;
	char		*db_ifname = NULL;
	nvlist_t	*db_addr = NULL;
	char		name[IPMGMT_STRSIZE];
	nvpair_t	*nvp;
	boolean_t	add_nvl = B_FALSE;

	/* Parse db nvlist */
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		if (nvpair_type(nvp) == DATA_TYPE_NVLIST)
			(void) nvpair_value_nvlist(nvp, &db_addr);
		else if (strcmp(nvpair_name(nvp), IPADM_NVP_IFNAME) == 0)
			(void) nvpair_value_string(nvp, &db_ifname);
		else if (strcmp(nvpair_name(nvp), IPADM_NVP_AOBJNAME) == 0)
			(void) nvpair_value_string(nvp, &db_aobjname);
	}

	if (db_aobjname == NULL) /* Not an address */
		return (B_TRUE);

	/* Check for a match between the aobjnames or the interface name */
	if (cbarg->cb_aobjname[0] != '\0') {
		if (strcmp(cbarg->cb_aobjname, db_aobjname) == 0)
			add_nvl = B_TRUE;
	} else if (cbarg->cb_ifname[0] != '\0') {
		if (strcmp(cbarg->cb_ifname, db_ifname) == 0)
			add_nvl = B_TRUE;
	} else {
		add_nvl = B_TRUE;
	}

	if (add_nvl) {
		(void) snprintf(name, sizeof (name), "%s_%d", db_ifname,
		    cbarg->cb_ocnt);
		*errp = nvlist_add_nvlist(cbarg->cb_onvl, name, db_nvl);
		if (*errp == 0)
			cbarg->cb_ocnt++;
	}
	return (B_TRUE);
}

/*
 * This function only gets called if a volatile filesystem version
 * of the configuration file has been created. This only happens in the
 * extremely rare case that a request has been made to update the configuration
 * file at boottime while the root filesystem was read-only. This is
 * really a rare occurrence now that we don't support UFS root filesystems
 * any longer. This function will periodically attempt to write the
 * configuration back to its location on the root filesystem. Success
 * will indicate that the filesystem is no longer read-only.
 */
/* ARGSUSED */
static void *
ipmgmt_db_restore_thread(void *arg)
{
	int err;

	for (;;) {
		(void) sleep(5);
		(void) pthread_rwlock_wrlock(&ipmgmt_dbconf_lock);
		if (!ipmgmt_rdonly_root)
			break;
		err = ipmgmt_cpfile(IPADM_VOL_DB_FILE, IPADM_DB_FILE, B_FALSE);
		if (err == 0) {
			ipmgmt_rdonly_root = B_FALSE;
			break;
		}
		(void) pthread_rwlock_unlock(&ipmgmt_dbconf_lock);
	}
	(void) pthread_rwlock_unlock(&ipmgmt_dbconf_lock);
	return (NULL);
}

/*
 * This function takes the appropriate lock, read or write, based on the
 * `db_op' and then calls DB walker ipadm_rw_db(). The code is complicated
 * by the fact that we are not always guaranteed to have a writable root
 * filesystem since it is possible that we are reading or writing during
 * bootime while the root filesystem is still read-only. This is, by far,
 * the exception case. Normally, this function will be called when the
 * root filesystem is writable. In the unusual case where this is not
 * true, the configuration file is copied to the volatile file system
 * and is updated there until the root filesystem becomes writable. At
 * that time the file will be moved back to its proper location by
 * ipmgmt_db_restore_thread().
 */
extern int
ipmgmt_db_walk(db_wfunc_t *db_walk_func, void *db_warg, ipadm_db_op_t db_op)
{
	int		err;
	boolean_t	writeop;
	mode_t		mode;
	pthread_t	tid;
	pthread_attr_t	attr;

	writeop = (db_op != IPADM_DB_READ);
	if (writeop) {
		(void) pthread_rwlock_wrlock(&ipmgmt_dbconf_lock);
		mode = IPADM_FILE_MODE;
	} else {
		(void) pthread_rwlock_rdlock(&ipmgmt_dbconf_lock);
		mode = 0;
	}

	/*
	 * Did a previous write attempt fail? If so, don't even try to
	 * read/write to IPADM_DB_FILE.
	 */
	if (!ipmgmt_rdonly_root) {
		err = ipadm_rw_db(db_walk_func, db_warg, IPADM_DB_FILE,
		    mode, db_op);
		if (err != EROFS)
			goto done;
	}

	/*
	 * If we haven't already copied the file to the volatile
	 * file system, do so. This should only happen on a failed
	 * writeop(i.e., we have acquired the write lock above).
	 */
	if (access(IPADM_VOL_DB_FILE, F_OK) != 0) {
		assert(writeop);
		err = ipmgmt_cpfile(IPADM_DB_FILE, IPADM_VOL_DB_FILE, B_TRUE);
		if (err != 0)
			goto done;
		(void) pthread_attr_init(&attr);
		(void) pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		err = pthread_create(&tid, &attr, ipmgmt_db_restore_thread,
		    NULL);
		(void) pthread_attr_destroy(&attr);
		if (err != 0) {
			(void) unlink(IPADM_VOL_DB_FILE);
			goto done;
		}
		ipmgmt_rdonly_root = B_TRUE;
	}

	/*
	 * Read/write from the volatile copy.
	 */
	err = ipadm_rw_db(db_walk_func, db_warg, IPADM_VOL_DB_FILE,
	    mode, db_op);
done:
	(void) pthread_rwlock_unlock(&ipmgmt_dbconf_lock);
	return (err);
}

/*
 * Used to add an entry towards the end of DB. It just returns B_TRUE for
 * every line of the DB. When we reach the end, ipadm_rw_db() adds the
 * line at the end.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_add(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen, int *errp)
{
	return (B_TRUE);
}

/*
 * This function is used to update or create an entry in DB. The nvlist_t,
 * `in_nvl', represents the line we are looking for. Once we ensure the right
 * line from DB, we update that entry.
 */
boolean_t
ipmgmt_db_update(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipadm_dbwrite_cbarg_t	*cb = arg;
	uint_t			flags = cb->dbw_flags;
	nvlist_t		*in_nvl = cb->dbw_nvl;
	nvpair_t		*nvp;
	char			*name, *instrval = NULL, *dbstrval = NULL;
	char			pval[MAXPROPVALLEN];

	*errp = 0;
	if (!ipmgmt_nvlist_intersects(db_nvl, in_nvl))
		return (B_TRUE);

	for (nvp = nvlist_next_nvpair(in_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(in_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (!IPADM_PRIV_NVP(name) && nvlist_exists(db_nvl, name))
			break;
	}

	if (nvp == NULL)
		return (B_TRUE);

	assert(nvpair_type(nvp) == DATA_TYPE_STRING);

	if ((*errp = nvpair_value_string(nvp, &instrval)) != 0)
		return (B_FALSE);

	/*
	 * If IPMGMT_APPEND is set then we are dealing with multi-valued
	 * properties. We append to the entry from the db, with the new value.
	 */
	if (flags & IPMGMT_APPEND) {
		if ((*errp = nvlist_lookup_string(db_nvl, name,
		    &dbstrval)) != 0)
			return (B_FALSE);
		(void) snprintf(pval, MAXPROPVALLEN, "%s,%s", dbstrval,
		    instrval);
		if ((*errp = nvlist_add_string(db_nvl, name, pval)) != 0)
			return (B_FALSE);
	} else {
		/* case	of in-line update of a db entry */
		if ((*errp = nvlist_add_string(db_nvl, name, instrval)) != 0)
			return (B_FALSE);
	}

	(void) memset(buf, 0, buflen);
	if (ipadm_nvlist2str(db_nvl, buf, buflen) == 0) {
		/* buffer overflow */
		*errp = ENOBUFS;
	}

	/* we updated the DB entry, so do not continue */
	return (B_FALSE);
}

/*
 * Helper function used to search for certain nvpairs in `db_nvl' for the
 * given interface `ifname'. The nvpairs that this function looks for
 * include:
 * - IPADM_NVP_FAMILY
 * - IPADM_NVP_IFCLASS
 * - IPADM_NVP_UNDERIF
 * - "standby"
 *
 * When any of the above is found, it adds the nvpair to `onvl'.
 */
int
i_ipmgmt_db_getif(nvlist_t *onvl, nvlist_t *db_nvl, const char *ifname,
    const char *db_ifname)
{
	char		*db_val;
	char		*curval;
	char		*name;
	char		*over;
	nvpair_t	*nvp;
	int		err = 0;

	if (strcmp(ifname, db_ifname) != 0) {
		/*
		 * `db_ifname' might be the IPMP interface for
		 * the underlying interface `ifname'.
		 */
		if (nvlist_lookup_string(db_nvl, IPADM_NVP_UNDERIF,
		    &over) == 0 && strcmp(ifname, over) == 0) {
			err = nvlist_add_string(onvl, IPADM_NVP_IPMPIF,
			    db_ifname);
		}
		return (err);
	}
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_FAMILY) != 0 &&
		    strcmp(name, IPADM_NVP_IFCLASS) != 0 &&
		    strcmp(name, "standby") != 0 &&
		    strcmp(name, IPADM_NVP_UNDERIF) != 0)
			continue;
		if ((err = nvpair_value_string(nvp, &db_val)) != 0)
			return (err);
		/*
		 * The names IPADM_NVP_FAMILY and IPADM_NVP_UNDERIF for an
		 * interface can have multiple values, but these values are
		 * stored as separate lines in the file. We concatenate
		 * all the values for each of these names into a single
		 * nvpair in `onvl'.
		 */
		if (nvlist_lookup_string(onvl, name, &curval) == 0 &&
		    curval[0] != '\0') {
			size_t	size;
			char	*lasts, *newval, *oldval, *val;

			oldval = strdup(curval);
			if (oldval == NULL)
				return (errno);
			val = strtok_r(curval, ",", &lasts);
			for (; val != NULL;
			    val = strtok_r(NULL, ",", &lasts)) {
				if (strcmp(val, db_val) == 0)
					break;
			}
			if (val != NULL) {
				free(oldval);
				continue;
			}
			size = strlen(oldval) + 2 + strlen(db_val);
			if ((newval = malloc(size)) == NULL) {
				free(oldval);
				return (errno);
			}
			(void) snprintf(newval, size, "%s,%s", oldval, db_val);
			err = nvlist_add_string(onvl, name, newval);
			free(newval);
			free(oldval);
		} else {
			err = nvlist_add_string(onvl, name, db_val);
		}
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * For the given `cbarg->cb_ifname' interface, retrieves any persistent
 * interface information (used in 'ipadm show-if')
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_getif(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_getif_cbarg_t	*cbarg = arg;
	char			*ifname = cbarg->cb_ifname;
	char			*intf = NULL;
	nvlist_t		*onvl = cbarg->cb_onvl;
	nvlist_t		*nvlif = NULL;
	nvpair_t		*nvp;
	char			*name;

	*errp = 0;

	if (nvlist_lookup_string(db_nvl, IPADM_NVP_IFNAME, &intf) != 0)
		return (B_TRUE);

	/*
	 * Create and append an nvlist for the current interface
	 * if one doesn't already exist in `onvl'.
	 */
	if (!nvlist_exists(onvl, intf)) {
		if (ifname[0] == '\0' || strcmp(ifname, intf) == 0) {
			if ((*errp = nvlist_alloc(&nvlif, NV_UNIQUE_NAME,
			    0)) != 0 || (*errp = nvlist_add_nvlist(onvl,
			    intf, nvlif)) != 0) {
				nvlist_free(nvlif);
				return (B_FALSE);
			}
			nvlist_free(nvlif);
			cbarg->cb_ocnt++;
		} else if (nvlist_empty(onvl)) {
			/*
			 * No interface has been found yet, for which
			 * the `db_nvl' might contain an update.
			 */
			return (B_TRUE);
		}
	}
	/*
	 * Go through each interface accumulated in `onvl' to check
	 * if the current line contains an update for that interface.
	 */
	for (nvp = nvlist_next_nvpair(onvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(onvl, nvp)) {
		name = nvpair_name(nvp);
		if (nvpair_value_nvlist(nvp, &nvlif) != 0)
			continue;
		*errp = i_ipmgmt_db_getif(nvlif, db_nvl, name, intf);
		if (*errp != 0)
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Finds which address families exist for the given interface.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_searchif(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_if_cbarg_t	*cbarg = arg;
	char			*ifname = cbarg->cb_ifname;
	char			*db_ifname;
	char			*afstr;
	sa_family_t		af;

	*errp = 0;
	if (nvlist_lookup_string(db_nvl, IPADM_NVP_IFNAME, &db_ifname) != 0 ||
	    strcmp(ifname, db_ifname) != 0 ||
	    nvlist_lookup_string(db_nvl, IPADM_NVP_FAMILY, &afstr) != 0) {
		return (B_TRUE);
	}
	af = atoi(afstr);
	if (af == AF_INET) {
		cbarg->cb_hasv4 = B_TRUE;
	} else {
		assert(af == AF_INET6);
		cbarg->cb_hasv6 = B_TRUE;
	}
	if (cbarg->cb_hasv4 && cbarg->cb_hasv6)
		return (B_FALSE);
	return (B_TRUE);
}

/*
 * Finds if the given interface is an underlying interface.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_is_underif(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_if_cbarg_t	*cbarg = arg;
	char			*ifname = cbarg->cb_ifname;
	char			*db_ifname;

	*errp = 0;
	if (nvlist_lookup_string(db_nvl, IPADM_NVP_UNDERIF, &db_ifname) == 0 &&
	    strcmp(ifname, db_ifname) == 0) {
		cbarg->cb_isunder = B_TRUE;
		return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Deletes those entries from the database for which interface name
 * matches with the given `cbarg->cb_ifname'
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_resetif(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_if_cbarg_t *cbarg = arg;
	boolean_t	isv6 = (cbarg->cb_family == AF_INET6);
	char		*ifname = cbarg->cb_ifname;
	char		*modstr = NULL;
	char		*db_afstr = NULL;
	char		*db_ifname = NULL;
	char		*db_underif = NULL;
	char		*aobjname;
	uint_t		proto;
	ipmgmt_aobjmap_t *head;
	boolean_t	aobjfound = B_FALSE;
	nvpair_t	*nvp;
	char		*name;
	boolean_t	oaf_exists = (isv6 ? cbarg->cb_hasv4 : cbarg->cb_hasv6);

	*errp = 0;

	/* walk through db_nvl and retrieve all private nvpairs */
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(IPADM_NVP_IFNAME, name) == 0) {
			(void) nvpair_value_string(nvp, &db_ifname);
		} else if (strcmp(IPADM_NVP_FAMILY, name) == 0) {
			(void) nvpair_value_string(nvp, &db_afstr);
		} else if (strcmp(IPADM_NVP_UNDERIF, name) == 0) {
			(void) nvpair_value_string(nvp, &db_underif);
		}
	}
	if (db_ifname == NULL)
		return (B_TRUE);
	if (db_underif != NULL && !oaf_exists) {
		/*
		 * If a match for `ifname' is found in the current line,
		 * delete the line.
		 * If `db_nvl' does not have `ifname', the following is the
		 * scenario for which the current line should be deleted.
		 * - Input interface `ifname' is an IP interface,
		 *    db interface `db_ifname' is an IPMP interface for which
		 *    `ifname' is an underlying interface.
		 * Even if the above is true, the line should not be
		 * deleted if the other address family for the IP interface
		 * `ifname' exists.
		 */
		if (strcmp(db_ifname, ifname) == 0 ||
		    strcmp(db_underif, ifname) == 0)
			goto delete;
		return (B_TRUE);
	}
	if (strcmp(db_ifname, ifname) != 0)
		return (B_TRUE);
	if (db_afstr != NULL) {
		if (atoi(db_afstr) == cbarg->cb_family)
			goto delete;
		return (B_TRUE);
	}

	/* Reset all the interface configurations for 'ifname' */
	if (isv6 && (nvlist_exists(db_nvl, IPADM_NVP_IPV6ADDR) ||
	    nvlist_exists(db_nvl, IPADM_NVP_INTFID))) {
		goto delete;
	}
	if (!isv6 &&
	    (nvlist_exists(db_nvl, IPADM_NVP_IPV4ADDR) ||
	    nvlist_exists(db_nvl, IPADM_NVP_DHCP))) {
		goto delete;
	}
	if (nvlist_lookup_string(db_nvl, IPADM_NVP_AOBJNAME, &aobjname) == 0) {
		/*
		 * This must be an address property. Delete this
		 * line if there is a match in the address family.
		 */
		head = aobjmap.aobjmap_head;
		while (head != NULL) {
			if (strcmp(head->am_aobjname, aobjname) == 0) {
				aobjfound = B_TRUE;
				if (head->am_family == cbarg->cb_family)
					goto delete;
			}
			head = head->am_next;
		}
		/*
		 * If aobjfound = B_FALSE, then this address is not
		 * available in active configuration. We should go ahead
		 * and delete it.
		 */
		if (!aobjfound)
			goto delete;
	}

	/*
	 * If we are removing both v4 and v6 interface, then we get rid of
	 * all the properties for that interface. On the other hand, if we
	 * are deleting only v4 instance of an interface, then we delete v4
	 * properties only.
	 */
	if (nvlist_lookup_string(db_nvl, IPADM_NVP_PROTONAME, &modstr) == 0) {
		proto = ipadm_str2proto(modstr);
		switch (proto) {
		case MOD_PROTO_IPV6:
			if (isv6)
				goto delete;
			break;
		case MOD_PROTO_IPV4:
			if (!isv6)
				goto delete;
			break;
		case MOD_PROTO_IP:
			if (!oaf_exists)
				goto delete;
			break;
		}
	}
	/* Not found a match yet. Continue processing the db */
	return (B_TRUE);
delete:
	/* delete the line from the db */
	buf[0] = '\0';
	return (B_TRUE);
}

/*
 * Deletes those entries from the database for which address object name
 * matches with the given `cbarg->cb_aobjname'
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_resetaddr(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_resetaddr_cbarg_t *cbarg = arg;
	char		*aobjname = cbarg->cb_aobjname;

	*errp = 0;
	if (!ipmgmt_nvlist_contains(db_nvl, NULL, NULL, aobjname))
		return (B_TRUE);

	/* delete the line from the db */
	buf[0] = '\0';
	return (B_TRUE);
}

/*
 * Retrieves all interface props, including addresses and list of underlying
 * interfaces for all interfaces, unless a specific interface has been
 * requested in `cbarg->cb_ifname'.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_initif(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_initif_cbarg_t	*cbarg = arg;
	nvlist_t		*onvl = cbarg->cb_onvl;
	char			*db_ifname;
	char			name[IPMGMT_STRSIZE];
	char			*over;

	*errp = 0;
	if (nvlist_lookup_string(db_nvl, IPADM_NVP_IFNAME, &db_ifname) != 0)
		return (B_TRUE);

	if (cbarg->cb_ifname[0] == '\0' ||
	    strcmp(db_ifname, cbarg->cb_ifname) == 0 ||
	    (nvlist_lookup_string(db_nvl, IPADM_NVP_UNDERIF, &over) == 0 &&
	    strcmp(over, cbarg->cb_ifname) == 0)) {
		(void) snprintf(name, sizeof (name), "%s_%d", db_ifname,
		    cbarg->cb_ocnt);
		*errp = nvlist_add_nvlist(onvl, name, db_nvl);
		if (*errp == 0)
			cbarg->cb_ocnt++;
	}
	return (B_TRUE);
}

/*
 * helper function for ipmgmt_aobjmap_op(). Adds the node pointed by `nodep'
 * into `aobjmap' structure.
 */
int
i_ipmgmt_add_amnode(ipmgmt_aobjmap_t *nodep)
{
	ipmgmt_aobjmap_t	*new, *head;

	head = aobjmap.aobjmap_head;
	if ((new = malloc(sizeof (ipmgmt_aobjmap_t))) == NULL)
		return (ENOMEM);
	*new = *nodep;
	new->am_next = NULL;

	/* Add the node at the beginning of the list */
	if (head == NULL) {
		aobjmap.aobjmap_head = new;
	} else {
		new->am_next = aobjmap.aobjmap_head;
		aobjmap.aobjmap_head = new;
	}
	return (0);
}

/*
 * A recursive function to generate alphabetized number given a decimal number.
 * Decimal 0 to 25 maps to 'a' to 'z' and then the counting continues with 'aa',
 * 'ab', 'ac', et al.
 */
static void
i_ipmgmt_num2priv_aobjname(uint32_t num, char **cp, char *endp)
{
	if (num >= 26)
		i_ipmgmt_num2priv_aobjname(num / 26 - 1, cp, endp);
	if (*cp != endp) {
		*cp[0] = 'a' + (num % 26);
		(*cp)++;
	}
}

/*
 * Retrieves the decimal number from the given alphabetized number.
 * 'a' to 'z' map to 0 to 25, 'aa' to 'az' map to * 26 to 47, and so on.
 * This does the inverse of the function i_ipmgmt_num2priv_aobjname().
 * The input string should be null-terminated.
 */
static uint32_t
i_ipmgmt_priv_aobjname2num(char *cp)
{
	uint32_t	num = 0;

	if (*cp == '\0')
		return (num);
	while (*(cp + 1) != '\0') {
		assert('a' <= *cp && *cp <= 'z');
		num += 26 * (*cp - 'a' + 1);
		cp++;
	}
	assert('a' <= *cp && *cp <= 'z');
	return (num + *cp - 'a');
}

int
i_ipmgmt_get_priv_aobjname(char *aobjname, size_t len, const char *ifname,
    uint32_t nextnum)
{
	char tmpstr[IPADM_AOBJ_USTRSIZ - 1];  /* 1 for leading  '_' */
	char *cp = tmpstr;
	char *endp = tmpstr + sizeof (tmpstr);

	i_ipmgmt_num2priv_aobjname(nextnum, &cp, endp);

	if (cp == endp)
		return (EINVAL);
	cp[0] = '\0';

	if (snprintf(aobjname, len, "%s/_%s", ifname, tmpstr) >= IPADM_AOBJSIZ)
		return (EINVAL);
	return (0);
}

/*
 * This function generates an `aobjname', when required, and then does
 * lookup-add. If `nodep->am_aobjname' is not an empty string, then it walks
 * through the `aobjmap' to check if an address object with the same
 * `nodep->am_aobjname' exists. If it exists, EEXIST is returned as duplicate
 * `aobjname's are not allowed.
 *
 * If `nodep->am_aobjname' is an empty string then the daemon generates an
 * `aobjname' using the `am_nextnum', which contains the next number to be
 * used to generate `aobjname'. `am_nextnum' is converted to base26 using
 * `a-z' alphabets in i_ipmgmt_num2priv_aobjname().
 *
 * `am_nextnum' will be 0 to begin with. Every time an address object that
 * needs `aobjname' is added it's incremented by 1. So for the first address
 * object on net0 the `am_aobjname' will be net0/_a and `am_nextnum' will be 1.
 * For the second address object on that interface `am_aobjname' will be net0/_b
 * and  `am_nextnum' will incremented to 2.
 */
static int
i_ipmgmt_lookupadd_amnode(ipmgmt_aobjmap_t *nodep)
{
	ipmgmt_aobjmap_t	*head;
	uint32_t		nextnum;
	int			err;

	for (head = aobjmap.aobjmap_head; head != NULL; head = head->am_next)
		if (strcmp(head->am_ifname, nodep->am_ifname) == 0)
			break;
	nextnum = (head == NULL ? 0 : head->am_nextnum);

	/*
	 * if `aobjname' is empty, then the daemon has to generate the
	 * next `aobjname' for the given interface and family.
	 */
	if (nodep->am_aobjname[0] == '\0') {
		err = i_ipmgmt_get_priv_aobjname(nodep->am_aobjname,
		    IPADM_AOBJSIZ, nodep->am_ifname, nextnum);
		if (err != 0)
			return (err);
		nodep->am_nextnum = ++nextnum;
	} else {
		for (head = aobjmap.aobjmap_head; head != NULL;
		    head = head->am_next) {
			if (strcmp(head->am_aobjname, nodep->am_aobjname) == 0)
				return (EEXIST);
		}
		nodep->am_nextnum = nextnum;
	}
	return (i_ipmgmt_add_amnode(nodep));
}

/*
 * Performs following operations on the global `aobjmap' linked list.
 * (a) ADDROBJ_ADD: add or update address object in `aobjmap'
 * (b) ADDROBJ_DELETE: delete address object from `aobjmap'
 * (c) ADDROBJ_LOOKUPADD: place a stub address object in `aobjmap'
 * (d) ADDROBJ_SETLIFNUM: Sets the lifnum for an address object in `aobjmap'
 */
int
ipmgmt_aobjmap_op(ipmgmt_aobjmap_t *nodep, uint32_t op)
{
	ipmgmt_aobjmap_t	*head, *prev, *matched = NULL;
	boolean_t		update = B_TRUE;
	int			err = 0;
	ipadm_db_op_t		db_op;
	char			*sep;

	(void) pthread_rwlock_wrlock(&aobjmap.aobjmap_rwlock);

	head = aobjmap.aobjmap_head;
	switch (op) {
	case ADDROBJ_ADD:
		/*
		 * check for stub nodes (added by ADDROBJ_LOOKUPADD) and
		 * update, else add the new node.
		 */
		for (; head != NULL; head = head->am_next) {
			/*
			 * For IPv6, we need to distinguish between the
			 * linklocal and non-linklocal nodes
			 */
			if (strcmp(head->am_aobjname,
			    nodep->am_aobjname) == 0 &&
			    (head->am_atype != IPADM_ADDR_IPV6_ADDRCONF ||
			    (head->am_linklocal == B_TRUE &&
			    nodep->am_linklocal == B_TRUE)))
				break;
		}

		if (head != NULL) {
			/* update the node */
			(void) strlcpy(head->am_ifname, nodep->am_ifname,
			    sizeof (head->am_ifname));
			head->am_lnum = nodep->am_lnum;
			head->am_family = nodep->am_family;
			head->am_flags = nodep->am_flags;
			head->am_atype = nodep->am_atype;
			if (head->am_atype == IPADM_ADDR_IPV6_ADDRCONF) {
				head->am_ifid = nodep->am_ifid;
				head->am_ifidplen = nodep->am_ifidplen;
				head->am_linklocal = nodep->am_linklocal;
			}
		} else {
			uint32_t nnum;

			for (head = aobjmap.aobjmap_head; head != NULL;
			    head = head->am_next) {
				if (strcmp(head->am_ifname,
				    nodep->am_ifname) == 0)
					break;
			}
			nnum = (head == NULL ? 0 : head->am_nextnum);
			sep = strrchr(nodep->am_aobjname, '/');
			assert(sep != NULL);
			if (*++sep == '_') {
				nodep->am_nextnum = 1 +
				    i_ipmgmt_priv_aobjname2num(sep + 1);
			}
			if (nnum > nodep->am_nextnum)
				nodep->am_nextnum = nnum;
			err = i_ipmgmt_add_amnode(nodep);
		}
		db_op = IPADM_DB_WRITE;
		break;
	case ADDROBJ_DELETE:
		prev = head;
		while (head != NULL) {
			if (strcmp(head->am_aobjname,
			    nodep->am_aobjname) == 0) {
				nodep->am_atype = head->am_atype;
				/*
				 * There could be multiple IPV6_ADDRCONF nodes,
				 * with same address object name, so check for
				 * logical number also.
				 */
				if (head->am_atype !=
				    IPADM_ADDR_IPV6_ADDRCONF ||
				    nodep->am_lnum == head->am_lnum)
					break;
			}
			prev = head;
			head = head->am_next;
		}
		if (head != NULL) {
			/*
			 * If the address object is in both active and
			 * persistent configuration and the user is deleting it
			 * only from active configuration then mark this node
			 * for deletion by reseting IPMGMT_ACTIVE bit.
			 * With this the same address object name cannot
			 * be reused until it is permanently removed.
			 */
			if (head->am_flags == (IPMGMT_ACTIVE|IPMGMT_PERSIST) &&
			    nodep->am_flags == IPMGMT_ACTIVE) {
				/* Update flags in the in-memory map. */
				head->am_flags &= ~IPMGMT_ACTIVE;
				head->am_lnum = -1;

				/* Update info in file. */
				db_op = IPADM_DB_WRITE;
				*nodep = *head;
			} else {
				(void) strlcpy(nodep->am_ifname,
				    head->am_ifname,
				    sizeof (nodep->am_ifname));
				/* otherwise delete the node */
				if (head == aobjmap.aobjmap_head)
					aobjmap.aobjmap_head = head->am_next;
				else
					prev->am_next = head->am_next;
				free(head);
				db_op = IPADM_DB_DELETE;
			}
		} else {
			err = ENOENT;
		}
		break;
	case ADDROBJ_LOOKUPADD:
		err = i_ipmgmt_lookupadd_amnode(nodep);
		update = B_FALSE;
		break;
	case ADDROBJ_SETLIFNUM:
		update = B_FALSE;
		for (; head != NULL; head = head->am_next) {
			if (strcmp(head->am_ifname,
			    nodep->am_ifname) == 0 &&
			    head->am_family == nodep->am_family &&
			    head->am_lnum == nodep->am_lnum) {
				err = EEXIST;
				break;
			}
			if (strcmp(head->am_aobjname,
			    nodep->am_aobjname) == 0) {
				matched = head;
			}
		}
		if (err == EEXIST)
			break;
		if (matched != NULL) {
			/* update the lifnum */
			matched->am_lnum = nodep->am_lnum;
		} else {
			err = ENOENT;
		}
		break;
	default:
		assert(0);
	}

	if (err == 0 && update)
		err = ipmgmt_persist_aobjmap(nodep, db_op);

	(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);

	return (err);
}

/*
 * Given a node in `aobjmap', this function converts it into nvlist_t structure.
 * The content to be written to DB must be represented as nvlist_t.
 */
static int
i_ipmgmt_node2nvl(nvlist_t **nvl, ipmgmt_aobjmap_t *np)
{
	int	err;
	char	strval[IPMGMT_STRSIZE];

	*nvl = NULL;
	if ((err = nvlist_alloc(nvl, NV_UNIQUE_NAME, 0)) != 0)
		goto fail;

	if ((err = nvlist_add_string(*nvl, IPADM_NVP_AOBJNAME,
	    np->am_aobjname)) != 0)
		goto fail;

	if ((err = nvlist_add_string(*nvl, IPADM_NVP_IFNAME,
	    np->am_ifname)) != 0)
		goto fail;

	(void) snprintf(strval, IPMGMT_STRSIZE, "%d", np->am_lnum);
	if ((err = nvlist_add_string(*nvl, IPADM_NVP_LIFNUM, strval)) != 0)
		goto fail;

	(void) snprintf(strval, IPMGMT_STRSIZE, "%d", np->am_family);
	if ((err = nvlist_add_string(*nvl, IPADM_NVP_FAMILY, strval)) != 0)
		goto fail;

	(void) snprintf(strval, IPMGMT_STRSIZE, "%d", np->am_flags);
	if ((err = nvlist_add_string(*nvl, FLAGS, strval)) != 0)
		goto fail;

	(void) snprintf(strval, IPMGMT_STRSIZE, "%d", np->am_atype);
	if ((err = nvlist_add_string(*nvl, ATYPE, strval)) != 0)
		goto fail;

	if (np->am_atype == IPADM_ADDR_IPV6_ADDRCONF) {
		struct sockaddr_in6	*in6;

		in6 = (struct sockaddr_in6 *)&np->am_ifid;
		if (np->am_linklocal &&
		    IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr)) {
			if ((err = nvlist_add_string(*nvl, IPADM_NVP_IPNUMADDR,
			    "default")) != 0)
				goto fail;
		} else {
			char prefix[IPMGMT_STRSIZE];

			if (inet_ntop(AF_INET6, &in6->sin6_addr, strval,
			    IPMGMT_STRSIZE) == NULL) {
				err = errno;
				goto fail;
			}
			(void) snprintf(prefix, sizeof (prefix), "/%d",
			    np->am_ifidplen);
			(void) strlcat(strval, prefix, sizeof (strval));
			if ((err = nvlist_add_string(*nvl, IPADM_NVP_IPNUMADDR,
			    strval)) != 0)
				goto fail;
		}
	} else {
		if ((err = nvlist_add_string(*nvl, IPADM_NVP_IPNUMADDR,
		    "")) != 0)
			goto fail;
	}
	return (err);
fail:
	nvlist_free(*nvl);
	return (err);
}

/*
 * Read the aobjmap data store and build the in-memory representation
 * of the aobjmap. We don't need to hold any locks while building this as
 * we do this in very early stage of daemon coming up, even before the door
 * is opened.
 */
/* ARGSUSED */
extern boolean_t
ipmgmt_aobjmap_init(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	nvpair_t		*nvp = NULL;
	char			*name, *strval = NULL;
	ipmgmt_aobjmap_t 	node;
	struct sockaddr_in6	*in6;
	char			*sep;

	*errp = 0;
	node.am_next = NULL;
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);

		if ((*errp = nvpair_value_string(nvp, &strval)) != 0)
			return (B_TRUE);
		if (strcmp(IPADM_NVP_AOBJNAME, name) == 0) {
			(void) strlcpy(node.am_aobjname, strval,
			    sizeof (node.am_aobjname));
		} else if (strcmp(IPADM_NVP_IFNAME, name) == 0) {
			(void) strlcpy(node.am_ifname, strval,
			    sizeof (node.am_ifname));
		} else if (strcmp(IPADM_NVP_LIFNUM, name) == 0) {
			node.am_lnum = atoi(strval);
		} else if (strcmp(IPADM_NVP_FAMILY, name) == 0) {
			node.am_family = (sa_family_t)atoi(strval);
		} else if (strcmp(FLAGS, name) == 0) {
			node.am_flags = atoi(strval);
		} else if (strcmp(ATYPE, name) == 0) {
			node.am_atype = (ipadm_addr_type_t)atoi(strval);
		} else if (strcmp(IPADM_NVP_IPNUMADDR, name) == 0) {
			if (node.am_atype == IPADM_ADDR_IPV6_ADDRCONF) {
				in6 = (struct sockaddr_in6 *)&node.am_ifid;
				if (strcmp(strval, "default") == 0) {
					bzero(in6, sizeof (node.am_ifid));
					node.am_ifidplen = 0;
					node.am_linklocal = B_TRUE;
				} else {
					char *cp;

					node.am_ifidplen = 0;
					cp = strchr(strval, '/');
					if (cp != NULL) {
						*cp++ = '\0';
						node.am_ifidplen =
						    strtoul(cp, NULL, 10);
						if (node.am_ifidplen >
						    IPV6_ABITS)
							node.am_ifidplen = 0;
					}
					(void) inet_pton(AF_INET6, strval,
					    &in6->sin6_addr);
					if (IN6_IS_ADDR_UNSPECIFIED(
					    &in6->sin6_addr))
						node.am_linklocal = B_TRUE;
				}
			}
		}
	}

	sep = strrchr(node.am_aobjname, '/');
	assert(sep != NULL);
	if (*++sep == '_') {
		node.am_nextnum = 1 + i_ipmgmt_priv_aobjname2num(sep + 1);
	} else {
		ipmgmt_aobjmap_t *head = aobjmap.aobjmap_head;

		for (; head != NULL; head = head->am_next)
			if (strcmp(head->am_ifname, node.am_ifname) == 0)
				break;
		node.am_nextnum = (head == NULL ? 0 : head->am_nextnum);
	}
	/* we have all the information we need, add the node */
	*errp = i_ipmgmt_add_amnode(&node);

	return (B_TRUE);
}

/*
 * Updates an entry from the temporary cache file, which matches the given
 * address object name.
 */
/* ARGSUSED */
static boolean_t
ipmgmt_update_aobjmap(void *arg, nvlist_t *db_nvl, char *buf,
    size_t buflen, int *errp)
{
	ipadm_dbwrite_cbarg_t	*cb = arg;
	nvlist_t		*in_nvl = cb->dbw_nvl;
	uint32_t		flags = cb->dbw_flags;
	char			*db_lifnumstr = NULL, *in_lifnumstr = NULL;

	*errp = 0;
	if (!ipmgmt_nvlist_intersects(db_nvl, in_nvl))
		return (B_TRUE);

	if (flags & IPMGMT_ATYPE_V6ACONF) {
		if (nvlist_lookup_string(db_nvl, IPADM_NVP_LIFNUM,
		    &db_lifnumstr) != 0 ||
		    nvlist_lookup_string(in_nvl, IPADM_NVP_LIFNUM,
		    &in_lifnumstr) != 0 ||
		    (atoi(db_lifnumstr) != -1 && atoi(in_lifnumstr) != -1 &&
		    strcmp(db_lifnumstr, in_lifnumstr) != 0))
			return (B_TRUE);
	}

	/* we found the match */
	(void) memset(buf, 0, buflen);
	if (ipadm_nvlist2str(in_nvl, buf, buflen) == 0) {
		/* buffer overflow */
		*errp = ENOBUFS;
	}

	/* stop the walker */
	return (B_FALSE);
}

/*
 * Deletes an entry from the temporary cache file, which matches the given
 * address object name.
 */
/* ARGSUSED */
static boolean_t
ipmgmt_delete_aobjmap(void *arg, nvlist_t *db_nvl, char *buf,
    size_t buflen, int *errp)
{
	ipmgmt_aobjmap_t	*nodep = arg;
	char			*db_lifnumstr = NULL;

	*errp = 0;
	if (!ipmgmt_nvlist_match(db_nvl, NULL, nodep->am_ifname,
	    nodep->am_aobjname))
		return (B_TRUE);

	if (nodep->am_atype == IPADM_ADDR_IPV6_ADDRCONF) {
		if (nvlist_lookup_string(db_nvl, IPADM_NVP_LIFNUM,
		    &db_lifnumstr) != 0 || atoi(db_lifnumstr) != nodep->am_lnum)
			return (B_TRUE);
	}

	/* we found the match, delete the line from the db */
	buf[0] = '\0';

	/* stop the walker */
	return (B_FALSE);
}

/*
 * Adds or deletes aobjmap node information into a temporary cache file.
 */
extern int
ipmgmt_persist_aobjmap(ipmgmt_aobjmap_t *nodep, ipadm_db_op_t op)
{
	int			err;
	ipadm_dbwrite_cbarg_t	cb;
	nvlist_t		*nvl = NULL;

	if (op == IPADM_DB_WRITE) {
		if ((err = i_ipmgmt_node2nvl(&nvl, nodep)) != 0)
			return (err);
		cb.dbw_nvl = nvl;
		if (nodep->am_atype == IPADM_ADDR_IPV6_ADDRCONF)
			cb.dbw_flags = IPMGMT_ATYPE_V6ACONF;
		else
			cb.dbw_flags = 0;

		err = ipadm_rw_db(ipmgmt_update_aobjmap, &cb,
		    ADDROBJ_MAPPING_DB_FILE, IPADM_FILE_MODE, IPADM_DB_WRITE);
		nvlist_free(nvl);
	} else {
		assert(op == IPADM_DB_DELETE);

		err = ipadm_rw_db(ipmgmt_delete_aobjmap, nodep,
		    ADDROBJ_MAPPING_DB_FILE, IPADM_FILE_MODE, IPADM_DB_DELETE);
	}
	return (err);
}

/*
 * Function upgrades the properties in the given nvlist from the persistent
 * db. If anything is updated in `db_nvl', it returns B_TRUE.
 */
boolean_t
i_ipmgmt_db_upgrade_prop(nvlist_t *db_nvl)
{
	nvpair_t	*nvp;
	char		*name, *pname = NULL, *protostr = NULL, *pval = NULL;
	char		*lname;
	uint_t		proto, nproto;
	char		nname[IPMGMT_STRSIZE], tmpstr[IPMGMT_STRSIZE];

	/*
	 * extract the propname from the `db_nvl' and also extract the
	 * protocol from the `db_nvl'.
	 */
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (strcmp(name, IPADM_NVP_PROTONAME) == 0) {
			if (nvpair_value_string(nvp, &protostr) != 0)
				return (B_FALSE);
		} else {
			assert(!IPADM_PRIV_NVP(name));
			lname = pname = name;
			if (nvpair_value_string(nvp, &pval) != 0)
				return (B_FALSE);
		}
	}

	/*
	 * if it's a private property check to see if it has been
	 * converted to public property
	 */
	if (strncmp(lname, IPADM_PERSIST_PRIVPROP_PREFIX,
	    strlen(IPADM_PERSIST_PRIVPROP_PREFIX)) == 0) {
		lname++;
	}
	nproto = proto = ipadm_str2proto(protostr);
	if (ipadm_legacy2new_propname(lname, nname, sizeof (nname),
	    &nproto) != 0) {
		/* it's a public property move onto the next property */
		return (B_FALSE);
	}

	/* replace the old protocol with new protocol, if required */
	if (nproto != proto) {
		protostr = ipadm_proto2str(nproto);
		if (nvlist_add_string(db_nvl, IPADM_NVP_PROTONAME,
		    protostr) != 0) {
			return (B_FALSE);
		}
	}

	/* add the prefix to property name, if required */
	(void) snprintf(tmpstr, sizeof (tmpstr), "%s%s",
	    (nname[0] == '_' ? "_" : ""), nname);
	if (nvlist_add_string(db_nvl, tmpstr, pval) == 0 &&
	    nvlist_remove(db_nvl, pname, DATA_TYPE_STRING) == 0)
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * upgrades the ipadm data-store. It renames all the old private protocol
 * property names which start with leading protocol names to begin with
 * IPADM_PRIV_PROP_PREFIX and updates the interface lines with their
 * interface class.
 */
/* ARGSUSED */
boolean_t
ipmgmt_db_upgrade(void *arg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	char			*db_ifname;
	boolean_t		upgrade_prop, upgrade_if;

	*errp = 0;
	/*
	 * We are interested in lines which contain protocol properties or
	 * interface with address family definition. We walk through other
	 * lines in the DB.
	 */
	if (nvlist_exists(db_nvl, IPADM_NVP_AOBJNAME) ||
	    (nvlist_lookup_string(db_nvl, IPADM_NVP_IFNAME, &db_ifname) == 0 &&
	    !nvlist_exists(db_nvl, IPADM_NVP_FAMILY))) {
		return (B_TRUE);
	}
	upgrade_prop = nvlist_exists(db_nvl, IPADM_NVP_PROTONAME);
	upgrade_if = nvlist_exists(db_nvl, IPADM_NVP_FAMILY);
	assert(upgrade_prop || upgrade_if);

	if (upgrade_prop) {
		assert(!upgrade_if);
		/*
		 * Check the return value to see if the current line
		 * needs an update.
		 */
		if (i_ipmgmt_db_upgrade_prop(db_nvl) == B_FALSE)
			return (B_TRUE);
	} else {
		assert(upgrade_if);
		if (nvlist_exists(db_nvl, IPADM_NVP_IFCLASS)) {
			return (B_TRUE);
		} else {
			ipadm_if_class_t	ifclass;
			char			strval[IPMGMT_STRSIZE];

			if (ipadm_is_loopback(db_ifname))
				ifclass = IPADMIF_CLASS_LOOPBACK;
			else if (ipadm_is_vni(db_ifname))
				ifclass = IPADMIF_CLASS_VNI;
			else
				ifclass = IPADMIF_CLASS_IP;
			(void) snprintf(strval, IPMGMT_STRSIZE, "%d", ifclass);
			*errp = nvlist_add_string(db_nvl, IPADM_NVP_IFCLASS,
			    strval);
			if (*errp != 0)
				return (B_TRUE);
		}
	}
	(void) memset(buf, 0, buflen);
	if (ipadm_nvlist2str(db_nvl, buf, buflen) == 0) {
		/* buffer overflow */
		*errp = ENOBUFS;
	}
	return (B_TRUE);
}

/*
 * callback argument used by ipmgmt_db_initprop() and ipmgmt_init_prop().
 */
typedef struct ipmgmt_initprop_cbarg_s {
	ipadm_handle_t	iic_iph;
	nvlist_t	*iic_nvl;
} ipmgmt_initprop_cbarg_t;

/*
 * Called during boot.
 *
 * Walk through the DB and apply all the global module properties. We plow
 * through the DB even if we fail to apply property.
 *
 * Due to the inter-dependency nature of 'recv_buf' and 'send_buf' on 'max_buf'
 * these properties are not immediately applied. They are captured in the
 * nvlist and reapplied later by the ipmgmt_init_prop().
 */
/* ARGSUSED */
static boolean_t
ipmgmt_db_initprop(void *cbarg, nvlist_t *db_nvl, char *buf, size_t buflen,
    int *errp)
{
	ipmgmt_initprop_cbarg_t *iicp = cbarg;
	nvpair_t	*nvp, *pnvp;
	char		*strval = NULL, *name, *mod = NULL, *pname;
	char		tmpstr[IPMGMT_STRSIZE];
	uint_t		proto;

	/*
	 * We could have used nvl_exists() directly, however we need several
	 * calls to it and each call traverses the list. Since this codepath
	 * is exercised during boot, let's traverse the list ourselves and do
	 * the necessary checks.
	 */
	for (nvp = nvlist_next_nvpair(db_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(db_nvl, nvp)) {
		name = nvpair_name(nvp);
		if (IPADM_PRIV_NVP(name)) {
			if (strcmp(name, IPADM_NVP_IFNAME) == 0 ||
			    strcmp(name, IPADM_NVP_AOBJNAME) == 0)
				return (B_TRUE);
			else if (strcmp(name, IPADM_NVP_PROTONAME) == 0 &&
			    nvpair_value_string(nvp, &mod) != 0)
				return (B_TRUE);
		} else {
			/* possible a property */
			pnvp = nvp;
		}
	}

	/* if we are here than we found a global property */
	assert(mod != NULL);
	assert(nvpair_type(pnvp) == DATA_TYPE_STRING);

	proto = ipadm_str2proto(mod);
	name = nvpair_name(pnvp);
	if (nvpair_value_string(pnvp, &strval) == 0) {
		ipadm_status_t	status = IPADM_FAILURE;
		char		nvpname[IPMGMT_STRSIZE];

		if (strncmp(name, IPADM_PERSIST_PRIVPROP_PREFIX,
		    strlen(IPADM_PERSIST_PRIVPROP_PREFIX)) == 0) {
			/* private protocol property */
			pname = &name[1];
		} else if (ipadm_legacy2new_propname(name, tmpstr,
		    sizeof (tmpstr), &proto) == 0) {
			pname = tmpstr;
		} else {
			pname = name;
		}
		if (strcmp(pname, "recv_buf") == 0 ||
		    strcmp(pname, "send_buf") == 0) {
			(void) snprintf(nvpname, sizeof (nvpname), "%s_%s",
			    mod, pname);
			if (iicp->iic_nvl != NULL &&
			    nvlist_add_string(iicp->iic_nvl, nvpname,
			    strval) == 0) {
				status = IPADM_SUCCESS;
			}
		} else {
			status = ipadm_set_prop(iicp->iic_iph, pname,
			    strval, proto, IPADM_OPT_ACTIVE);
		}

		if (status != IPADM_SUCCESS) {
			ipmgmt_log(LOG_WARNING,
			    "Failed to reapply property %s on protocol %s : %s",
			    pname, mod, ipadm_status2str(status));
		}
	}

	return (B_TRUE);
}

/*
 * Called during boot and service refresh.
 *
 * Retrieve SMF properties from congestion control service instances
 * and apply corresponding ipadm properties.
 */
static boolean_t
ipmgmt_cong_db_init(void *cbarg, nvlist_t *filter, const char *name,
    const char *strval, const char *mod)
{
	ipadm_handle_t	iph = cbarg;
	uint_t		proto;

	/* if filter is supplied, only set properties that are on the list */
	if (filter != NULL && !nvlist_exists(filter, name))
		return (B_TRUE);

	proto = ipadm_str2proto(mod);
	(void) ipadm_set_prop(iph, name, strval, proto, IPADM_OPT_ACTIVE);

	return (B_TRUE);
}

/* initialize global module properties */
void
ipmgmt_init_prop()
{
	ipmgmt_initprop_cbarg_t iic;
	ipadm_status_t	status;

	bzero(&iic, sizeof (iic));
	if (ipadm_open(&iic.iic_iph, IH_INIT) != IPADM_SUCCESS) {
		ipmgmt_log(LOG_WARNING, "Could not reapply any of the "
		    "persisted protocol properties");
		return;
	}

	/* congestion control properties are retrieved separately from SMF */
	status = ipadm_cong_walk_db(iic.iic_iph, NULL, ipmgmt_cong_db_init);
	if (status != IPADM_SUCCESS) {
		ipmgmt_log(LOG_WARNING, "Could not initialize congestion "
		    "control properties: %s", ipadm_status2str(status));
	}

	/*
	 * If we fail to allocate the nvlist we still continue to restore
	 * other properties. This nvlist is needed to handle properties with
	 * dependencies and these properties will be applied later after all
	 * its dependencies are applied.
	 */
	if (nvlist_alloc(&iic.iic_nvl, NV_UNIQUE_NAME, 0) != 0)
		iic.iic_nvl = NULL;
	/* ipmgmt_db_init() logs warnings if there are any issues */
	(void) ipmgmt_db_walk(ipmgmt_db_initprop, &iic, IPADM_DB_READ);
	if (iic.iic_nvl != NULL) {
		ipadm_status_t	status;
		nvpair_t	*nvp;
		char		protostr[IPMGMT_STRSIZE];
		char		*pname, *strval;

		/* check to see if there are any properites to be applied */
		for (nvp = nvlist_next_nvpair(iic.iic_nvl, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(iic.iic_nvl, nvp)) {
			(void) strlcpy(protostr, nvpair_name(nvp),
			    IPMGMT_STRSIZE);
			if ((pname = strchr(protostr, '_')) == NULL)
				continue;
			*pname++ = '\0';
			status = IPADM_FAILURE;
			if (nvpair_value_string(nvp, &strval) == 0) {
				status = ipadm_set_prop(iic.iic_iph, pname,
				    strval, ipadm_str2proto(protostr),
				    IPADM_OPT_ACTIVE);
			}
			if (status != IPADM_SUCCESS) {
				ipmgmt_log(LOG_WARNING,
				    "Failed to reapply property %s on protocol "
				    "%s: %s", pname, protostr,
				    ipadm_status2str(status));
			}
		}
	}
	nvlist_free(iic.iic_nvl);
	ipadm_close(iic.iic_iph);
}

/*
 * Reapply global module properties on service refresh.
 * Right now only 'cong_enabled' needs refreshing, triggered by IPS
 * when congestion control packages are installed or removed.
 */
void
ipmgmt_refresh_prop()
{
	ipadm_handle_t	iph = NULL;
	nvlist_t 	*filter = NULL;
	ipadm_status_t	status;

	if (ipadm_open(&iph, IH_INIT) != IPADM_SUCCESS) {
		ipmgmt_log(LOG_WARNING, "Could not refresh any of the "
		    "persisted protocol properties");
		return;
	}
	if (ipadm_str2nvlist("cong_enabled", &filter, IPADM_NORVAL) != 0) {
		ipmgmt_log(LOG_WARNING, "Could not refresh any of the "
		    "persisted protocol properties");
	} else {
		status = ipadm_cong_walk_db(iph, filter, ipmgmt_cong_db_init);
		if (status != IPADM_SUCCESS)
			ipmgmt_log(LOG_WARNING, "Could not refresh congestion "
			    "control properties: %s", ipadm_status2str(status));
		nvlist_free(filter);
	}
	ipadm_close(iph);
}

void
ipmgmt_release_scf_resources(scf_resources_t *res)
{
	scf_entry_destroy(res->sr_ent);
	scf_transaction_destroy(res->sr_tx);
	scf_value_destroy(res->sr_val);
	scf_property_destroy(res->sr_prop);
	scf_pg_destroy(res->sr_pg);
	scf_instance_destroy(res->sr_inst);
	(void) scf_handle_unbind(res->sr_handle);
	scf_handle_destroy(res->sr_handle);
}

/*
 * It creates the necessary SCF handles and binds the given `fmri' to an
 * instance. These resources are required for retrieving property value,
 * creating property groups and modifying property values.
 */
int
ipmgmt_create_scf_resources(const char *fmri, scf_resources_t *res)
{
	res->sr_tx = NULL;
	res->sr_ent = NULL;
	res->sr_inst = NULL;
	res->sr_pg = NULL;
	res->sr_prop = NULL;
	res->sr_val = NULL;

	if ((res->sr_handle = scf_handle_create(SCF_VERSION)) == NULL)
		return (-1);

	if (scf_handle_bind(res->sr_handle) != 0) {
		scf_handle_destroy(res->sr_handle);
		return (-1);
	}
	if ((res->sr_inst = scf_instance_create(res->sr_handle)) == NULL)
		goto failure;
	if (scf_handle_decode_fmri(res->sr_handle, fmri, NULL, NULL,
	    res->sr_inst, NULL, NULL, SCF_DECODE_FMRI_REQUIRE_INSTANCE) != 0) {
		goto failure;
	}
	/* we will create the rest of the resources on demand */
	return (0);

failure:
	ipmgmt_log(LOG_WARNING, "failed to create scf resources: %s",
	    scf_strerror(scf_error()));
	ipmgmt_release_scf_resources(res);
	return (-1);
}

/*
 * persists the `pval' for a given property `pname' in SCF. The only supported
 * SCF property types are INTEGER and ASTRING.
 */
static int
ipmgmt_set_scfprop_value(scf_resources_t *res, const char *pname, void *pval,
    scf_type_t ptype)
{
	int result = -1;
	boolean_t new;

	if ((res->sr_val = scf_value_create(res->sr_handle)) == NULL)
		goto failure;
	switch (ptype) {
	case SCF_TYPE_INTEGER:
		scf_value_set_integer(res->sr_val, *(int64_t *)pval);
		break;
	case SCF_TYPE_ASTRING:
		if (scf_value_set_astring(res->sr_val, (char *)pval) != 0) {
			ipmgmt_log(LOG_WARNING, "Error setting string value %s "
			    "for property %s: %s", pval, pname,
			    scf_strerror(scf_error()));
			goto failure;
		}
		break;
	default:
		goto failure;
	}

	if ((res->sr_tx = scf_transaction_create(res->sr_handle)) == NULL)
		goto failure;
	if ((res->sr_ent = scf_entry_create(res->sr_handle)) == NULL)
		goto failure;
	if ((res->sr_prop = scf_property_create(res->sr_handle)) == NULL)
		goto failure;

retry:
	new = (scf_pg_get_property(res->sr_pg, pname, res->sr_prop) != 0);
	if (scf_transaction_start(res->sr_tx, res->sr_pg) == -1)
		goto failure;
	if (new) {
		if (scf_transaction_property_new(res->sr_tx, res->sr_ent,
		    pname, ptype) == -1) {
			goto failure;
		}
	} else {
		if (scf_transaction_property_change(res->sr_tx, res->sr_ent,
		    pname, ptype) == -1) {
			goto failure;
		}
	}

	if (scf_entry_add_value(res->sr_ent, res->sr_val) != 0)
		goto failure;

	result = scf_transaction_commit(res->sr_tx);
	if (result == 0) {
		scf_transaction_reset(res->sr_tx);
		if (scf_pg_update(res->sr_pg) == -1) {
			goto failure;
		}
		goto retry;
	}
	if (result == -1)
		goto failure;
	return (0);

failure:
	ipmgmt_log(LOG_WARNING, "failed to save the data in SCF: %s",
	    scf_strerror(scf_error()));
	return (-1);
}

/*
 * Given a `pgname'/`pname', it retrieves the value based on `ptype' and
 * places it in `pval'.
 */
static int
ipmgmt_get_scfprop(scf_resources_t *res, const char *pgname, const char *pname,
    void *pval, scf_type_t ptype)
{
	ssize_t		numvals;
	scf_simple_prop_t *prop;

	prop = scf_simple_prop_get(res->sr_handle, IPMGMTD_FMRI, pgname, pname);
	numvals = scf_simple_prop_numvalues(prop);
	if (numvals <= 0)
		goto ret;
	switch (ptype) {
	case SCF_TYPE_INTEGER:
		*(int64_t **)pval = scf_simple_prop_next_integer(prop);
		break;
	case SCF_TYPE_ASTRING:
		*(char **)pval = scf_simple_prop_next_astring(prop);
		break;
	}
ret:
	scf_simple_prop_free(prop);
	return (numvals);
}

/*
 * It stores the `pval' for given `pgname'/`pname' property group in SCF.
 */
static int
ipmgmt_set_scfprop(scf_resources_t *res, const char *pgname, const char *pname,
    void *pval, scf_type_t ptype)
{
	scf_error_t		err;

	if ((res->sr_pg = scf_pg_create(res->sr_handle)) == NULL) {
		ipmgmt_log(LOG_WARNING, "failed to create property group: %s",
		    scf_strerror(scf_error()));
		return (-1);
	}

	if (scf_instance_add_pg(res->sr_inst, pgname, SCF_GROUP_APPLICATION,
	    0, res->sr_pg) != 0) {
		if ((err = scf_error()) != SCF_ERROR_EXISTS) {
			ipmgmt_log(LOG_WARNING,
			    "Error adding property group '%s/%s': %s",
			    pgname, pname, scf_strerror(err));
			return (-1);
		}
		/*
		 * if the property group already exists, then we get the
		 * composed view of the property group for the given instance.
		 */
		if (scf_instance_get_pg_composed(res->sr_inst, NULL, pgname,
		    res->sr_pg) != 0) {
			ipmgmt_log(LOG_WARNING, "Error getting composed view "
			    "of the property group '%s/%s': %s", pgname, pname,
			    scf_strerror(scf_error()));
			return (-1);
		}
	}

	return (ipmgmt_set_scfprop_value(res, pname, pval, ptype));
}

/*
 * Returns B_TRUE, if the data-store needs upgrade otherwise returns B_FALSE.
 * Today we have to take care of, one case of, upgrading from version 0 to
 * version 1, so we will use boolean_t as means to decide if upgrade is needed
 * or not. Further, the upcoming projects might completely move the flatfile
 * data-store into SCF and hence we shall keep this interface simple.
 */
boolean_t
ipmgmt_needs_upgrade(scf_resources_t *res)
{
	boolean_t	bval = B_TRUE;
	int64_t		*verp;

	if (ipmgmt_get_scfprop(res, IPMGMTD_APP_PG, IPMGMTD_PROP_DBVER,
	    &verp, SCF_TYPE_INTEGER) > 0) {
		if (*verp == IPADM_DB_VERSION)
			bval = B_FALSE;
	}
	/*
	 * 'datastore_version' doesn't exist. Which means that we need to
	 * upgrade the datastore. We will create 'datastore_version' and set
	 * the version value to IPADM_DB_VERSION, after we upgrade the file.
	 */
	return (bval);
}

/*
 * This is called after the successful upgrade of the local data-store. With
 * the data-store upgraded to recent version we don't have to do anything on
 * subsequent reboots.
 */
void
ipmgmt_update_dbver(scf_resources_t *res)
{
	int64_t		version = IPADM_DB_VERSION;

	(void) ipmgmt_set_scfprop(res, IPMGMTD_APP_PG,
	    IPMGMTD_PROP_DBVER, &version, SCF_TYPE_INTEGER);
}

ipmgmt_aobjmap_t *
ipmgmt_aobjmap_search(const char *ifname, uint32_t lnum, sa_family_t af)
{
	ipmgmt_aobjmap_t	*head;

	(void) pthread_rwlock_rdlock(&aobjmap.aobjmap_rwlock);
	head = aobjmap.aobjmap_head;
	for (; head; head = head->am_next) {
		if (strcmp(head->am_ifname, ifname) == 0 &&
		    head->am_lnum == lnum &&
		    head->am_family == af) {
			break;
		}
	}
	(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);
	return (head);
}

uint32_t
ipmgmt_get_nextnum(const char *ifname)
{
	ipmgmt_aobjmap_t	*head;

	for (head = aobjmap.aobjmap_head; head != NULL; head = head->am_next)
		if (strcmp(head->am_ifname, ifname) == 0)
			break;
	return (head == NULL ? 0 : head->am_nextnum);
}
