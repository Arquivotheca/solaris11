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
 * IOV Configuration Module
 */

#include	<sys/conf.h>
#include	<sys/mkdev.h>
#include	<sys/modctl.h>
#include	<sys/stat.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/iovcfg.h>
#include	<sys/iovcfg_net.h>
#include	<sys/pciconf.h>

#define	IOVCFG_INFO	"IOV Configuration Module"

/*
 * Module linkage information for the kernel.
 */
static	struct modlmisc		iovcfg_modlmisc = {
	&mod_miscops,
	IOVCFG_INFO,
};

static	struct modlinkage	iovcfg_modlinkage = {
	MODREV_1,
	(void *)&iovcfg_modlmisc,
	NULL
};

#ifdef DEBUG
int iovcfg_dbg = 0;
#endif

/* Defined in common/os/modctl.c */
extern iovcfg_modctl_fn_t iovcfg_modctl_fn_p;

/* Imported functions */
extern void iovcfg_plat_config(iov_pf_t *pfp);
#ifdef IOVCFG_UNCONFIG_SUPPORTED
extern void iovcfg_plat_unconfig(iov_pf_t *pfp);
#endif

/* Internal functions */
static iov_class_ops_t *iovcfg_class_lookup(char *cl_str);

/* List of iov-device nodes */
iov_pf_t *iovcfg_pf_listp;

/*
 * IOV device class specific operations table.
 * Only network class is supported for now.
 */
iov_class_ops_t iov_class_ops[] =
{

	{
		IOV_CLASS_NET,
		"iov-network",
		iovcfg_alloc_pf_net,
		iovcfg_free_pf_net,
		iovcfg_config_pf_net,
#ifdef IOVCFG_UNCONFIG_SUPPORTED
		iovcfg_unconfig_pf_net,
#endif
		iovcfg_alloc_vf_net,
		iovcfg_free_vf_net,
		iovcfg_reconfig_reg_pf_net,
#ifdef IOVCFG_UNCONFIG_SUPPORTED
		iovcfg_reconfig_unreg_pf_net,
#endif
		iovcfg_read_props_net
	}
};

#ifdef DEBUG

int iovcfg_debug = 1;

void
iovcfg_debug_printf(const char *fname, const char *fmt, ...)
{
	char    buf[512];
	va_list ap;
	char    *bufp = buf;

	(void) sprintf(bufp, "%s: ", fname);
	bufp += strlen(bufp);
	va_start(ap, fmt);
	(void) vsprintf(bufp, fmt, ap);
	va_end(ap);
	cmn_err(CE_CONT, "%s\n", buf);
}

#endif

/*
 * Module gets loaded when the px driver is loaded.
 */
int
_init(void)
{
	int	rv;

	if ((rv = mod_install(&iovcfg_modlinkage)) != 0) {
		return (rv);
	}
	(void) iovcfg_plat_init();
	iovcfg_modctl_fn_p = iovcfg_modctl;
	return (rv);
}

/*
 * Module unloading shouldn't be allowed as px depends on it. The module
 * provides services to PFs. If there are PFs we can't allow unloading of this
 * module.
 */
int
_fini(void)
{
	if (iovcfg_pf_listp != NULL) {
		return (1);
	}
#ifdef IOVCFG_UNCONFIG_SUPPORTED
	iovcfg_unconfig_class();
	iovcfg_plat_fini();
#endif
	return (mod_remove(&iovcfg_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&iovcfg_modlinkage, modinfop));
}

/*
 * Allocate a param handle for the PF. We first find the PF based on its path
 * provided, in our cached list of PFs. If the specified PF is found, we
 * allocate a snapshot structure and simply dup the properties of both the PF
 * and its associated VFs into the snapshot structure.
 * Returns:
 * 	Success: 0
 * 	Failure: EINVAL - specified PF path is invalid.
 */
