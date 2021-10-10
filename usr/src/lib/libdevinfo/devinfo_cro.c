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

#include "libdevinfo.h"
#include "device_info.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <regex.h>
#include <strings.h>
#include <sys/varargs.h>
#include <libsysevent.h>
#include <sys/sysevent/eventdefs.h>

/* define aspects of record fields */
#define	F_product_id			0
#define	F_chassis_id			1
#define	F_alias_id			2
#define	F_receptacle_name		3
#define	F_receptacle_type		4
#define	F_receptacle_fmri		5
#define	F_occupant_type			6
#define	F_occupant_instance		7
#define	F_devchassis_path		8
#define	F_occupant_devices		9
#define	F_occupant_paths		10
#define	F_occupant_compdev		11
#define	F_occupant_devid		12
#define	F_occupant_mfg			13
#define	F_occupant_model		14
#define	F_occupant_part			15
#define	F_occupant_serial		16
#define	F_occupant_firm			17
#define	F_occupant_misc_1		18
#define	F_occupant_misc_2		19
#define	F_occupant_misc_3		20
#define	CRO_REC_FN			21
#define	CRO_REC_FNM	{		\
	DI_CRO_Q_PRODUCT_ID,		\
	DI_CRO_Q_CHASSIS_ID,		\
	DI_CRO_Q_ALIAS_ID,		\
	DI_CRO_Q_RECEPTACLE_NAME,	\
	DI_CRO_Q_RECEPTACLE_TYPE,	\
	DI_CRO_Q_RECEPTACLE_FMRI,	\
	DI_CRO_Q_OCCUPANT_TYPE,		\
	DI_CRO_Q_OCCUPANT_INSTANCE,	\
	DI_CRO_Q_DEVCHASSIS_PATH,	\
	DI_CRO_Q_OCCUPANT_DEVICES,	\
	DI_CRO_Q_OCCUPANT_PATHS,	\
	DI_CRO_Q_OCCUPANT_COMPDEV,	\
	DI_CRO_Q_OCCUPANT_DEVID,	\
	DI_CRO_Q_OCCUPANT_MFG,		\
	DI_CRO_Q_OCCUPANT_MODEL,	\
	DI_CRO_Q_OCCUPANT_PART,		\
	DI_CRO_Q_OCCUPANT_SERIAL,	\
	DI_CRO_Q_OCCUPANT_FIRM,		\
	DI_CRO_Q_OCCUPANT_MISC_1,	\
	DI_CRO_Q_OCCUPANT_MISC_2,	\
	DI_CRO_Q_OCCUPANT_MISC_3	\
}

#define	CRO_REC_F(t, r)		t r[CRO_REC_FN]
#define	CRO_REC_FP(t, r)	t (*r)[CRO_REC_FN]

CRO_REC_F(char *,	cro_rec_fnm) = CRO_REC_FNM;

/* implementation of a field, with indexed values, in a record */
typedef struct rec_f {
	uint_t		f_nv;		/* number of indexed values */
	char		**f_v;		/* indexed values */
} rec_f_t;

/* implementation of opaque interface datatypes */
struct di_cro_rec {
	di_cro_rec_t	r_next;		/* next record on linked list */
	void		*r_priv;
	uint32_t	r_rec_flag;
	CRO_REC_F(rec_f_t, r_f);
};

struct di_cro_reca  {
	int		ra_nrec;
	di_cro_rec_t	ra_r[1];	/* array of record pointers */
};

struct di_cro_hdl {
	di_cro_rec_t	h_rec;		/* linked list of records */
	int		h_nrec;
	char		*h_db_file;
	char		*h_date;
	char		*h_server_id;
	char		*h_product_id;
	char		*h_chassis_id;
	char		*h_fletcher;
	uint64_t	h_cna;
};

struct di_cromk_hdl {
	di_cro_rec_t	mk_rec;		/* linked list of records */
	int		mk_nrec;
	char		*mk_date;
	char		*mk_server_id;
	char		*mk_product_id;
	char		*mk_chassis_id;
	uint64_t	mk_cna;

	di_pca_hdl_t	mk_pca;		/* Product.Chassis Alias snapshot */
};

#define	RF0(r, f)		r->r_f[F_##f].f_v[0]
#define	RF0_HASINFO(r, f)	((RF0(r, f) && *RF0(r, f)) ? 1 : 0)
#define	RF0_REASSIGN(r, f, v)	{					\
	if (RF0(r, f))							\
		free(RF0(r, f));					\
	RF0(r, f) = v;							\
	}

#define	DB_PERMS		(S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR)

#define	DB_VERSION		"version"
#define	DB_VERSION_VALUE	1
#define	DB_MAGIC		"magic"
#define	DB_MAGIC_VALUE		0x43524F00		/* CRO\0 */
#define	DB_DATE			"date"
#define	DB_SERVER_ID		"server-id"
#define	DB_PRODUCT_ID		"product-id"
#define	DB_CHASSIS_ID		"chassis-id"
#define	DB_CNA			"cna"
#define	DB_NREC			"nrec"
#define	DB_FLETCHER		"fletcher"
#define	DB_RECA			"reca"
#define	DB_REC_FLAG		"rec_flag"

static int _cro_rec_walk(di_cro_hdl_t h, uint32_t rec_flag,
    CRO_REC_FP(char *, refp), void *arg,
    int (*r_callback)(di_cro_rec_t r, void *arg));

/* ARGSUSED */
static int
_cro_rec_free(di_cro_rec_t r, void *arg)
{
	int	i, j;

	for (i = 0; i < CRO_REC_FN; i++) {
		if (r->r_f[i].f_v) {
			for (j = 0; j < r->r_f[i].f_nv; j++) {
				if (r->r_f[i].f_v[j])
					free(r->r_f[i].f_v[j]);
			}
			free(r->r_f[i].f_v);
		}
	}
	free(r);
	return (DI_WALK_CONTINUE);
}

/*
 * NOTE: At some point, we may need to improve clean.  At that time, consider:
 *
 *  o	topo_cleanup_auth_str
 *  o	http://en.wikipedia.org/wiki/Filename
 *  o	escape via http://en.wikipedia.org/wiki/URI
 *  o	compact white space.
 */
char *
di_cro_strclean(char *s0, int doslash, int doperiod)
{
	char	*s = s0;

	if (s == NULL)
		return (NULL);

	/* for now, simple clean-in-place */
	for (; s && *s; s++) {
		if (*s != (*s & 0x7F))
			*s = *s & 0x7F;
		if (*s == ' ')
			*s = '_';
		if (doslash && (*s == '/'))
			*s = '_';
		if (doperiod && (*s == '.'))
			*s = '_';
	}
	return (s0);
}

