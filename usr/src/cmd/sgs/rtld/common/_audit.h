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
 * Copyright (c) 1996, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	__AUDIT_DOT_H
#define	__AUDIT_DOT_H

#ifndef _ASM

#include <sys/types.h>
#include <rtld.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Define all auditing structures.
 *
 * A shared object may be a client of one or more audit libraries.  Calls to an
 * auditor are accompanied with cookies that identify an object being audited.
 * These cookies are initialized to the link-map pointers of the object being
 * audited, however the auditor is free to re-allocate these cookies, and thus
 * associate their own data with each object being audited.
 *
 * With auditing version LAV_VERSION5, local auditors can provide la_preinit()
 * and la_activity() routines, each of which should be passed a cookie that
 * represents the link-map of the head of the associated link-map list.  These
 * cookies are maintained on the associated link-map list, using the lm_cookies
 * alist.  These cookies are created by _audit_add_head(), and dynamically
 * retrieved by the _audit_preinit() and _audit_activity() routines.
 *
 * Having these cookies kept on the link-map list decouples these cookies from
 * the Audit_client structure of the object being locally audited.  In addition,
 * this model ensures that multiple objects, undergoing local auditing from the
 * same auditor, receive the same head link-map cookie.
 */
typedef struct {
	Rt_map		*ac_lmp;	/* audit library identifier */
	uintptr_t	ac_cookie;	/* cookie assigned to audit library */
	Word		ac_flags;	/*    and its associated flags */
} Audit_client;

#define	FLG_AC_BINDTO	0x00001
#define	FLG_AC_BINDFROM	0x00002

/*
 * Each shared object being audited may provide a list of client structures
 * and dynamic plts (one per auditor).
 */
struct audit_info {
	uint_t		ai_cnt;		/* no. of clients */
	Audit_client	*ai_clients;	/* array of client structures */
	void		*ai_dynplts;	/* array of dynamic plts */
};

/*
 * Define an Audit Descriptor - each audit object is added to this descriptor
 * as an Audit Interface.  There is one global audit descriptor - auditors,
 * and a specific object my require its own - AUDITORS(lmp).
 */
struct audit_desc {
	char		*ad_name;	/* originating audit names */
	APlist		*ad_list;	/* audit objs Audit Interface list */
	uint_t		ad_cnt;		/* no. of audit objs in this desc. */
	uint_t		ad_flags;	/* audit capabilities found.  See */
					/*    LML_TFLG_AUD_* flags */
};

/*
 * Define an Audit List descriptor for each audit object.
 */
struct audit_list {
	const char	*al_libname;	/* object name for diagnostics */
	Rt_map		*al_lmp;	/* object link-map */
	Grp_hdl		*al_ghp;	/* object handle */
	uint_t		al_flags;	/* audit capabilities found */
	uint_t		(*al_version)(uint_t);
	void		(*al_preinit)(uintptr_t *);
	char		*(*al_objsearch)(const char *, uintptr_t *, uint_t);
	uint_t		(*al_objopen)(Link_map *, Lmid_t, uintptr_t *);
	int		(*al_objfilter)(uintptr_t *, const char *, uintptr_t *,
				uint_t);
	uint_t		(*al_objclose)(uintptr_t *);
	void		(*al_activity)(uintptr_t *, uint_t);
#if	defined(_ELF64)
	uintptr_t	(*al_pltenter)(Sym *, uint_t, uintptr_t *, uintptr_t *,
				void *, uint_t *, const char *);
	uintptr_t	(*al_pltexit)(Sym *, uint_t, uintptr_t *, uintptr_t *,
				uintptr_t, const char *);
	uintptr_t	(*al_symbind)(Sym *, uint_t, uintptr_t *,
				uintptr_t *, uint_t *, const char *);
#else
	uintptr_t	(*al_pltenter)(Sym *, uint_t, uintptr_t *, uintptr_t *,
				void *, uint_t *);
	uintptr_t	(*al_pltexit)(Sym *, uint_t, uintptr_t *, uintptr_t *,
				uintptr_t);
	uintptr_t	(*al_symbind)(Sym *, uint_t, uintptr_t *,
				uintptr_t *, uint_t *);
#endif /* _ELF64 */
	uint_t		al_vernum;	/* object version */
};

/*
 * Link-Edit audit functions
 */
extern int		audit_setup(Rt_map *, Audit_desc *, uint_t, int *);

extern void		audit_desc_cleanup(Rt_map *);
extern void		audit_info_cleanup(Rt_map *);

extern int		audit_objopen(Rt_map *, Rt_map *);
extern int		audit_objfilter(Rt_map *, const char *, Rt_map *,
			    uint_t flags);
extern void		audit_activity(Rt_map *, uint_t);
extern void		audit_preinit(Rt_map *);
extern char		*audit_objsearch(Rt_map *, const char *, uint_t);
extern void		audit_objclose(Rt_map *, Rt_map *);
extern void		_audit_objclose(APlist *, Rt_map *);
extern Addr		audit_symbind(Rt_map *, Rt_map *, Sym *, uint_t,
			    Addr value, uint_t *);
extern Addr		audit_pltenter(Rt_map *, Rt_map *, Sym *, uint_t,
			    void *, uint_t *);
extern Addr		audit_pltexit(uintptr_t, Rt_map *, Rt_map *, Sym *,
			    uint_t);

extern uint_t		audit_flags;

#endif /* _ASM */

/*
 * Values for audit_flags.  Intended to be the same as the LML equivalents
 * but kept in a separate variable to simplify boot_elf.s coding.
 */
#define	AF_PLTENTER	0x01		/* same as LML_AUD_PLTENTER */
#define	AF_PLTEXIT	0x02		/* Same as LML_AUD_PLTEXIT */

#ifdef	__cplusplus
}
#endif

#endif /* __AUDIT_DOT_H */