int
iovcfg_param_get(char *pf_pathname, nvlist_t **nvlp)
{
	int			rv;
	iov_pf_t		*pfp;


	if ((pf_pathname == NULL) || (nvlp == NULL))
		return (EINVAL);
	*nvlp = NULL;
	pfp = iovcfg_pf_lookup(pf_pathname);
	if ((pfp == NULL) || (pfp->ipf_params == NULL))
		return (EINVAL);
	rv = nvlist_dup(pfp->ipf_params, nvlp, KM_SLEEP);
	return (rv);
}

/*
 * Obtain the #vfs to be configured for the PF.
 */
int
iovcfg_get_numvfs(char *pf_path, uint_t *num_vf_p)
{
	iov_pf_t	*pfp;

	if ((pf_path == NULL) || (num_vf_p == NULL))
		return (EINVAL);
	*num_vf_p = 0;
	pfp = iovcfg_pf_lookup(pf_path);
	if (pfp == NULL)
		(void) pciv_get_numvfs(pf_path, num_vf_p);
	else
		*num_vf_p = pfp->ipf_numvfs;
	return (0);
}

/*
 * Determine whether the VF specified by its index is assigned to a domain.
 */
int
iovcfg_is_vf_assigned(char *pf_path, uint_t vf_index, boolean_t *loaned_p)
{
	iov_pf_t	*pfp;
	iov_vf_t	*vfp;

	if (pf_path == NULL || loaned_p == NULL)
		return (EINVAL);
	pfp = iovcfg_pf_lookup(pf_path);
	if (pfp == NULL)
		return (EINVAL);
	if (vf_index >= pfp->ipf_numvfs)
		return (EINVAL);
	for (vfp = pfp->ipf_vfp; vfp != NULL; vfp = vfp->ivf_nextp) {
		if (vfp->ivf_id == vf_index) {
			break;
		}
	}
	if (vfp != NULL) {
		*loaned_p = vfp->ivf_loaned;
		return (0);
	}
	return (EINVAL);
}

/*
 * Perform class specific configuration for the PF specified by the path.
 */
int
iovcfg_configure_pf_class(char *pf_path)
{
	int		rv;
	iov_pf_t	*pfp;
	iov_class_ops_t	*cl_ops;

	if (pf_path == NULL)
		return (DDI_FAILURE);
	pfp = iovcfg_pf_lookup(pf_path);
	if (pfp == NULL) {
		return (DDI_FAILURE);
	}
	cl_ops = pfp->ipf_cl_ops;
	if (cl_ops == NULL) {
		/* No class ops; nothing really to configure; return success */
		return (DDI_SUCCESS);
	}

	/*
	 * The platform should have discovered the VFs under this PF by now.
	 * Otherwise, we give it another chance to build the VF list, before we
	 * invoke class specific configuration for the PF.
	 */
	if (pfp->ipf_vfp == NULL) {
		(void) iovcfg_plat_refresh_vflist(pfp);
	}
	rv = cl_ops->iop_class_config_pf(pfp);

	/*
	 * Also perform any platform specific configuration.
	 */
	iovcfg_plat_config(pfp);

	return (rv);
}

/*
 * Process modctl() calls from userland. The specific IOV config sub command
 * being issued is determined by iov_op.
 * Arguments:
 * 	arg:	  User provided command specific optional argument buf/struct
 * 	arg_size: Size of arg buf
 * Returns:
 * 	rbuf:	   User provided command specific optional return buf/struct
 * 	rbuf_size: Size of returned rbuf
 *
 * rv:
 * 	Success:	0
 * 	Failure:	EIO    - Unable to process the command
 * 			ENODEV - No device(s)
 * 			EFAULT - Error while copying in/out
 *			EINVAL - Invalid cmd/arg
 */