/* add "/devices/" prefix (if needed) */
static char *
_cro_devices(char *s0)
{
	char	*s = s0;
	char	*ps;
	int	len;

	if ((s == NULL) || (*s == 0))
		return (s0);
	if (strncmp("/devices/", s, strlen("/devices/")) == 0)
		return (s0);

	while (*s && (*s == '/'))
		s++;
	if (*s == 0)
		return (s0);

	len = strlen("/devices") + 1 + strlen(s) + 1;
	if ((ps = malloc(len)) == NULL)
		return (s0);

	(void) snprintf(ps, len, "/devices/%s", s);
	free(s0);
	return (ps);
}

/*
 * Keep all the knowledge of how to form /dev/chassis namespace in a single
 * place. This interface is used both to form the 'standard' devchassis_path
 * representation the CRO database, and by devchassisd(1M) to construct the
 * namespace so that the 'standard' form references resolve (this includes
 * the 'raw' path form and any 'alias' symlinks).
 *
 * NOTE: internal is only defined for DI_CRODC_REC_LINKINFO_STD.
 */
int
di_crodc_rec_linkinfo(di_cro_hdl_t h, di_cro_rec_t r, int internal, int info,
    char **ppath, char **plink)
{
	char	*path = NULL;
	char	*link = NULL;
	int	len;
	char	*s;

	if (ppath)
		*ppath = NULL;
	if (plink)
		*plink = NULL;

	switch (info) {
	case DI_CRODC_REC_LINKINFO_STD:
	case DI_CRODC_REC_LINKINFO_RAW:
		/*
		 * Form the 'standard' devchassis_path:
		 *
		 *  /dev/chassis/<product_id>.<chassis_id>/<receptacle_name>
		 *  [/<occupant_type>[<occupant_instance>]]
		 *
		 * using <alias_id> when possible (unless caller is asking
		 * for RAW). NOTE: if the <alias_id> is "SYS" then we have
		 * already added the well-known SYS prefix to the
		 * <receptacle_name>, so don't add the <alias_id>.
		 *
		 * The 'raw' form has not symlinks - so it inserts
		 * DI_CRODC_ALIASED_DIR after /dev/chassis/ when there is
		 * a alias specified, and does not use the alias in the
		 * path.
		 *
		 * Need record with product_id, chassis_id, and
		 * receptacle_name to do anything.
		 */
		if (!(ppath && r &&
		    RF0_HASINFO(r, product_id) &&
		    RF0_HASINFO(r, chassis_id) &&
		    RF0_HASINFO(r, receptacle_name)))
			goto fail;

		len = strlen(DI_CRODC_DEVCHASSIS) + 1;
		if ((info == DI_CRODC_REC_LINKINFO_RAW) &&
		    RF0_HASINFO(r, alias_id))
			len += strlen(DI_CRODC_ALIASED_DIR) + 1;

		if ((info == DI_CRODC_REC_LINKINFO_STD) &&
		    RF0_HASINFO(r, alias_id)) {
			if (!internal)
				len += strlen(RF0(r, alias_id)) + 1;
			len += strlen(RF0(r, receptacle_name)) + 1;
		} else
			len += strlen(RF0(r, product_id)) + 1 +
			    strlen(RF0(r, chassis_id)) + 1 +
			    strlen(RF0(r, receptacle_name)) + 1;

		if (RF0_HASINFO(r, occupant_type))
			len += strlen(RF0(r, occupant_type)) + 1;
		if (RF0_HASINFO(r, occupant_instance))
			len += strlen(RF0(r, occupant_instance));

		if ((path = s = malloc(len)) == NULL)
			goto fail;

		(void) snprintf(s, len, "%s", DI_CRODC_DEVCHASSIS);
		len -= strlen(s);
		s += strlen(s);

		if ((info == DI_CRODC_REC_LINKINFO_RAW) &&
		    RF0_HASINFO(r, alias_id)) {
			(void) snprintf(s, len, "/%s", DI_CRODC_ALIASED_DIR);
			len -= strlen(s);
			s += strlen(s);
		}

		if ((info == DI_CRODC_REC_LINKINFO_STD) &&
		    RF0_HASINFO(r, alias_id)) {
			if (!internal)
				(void) snprintf(s, len, "/%s/%s",
				    RF0(r, alias_id),
				    RF0(r, receptacle_name));
			else
				(void) snprintf(s, len, "/%s",
				    RF0(r, receptacle_name));
		} else
			(void) snprintf(s, len, "/" DI_CRODC_PC_FMT "/%s",
			    RF0(r, product_id), RF0(r, chassis_id),
			    RF0(r, receptacle_name));
		len -= strlen(s);
		s += strlen(s);

		if (RF0_HASINFO(r, occupant_type)) {
			(void) snprintf(s, len, "/%s", RF0(r, occupant_type));
			len -= strlen(s);
			s += strlen(s);
		}
		if (RF0_HASINFO(r, occupant_instance)) {
			(void) snprintf(s, len, "%s",
			    RF0(r, occupant_instance));
			len -= strlen(s);
			s += strlen(s);
		}

		if (plink && RF0_HASINFO(r, occupant_devices))
			link = strdup(RF0(r, occupant_devices));
		break;

	case DI_CRODC_REC_LINKINFO_ALIASLINK:
		/*
		 * Return information to construct a alias based link.
		 * We need a alias, and the caller needs to be interested
		 * in links to return a result. NOTE: the well-known SYS
		 * alias is is excluded (it uses RAWSYS/SYSLINK).
		 */
		if (!(ppath && r &&
		    RF0_HASINFO(r, product_id) &&
		    RF0_HASINFO(r, chassis_id) &&
		    RF0_HASINFO(r, alias_id) &&
		    strcmp(RF0(r, alias_id), DI_CRODC_SYSALIAS)))
			goto fail;

		len = strlen(DI_CRODC_DEVCHASSIS) + 1 +
		    strlen(RF0(r, alias_id)) + 1;
		if ((path = malloc(len)) == NULL)
			goto fail;
		(void) snprintf(path, len, "%s/%s",
		    DI_CRODC_DEVCHASSIS, RF0(r, alias_id));

		len = strlen(DI_CRODC_ALIASED_DIR) + 1 +
		    strlen(RF0(r, product_id)) + 1 +
		    strlen(RF0(r, chassis_id)) + 1;
		if ((link = malloc(len)) == NULL)
			goto fail;
		(void) snprintf(link, len, "%s/" DI_CRODC_PC_FMT,
		    DI_CRODC_ALIASED_DIR,
		    RF0(r, product_id), RF0(r, chassis_id));
		break;

	case DI_CRODC_REC_LINKINFO_RAWSYS:
		if (!(h && ppath && h->h_product_id && h->h_chassis_id))
			goto fail;
		len = strlen(DI_CRODC_DEVCHASSIS) + 1 +
		    strlen(DI_CRODC_ALIASED_DIR) + 1 +
		    strlen(h->h_product_id) + 1 +
		    strlen(h->h_chassis_id) + 1 +
		    strlen(DI_CRODC_SYSALIAS) + 1;
		if ((path = malloc(len)) == NULL)
			goto fail;
		(void) snprintf(path, len, "%s/%s/" DI_CRODC_PC_FMT "/%s",
		    DI_CRODC_DEVCHASSIS, DI_CRODC_ALIASED_DIR,
		    h->h_product_id, h->h_chassis_id, DI_CRODC_SYSALIAS);
		break;

	case DI_CRODC_REC_LINKINFO_SYSLINK:
		/*
		 * Return information to construct a well-knonw SYS link.
		 * Caller needs to be interested in links and provide
		 * h->h_product_id and h->h_chassis_id.
		 */
		if (!(h && ppath && plink &&
		    h->h_product_id && h->h_chassis_id))
			goto fail;
		len = strlen(DI_CRODC_DEVCHASSIS) + 1 +
		    strlen(DI_CRODC_SYSALIAS) + 1;
		if ((path = malloc(len)) == NULL)
			goto fail;
		(void) snprintf(path, len, "%s/%s",
		    DI_CRODC_DEVCHASSIS, DI_CRODC_SYSALIAS);

		len = strlen(DI_CRODC_ALIASED_DIR) + 1 +
		    strlen(h->h_product_id) + 1 +
		    strlen(h->h_chassis_id) + 1 +
		    strlen(DI_CRODC_SYSALIAS) + 1;
		if ((link = malloc(len)) == NULL)
			goto fail;
		(void) snprintf(link, len, "%s/" DI_CRODC_PC_FMT "/%s",
		    DI_CRODC_ALIASED_DIR, h->h_product_id, h->h_chassis_id,
		    DI_CRODC_SYSALIAS);
		break;


	default:
		goto fail;
	}
	if (path)
		*ppath = path;
	if (plink)
		*plink = link;
	return (1);			/* success */

fail:	if (path)
		free(path);
	if (link)
		free(link);
	return (0);
}

