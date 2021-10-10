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

#ifndef	_SYS_IOVCFG_H
#define	_SYS_IOVCFG_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/param.h>
#include <sys/sunddi.h>
#include <sys/nvpair.h>

#define	PF_PATH_NVLIST_NAME	"PF_PATHNAME"
#define	MAX_NVSTRING_LEN	16

#ifdef	_KERNEL

/*
 * IOV device classes
 */
typedef enum {
	IOV_CLASS_NET = 1		/* Network (Ethernet) devices */
} iov_dev_class_t;

typedef struct iov_class_ops	iov_class_ops_t;
typedef struct iov_vf		iov_vf_t;
typedef struct iov_pf		iov_pf_t;

/*
 * IOV class specific operations structure
 */
struct iov_class_ops {
	/* class type */
	iov_dev_class_t	iop_class;				/* class */

	/* class name */
	char		iop_class_str[MAXNAMELEN];		/* name */

	/* alloc class specific section of PF */
	void		(*iop_class_alloc_pf)(iov_pf_t *);

	/* free class specific section of PF */
	void		(*iop_class_free_pf)(iov_pf_t *);

	/* class specific PF configuration */
	int		(*iop_class_config_pf)(iov_pf_t *);

#ifdef IOVCFG_UNCONFIG_SUPPORTED
	/* class specific PF unconfiguration */
	void		(*iop_class_unconfig_pf)(iov_pf_t *);
#endif

	/* alloc class specific section of VF */
	void		(*iop_class_alloc_vf)(iov_vf_t *);

	/* free class specific section of VF */
	void		(*iop_class_free_vf)(iov_vf_t *);

	/*
	 * The below 3 ops are really platform specific and could be moved be
	 * out to class-specific-platform section in the future.
	 */
	/* register class specific reconfiguration updates */
	void		(*iop_class_reconfig_reg_pf)(iov_pf_t *);

#ifdef IOVCFG_UNCONFIG_SUPPORTED
	/* unregister class specific reconfiguration updates */
	void		(*iop_class_reconfig_unreg_pf)(iov_pf_t *);
#endif

	/* read class specific VF props */
	int	(*iop_class_read_props)(iov_vf_t *, void *, void *);
};

/*
 * An instance of PF iov device.
 */
struct iov_pf {
	iov_pf_t	*ipf_nextp;			/* Next PF */
	char		ipf_pathname[MAXPATHLEN];	/* Pathname */
	uint_t		ipf_numvfs;			/* # vfs to configure */
	iov_class_ops_t	*ipf_cl_ops;			/* Device Class Ops */
	void		*ipf_cl_data;			/* class spec data */
	nvlist_t	*ipf_params;			/* Dev Props */
	iov_vf_t	*ipf_vfp;			/* VF list */
	ddi_taskq_t	*ipf_taskq;			/* config taskq */
};

/*
 * VF Configuration States
 */
typedef enum iov_vf_state {
	IOVCFG_VF_UNCONFIGURED = 0x1,		/* Uninitialized */
	IOVCFG_VF_CONFIGURING = 0x2,		/* Init task running */
	IOVCFG_VF_RECONFIGURING = 0x4,		/* Reconfig task running */
	IOVCFG_VF_CONFIGURED = 0x80		/* Configured */
} iov_vf_state_t;

/*
 * An instance of VF iov device.
 */
struct iov_vf {
	iov_vf_t		*ivf_nextp;		/* Next VF */
	kmutex_t		ivf_lock;		/* Sync state */
	iov_vf_state_t		ivf_state;		/* State */
	int			ivf_task_cnt;		/* # reconf tsks pend */
	iov_pf_t		*ivf_pfp;		/* Back ptr to PF */

	/* Standard props */
	char			ivf_pathname[MAXPATHLEN];	/* Pathname */
	int			ivf_id;			/* ID */
	boolean_t		ivf_loaned;		/* Loaned ? */

	/* Class specific data (opaque)  */
	void			*ivf_cl_data;

	/* Device specific data */
	nvlist_t		*ivf_params;

	/* Platform specific data (opaque) */
	void			*ivf_plat_data;
};

#ifdef DEBUG
extern int iovcfg_dbg;
void iovcfg_debug_printf(const char *fname, const char *fmt, ...);
#define	DPRINTF	iovcfg_debug_printf

/*
 * debug levels:
 * 0x1:	Trace iovcfg mdstore routines
 * 0x2: Trace netcfg routines
 * 0x4: Trace for x86 platform
 */

#define	DBG1(...)						\
    do {							\
	    if ((iovcfg_dbg & 0x1) != 0) {			\
		    DPRINTF(__func__, __VA_ARGS__);		\
	    }							\
    _NOTE(CONSTCOND) } while (0);

#define	DBGNET(...)						\
    do {							\
	    if ((iovcfg_dbg & 0x2) != 0) {			\
		    DPRINTF(__func__, __VA_ARGS__);		\
	    }							\
    _NOTE(CONSTCOND) } while (0);

#define	DBGx86(...)						\
    do {							\
	    if ((iovcfg_dbg & 0x4) != 0) {			\
		    DPRINTF(__func__, __VA_ARGS__);		\
	    }							\
    _NOTE(CONSTCOND) } while (0);


#else

#define	DBG1(...)	if (0)  do { } while (0)
#define	DBGNET(...)	if (0)  do { } while (0)
#define	DBGx86(...)	if (0)  do { } while (0)

#endif


/* Imported functions */
extern int pciv_get_pf_list(nvlist_t **);
extern int pciv_get_vf_list(char *, nvlist_t **);
extern int pciv_get_numvfs(char *, uint_t *);
extern int pciv_param_get(char *, nvlist_t **);
extern int pciv_plist_lookup(pci_plist_t, ...);
extern int pciv_plist_getvf(nvlist_t *, uint16_t, pci_plist_t *);
extern void pciv_class_config_completed(char *);

extern int iovcfg_plat_init(void);
int iovcfg_update_pflist(void);
extern int iovcfg_plat_refresh_vflist(iov_pf_t *);
extern void iovcfg_plat_fini(void);
int iovcfg_param_get(char *, nvlist_t **);
extern void iovcfg_plat_alloc_vf(iov_vf_t *);
extern void iovcfg_plat_free_vf(iov_vf_t *);

/* Exported functions */
extern int iovcfg_param_get(char *, nvlist_t **);
extern int iovcfg_get_numvfs(char *, uint_t *);
extern int iovcfg_is_vf_assigned(char *, uint_t, boolean_t *);
extern int iovcfg_update_pflist(void);
extern int iovcfg_configure_pf_class(char *);

iov_pf_t *iovcfg_pf_lookup(char *pf_path);
void iovcfg_config_class(void);
void iovcfg_unconfig_class(void);
int iovcfg_configure_pf_class(char *pf_path);
iov_pf_t *iovcfg_alloc_pf(char *, char *);
iov_vf_t *iovcfg_alloc_vf(iov_pf_t *, char *, int);
void iovcfg_free_pf(iov_pf_t *);
void iovcfg_free_vf(iov_vf_t *);
typedef int	(*iovcfg_modctl_fn_t) (int iov_op, uintptr_t arg,
    uintptr_t arg_size, uintptr_t rval, uintptr_t rval_size);
int iovcfg_modctl(int iov_op, uintptr_t arg, uintptr_t arg_size,
    uintptr_t rval, uintptr_t rval_size);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOVCFG_H */