/* ARGSUSED */
int
iovcfg_modctl(int iov_op, uintptr_t arg, uintptr_t arg_size,
    uintptr_t rbuf, uintptr_t rbuf_size)
{
	int			rv;
	uint_t			len;
	size_t			nvlist_len;
	int			error = 0;
	nvlist_t		*nvl = NULL;
	char			*buf = NULL;

	switch (iov_op) {

	case MODIOVOPS_GET_PF_LIST:
		if (rbuf_size == NULL) {
			/*
			 * The size ptr must be provided by the caller; buf
			 * is optional. Caller typically gets the required size
			 * first using modctl(); then allocates a buf of that
			 * size and makes another modctl() with the buf for
			 * copyout().
			 */
			return (EINVAL);
		}
		rv = pciv_get_pf_list(&nvl);
		if (rv != 0) {
			return (rv);
		}
		rv = nvlist_size(nvl, &nvlist_len, NV_ENCODE_NATIVE);
		if (rv != 0) {
			error = EIO;
			goto  exit;
		}
		len = (uint_t)nvlist_len;

		/* copyout the bufsize needed to hold the PF list */
		rv = copyout(&len, (void *)rbuf_size, sizeof (len));
		if (rv != 0) {
			error = EFAULT;
			goto exit;
		}

		/*
		 * If buffer is provided copyout the PF list.
		 * Otherwise we return only the size to caller.
		 */
		if (rbuf != NULL)  {
			buf = kmem_alloc(len, KM_SLEEP);
			rv = nvlist_pack(nvl, &buf, &nvlist_len,
			    NV_ENCODE_NATIVE, KM_SLEEP);
			if (rv != 0) {
				error = EIO;
				goto exit;
			}
			/* copyout buffer */
			rv = copyout((void *)buf, (void *)rbuf, len);
			if (rv != 0) {
				error = EFAULT;
				goto exit;
			}
		}
		break;
	default:
		error = EINVAL;

	}

exit:
	if (buf != NULL) {
		kmem_free(buf, len);
	}
	if (nvl != NULL) {
		nvlist_free(nvl);
	}
	return (error);
}

/*
 * Below are the support functions used by common, class and platform specific
 * code.
 */

/*
 * Lookup class-ops given the class string.
 */
static iov_class_ops_t *
iovcfg_class_lookup(char *cl_str)
{
	int	i;
	int	tblsz;

	tblsz = sizeof (iov_class_ops) / sizeof (iov_class_ops_t);
	for (i = 0; i < tblsz; i++) {
		if (strcmp(iov_class_ops[i].iop_class_str, cl_str) == 0) {
			return (&iov_class_ops[i]);
		}
	}

	return (NULL);
}

/*
 * Lookup the pf instance given its path.
 */
iov_pf_t *
iovcfg_pf_lookup(char *pf_path)
{
	iov_pf_t	*pfp;

	for (pfp = iovcfg_pf_listp; pfp != NULL; pfp = pfp->ipf_nextp) {
		if (strcmp(pfp->ipf_pathname, pf_path) == 0)
			break;
	}
	return (pfp);
}

/*
 * Allocate an instance of PF. The function first allocates the
 * common PF structure and then allocates class specific part.
 */
iov_pf_t *
iovcfg_alloc_pf(char *cl_str, char *path)
{
	iov_pf_t	*pfp;
	iov_class_ops_t	*cl_ops;

	if (path == NULL || cl_str == NULL) {
		return (NULL);
	}

	/*
	 * Lookup the class ops based on the class string. If the class is not
	 * supported it returns NULL; in that case, we still allocate the PF,
	 * but without any class specific operations.
	 */
	cl_ops = iovcfg_class_lookup(cl_str);
	pfp = kmem_zalloc(sizeof (*pfp), KM_SLEEP);
	(void) strncpy(pfp->ipf_pathname, path, strlen(path) + 1);
	pfp->ipf_cl_ops = cl_ops;

	/* Allocate class specific PF data */
	if (cl_ops != NULL) {
		cl_ops->iop_class_alloc_pf(pfp);
	}

	return (pfp);
}

/*
 * Allocate an instance of VF. The function first allocates the common
 * VF structure and then allocates class and platform specific parts.
 */