/* Compare two records for qsort */
static int
_cro_rec_cmp(const void *arg1, const void *arg2)
{
	di_cro_rec_t	r1 = *((di_cro_rec_t *)arg1);
	di_cro_rec_t	r2 = *((di_cro_rec_t *)arg2);
	char		*s1 = NULL;
	char		*s2 = NULL;
	char		*p1;
	char		*p2;
	char		*b1;
	char		*b2;
	int		n1;
	int		n2;

	/* sort by location (/dev/chassis path) */
	if (RF0_HASINFO(r1, devchassis_path))
		s1 = RF0(r1, devchassis_path);
	if (RF0_HASINFO(r2, devchassis_path))
		s2 = RF0(r2, devchassis_path);
	if (s1 && s2) {
		/*
		 * NOTE: this code matches that used by format(1M).
		 *
		 * Put internal "/SYS/" disks first, and among internal
		 * disks, put "/BOOT" disks first.
		 */
		p1 = strstr(s1, DI_CRODC_SYSALIAS_SS);
		p2 = strstr(s2, DI_CRODC_SYSALIAS_SS);
		if (p1 && p1) {
			b1 = strstr(s1, "/BOOT");
			b2 = strstr(s2, "/BOOT");
			if (b1 && !b2)
				return (-1);
			if (!b1 && b2)
				return (1);
		}
		if (p1 && !p2)
			return (-1);
		if (!p1 && p2)
			return (1);

		for (;;) {
			if (*s1 == 0 || *s2 == 0)
				break;
			if (((*s1 >= '0') && (*s1 <= '9')) &&
			    ((*s2 >= '0') && (*s2 <= '9'))) {
				n1 = strtol(s1, &p1, 10);
				n2 = strtol(s2, &p2, 10);
				if (n1 != n2) {
					return (n1 - n2);
				}
				s1 = p1;
				s2 = p2;
			} else if (*s1 != *s2) {
				break;
			} else {
				s1++;
				s2++;
			}
		}

		return (*s1 - *s2);
	}
	if (s1 && !s2)
		return (-1);
	if (!s1 && s2)
		return (1);
	return (0);
}

static void
_crodb_rec_sort(di_cromk_hdl_t mk)
{
	di_cro_rec_t	*ra, *rap, *rp;
	di_cro_rec_t	r;

	/* Allocate memory for array of pointers to records. */
	if ((ra = (di_cro_rec_t *)calloc(1,
	    (mk->mk_nrec + 1) * sizeof (di_cro_rec_t))) == NULL)
		return;

	/* Form array of pointers to records. */
	for (rap = ra, r = mk->mk_rec; r; rap++, r = r->r_next)
		*rap = r;
	*rap = NULL;

	/* Sort the array. */
	qsort((void *) ra, mk->mk_nrec, sizeof (di_cro_rec_t), _cro_rec_cmp);

	/* Rebuild list of records from sorted array. */
	for (rp = &mk->mk_rec, rap = ra, r = *rap; r; rap++, r = *rap) {
		*rp = r;
		rp = &r->r_next;
	}
	*rp = NULL;
	free(ra);
}

