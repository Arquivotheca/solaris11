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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_DEV_ALLOC_H
#define	_DEV_ALLOC_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <secdb.h>


#define	DAOPT_AUTHS	"auths"
#define	DAOPT_CLASS	"class"
#define	DAOPT_CSCRIPT	"cleanscript"
#define	DAOPT_MINLABEL	"minlabel"
#define	DAOPT_MAXLABEL	"maxlabel"
#define	DAOPT_XDISPLAY	"xdpy"
#define	DAOPT_ZONE	"zone"
#define	DA_RESERVED	"reserved"

/*
 * These are unsupported, SUN-private interfaces.
 */

typedef struct {
	char	*da_devname;
	char	*da_devtype;
	char	*da_devauth;
	char	*da_devexec;
	kva_t	*da_devopts;
} devalloc_t;

typedef struct {
	char	*dmap_devname;
	char	*dmap_devtype;
	char	*dmap_devlist;
	char	**dmap_devarray;
} devmap_t;

int		getdadmline(char *, int, FILE *);

devalloc_t	*getdaent(void);
devalloc_t	*getdatype(char *);
devalloc_t	*getdanam(char *);
void		setdaent(void);
void		enddaent(void);
void		freedaent(devalloc_t *);
void		setdafile(char *);

devmap_t	*getdmapent(void);
devmap_t	*getdmaptype(char *);
devmap_t	*getdmapnam(char *);
devmap_t	*getdmapdev(char *);
void		setdmapent(void);
void		enddmapent(void);
void		freedmapent(devmap_t *);
void		setdmapfile(char *);
char		*getdmapfield(char *);
char		*getdmapdfield(char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DEV_ALLOC_H */