iov_vf_t *
iovcfg_alloc_vf(iov_pf_t *pfp, char *path, int id)
{
	iov_vf_t	*vfp;
	iov_class_ops_t	*cl_ops = pfp->ipf_cl_ops;

	if (path == NULL || pfp == NULL) {
		return (NULL);
	}
	vfp = kmem_zalloc(sizeof (*vfp), KM_SLEEP);
	mutex_init(&vfp->ivf_lock, NULL, MUTEX_DRIVER, NULL);
	vfp->ivf_state = IOVCFG_VF_UNCONFIGURED;
	vfp->ivf_pfp = pfp;
	vfp->ivf_id = id;
	(void) strncpy(vfp->ivf_pathname, path, strlen(path) + 1);

	/*
	 * Allocate platform and class specific VF data. Note that platform
	 * specific reconfig callbacks are registered/unregistered thru class
	 * ops (see iov_class_ops_t) and thus we need to allocate plat specific
	 * data only if the class ops exists.
	 */
	if (cl_ops != NULL) {
		iovcfg_plat_alloc_vf(vfp);
		cl_ops->iop_class_alloc_vf(vfp);
	}

	return (vfp);
}

/*
 * Free a PF instance.
 */
void
iovcfg_free_pf(iov_pf_t *pfp)
{
	iov_vf_t	*vfp;
	iov_vf_t	*nvfp;
	iov_class_ops_t	*cl_ops = pfp->ipf_cl_ops;

	if (pfp == NULL)
		return;
	if (pfp->ipf_params != NULL) {
		nvlist_free(pfp->ipf_params);
	}
	for (vfp = pfp->ipf_vfp; vfp != NULL; ) {
		nvfp = vfp->ivf_nextp;
		iovcfg_free_vf(vfp);
		vfp = nvfp;
	}

	/* Destroy class specific PF data */
	if (cl_ops != NULL) {
		cl_ops->iop_class_free_pf(pfp);
	}

	kmem_free(pfp, sizeof (*pfp));
}

/*
 * Free a VF instance.
 */
void
iovcfg_free_vf(iov_vf_t *vfp)
{
	iov_class_ops_t	*cl_ops = vfp->ivf_pfp->ipf_cl_ops;

	if (vfp == NULL)
		return;
	if (vfp->ivf_params != NULL) {
		nvlist_free(vfp->ivf_params);
	}

	if (cl_ops != NULL) {
		/* Destroy platform specific VF data */
		iovcfg_plat_free_vf(vfp);
		/* Destroy class specific PF data */
		cl_ops->iop_class_free_vf(vfp);
	}

	kmem_free(vfp, sizeof (*vfp));
}

void
iovcfg_free_pfs(void)
{
	iov_pf_t	*pfp;
	iov_pf_t	*npfp;

	for (pfp = iovcfg_pf_listp; pfp != NULL; pfp = npfp) {
		npfp = pfp->ipf_nextp;
		iovcfg_free_pf(pfp);
	}

	iovcfg_pf_listp = NULL;
}

#ifdef IOVCFG_UNCONFIG_SUPPORTED

/*
 * Undo class specific IOV configuration.
 */
void
iovcfg_unconfig_class(void)
{
	iov_pf_t	*pfp;

	if (iovcfg_pf_listp == NULL) {
		return;
	}
	for (pfp = iovcfg_pf_listp; pfp != NULL; pfp = pfp->ipf_nextp) {
		(void) iovcfg_unconfigure_pf_class(pfp);
	}
}

int
iovcfg_unconfigure_pf_class(iov_pf_t *pfp)
{
	iov_class_ops_t	*cl_ops;

	if (pfp == NULL) {
		return (DDI_FAILURE);
	}
	cl_ops = pfp->ipf_cl_ops;

	/*
	 * First unconfigure any platform hooks; this
	 * should stop further reconfig callbacks/tasks.
	 */
	iovcfg_plat_unconfig(pfp);

	/*
	 * Now invoke class specific unconfig routine.
	 */
	if (cl_ops != NULL) {
		cl_ops->iop_class_unconfig_pf(pfp);
	}
	return (DDI_SUCCESS);
}

#endif