/* Enhance an record before it gets written to the database */
/*ARGSUSED*/
static void
_crodb_rec_prewrite_enhance(di_cromk_hdl_t mk, di_cro_rec_t r, int flags)
{
	int		i, j;
	char		*pc;
	di_pca_rec_t	pca_r = NULL;
	char		*alias_id = NULL;
	char		*rn, *receptacle;
	int		len;
	char		*devchassis_path;
	int		internal = 0;

	/* clean up the fields */
	for (i = 0; i < CRO_REC_FN; i++)
		if (r->r_f[i].f_nv && r->r_f[i].f_v)
			for (j = 0; j < r->r_f[i].f_nv; j++)
				r->r_f[i].f_v[j] =
				    di_cro_strclean(r->r_f[i].f_v[j], 0, 0);

	/* clean '/' from single-component path fields */
	RF0(r, product_id)	= di_cro_strclean(RF0(r, product_id), 1, 1);
	RF0(r, chassis_id)	= di_cro_strclean(RF0(r, chassis_id), 1, 1);
	RF0(r, alias_id)	= di_cro_strclean(RF0(r, alias_id), 1, 0);
	RF0(r, occupant_type)	= di_cro_strclean(RF0(r, occupant_type), 1, 1);

	if (mk->mk_product_id && RF0_HASINFO(r, product_id) &&
	    mk->mk_chassis_id && RF0_HASINFO(r, chassis_id) &&
	    (strcmp(mk->mk_product_id, RF0(r, product_id)) == 0) &&
	    (strcmp(mk->mk_chassis_id, RF0(r, chassis_id)) == 0))
		internal = 1;

	/* look for <product_id>.<chassis_id> alias */
	if (!RF0_HASINFO(r, alias_id) &&
	    mk->mk_product_id && RF0_HASINFO(r, product_id) &&
	    mk->mk_chassis_id && RF0_HASINFO(r, chassis_id)) {
		len = strlen(RF0(r, product_id)) + 1 +
		    strlen(RF0(r, chassis_id)) + 1;
		if ((pc = malloc(len)) != NULL) {
			(void) snprintf(pc, len, DI_CRODC_PC_FMT,
			    RF0(r, product_id), RF0(r, chassis_id));
			pca_r = di_pca_rec_lookup(mk->mk_pca, pc);
			free(pc);
		}

		if (pca_r)
			alias_id = di_pca_rec_get_alias_id(pca_r);
		else
			alias_id = NULL;

		/* If no alias, use well-known SYS alias for internal */
		if ((alias_id == NULL) && internal)
			alias_id = DI_CRODC_SYSALIAS;
		if (alias_id) {
			RF0_REASSIGN(r, alias_id, strdup(alias_id));
		}
	}

	/* If internal well-known SYS prefix is missing, add it. */
	if (RF0_HASINFO(r, receptacle_name)) {
		rn = RF0(r, receptacle_name);
		while (*rn == '/')
			rn++;
	} else
		rn = NULL;
	if (internal && rn &&
	    (strncmp(DI_CRODC_SYSALIAS "/", rn,
	    strlen(DI_CRODC_SYSALIAS) + 1) != 0)) {
		len = strlen(DI_CRODC_SYSALIAS) + 1 + strlen(rn) + 1;
		if ((receptacle = malloc(len)) != NULL) {
			(void) snprintf(receptacle, len, "%s/%s",
			    DI_CRODC_SYSALIAS, rn);
			RF0_REASSIGN(r, receptacle_name, receptacle);
		}
	}

	/* Place the standard /dev/chassis path in the database */
	if (!RF0_HASINFO(r, devchassis_path) &&
	    di_crodc_rec_linkinfo(NULL, r, internal,
	    DI_CRODC_REC_LINKINFO_STD, &devchassis_path, NULL) &&
	    devchassis_path) {
		RF0_REASSIGN(r, devchassis_path, devchassis_path);
	}

	/* ensure "/devices/" prefix */
	for (i = 0; i < r->r_f[F_occupant_devices].f_nv; i++)
		r->r_f[F_occupant_devices].f_v[i] =
		    _cro_devices(r->r_f[F_occupant_devices].f_v[i]);
	for (i = 0; i < r->r_f[F_occupant_paths].f_nv; i++)
		r->r_f[F_occupant_paths].f_v[i] =
		    _cro_devices(r->r_f[F_occupant_paths].f_v[i]);
}

/*
 * This is a local copy of the fletcher_4 algorithm used by ZFS.
 * This code is cribbed from usr/src/common/zfs/zfs_fletcher.c.
 * If this code is ever available in a 'convient' non-zfs library, like
 * libmd, then the implementation here should be removed.
 */
static char *
fletcher_4_native(const void *buf, uint64_t size)
{
	const uint32_t	*ip = buf;
	const uint32_t	*ipend = ip + (size / sizeof (uint32_t));
	uint64_t	a, b, c, d;
	int		fletcher_str_len;
	char		*fletcher;

	for (a = b = c = d = 0; ip < ipend; ip++) {
		a += ip[0];
		b += a;
		c += b;
		d += c;
	}

	/* format fletcher_4 the same way ZFS does */
	fletcher_str_len = (sizeof (uint64_t) * 2 * 4) + 4;
	fletcher = calloc(1, fletcher_str_len);
	if (fletcher)
		(void) snprintf(fletcher, fletcher_str_len,
		    "%llx:%llx:%llx:%llx", a, b, c, d);
	return (fletcher);
}

/*ARGSUSED*/
static int
_crodb_write(di_cromk_hdl_t mk, int flags)
{
	nvlist_t	*db_nvl = NULL;
	nvlist_t	**ra_nvl = NULL;
	char		*db_buf = NULL;
	size_t		db_buf_size = 0;
	int		db_fd = -1;
	di_cro_rec_t	r;
	int		i, j;
	di_cro_hdl_t	dch;
	nvlist_t	*fletcher_nvl = NULL;
	char		*fletcher_buf = NULL;
	size_t		fletcher_buf_size;
	char		*fletcher = NULL;
	char		*fletcher_c;
	sysevent_id_t	eid;
	struct stat	db_stat;
	int		rval = -1;

	/*
	 * Since topo snapshots take a long time, take the ProductChassisAlias
	 * snapshot here instead of in di_cromk_init.
	 */
	mk->mk_pca = di_pca_init(0);

	/* Allocate and initialize the db_nvl nvlist (the entire db file). */
	if (nvlist_alloc(&db_nvl, NV_UNIQUE_NAME, 0) ||
	    nvlist_add_int32(db_nvl, DB_VERSION, DB_VERSION_VALUE) ||
	    nvlist_add_int32(db_nvl, DB_MAGIC, DB_MAGIC_VALUE) ||
	    nvlist_add_string(db_nvl, DB_DATE, mk->mk_date) ||
	    nvlist_add_string(db_nvl, DB_SERVER_ID, mk->mk_server_id) ||
	    nvlist_add_string(db_nvl, DB_PRODUCT_ID, mk->mk_product_id) ||
	    nvlist_add_string(db_nvl, DB_CHASSIS_ID, mk->mk_chassis_id) ||
	    nvlist_add_uint64(db_nvl, DB_CNA, mk->mk_cna) ||
	    nvlist_add_int32(db_nvl, DB_NREC, mk->mk_nrec))
		goto fail;

	if (mk->mk_nrec == 0)
		goto empty;

	/* Allocate array of nvlist pointers for db records */
	if ((ra_nvl = calloc(mk->mk_nrec, sizeof (*ra_nvl))) == NULL)
		goto fail;

	/* Walk the mk records, "enhancing" the records */
	for (r = mk->mk_rec, i = 0; i < mk->mk_nrec; r = r->r_next, i++)
		_crodb_rec_prewrite_enhance(mk, r, flags);

	/* Sort the enhanced records */
	_crodb_rec_sort(mk);

	/* Walk the mk records, allocating and initializing record nvlist */
	for (r = mk->mk_rec, i = 0; i < mk->mk_nrec; r = r->r_next, i++) {
		if (nvlist_alloc(&ra_nvl[i], NV_UNIQUE_NAME, 0))
			goto fail;

		for (j = 0; j < CRO_REC_FN; j++) {
			if (nvlist_add_string_array(ra_nvl[i],
			    cro_rec_fnm[j], r->r_f[j].f_v, r->r_f[j].f_nv))
				break;
		}
		if (j < CRO_REC_FN)
			break;

		if (nvlist_add_uint32(ra_nvl[i], DB_REC_FLAG, r->r_rec_flag))
			break;
	}

	/* Sanity test - we must be at the end of the list. */
	if (r)
		goto fail;

	/* Pack all the records so we can compute their fletcher checksum. */
	if (nvlist_alloc(&fletcher_nvl, NV_UNIQUE_NAME, 0) ||
	    nvlist_add_nvlist_array(fletcher_nvl, DB_RECA, ra_nvl,
	    mk->mk_nrec) ||
	    nvlist_pack(fletcher_nvl, &fletcher_buf, &fletcher_buf_size,
	    NV_ENCODE_NATIVE, 0))
		goto fail;

	fletcher = fletcher_4_native(fletcher_buf, fletcher_buf_size);
	if (fletcher == NULL)
		goto fail;

	/*
	 * Get fletcher from a header-only snapshot of the current database to
	 * see if we need to update the cro database.
	 */
	dch = di_cro_init(NULL, DI_INIT_HEADERONLY);
	if (dch) {
		fletcher_c = di_cro_get_fletcher(dch);

		if (fletcher_c && (strcmp(fletcher_c, fletcher) == 0)) {
			/*
			 * With fletcher already in sync, we are not going to
			 * signal devchassisd, but we still have di_pca_sync
			 * waiting... so indicate finish here.
			 */
			(void) sysevent_post_event(EC_CRO,
			    ESC_CRO_DBUPDATE_FINISH, SUNW_VENDOR, "fmd",
			    NULL, &eid);
			rval = 0;		/* success */
			di_cro_fini(dch);
			goto out;		/* no database update needed */
		}
		di_cro_fini(dch);
	}

	/* Add the checksum and all the records to the database nvlist. */
	if (nvlist_add_string(db_nvl, DB_FLETCHER, fletcher) ||
	    nvlist_add_nvlist_array(db_nvl, DB_RECA, ra_nvl, mk->mk_nrec))
		goto fail;

	/* Pack the database nvlist and write it to the database file. */
empty:	if (nvlist_pack(db_nvl, &db_buf, &db_buf_size, NV_ENCODE_NATIVE, 0))
		goto fail;

	if ((db_fd = open(DI_CRO_DB_FILE_NEW,
	    O_WRONLY | O_CREAT | O_TRUNC, DB_PERMS)) < 0)
		goto fail;

	if (write(db_fd, db_buf, db_buf_size) != db_buf_size)
		goto fail;

	(void) close(db_fd);
	db_fd = -1;

	/* rotate files to put new one in place */
	if (stat(DI_CRO_DB_FILE, &db_stat) == 0)
		(void) rename(DI_CRO_DB_FILE, DI_CRO_DB_FILE_OLD);
	(void) rename(DI_CRO_DB_FILE_NEW, DI_CRO_DB_FILE);

	/* Signal cro database update with sysevent */
	if (sysevent_post_event(EC_CRO, ESC_CRO_DBUPDATE,
	    SUNW_VENDOR, "fmd", NULL, &eid) != 0)
		goto fail;
out:	rval = 0;			/* success */

fail:	if (db_fd >= 0)
		(void) close(db_fd);
	if (db_buf)
		free(db_buf);

	if (fletcher)
		free(fletcher);
	if (fletcher_buf)
		free(fletcher_buf);
	if (fletcher_nvl)
		nvlist_free(fletcher_nvl);

	if (ra_nvl) {
		for (i = 0; i < mk->mk_nrec; i++)
			nvlist_free(ra_nvl[i]);
		free(ra_nvl);
	}
	if (db_nvl)
		nvlist_free(db_nvl);

	if (mk->mk_pca)
		di_pca_fini(mk->mk_pca);
	mk->mk_pca = NULL;

	return (rval);
}

