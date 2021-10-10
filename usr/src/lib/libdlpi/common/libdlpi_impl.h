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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBDLPI_IMPL_H
#define	_LIBDLPI_IMPL_H

#include <libdlpi.h>
#include <sys/sysmacros.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum DLPI response size, in bytes.
 */
#define	DLPI_CHUNKSIZE	8192

/*
 * Maximum SAP length, in bytes.
 */
#define	DLPI_SAPLEN_MAX	4

/*
 * Number of elements in 'arr'.
 */
#define	NELEMS(arr)	(sizeof (arr) / sizeof ((arr)[0]))

/*
 * Allocate buffer size for DLPI message, in bytes and set DLPI primitive.
 */
#define	DLPI_MSG_CREATE(dlmsg, dlprimitive) \
	(dlmsg).dlm_msgsz = i_dlpi_getprimsize((dlprimitive)); \
	(dlmsg).dlm_msg = alloca((dlmsg).dlm_msgsz); \
	(dlmsg).dlm_msg->dl_primitive = (dlprimitive);

/*
 * Publicly available DLPI notification types. This list may change if
 * new DLPI notification types are made public. See dlpi(7P).
 *
 */
#define	DLPI_NOTIFICATION_TYPES	(DL_NOTE_LINK_DOWN | DL_NOTE_LINK_UP | \
	DL_NOTE_PHYS_ADDR | DL_NOTE_SDU_SIZE | DL_NOTE_SPEED | \
	DL_NOTE_PROMISC_ON_PHYS | DL_NOTE_PROMISC_OFF_PHYS | \
	DL_NOTE_FC_MODE)

/*
 * Used in a mactype lookup table.
 */
typedef struct dlpi_mactype_s {
	uint_t	dm_mactype;	/* DLPI/Private mactype */
	char 	*dm_desc;	/* Description of mactype */
} dlpi_mactype_t;

/*
 * Used to get the maximum DLPI message buffer size, in bytes.
 */
typedef struct dlpi_primsz {
	t_uscalar_t	dp_prim;	/* store DLPI primitive */
	size_t		dp_primsz;
				/* max. message size, in bytes, for dp_prim */
} dlpi_primsz_t;

/*
 * Used to create DLPI message.
 */
typedef struct dlpi_msg {
	union DL_primitives	*dlm_msg;
					/* store DLPI primitive message */
	size_t			dlm_msgsz;
					/* provide buffer size for dlm_msg */
} dlpi_msg_t;

typedef struct dlpi_notifyent {
	uint_t			dln_notes;
					/* notification types registered */
	dlpi_notifyfunc_t	*dln_fnp;
					/* callback to call */
	void 			*arg;	/* argument to pass to callback */
	uint_t			dln_rm;	/* true if should be removed */
	struct dlpi_notifyent	*dln_next;
} dlpi_notifyent_t;

/*
 * Private libdlpi structure associated with each DLPI handle.
 */
typedef struct dlpi_impl_s {
	int		dli_fd;		/* fd attached to stream */
	int		dli_timeout;	/* timeout for operations, in sec */
	char		dli_linkname[MAXLINKNAMESPECIFIER];
					/* full linkname including PPA */
	char		dli_provider[DLPI_LINKNAME_MAX];
					/* only provider name */
	t_uscalar_t	dli_style;	/* style 1 or 2 */
	uint_t		dli_saplen;	/* bound SAP length */
	uint_t		dli_sap;	/* bound SAP value */
	boolean_t 	dli_sapbefore;	/* true if SAP precedes address */
	uint_t		dli_ppa;	/* physical point of attachment */
	uint_t		dli_mactype;	/* mac type */
	uint_t		dli_oflags;	/* flags set at open */
	uint_t		dli_note_processing;
					/* true if notification is being */
					/* processed */
	dlpi_notifyent_t *dli_notifylistp;
					/* list of registered notifications */
} dlpi_impl_t;