static int
_crodb_read(di_cro_hdl_t h, int flags)
{
	struct stat	db_stat;
	int		db_fd = -1;
	char		*db_buf = NULL;
	nvlist_t	*db_nvl = NULL;
	nvlist_t	**ra_nvl = NULL;
	int		version;
	int		magic;
	int		ra_nrec;
	di_cro_rec_t	r, *rp;
	char		**s;
	int		i, j, k;
	uint_t		nele;
	int		rval = -1;
	char		*date, *server_id, *product_id, *chassis_id;
	char		*fletcher = NULL;

	h->h_rec = NULL;
	h->h_nrec = 0;

	if ((db_fd = open(h->h_db_file, O_RDONLY)) < 0)
		goto out;

	if (fstat(db_fd, &db_stat))
		goto out;

	if ((db_buf = calloc(1, db_stat.st_size)) == NULL)
		goto out;

	if (read(db_fd, db_buf, db_stat.st_size) != db_stat.st_size)
		goto out;

	if (nvlist_unpack(db_buf, db_stat.st_size, &db_nvl, 0))
		goto out;

	/* required with specific value */
	if (nvlist_lookup_int32(db_nvl, DB_VERSION, &version) ||
	    (version != DB_VERSION_VALUE) ||
	    nvlist_lookup_int32(db_nvl, DB_MAGIC, &magic) ||
	    (magic != DB_MAGIC_VALUE) ||
	    nvlist_lookup_int32(db_nvl, DB_NREC, &h->h_nrec) ||
	    (h->h_nrec < 0))
		goto out;

	/* required */
	if (nvlist_lookup_string(db_nvl, DB_DATE, &date) ||
	    nvlist_lookup_string(db_nvl, DB_SERVER_ID, &server_id) ||
	    nvlist_lookup_string(db_nvl, DB_PRODUCT_ID, &product_id) ||
	    nvlist_lookup_string(db_nvl, DB_CHASSIS_ID, &chassis_id) ||
	    nvlist_lookup_uint64(db_nvl, DB_CNA, &h->h_cna))
		goto out;

	/* optional */
	(void) nvlist_lookup_string(db_nvl, DB_FLETCHER, &fletcher);

	/* The header outlives the nvlist, so dup strings. */
	if (date)
		h->h_date = strdup(date);
	if (server_id)
		h->h_server_id = strdup(server_id);
	if (product_id)
		h->h_product_id = strdup(product_id);
	if (chassis_id)
		h->h_chassis_id = strdup(chassis_id);
	if (fletcher)
		h->h_fletcher = strdup(fletcher);
	if ((date && !h->h_date) ||
	    (server_id && !h->h_server_id) ||
	    (product_id && !h->h_product_id) ||
	    (chassis_id && !h->h_chassis_id) ||
	    (fletcher && !h->h_fletcher))
		goto out;		/* memory alloc problem above */

	if ((flags & DI_INIT_HEADERONLY) || h->h_nrec == 0)
		goto empty;

	if (nvlist_lookup_nvlist_array(db_nvl, DB_RECA,
	    &ra_nvl, (uint_t *)(&ra_nrec)) || (h->h_nrec != ra_nrec))
		goto out;

	/* Form in-memory records from nvlist (preserve file order) */
	h->h_nrec = 0;
	for (rp = &h->h_rec, i = 0; i < ra_nrec;
	    i++, rp = &r->r_next, h->h_nrec++) {
		if ((r = calloc(1, sizeof (*r))) == NULL)
			goto out;
		*rp = r;

		for (j = 0; j < CRO_REC_FN; j++) {
			/* ensure null for non-existent fields */
			if (nvlist_lookup_string_array(ra_nvl[i],
			    cro_rec_fnm[j], &s, &nele)) {
				s = NULL;
				nele = 0;
			}
			if (s && nele) {
				r->r_f[j].f_nv = nele;
				r->r_f[j].f_v = calloc(nele,
				    sizeof (r->r_f[j].f_v[0]));
				if (r->r_f[j].f_v)
					for (k = 0; k < nele; k++, s++)
						r->r_f[j].f_v[k] =
						    strdup(*s ? *s : "");
			} else {
				r->r_f[j].f_nv = 1;		/* null */
				r->r_f[j].f_v = calloc(1,
				    sizeof (r->r_f[j].f_v[0]));
				if (r->r_f[j].f_v)
					r->r_f[j].f_v[0] = strdup("");
			}
		}

		(void) nvlist_lookup_uint32(ra_nvl[i],
		    DB_REC_FLAG, &r->r_rec_flag);
	}

empty:	rval = 0;			/* success */

out:	if (db_fd >= 0)
		(void) close(db_fd);
	if (db_nvl)
		nvlist_free(db_nvl);
	if (db_buf)
		free(db_buf);
	return (rval);
}

/*ARGSUSED*/
di_cro_hdl_t
di_cro_init(char *cro_db_file, int flags)
{
	di_cro_hdl_t	h = NULL;

	if ((h = calloc(1, sizeof (*h))) == NULL)
		goto out;

	h->h_db_file = strdup(cro_db_file ? cro_db_file : DI_CRO_DB_FILE);

	if (_crodb_read(h, flags)) {
		di_cro_fini(h);
		h = NULL;
	}
out:	return (h);
}

uint64_t
di_cro_get_cna(di_cro_hdl_t h)
{
	return (h ? h->h_cna : 0LL);
}

char *
di_cro_get_fletcher(di_cro_hdl_t h)
{
	return (h ? h->h_fletcher : NULL);
}

char *
di_cro_get_date(di_cro_hdl_t h)
{
	return (h ? h->h_date : NULL);
}

char *
di_cro_get_server_id(di_cro_hdl_t h)
{
	return (h ? h->h_server_id : NULL);
}

char *
di_cro_get_product_id(di_cro_hdl_t h)
{
	return (h ? h->h_product_id : NULL);
}

char *
di_cro_get_chassis_id(di_cro_hdl_t h)
{
	return (h ? h->h_chassis_id : NULL);
}

void
di_cro_fini(di_cro_hdl_t h)
{
	if (h == NULL)
		return;

	/* walk all records and free */
	(void) _cro_rec_walk(h, 0, NULL, NULL, _cro_rec_free);

	if (h->h_db_file)
		free(h->h_db_file);
	if (h->h_date)
		free(h->h_date);
	if (h->h_server_id)
		free(h->h_server_id);
	if (h->h_product_id)
		free(h->h_product_id);
	if (h->h_chassis_id)
		free(h->h_chassis_id);
	if (h->h_fletcher)
		free(h->h_fletcher);

	free(h);
}

/*ARGSUSED*/
static int
_cro_rec_walk(di_cro_hdl_t h, uint32_t rec_flag, CRO_REC_FP(char *, refp),
    void *arg, int (*r_callback)(di_cro_rec_t r, void *arg))
{
	char			*h_product_id;
	char			*h_chassis_id;
	di_cro_rec_t		r, r_next;
	int			i, j, m;
	int			internal;
	int			wt;		/* walk terminate */
	CRO_REC_F(regex_t,	reg_f);

	if (h == NULL)
		return (NULL);


	h_product_id = di_cro_get_product_id(h);
	h_chassis_id = di_cro_get_chassis_id(h);

	for (i = 0; i < CRO_REC_FN; i++)
		if (refp && (*refp)[i] &&
		    regcomp(&reg_f[i], (*refp)[i], REG_EXTENDED | REG_NOSUB))
			break;
	if (i < CRO_REC_FN) {
		/* regfree what worked... */
		for (j = 0; j < i; j++)
			if (refp && (*refp)[j])
				regfree(&reg_f[j]);
		return (-1);
	}

	for (r = h->h_rec; r; r = r_next) {
		r_next = r->r_next;	/* incase callback does free */

		if ((r->r_rec_flag & DI_CRO_REC_FLAG_PRIV) &&
		    !(rec_flag & DI_CRO_REC_FLAG_PRIV))
			continue;		/* skip private records */

		if (h_product_id && h_chassis_id &&
		    RF0_HASINFO(r, product_id) && RF0_HASINFO(r, chassis_id) &&
		    (strcmp(RF0(r, product_id), h_product_id) == 0) &&
		    (strcmp(RF0(r, chassis_id), h_chassis_id) == 0))
			internal = 1;
		else
			internal = 0;

		for (i = 0; i < CRO_REC_FN; i++) {
			if (refp && (*refp)[i]) {
				for (m = 0, j = 0; j < r->r_f[i].f_nv; j++) {
					if (r->r_f[i].f_v[j] &&
					    r->r_f[i].f_v[j][0] &&
					    (regexec(&reg_f[i],
					    r->r_f[i].f_v[j],
					    0, NULL, 0) == 0))
						m++;	/* match */
				}

				/*
				 * Special case compare/match for well-known
				 * SYS alias_id compare/match for the case
				 * when a head also has an administered,
				 * non-"SYS", alias.
				 */
				if ((i == F_alias_id) && internal &&
				    (regexec(&reg_f[i], DI_CRODC_SYSALIAS,
				    0, NULL, 0) == 0))
					m++;		/* match */

				if (m == 0)
					break;		/* mismatch */
			}
		}
		if (i < CRO_REC_FN)
			continue;			/* mismatch */

		wt = (*r_callback)(r, arg);
		if (wt == DI_WALK_TERMINATE)
			break;
	}

	for (i = 0; i < CRO_REC_FN; i++)
		if (refp && (*refp)[i])
			regfree(&reg_f[i]);
	return (0);
}

/* add new record to end of record array */
static int
_cro_reca_a(di_cro_rec_t r, void *arg)
{
	di_cro_reca_t	*rap = (di_cro_reca_t *)arg;
	di_cro_reca_t	ra = *rap;

	if (ra == NULL) {
		if ((ra = calloc(1, sizeof (*ra))) == NULL)
			return (DI_WALK_CONTINUE);
		ra->ra_nrec = 1;
		ra->ra_r[0] = r;
	} else {
		if ((ra = realloc(ra, sizeof (*ra) +
		    (ra->ra_nrec * sizeof (ra)))) == NULL)
			return (DI_WALK_CONTINUE);
		ra->ra_r[ra->ra_nrec] = r;
		ra->ra_nrec++;
	}
	*rap = ra;
	return (DI_WALK_CONTINUE);
}