/*
 * dlpi_info() version support notes:
 *
 * We have bumped the version field to 1 in support of a larger dlpi_info_t
 * structure that can hold linknames upto MAXLINKNAMESPECIFIER size. We need to
 * support clients passing both version 0 and version 1. In particular, there
 * are three cases we need to support:
 *
 * (a) Existing client binaries that pass version 0 and have a dlpi_info_t
 * structure in the binary that is of version 0. libdlpi at runtime should
 * copy in the version 0 dlpi_info_t back to the caller.
 * (b) Existing client source compiled against libdlpi but continue to use
 * version 0. The client when compiled using libdlpi.h should see the
 * version 0 dlpi_info_t structure as that is the version of the structure
 * we will copy back to the caller at runtime.
 * (c) Finally, new clients modified to use version 1. New clients should
 * be able to see version 1 dlpi_info_t from the libdlpi.h file and at
 * runtime libdlpi should copy over the version 1 dlpi_info_t structure
 * to the caller.
 *
 * We cannot modify dlpi_info_t in libdlpi.h for all clients because case (b)
 * above will fail as clients passing version 0 should continue to receive the
 * version 0 dlpi_info_t structure from the libdlpi.h header file. So libdlpi.h
 * has to expose version 0 of the dlpi_info_t structure to existing clients
 * compiled in future and expose version 1 of the dlpi_info_t structure to new
 * clients passing version 1. The way we support this is by requiring new
 * clients to pre-define DLPI_INFO_VERSION to value 1 before including libdlpi.h
 *
 * New clients that define the DLPI_INFO_VERSION to value 1 will see the
 * updated version 1 of the dlpi_info_t structure from libdlpi.h header file.
 * But if a new client passes value 1 but does not pre-define DLPI_INFO_VERSION
 * to 1 then client will run into silent failures as we will copy over version
 * 1 of the structure whereas the client has been compiled with version 0 of the
 * structure. Similarly if client passes value 0 even when the client has
 * pre-defined DLPI_INFO_VERSION to be 1, silent failures will occur.
 * Unfortunately we cannot detect either of these cases at runtime in libdlpi
 * and return an error to the caller.
 *
 * Existing clients will require no changes and will continue to work when
 * compiled and run using libdlpi. There is also the question of which version
 * of the dlpi_info_t structure that libdlpi is compiled with. We could compile
 * libdlpi with either version 1 or 0. libdlpi is currently compiled to use
 * version 0 of the dlpi_info_t structure.
 */

/*
 * Version 1 dlpi_info_t. When callers pass version 1 this structure will be
 * used by the dlpi_info() call. libdlpi is compiled with version 0 so the
 * dlpi_info_t from libdlpi.h is of version 0 and is used when callers pass
 * version 0. If libdlpi is compiled to use version 1 then we would need
 * a similar private dlpi_info_t in support of version 0.
 */
typedef struct {
	uint_t			di_opts;
	uint_t			di_max_sdu;
	uint_t			di_min_sdu;
	uint_t			di_state;
	uint_t			di_mactype;
	char			di_linkname[MAXLINKNAMESPECIFIER];
	uchar_t			di_physaddr[DLPI_PHYSADDR_MAX];
	uchar_t			di_physaddrlen;
	uchar_t			di_bcastaddr[DLPI_PHYSADDR_MAX];
	uchar_t			di_bcastaddrlen;
	uint_t			di_sap;
	int			di_timeout;
	dl_qos_cl_sel1_t	di_qos_sel;
	dl_qos_cl_range1_t 	di_qos_range;
} dlpi_info_vers1_t;

/*
 * Supported versions for dlpi_info(3DLPI). To specify version 1 callers must
 * define DLPI_INFO_VERSION to value 1 and pass caller defined DLPI_INFO_VERSION
 * constant to dlpi_info().
 */
#define	DLPI_INFO_VERS0		0
#define	DLPI_INFO_VERS1		1
#define	DLPI_INFO_MAXVERSION	DLPI_INFO_VERS1

/*
 * Private macros to set/get/copy either the dlpi_info_vers1_t
 * structure or the dlpi_info_t structure based on the given
 * version argument to dlpi_info() call.
 */
#define	_DLPI_INFO_SET(info, version, option, value) { \
	dlpi_info_vers1_t *infopv1; \
	dlpi_info_t *infopv0; \
	if ((version) == DLPI_INFO_VERS0) { \
		infopv0 = (dlpi_info_t *)(info); \
		infopv0->option = (value); \
	} else { \
		infopv1 = (dlpi_info_vers1_t *)(info); \
		infopv1->option = (value); \
	} \
}

#define	_DLPI_INFO_GET(info, version, option) \
	(((version) == DLPI_INFO_VERS0) ? (((dlpi_info_t *) \
	(info))->option) : (((dlpi_info_vers1_t *)(info))->option))

#define	_DLPI_INFO_GETADDR(info, version, option) \
	((((version) == DLPI_INFO_VERS0) ? (void *)&((dlpi_info_t *) \
	(info))->option : (void *)&((dlpi_info_vers1_t *)(info))->option))

#define	_DLPI_INFO_COPY(info, version, option, datap, datalen) { \
	(void) memcpy((_DLPI_INFO_GETADDR(info, version, option)), \
	    (datap), (datalen)); \
}

#ifdef __cplusplus
}
#endif

#endif /* _LIBDLPI_IMPL_H */