di_cro_reca_t
di_cro_reca_create(di_cro_hdl_t h,
    uint32_t	rec_flag,
    char	*re_product_id,
    char	*re_chassis_id,
    char	*re_alias_id,
    char	*re_receptacle_name,
    char	*re_receptacle_type,
    char	*re_receptacle_fmri,
    char	*re_occupant_type,
    char	*re_occupant_instance,
    char	*re_devchassis_path,
    char	*re_occupant_devices,
    char	*re_occupant_paths,
    char	*re_occupant_compdev,
    char	*re_occupant_devid,
    char	*re_occupant_mfg,
    char	*re_occupant_model,
    char	*re_occupant_part,
    char	*re_occupant_serial,
    char	*re_occupant_firm,
    char	*re_occupant_misc_1,
    char	*re_occupant_misc_2,
    char	*re_occupant_misc_3)
{
	CRO_REC_F(char *,	r_fre);
	di_cro_reca_t		ra = NULL;
	int			i;

	if (h == NULL)
		return (NULL);

	for (i = 0; i < CRO_REC_FN; i++)
		r_fre[i] = NULL;

	/* Gather RE arguments into array form. */
#define	R_FRE_SET(f)		r_fre[F_##f] = re_##f
	R_FRE_SET(product_id);
	R_FRE_SET(chassis_id);
	R_FRE_SET(alias_id);
	R_FRE_SET(receptacle_name);
	R_FRE_SET(receptacle_type);
	R_FRE_SET(receptacle_fmri);
	R_FRE_SET(occupant_type);
	R_FRE_SET(occupant_instance);
	R_FRE_SET(devchassis_path);
	R_FRE_SET(occupant_devices);
	R_FRE_SET(occupant_paths);
	R_FRE_SET(occupant_compdev);
	R_FRE_SET(occupant_devid);
	R_FRE_SET(occupant_mfg);
	R_FRE_SET(occupant_model);
	R_FRE_SET(occupant_part);
	R_FRE_SET(occupant_serial);
	R_FRE_SET(occupant_firm);
	R_FRE_SET(occupant_misc_1);
	R_FRE_SET(occupant_misc_2);
	R_FRE_SET(occupant_misc_3);

	(void) _cro_rec_walk(h, rec_flag, &r_fre, &ra, _cro_reca_a);
	return (ra);
}

/*
 * To protect consumers from any future addition of fields, we provide a
 * string-based query mechanism as an alternative to di_cro_reca_create.
 * The format of the query string uses JSON-like (json.org) syntax
 *
 *	name : "string" [, name : "string" ]*
 *
 * for example
 *
 *	char *query = "alias-id:\"SYS\",receptacle-type:\"bay\"";
 *
 * A NULL query value will return all records.
 */
static char *di_cro_reca_create_query_err = NULL;
di_cro_reca_t
di_cro_reca_create_query(di_cro_hdl_t h, uint32_t rec_flag, char *query)
{
	CRO_REC_F(char *,	r_fre);
	int			i;
	char			*s0 = NULL;
	char			*s;
	char			*ns, *nse;
	char			*vs, *vse;
	di_cro_reca_t		ra = NULL;

	if (h == NULL)
		return (NULL);

	for (i = 0; i < CRO_REC_FN; i++)
		r_fre[i] = NULL;

	if (query == NULL)
		goto walk;			/* all records */

	if ((s0 = strdup(query)) == NULL)
		return (NULL);

	/* parse: [ \t] ns [ \t] : [ \t] "vs" [ \t] [, <repeat>] */
	for (s = s0; *s; ) {
		ns = s + strspn(s, " \t");	/* skip whitespace */
		nse = ns + strcspn(ns, " \t:");	/* span non-whitespace/sep */

		s = nse + strspn(nse, " \t");	/* skip whitespace */
		if (*s != ':')			/* expect ':' */
			goto err;
		s++;

		s = s + strspn(s, " \t");	/* skip whitespace */
		if (*s != '"')			/* expect quoted string */
			goto err;
		s++;

		for (vs = s; *s; s++) {
			if ((*s == '\\') && *(s + 1))
				s++;		/* skip \" in quoted string */
			else if (*s == '"')
				break;
		}
		if (*s != '"')			/* expect end quoted string */
			goto err;
		vse = s++;			/* end of vs */

		s = s + strspn(s, " \t");	/* skip whitespace */
		if (*s) {			/* expect null or ',' */
			if (*s == ',')
				s++;
			else
				goto err;
		}
		*nse = 0;			/* terminate ns */
		*vse = 0;			/* terminate vs */

		for (i = 0; i < CRO_REC_FN; i++) {
			if (strcmp(cro_rec_fnm[i], ns) == 0) {
				r_fre[i] = vs;
				break;
			}
		}
		/* consider an unsupported field-name an error. */
		if (i >= CRO_REC_FN) {
			s = ns;
			goto err;
		}
	}

walk:	(void) _cro_rec_walk(h, rec_flag, &r_fre, &ra, _cro_reca_a);

done:	if (s0)
		free(s0);
	return (ra);

	/*
	 * As a debug aid, we dup the string at the point we are unable
	 * to parse.
	 */
err:	if (di_cro_reca_create_query_err)
		free(di_cro_reca_create_query_err);
	di_cro_reca_create_query_err = strdup(s);
	goto done;
}

di_cro_rec_t
di_cro_reca_next(di_cro_reca_t ra, di_cro_rec_t r)
{
	int	n;

	if (ra == NULL)
		return (NULL);
	if (r == NULL)
		return (ra->ra_r[0]);		/* start with index 0 record */

	for (n = 0; n < ra->ra_nrec; n++)
		if (ra->ra_r[n] == r)
			break;			/* found last record */
	n++;					/* advance to next */
	if (n < ra->ra_nrec)
		return (ra->ra_r[n]);		/* return next record */
	else
		return (NULL);			/* end with NULL */
}

void
di_cro_reca_destroy(di_cro_reca_t ra)
{
	if (ra == NULL)
		return;

	free(ra);
}

/*
 * libdevinfo cro snapshot interfaces: record get field functions
 *
 * Return indexed field value. If index out of bounds, return dflt.
 * If morep is non-null, set morep to indicate if another call will
 * will retrieve new data.
 */
#define	DI_CRO_REC_FGETI(f)						\
	char *di_cro_rec_fgeti_##f(di_cro_rec_t r,			\
	    int i, int *morep, char *dflt)				\
	{								\
		int	n;						\
		char	*v;						\
									\
		if (morep)						\
			*morep = 0;					\
		if (r == NULL)						\
			return (dflt);					\
		n = r->r_f[F_##f].f_nv;					\
		if (i >= n)						\
			return (dflt);					\
		if (morep && ((i + 1) < n))				\
			*morep = 1;					\
		v = r->r_f[F_##f].f_v[i];				\
		return ((v && *v) ? v : dflt);				\
	}
DI_CRO_REC_FGETI(product_id)
DI_CRO_REC_FGETI(chassis_id)
DI_CRO_REC_FGETI(alias_id)
DI_CRO_REC_FGETI(receptacle_name)
DI_CRO_REC_FGETI(receptacle_type)
DI_CRO_REC_FGETI(receptacle_fmri)
DI_CRO_REC_FGETI(occupant_type)
DI_CRO_REC_FGETI(occupant_instance)
DI_CRO_REC_FGETI(devchassis_path)
DI_CRO_REC_FGETI(occupant_devices)
DI_CRO_REC_FGETI(occupant_paths)
DI_CRO_REC_FGETI(occupant_compdev)
DI_CRO_REC_FGETI(occupant_devid)
DI_CRO_REC_FGETI(occupant_mfg)
DI_CRO_REC_FGETI(occupant_model)
DI_CRO_REC_FGETI(occupant_part)
DI_CRO_REC_FGETI(occupant_serial)
DI_CRO_REC_FGETI(occupant_firm)
DI_CRO_REC_FGETI(occupant_misc_1)
DI_CRO_REC_FGETI(occupant_misc_2)
DI_CRO_REC_FGETI(occupant_misc_3)

void *
di_cro_rec_private_get(di_cro_rec_t r)
{
	if (r == NULL)
		return (NULL);
	return (r->r_priv);
}

void
di_cro_rec_private_set(di_cro_rec_t r, void *priv)
{
	if (r)
		r->r_priv = priv;
}

/*ARGSUSED*/
di_cromk_hdl_t
di_cromk_begin(int flags)
{
	di_cromk_hdl_t	mk = NULL;
	time_t		t;
	char		*s;

	if ((mk = calloc(1, sizeof (*mk))) == NULL)
		goto out;

	/* timestamp is at beginning */
	(void) time(&t);
	s = ctime(&t);
	s[strlen(s) - 1] = '\0';	/* '\n' -> '\0' */
	mk->mk_date = strdup(s);

out:	return (mk);
}

di_cro_rec_t
di_cromk_recadd(di_cromk_hdl_t mk,
    uint32_t	rec_flag,
    char	*product_id,
    char	*chassis_id,
    char	*alias_id,
    char	*receptacle_name,
    char	*receptacle_type,
    char	*receptacle_fmri,
    char	*occupant_type,
    char	*occupant_instance,
    char	*devchassis_path,
    char	**occupant_devices,	int n_occupant_devices,
    char	**occupant_paths,	int n_occupant_paths,
    char	**occupant_compdev,	int n_occupant_compdev,
    char	*occupant_devid,
    char	*occupant_mfg,
    char	*occupant_model,
    char	*occupant_part,
    char	*occupant_serial,
    char	*occupant_firm,
    char	**occupant_misc_1,	int n_occupant_misc_1,
    char	**occupant_misc_2,	int n_occupant_misc_2,
    char	**occupant_misc_3,	int n_occupant_misc_3)
{
	di_cro_rec_t		r;
	int			i, j;
	char			**s;
	CRO_REC_F(rec_f_t,	r_f);
	char			*nothing = "";

	if (mk == NULL)
		return (NULL);

	/* Gather arguments into array form. */
#define	R_F_SET1(f)							\
	r_f[F_##f].f_nv = 1;						\
	r_f[F_##f].f_v = f ? &f : &nothing;
#define	R_F_SETN(f)							\
	if (f && n_##f) {						\
		r_f[F_##f].f_nv = n_##f;				\
		r_f[F_##f].f_v = f;					\
	} else {							\
		r_f[F_##f].f_nv = 1;					\
		r_f[F_##f].f_v = &nothing;				\
	}
	R_F_SET1(product_id);
	R_F_SET1(chassis_id);
	R_F_SET1(alias_id);
	R_F_SET1(receptacle_name);
	R_F_SET1(receptacle_type);
	R_F_SET1(receptacle_fmri);
	R_F_SET1(occupant_type);
	R_F_SET1(occupant_instance);
	R_F_SET1(devchassis_path);
	R_F_SETN(occupant_devices);
	R_F_SETN(occupant_paths);
	R_F_SETN(occupant_compdev);
	R_F_SET1(occupant_devid);
	R_F_SET1(occupant_mfg);
	R_F_SET1(occupant_model);
	R_F_SET1(occupant_part);
	R_F_SET1(occupant_serial);
	R_F_SET1(occupant_firm);
	R_F_SETN(occupant_misc_1);
	R_F_SETN(occupant_misc_2);
	R_F_SETN(occupant_misc_3);

	/* verify that required fields are specified */
	if ((product_id == NULL) || (chassis_id == NULL) ||
	    (receptacle_name == NULL))
		return (NULL);

	if ((r = calloc(1, sizeof (*r))) == NULL)
		return (NULL);

	for (i = 0; i < CRO_REC_FN; i++) {
		r->r_f[i].f_nv = r_f[i].f_nv;
		r->r_f[i].f_v = calloc(r_f[i].f_nv, sizeof (r_f[i].f_v[0]));
		if (r->r_f[i].f_v)
			for (s = r_f[i].f_v, j = 0; j < r_f[i].f_nv; j++, s++)
				r->r_f[i].f_v[j] = *s ? strdup(*s) : strdup("");
	}
	r->r_rec_flag = rec_flag;
	r->r_priv = NULL;
	r->r_next = mk->mk_rec;
	mk->mk_rec = r;
	mk->mk_nrec++;
	return (r);
}

/*ARGSUSED*/
void
di_cromk_end(di_cromk_hdl_t mk, int flags, char *root_server_id,
    char *root_product_id, char *root_chassis_id, uint64_t cna)
{
	di_cro_rec_t	r, rn;

	if (mk == NULL)
		return;

	if (!(flags & DI_CROMK_END_COMMIT))
		goto out;

	mk->mk_server_id = root_server_id ? root_server_id : "UNKNOWN";
	mk->mk_product_id = root_product_id ?
	    di_cro_strclean(root_product_id, 1, 1) : "UNKNOWN";
	mk->mk_chassis_id = root_chassis_id ?
	    di_cro_strclean(root_chassis_id, 1, 1) : "UNKNOWN";
	mk->mk_cna = cna;

	(void) _crodb_write(mk, flags);

out:	for (r = mk->mk_rec; r; r = rn) {
		rn = r->r_next;
		(void) _cro_rec_free(r, NULL);
	}

	if (mk->mk_date)
		free(mk->mk_date);

	free(mk);
}

/*
 * This interface is called as the process responsible for
 * CRO dataset updates is exiting (i.e. fmd(1M)).  Since that
 * process is no longer around to keep things up to date, move
 * the CRO dataset aside (if it exists).
 */
void
di_cromk_cleanup()
{
	struct stat	sbuf;

	/* rotate current db file to old (result: no db file) */
	if (stat(DI_CRO_DB_FILE, &sbuf) == 0)
		(void) rename(DI_CRO_DB_FILE, DI_CRO_DB_FILE_OLD);
}
