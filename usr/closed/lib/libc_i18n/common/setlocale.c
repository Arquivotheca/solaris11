/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: setlocale.c,v $ $Revision: 1.14.6.4"
 *	" $ (OSF) $Date: 1992/11/21 18:54:54 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: setlocale
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1989
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.12  com/lib/c/loc/setlocale.c, libcloc, bos320, 9132320m 8/11/91 14:14:10
 */

/*
 * New members added in the struct lconv by IEEE Std 1003.1-2001
 * are always activated in the locale object.
 * See <iso/locale_iso.h>.
 */
#ifndef _LCONV_C99
#define	_LCONV_C99
#endif

#pragma weak _setlocale = setlocale

#include "lint.h"
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/localedef.h>
#include "libc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <errno.h>
#include "libc_int.h"
#include "_loc_path.h"

#ifndef	_LastCategory
#define	_LastCategory	LC_MESSAGES
#endif

#define	_LC_SYMBOL	"instantiate"

/*
 * The maximum number of locale shared objects loaded and kept in
 * the locale shared object cache chain excluding C, POSIX, invalid,
 * and I/O stream bound locales.
 */
#define	_LC_MAX_OBJS			(12)

/*
 * The maximum number of characters in a locale shared object suffix including
 * the version number and a null byte, ".so.??".
 */
#define	_LC_MAX_SUFFIX_LEN		(7)

/*
 * Commonly used LC_MESSAGES category name string and its length including
 * a terminating null byte.
 */
#define	_LC_LCMSG			"LC_MESSAGES"
#define	_LC_LCMSG_LEN			(sizeof (_LC_LCMSG))

/*
 * The number of lclist_t components that will be increased whenever
 * there is no more space left in the locale list.
 */
#define	_LC_INC_NUM			(128)

/*
 * Allowed flag values for locale chain.
 *
 * The following eight flag values are used to indicate and find out
 * what categories of names and lconv pointer of lp in a locale component of
 * the locale chain have memory blocks allocated. They also can be used to
 * check against on what locale categories have been actually set in
 * a given composite locale.
 *
 * The _LC_LOC_ORIENTED is to indicate a locale is bound to an I/O stream
 * orientation.
 */
#define	_LC_LOC_LC_CTYPE		(0x0001)
#define	_LC_LOC_LC_NUMERIC		(0x0002)
#define	_LC_LOC_LC_TIME			(0x0004)
#define	_LC_LOC_LC_COLLATE		(0x0008)
#define	_LC_LOC_LC_MONETARY		(0x0010)
#define	_LC_LOC_LC_MESSAGES		(0x0020)
#define	_LC_LOC_LC_ALL			(0x0040)
#define	_LC_LOC_LCONV			(0x0080)

#define	_LC_LOC_ORIENTED		(0x0100)

#define	_LC_LOC_ALLNAMES \
	(_LC_LOC_LC_CTYPE | _LC_LOC_LC_NUMERIC | _LC_LOC_LC_TIME | \
	_LC_LOC_LC_COLLATE | _LC_LOC_LC_MONETARY | _LC_LOC_LC_MESSAGES | \
	_LC_LOC_LC_ALL)

extern _LC_locale_t	*__C_locale;	/* Hardwired default */
extern struct _lc_locale_t	__C_locale_object;

static const char	POSIX[] = "POSIX";
static const char	C[] = "C";

/*
 * The following three types are used to map locale names into alternative
 * locale names for the support of locale aliases specified in PSARC/2009/594.
 */
typedef struct {
	const ushort_t	start;
	const ushort_t	end;
} lc_can_index_t;

typedef struct {
	const char	*alias;
	const char	*canonical;
} lc_name_list_comp_t;

typedef struct {
	const char	*lc_name;
	const ushort_t	start;
	const ushort_t	end;
} lc_obs_msg_index_t;

/*
 * The type of components of the locale shared object chain.
 */
typedef struct _obchain {
	char		*name;
	_LC_locale_t	*lp;
	void		*handle;
	uint_t		ref_count;
	struct _obchain *prev;
	struct _obchain *next;
} obj_chain_t;

/*
 * The type of components of the locale chain.
 */
typedef struct _lcchain {
	char		*names[_LastCategory + 2];	/* LC_* + LC_ALL */
	uint_t		call_count;
	int		flag;
	_LC_locale_t	*lp;
	obj_chain_t	*lc_objs[_LastCategory + 1];
	struct _lcchain	*prev;
	struct _lcchain	*next;
} loc_chain_t;

static loc_chain_t	POSIX_entry = {
	{
		(char *)POSIX, (char *)POSIX, (char *)POSIX,
		(char *)POSIX, (char *)POSIX, (char *)POSIX,
		(char *)POSIX
	},
	UINT_MAX,
	_LC_LOC_ORIENTED,
	(_LC_locale_t *)&__C_locale_object,
	{
		NULL, NULL, NULL, NULL, NULL, NULL
	},
	NULL,
	NULL
};

static loc_chain_t	C_entry = {
	{
		(char *)C, (char *)C, (char *)C,
		(char *)C, (char *)C, (char *)C,
		(char *)C
	},
	UINT_MAX,
	_LC_LOC_ORIENTED,
	(_LC_locale_t *)&__C_locale_object,
	{
		NULL, NULL, NULL, NULL, NULL, NULL
	},
	NULL,
	NULL
};

static loc_chain_t	*chain_head = NULL;
static loc_chain_t	*curr_locale = &C_entry;

static obj_chain_t	*obj_chain_head = NULL;
static size_t		obj_counter = 0;

static _LC_locale_t	*load_locale(const char *, obj_chain_t **);
static _LC_locale_t	*check_msg(char *);
static _LC_locale_t	*load_composite_locale(char **, _LC_locale_t *,
    loc_chain_t *);
static char		*expand_locale_name(const char *, char **, int *);
static char		*locale_per_category(const char *, int);
static char		*create_composite_locale(char *[]);
static loc_chain_t	*cache_check(char *);
static loc_chain_t	*alloc_chain(_LC_locale_t *);
static void		informrtld(char *);
static size_t		get_locale_dir_n_suffix(char *, char *, size_t *);
static void		evict_locales();

/*
 * The category_name array is used in build_path.
 * The orderring of these names must correspond to the order
 * of the LC_* categories in locale.h.
 * i.e., category_name[LC_CTYPE] = "LC_CTYPE".
 */
static const char	*category_name[] = {
	"LC_CTYPE",
	"LC_NUMERIC",
	"LC_TIME",
	"LC_COLLATE",
	"LC_MONETARY",
	"LC_MESSAGES",
};

/*
 * The following is to indicate which category has a memory block allocated
 * for a locale name of the category. The index values correspond to LC_*
 * category values defined in <locale.h>.
 */
static const int	names_allocated[] = {
	_LC_LOC_LC_CTYPE,
	_LC_LOC_LC_NUMERIC,
	_LC_LOC_LC_TIME,
	_LC_LOC_LC_COLLATE,
	_LC_LOC_LC_MONETARY,
	_LC_LOC_LC_MESSAGES,
	_LC_LOC_LC_ALL,
};

/*
 * We use the following as the min and max lengths of supported locale
 * name aliases (including terminating null byte for the max length).
 * If there is any new locale name alias that is shorter than 2 or longer
 * than 32, the following should be decreased or increased as needed to
 * accomodate such lengths.
 */
#define	_LC_MIN_LOCALE_NAME_LENGTH	(2)
#define	_LC_MAX_LOCALE_NAME_LENGTH	(32)

/*
 * The following two tables are used in mapping a locale aliase into
 * a canonical locale name.
 *
 * The __lc_can_index_list[] maps to the start and end indices of the search
 * domain based on the first letter of an alias name. The USHRT_MAX means
 * there is no mapping for locale name aliases starting with such character.
 * The __lc_can_lc_list[] is the actual table that maps normalized locale
 * aliases into corresponding canonical locale names.
 *
 * The two tables are formulated from the first list of locale_alias(5) man
 * page. The man page also describes on how the locale aliases are normalized.
 */
static const lc_can_index_t __lc_can_index_list[] = {
	{ 0, 33 },			/* 'A' - 34 locales */
	{ 34, 39 },			/* 'B' - 6 locales */
	{ 40, 47 },			/* 'C' - 8 locales */
	{ 48, 63 },			/* 'D' - 16 locales */
	{ 64, 139 },			/* 'E' - 76 locales */
	{ 140, 159 },			/* 'F' - 20 locales */
	{ 160, 161 },			/* 'G' - 2 locales */
	{ 162, 169 },			/* 'H' - 8 locales */
	{ 170, 179 },			/* 'I' - 10 locales */
	{ 180, 181 },			/* 'J' - 2 locales */
	{ 182, 187 },			/* 'K' - 6 locales */
	{ 188, 191 },			/* 'L' - 4 locales */
	{ 192, 201 },			/* 'M' - 10 locales */
	{ 202, 211 },			/* 'N' - 10 locales */
	{ 212, 213 },			/* 'O' - 2 locales */
	{ 214, 223 },			/* 'P' - 10 locales */
	{ USHRT_MAX, USHRT_MAX },	/* 'Q' - none */
	{ 224, 227 },			/* 'R' - 4 locales */
	{ 228, 243 },			/* 'S' - 16 locales */
	{ 244, 251 },			/* 'T' - 8 locales */
	{ 252, 257 },			/* 'U' - 6 locales */
	{ 258, 259 },			/* 'V' - 2 locales */
	{ USHRT_MAX, USHRT_MAX },	/* 'W' - none */
	{ USHRT_MAX, USHRT_MAX },	/* 'X' - none */
	{ USHRT_MAX, USHRT_MAX },	/* 'Y' - none */
	{ 260, 271 },			/* 'Z' - 12 locales */
	{ USHRT_MAX, USHRT_MAX },	/* '[' - none */
	{ USHRT_MAX, USHRT_MAX },	/* '\' - none */
	{ USHRT_MAX, USHRT_MAX },	/* ']' - none */
	{ USHRT_MAX, USHRT_MAX },	/* '^' - none */
	{ USHRT_MAX, USHRT_MAX },	/* '_' - none */
	{ USHRT_MAX, USHRT_MAX },	/* '`' - none */
	{ 272, 343 },			/* 'a' - 72 locales */
	{ 344, 361 },			/* 'b' - 18 locales */
	{ 362, 386 },			/* 'c' - 25 locales */
	{ 387, 431 },			/* 'd' - 45 locales */
	{ 432, 603 },			/* 'e' - 172 locales */
	{ 604, 656 },			/* 'f' - 53 locales */
	{ 657, 674 },			/* 'g' - 18 locales */
	{ 675, 689 },			/* 'h' - 15 locales */
	{ 690, 716 },			/* 'i' - 27 locales */
	{ 717, 721 },			/* 'j' - 5 locales */
	{ 722, 744 },			/* 'k' - 23 locales */
	{ 745, 755 },			/* 'l' - 11 locales */
	{ 756, 777 },			/* 'm' - 22 locales */
	{ 778, 813 },			/* 'n' - 36 locales */
	{ 814, 823 },			/* 'o' - 10 locales */
	{ 824, 846 },			/* 'p' - 23 locales */
	{ USHRT_MAX, USHRT_MAX },	/* 'q' - none */
	{ 847, 861 },			/* 'r' - 15 locales */
	{ 862, 921 },			/* 's' - 60 locales */
	{ 922, 952 },			/* 't' - 31 locales */
	{ 953, 959 },			/* 'u' - 7 locales */
	{ 960, 964 },			/* 'v' - 5 locales */
	{ 965, 969 },			/* 'w' - 5 locales */
	{ 970, 972 },			/* 'x' - 3 locales */
	{ 973, 975 },			/* 'y' - 3 locales */
	{ 976, 999 }			/* 'z' - 24 locales */
};

static const lc_name_list_comp_t __lc_can_lc_list[] = {
	{ "AR_AA", "ar_AA.UTF-8" },
	{ "AR_AAutf8", "ar_AA.UTF-8" },
	{ "AR_AE", "ar_AE.UTF-8" },
	{ "AR_AEutf8", "ar_AE.UTF-8" },
	{ "AR_BH", "ar_BH.UTF-8" },
	{ "AR_BHutf8", "ar_BH.UTF-8" },
	{ "AR_DZ", "ar_DZ.UTF-8" },
	{ "AR_DZutf8", "ar_DZ.UTF-8" },
	{ "AR_EG", "ar_EG.UTF-8" },
	{ "AR_EGutf8", "ar_EG.UTF-8" },
	{ "AR_JO", "ar_JO.UTF-8" },
	{ "AR_JOutf8", "ar_JO.UTF-8" },
	{ "AR_KW", "ar_KW.UTF-8" },
	{ "AR_KWutf8", "ar_KW.UTF-8" },
	{ "AR_LB", "ar_LB.UTF-8" },
	{ "AR_LButf8", "ar_LB.UTF-8" },
	{ "AR_MA", "ar_MA.UTF-8" },
	{ "AR_MAutf8", "ar_MA.UTF-8" },
	{ "AR_OM", "ar_OM.UTF-8" },
	{ "AR_OMutf8", "ar_OM.UTF-8" },
	{ "AR_QA", "ar_QA.UTF-8" },
	{ "AR_QAutf8", "ar_QA.UTF-8" },
	{ "AR_SA", "ar_SA.UTF-8" },
	{ "AR_SAutf8", "ar_SA.UTF-8" },
	{ "AR_SY", "ar_SY.UTF-8" },
	{ "AR_SYutf8", "ar_SY.UTF-8" },
	{ "AR_TN", "ar_TN.UTF-8" },
	{ "AR_TNutf8", "ar_TN.UTF-8" },
	{ "AR_YE", "ar_YE.UTF-8" },
	{ "AR_YEutf8", "ar_YE.UTF-8" },
	{ "AS_IN", "as_IN.UTF-8" },
	{ "AS_INutf8", "as_IN.UTF-8" },
	{ "AZ_AZ", "az_AZ.UTF-8" },
	{ "AZ_AZutf8", "az_AZ.UTF-8" },
	{ "BE_BY", "be_BY.UTF-8" },
	{ "BE_BYutf8", "be_BY.UTF-8" },
	{ "BG_BG", "bg_BG.UTF-8" },
	{ "BG_BGutf8", "bg_BG.UTF-8" },
	{ "BN_IN", "bn_IN.UTF-8" },
	{ "BN_INutf8", "bn_IN.UTF-8" },
	{ "CA_ES", "ca_ES.UTF-8" },
	{ "CA_ESeuro", "ca_ES.UTF-8" },
	{ "CA_ESutf8", "ca_ES.UTF-8" },
	{ "CA_ESutf8euro", "ca_ES.UTF-8" },
	{ "CS_CZ", "cs_CZ.UTF-8" },
	{ "CS_CZutf8", "cs_CZ.UTF-8" },
	{ "CY_GB", "cy_GB.UTF-8" },
	{ "CY_GButf8", "cy_GB.UTF-8" },
	{ "DA_DK", "da_DK.UTF-8" },
	{ "DA_DKutf8", "da_DK.UTF-8" },
	{ "DE_AT", "de_AT.UTF-8" },
	{ "DE_ATeuro", "de_AT.UTF-8" },
	{ "DE_ATutf8", "de_AT.UTF-8" },
	{ "DE_ATutf8euro", "de_AT.UTF-8" },
	{ "DE_CH", "de_CH.UTF-8" },
	{ "DE_CHutf8", "de_CH.UTF-8" },
	{ "DE_DE", "de_DE.UTF-8" },
	{ "DE_DEeuro", "de_DE.UTF-8" },
	{ "DE_DEutf8", "de_DE.UTF-8" },
	{ "DE_DEutf8euro", "de_DE.UTF-8" },
	{ "DE_LU", "de_LU.UTF-8" },
	{ "DE_LUeuro", "de_LU.UTF-8" },
	{ "DE_LUutf8", "de_LU.UTF-8" },
	{ "DE_LUutf8euro", "de_LU.UTF-8" },
	{ "EL_GR", "el_GR.UTF-8" },
	{ "EL_GRutf8", "el_GR.UTF-8" },
	{ "EN_AU", "en_AU.UTF-8" },
	{ "EN_AUutf8", "en_AU.UTF-8" },
	{ "EN_BE", "en_BE.UTF-8" },
	{ "EN_BEeuro", "en_BE.UTF-8" },
	{ "EN_BEutf8", "en_BE.UTF-8" },
	{ "EN_BEutf8euro", "en_BE.UTF-8" },
	{ "EN_CA", "en_CA.UTF-8" },
	{ "EN_CAutf8", "en_CA.UTF-8" },
	{ "EN_GB", "en_GB.UTF-8" },
	{ "EN_GBeuro", "en_GB.UTF-8" },
	{ "EN_GButf8", "en_GB.UTF-8" },
	{ "EN_GButf8euro", "en_GB.UTF-8" },
	{ "EN_HK", "en_HK.UTF-8" },
	{ "EN_HKutf8", "en_HK.UTF-8" },
	{ "EN_IE", "en_IE.UTF-8" },
	{ "EN_IEeuro", "en_IE.UTF-8" },
	{ "EN_IEutf8", "en_IE.UTF-8" },
	{ "EN_IEutf8euro", "en_IE.UTF-8" },
	{ "EN_IN", "en_IN.UTF-8" },
	{ "EN_INutf8", "en_IN.UTF-8" },
	{ "EN_NZ", "en_NZ.UTF-8" },
	{ "EN_NZutf8", "en_NZ.UTF-8" },
	{ "EN_PH", "en_PH.UTF-8" },
	{ "EN_PHutf8", "en_PH.UTF-8" },
	{ "EN_SG", "en_SG.UTF-8" },
	{ "EN_SGutf8", "en_SG.UTF-8" },
	{ "EN_US", "en_US.UTF-8" },
	{ "EN_USutf8", "en_US.UTF-8" },
	{ "EN_ZA", "en_ZA.UTF-8" },
	{ "EN_ZAutf8", "en_ZA.UTF-8" },
	{ "ES_AR", "es_AR.UTF-8" },
	{ "ES_ARutf8", "es_AR.UTF-8" },
	{ "ES_BO", "es_BO.UTF-8" },
	{ "ES_BOutf8", "es_BO.UTF-8" },
	{ "ES_CL", "es_CL.UTF-8" },
	{ "ES_CLutf8", "es_CL.UTF-8" },
	{ "ES_CO", "es_CO.UTF-8" },
	{ "ES_COutf8", "es_CO.UTF-8" },
	{ "ES_CR", "es_CR.UTF-8" },
	{ "ES_CRutf8", "es_CR.UTF-8" },
	{ "ES_DO", "es_DO.UTF-8" },
	{ "ES_DOutf8", "es_DO.UTF-8" },
	{ "ES_EC", "es_EC.UTF-8" },
	{ "ES_ECutf8", "es_EC.UTF-8" },
	{ "ES_ES", "es_ES.UTF-8" },
	{ "ES_ESeuro", "es_ES.UTF-8" },
	{ "ES_ESutf8", "es_ES.UTF-8" },
	{ "ES_ESutf8euro", "es_ES.UTF-8" },
	{ "ES_GT", "es_GT.UTF-8" },
	{ "ES_GTutf8", "es_GT.UTF-8" },
	{ "ES_HN", "es_HN.UTF-8" },
	{ "ES_HNutf8", "es_HN.UTF-8" },
	{ "ES_MX", "es_MX.UTF-8" },
	{ "ES_MXutf8", "es_MX.UTF-8" },
	{ "ES_NI", "es_NI.UTF-8" },
	{ "ES_NIutf8", "es_NI.UTF-8" },
	{ "ES_PA", "es_PA.UTF-8" },
	{ "ES_PAutf8", "es_PA.UTF-8" },
	{ "ES_PE", "es_PE.UTF-8" },
	{ "ES_PEutf8", "es_PE.UTF-8" },
	{ "ES_PR", "es_PR.UTF-8" },
	{ "ES_PRutf8", "es_PR.UTF-8" },
	{ "ES_PY", "es_PY.UTF-8" },
	{ "ES_PYutf8", "es_PY.UTF-8" },
	{ "ES_SV", "es_SV.UTF-8" },
	{ "ES_SVutf8", "es_SV.UTF-8" },
	{ "ES_US", "es_US.UTF-8" },
	{ "ES_USutf8", "es_US.UTF-8" },
	{ "ES_UY", "es_UY.UTF-8" },
	{ "ES_UYutf8", "es_UY.UTF-8" },
	{ "ES_VE", "es_VE.UTF-8" },
	{ "ES_VEutf8", "es_VE.UTF-8" },
	{ "ET_EE", "et_EE.UTF-8" },
	{ "ET_EEutf8", "et_EE.UTF-8" },
	{ "FI_FI", "fi_FI.UTF-8" },
	{ "FI_FIeuro", "fi_FI.UTF-8" },
	{ "FI_FIutf8", "fi_FI.UTF-8" },
	{ "FI_FIutf8euro", "fi_FI.UTF-8" },
	{ "FR_BE", "fr_BE.UTF-8" },
	{ "FR_BEeuro", "fr_BE.UTF-8" },
	{ "FR_BEutf8", "fr_BE.UTF-8" },
	{ "FR_BEutf8euro", "fr_BE.UTF-8" },
	{ "FR_CA", "fr_CA.UTF-8" },
	{ "FR_CAutf8", "fr_CA.UTF-8" },
	{ "FR_CH", "fr_CH.UTF-8" },
	{ "FR_CHutf8", "fr_CH.UTF-8" },
	{ "FR_FR", "fr_FR.UTF-8" },
	{ "FR_FReuro", "fr_FR.UTF-8" },
	{ "FR_FRutf8", "fr_FR.UTF-8" },
	{ "FR_FRutf8euro", "fr_FR.UTF-8" },
	{ "FR_LU", "fr_LU.UTF-8" },
	{ "FR_LUeuro", "fr_LU.UTF-8" },
	{ "FR_LUutf8", "fr_LU.UTF-8" },
	{ "FR_LUutf8euro", "fr_LU.UTF-8" },
	{ "GU_IN", "gu_IN.UTF-8" },
	{ "GU_INutf8", "gu_IN.UTF-8" },
	{ "HE_IL", "he_IL.UTF-8" },
	{ "HE_ILutf8", "he_IL.UTF-8" },
	{ "HI_IN", "hi_IN.UTF-8" },
	{ "HI_INutf8", "hi_IN.UTF-8" },
	{ "HR_HR", "hr_HR.UTF-8" },
	{ "HR_HRutf8", "hr_HR.UTF-8" },
	{ "HU_HU", "hu_HU.UTF-8" },
	{ "HU_HUutf8", "hu_HU.UTF-8" },
	{ "ID_ID", "id_ID.UTF-8" },
	{ "ID_IDutf8", "id_ID.UTF-8" },
	{ "IS_IS", "is_IS.UTF-8" },
	{ "IS_ISutf8", "is_IS.UTF-8" },
	{ "IT_CH", "it_CH.UTF-8" },
	{ "IT_CHutf8", "it_CH.UTF-8" },
	{ "IT_IT", "it_IT.UTF-8" },
	{ "IT_ITeuro", "it_IT.UTF-8" },
	{ "IT_ITutf8", "it_IT.UTF-8" },
	{ "IT_ITutf8euro", "it_IT.UTF-8" },
	{ "JA_JP", "ja_JP.UTF-8" },
	{ "JA_JPutf8", "ja_JP.UTF-8" },
	{ "KK_KZ", "kk_KZ.UTF-8" },
	{ "KK_KZutf8", "kk_KZ.UTF-8" },
	{ "KN_IN", "kn_IN.UTF-8" },
	{ "KN_INutf8", "kn_IN.UTF-8" },
	{ "KO_KR", "ko_KR.UTF-8" },
	{ "KO_KRutf8", "ko_KR.UTF-8" },
	{ "LT_LT", "lt_LT.UTF-8" },
	{ "LT_LTutf8", "lt_LT.UTF-8" },
	{ "LV_LV", "lv_LV.UTF-8" },
	{ "LV_LVutf8", "lv_LV.UTF-8" },
	{ "MK_MK", "mk_MK.UTF-8" },
	{ "MK_MKutf8", "mk_MK.UTF-8" },
	{ "ML_IN", "ml_IN.UTF-8" },
	{ "ML_INutf8", "ml_IN.UTF-8" },
	{ "MR_IN", "mr_IN.UTF-8" },
	{ "MR_INutf8", "mr_IN.UTF-8" },
	{ "MS_MY", "ms_MY.UTF-8" },
	{ "MS_MYutf8", "ms_MY.UTF-8" },
	{ "MT_MT", "mt_MT.UTF-8" },
	{ "MT_MTutf8", "mt_MT.UTF-8" },
	{ "NL_BE", "nl_BE.UTF-8" },
	{ "NL_BEeuro", "nl_BE.UTF-8" },
	{ "NL_BEutf8", "nl_BE.UTF-8" },
	{ "NL_BEutf8euro", "nl_BE.UTF-8" },
	{ "NL_NL", "nl_NL.UTF-8" },
	{ "NL_NLeuro", "nl_NL.UTF-8" },
	{ "NL_NLutf8", "nl_NL.UTF-8" },
	{ "NL_NLutf8euro", "nl_NL.UTF-8" },
	{ "NO_NO", "no_NO.UTF-8" },
	{ "NO_NOutf8", "no_NO.UTF-8" },
	{ "OR_IN", "or_IN.UTF-8" },
	{ "OR_INutf8", "or_IN.UTF-8" },
	{ "PA_IN", "pa_IN.UTF-8" },
	{ "PA_INutf8", "pa_IN.UTF-8" },
	{ "PL_PL", "pl_PL.UTF-8" },
	{ "PL_PLutf8", "pl_PL.UTF-8" },
	{ "PT_BR", "pt_BR.UTF-8" },
	{ "PT_BRutf8", "pt_BR.UTF-8" },
	{ "PT_PT", "pt_PT.UTF-8" },
	{ "PT_PTeuro", "pt_PT.UTF-8" },
	{ "PT_PTutf8", "pt_PT.UTF-8" },
	{ "PT_PTutf8euro", "pt_PT.UTF-8" },
	{ "RO_RO", "ro_RO.UTF-8" },
	{ "RO_ROutf8", "ro_RO.UTF-8" },
	{ "RU_RU", "ru_RU.UTF-8" },
	{ "RU_RUutf8", "ru_RU.UTF-8" },
	{ "SH_SP", "sh_SP.UTF-8" },
	{ "SH_SPutf8", "sh_SP.UTF-8" },
	{ "SH_YU", "sh_YU.UTF-8" },
	{ "SH_YUutf8", "sh_YU.UTF-8" },
	{ "SK_SK", "sk_SK.UTF-8" },
	{ "SK_SKutf8", "sk_SK.UTF-8" },
	{ "SL_SI", "sl_SI.UTF-8" },
	{ "SL_SIutf8", "sl_SI.UTF-8" },
	{ "SQ_AL", "sq_AL.UTF-8" },
	{ "SQ_ALutf8", "sq_AL.UTF-8" },
	{ "SR_SP", "sr_SP.UTF-8" },
	{ "SR_SPutf8", "sr_SP.UTF-8" },
	{ "SR_YU", "sr_YU.UTF-8" },
	{ "SR_YUutf8", "sr_YU.UTF-8" },
	{ "SV_SE", "sv_SE.UTF-8" },
	{ "SV_SEutf8", "sv_SE.UTF-8" },
	{ "TA_IN", "ta_IN.UTF-8" },
	{ "TA_INutf8", "ta_IN.UTF-8" },
	{ "TE_IN", "te_IN.UTF-8" },
	{ "TE_INutf8", "te_IN.UTF-8" },
	{ "TH_TH", "th_TH.UTF-8" },
	{ "TH_THutf8", "th_TH.UTF-8" },
	{ "TR_TR", "tr_TR.UTF-8" },
	{ "TR_TRutf8", "tr_TR.UTF-8" },
	{ "UK_UA", "uk_UA.UTF-8" },
	{ "UK_UAutf8", "uk_UA.UTF-8" },
	{ "UR_IN", "ur_IN.UTF-8" },
	{ "UR_INutf8", "ur_IN.UTF-8" },
	{ "UR_PK", "ur_PK.UTF-8" },
	{ "UR_PKutf8", "ur_PK.UTF-8" },
	{ "VI_VN", "vi_VN.UTF-8" },
	{ "VI_VNutf8", "vi_VN.UTF-8" },
	{ "ZH_CN", "zh_CN.UTF-8" },
	{ "ZH_CNutf8", "zh_CN.UTF-8" },
	{ "ZH_HK", "zh_HK.UTF-8" },
	{ "ZH_HKutf8", "zh_HK.UTF-8" },
	{ "ZH_SG", "zh_SG.UTF-8" },
	{ "ZH_SGutf8", "zh_SG.UTF-8" },
	{ "ZH_TW", "zh_TW.UTF-8" },
	{ "ZH_TWutf8", "zh_TW.UTF-8" },
	{ "Zh_CN", "zh_CN.GB18030" },
	{ "Zh_CNgb18030", "zh_CN.GB18030" },
	{ "Zh_TW", "zh_TW.BIG5" },
	{ "Zh_TWbig5", "zh_TW.BIG5" },
	{ "aa_DJ", "aa_DJ.ISO8859-1" },
	{ "aa_DJiso88591", "aa_DJ.ISO8859-1" },
	{ "aa_DJutf8", "aa_DJ.UTF-8" },
	{ "aa_ER", "aa_ER.UTF-8" },
	{ "aa_ERutf8", "aa_ER.UTF-8" },
	{ "aa_ET", "aa_ET.UTF-8" },
	{ "aa_ETutf8", "aa_ET.UTF-8" },
	{ "af_ZA", "af_ZA.ISO8859-1" },
	{ "af_ZAiso88591", "af_ZA.ISO8859-1" },
	{ "af_ZAutf8", "af_ZA.UTF-8" },
	{ "am_ET", "am_ET.UTF-8" },
	{ "am_ETutf8", "am_ET.UTF-8" },
	{ "an_ES", "an_ES.ISO8859-15" },
	{ "an_ESiso885915", "an_ES.ISO8859-15" },
	{ "an_ESutf8", "an_ES.UTF-8" },
	{ "ar", "ar_EG.ISO8859-6" },
	{ "ar_AA", "ar_AA.ISO8859-6" },
	{ "ar_AE", "ar_AE.ISO8859-6" },
	{ "ar_AEiso88596", "ar_AE.ISO8859-6" },
	{ "ar_AEutf8", "ar_AE.UTF-8" },
	{ "ar_BH", "ar_BH.ISO8859-6" },
	{ "ar_BHiso88596", "ar_BH.ISO8859-6" },
	{ "ar_BHutf8", "ar_BH.UTF-8" },
	{ "ar_DZ", "ar_DZ.ISO8859-6" },
	{ "ar_DZiso88596", "ar_DZ.ISO8859-6" },
	{ "ar_DZutf8", "ar_DZ.UTF-8" },
	{ "ar_EG", "ar_EG.ISO8859-6" },
	{ "ar_EGiso88596", "ar_EG.ISO8859-6" },
	{ "ar_EGutf8", "ar_EG.UTF-8" },
	{ "ar_IN", "ar_IN.UTF-8" },
	{ "ar_INutf8", "ar_IN.UTF-8" },
	{ "ar_IQ", "ar_IQ.ISO8859-6" },
	{ "ar_IQiso88596", "ar_IQ.ISO8859-6" },
	{ "ar_IQutf8", "ar_IQ.UTF-8" },
	{ "ar_JO", "ar_JO.ISO8859-6" },
	{ "ar_JOiso88596", "ar_JO.ISO8859-6" },
	{ "ar_JOutf8", "ar_JO.UTF-8" },
	{ "ar_KW", "ar_KW.ISO8859-6" },
	{ "ar_KWiso88596", "ar_KW.ISO8859-6" },
	{ "ar_KWutf8", "ar_KW.UTF-8" },
	{ "ar_LB", "ar_LB.ISO8859-6" },
	{ "ar_LBiso88596", "ar_LB.ISO8859-6" },
	{ "ar_LButf8", "ar_LB.UTF-8" },
	{ "ar_LY", "ar_LY.ISO8859-6" },
	{ "ar_LYiso88596", "ar_LY.ISO8859-6" },
	{ "ar_LYutf8", "ar_LY.UTF-8" },
	{ "ar_MA", "ar_MA.ISO8859-6" },
	{ "ar_MAiso88596", "ar_MA.ISO8859-6" },
	{ "ar_MAutf8", "ar_MA.UTF-8" },
	{ "ar_OM", "ar_OM.ISO8859-6" },
	{ "ar_OMiso88596", "ar_OM.ISO8859-6" },
	{ "ar_OMutf8", "ar_OM.UTF-8" },
	{ "ar_QA", "ar_QA.ISO8859-6" },
	{ "ar_QAiso88596", "ar_QA.ISO8859-6" },
	{ "ar_QAutf8", "ar_QA.UTF-8" },
	{ "ar_SA", "ar_SA.ISO8859-6" },
	{ "ar_SAiso88596", "ar_SA.ISO8859-6" },
	{ "ar_SAutf8", "ar_SA.UTF-8" },
	{ "ar_SD", "ar_SD.ISO8859-6" },
	{ "ar_SDiso88596", "ar_SD.ISO8859-6" },
	{ "ar_SDutf8", "ar_SD.UTF-8" },
	{ "ar_SY", "ar_SY.ISO8859-6" },
	{ "ar_SYiso88596", "ar_SY.ISO8859-6" },
	{ "ar_SYutf8", "ar_SY.UTF-8" },
	{ "ar_TN", "ar_TN.ISO8859-6" },
	{ "ar_TNiso88596", "ar_TN.ISO8859-6" },
	{ "ar_TNutf8", "ar_TN.UTF-8" },
	{ "ar_YE", "ar_YE.ISO8859-6" },
	{ "ar_YEiso88596", "ar_YE.ISO8859-6" },
	{ "ar_YEutf8", "ar_YE.UTF-8" },
	{ "as_INutf8", "as_IN.UTF-8" },
	{ "az_AZutf8", "az_AZ.UTF-8" },
	{ "be_BY", "be_BY.ISO8859-5" },
	{ "be_BYcp1251", "be_BY.ANSI1251" },
	{ "be_BYutf8", "be_BY.UTF-8" },
	{ "bg_BG", "bg_BG.ISO8859-5" },
	{ "bg_BGcp1251", "bg_BG.ANSI1251" },
	{ "bg_BGutf8", "bg_BG.UTF-8" },
	{ "bn_BD", "bn_BD.UTF-8" },
	{ "bn_BDutf8", "bn_BD.UTF-8" },
	{ "bn_IN", "bn_IN.UTF-8" },
	{ "bn_INutf8", "bn_IN.UTF-8" },
	{ "br_FR", "br_FR.ISO8859-1" },
	{ "br_FReuro", "br_FR.ISO8859-15" },
	{ "br_FRiso88591", "br_FR.ISO8859-1" },
	{ "br_FRiso885915euro", "br_FR.ISO8859-15" },
	{ "br_FRutf8", "br_FR.UTF-8" },
	{ "bs_BA", "bs_BA.ISO8859-2" },
	{ "bs_BAiso88592", "bs_BA.ISO8859-2" },
	{ "bs_BAutf8", "bs_BA.UTF-8" },
	{ "ca", "ca_ES.ISO8859-1" },
	{ "ca_AD", "ca_AD.ISO8859-15" },
	{ "ca_ADiso885915", "ca_AD.ISO8859-15" },
	{ "ca_ADutf8", "ca_AD.UTF-8" },
	{ "ca_ES", "ca_ES.ISO8859-1" },
	{ "ca_ES885915", "ca_ES.ISO8859-15" },
	{ "ca_ES885915euro", "ca_ES.ISO8859-15" },
	{ "ca_ESeuro", "ca_ES.ISO8859-15" },
	{ "ca_ESibm1252", "ca_ES.ANSI1252" },
	{ "ca_ESiso88591", "ca_ES.ISO8859-1" },
	{ "ca_ESiso885915euro", "ca_ES.ISO8859-15" },
	{ "ca_ESutf8", "ca_ES.UTF-8" },
	{ "ca_FR", "ca_FR.ISO8859-15" },
	{ "ca_FRiso885915", "ca_FR.ISO8859-15" },
	{ "ca_FRutf8", "ca_FR.UTF-8" },
	{ "ca_IT", "ca_IT.ISO8859-15" },
	{ "ca_ITiso885915", "ca_IT.ISO8859-15" },
	{ "ca_ITutf8", "ca_IT.UTF-8" },
	{ "cs", "cs_CZ.ISO8859-2" },
	{ "cs_CZ", "cs_CZ.ISO8859-2" },
	{ "cs_CZiso88592", "cs_CZ.ISO8859-2" },
	{ "cs_CZutf8", "cs_CZ.UTF-8" },
	{ "cy_GB", "cy_GB.ISO8859-14" },
	{ "cy_GBiso885914", "cy_GB.ISO8859-14" },
	{ "cy_GButf8", "cy_GB.UTF-8" },
	{ "da", "da_DK.ISO8859-1" },
	{ "da_DK", "da_DK.ISO8859-1" },
	{ "da_DK885915", "da_DK.ISO8859-15" },
	{ "da_DKiso88591", "da_DK.ISO8859-1" },
	{ "da_DKiso885915", "da_DK.ISO8859-15" },
	{ "da_DKiso885915euro", "da_DK.ISO8859-15" },
	{ "da_DKutf8", "da_DK.UTF-8" },
	{ "daiso885915", "da_DK.ISO8859-15" },
	{ "de", "de_DE.ISO8859-1" },
	{ "de_AT", "de_AT.ISO8859-1" },
	{ "de_AT885915", "de_AT.ISO8859-15" },
	{ "de_AT885915euro", "de_AT.ISO8859-15" },
	{ "de_ATeuro", "de_AT.ISO8859-15" },
	{ "de_ATiso88591", "de_AT.ISO8859-1" },
	{ "de_ATiso885915euro", "de_AT.ISO8859-15" },
	{ "de_ATutf8", "de_AT.UTF-8" },
	{ "de_BE", "de_BE.ISO8859-1" },
	{ "de_BEeuro", "de_BE.ISO8859-15" },
	{ "de_BEiso88591", "de_BE.ISO8859-1" },
	{ "de_BEiso885915euro", "de_BE.ISO8859-15" },
	{ "de_BEutf8", "de_BE.UTF-8" },
	{ "de_CH", "de_CH.ISO8859-1" },
	{ "de_CH885915", "de_CH.ISO8859-15" },
	{ "de_CHiso88591", "de_CH.ISO8859-1" },
	{ "de_CHutf8", "de_CH.UTF-8" },
	{ "de_DE", "de_DE.ISO8859-1" },
	{ "de_DE885915", "de_DE.ISO8859-15" },
	{ "de_DE885915euro", "de_DE.ISO8859-15" },
	{ "de_DEeuro", "de_DE.ISO8859-15" },
	{ "de_DEibm1252", "de_DE.ANSI1252" },
	{ "de_DEiso88591", "de_DE.ISO8859-1" },
	{ "de_DEiso885915euro", "de_DE.ISO8859-15" },
	{ "de_DEutf8", "de_DE.UTF-8" },
	{ "de_DEutf8euro", "de_DE.UTF-8" },
	{ "de_LU", "de_LU.ISO8859-15" },
	{ "de_LU885915", "de_LU.ISO8859-15" },
	{ "de_LU885915euro", "de_LU.ISO8859-15" },
	{ "de_LUeuro", "de_LU.ISO8859-15" },
	{ "de_LUiso88591", "de_LU.ISO8859-1" },
	{ "de_LUiso885915euro", "de_LU.ISO8859-15" },
	{ "de_LUutf8", "de_LU.UTF-8" },
	{ "deiso885915", "de_DE.ISO8859-15" },
	{ "deutf8", "de_DE.UTF-8" },
	{ "dz_BT", "dz_BT.UTF-8" },
	{ "dz_BTutf8", "dz_BT.UTF-8" },
	{ "el", "el_GR.ISO8859-7" },
	{ "el_CY", "el_CY.ISO8859-7" },
	{ "el_CYiso88597", "el_CY.ISO8859-7" },
	{ "el_CYutf8", "el_CY.UTF-8" },
	{ "el_GR", "el_GR.ISO8859-7" },
	{ "el_GRiso88597", "el_GR.ISO8859-7" },
	{ "el_GRiso88597euro", "el_GR.ISO8859-7" },
	{ "el_GRutf8", "el_GR.UTF-8" },
	{ "elsuneugreek", "el_GR.ISO8859-7" },
	{ "elutf8", "el_CY.UTF-8" },
	{ "en_AU", "en_AU.ISO8859-1" },
	{ "en_AU885915", "en_AU.ISO8859-15" },
	{ "en_AUiso88591", "en_AU.ISO8859-1" },
	{ "en_AUutf8", "en_AU.UTF-8" },
	{ "en_BE", "en_BE.ISO8859-15" },
	{ "en_BE885915", "en_BE.ISO8859-15" },
	{ "en_BE885915euro", "en_BE.ISO8859-15" },
	{ "en_BEeuro", "en_BE.ISO8859-15" },
	{ "en_BW", "en_BW.ISO8859-1" },
	{ "en_BWiso88591", "en_BW.ISO8859-1" },
	{ "en_BWutf8", "en_BW.UTF-8" },
	{ "en_CA", "en_CA.ISO8859-1" },
	{ "en_CA885915", "en_CA.ISO8859-15" },
	{ "en_CAiso88591", "en_CA.ISO8859-1" },
	{ "en_CAutf8", "en_CA.UTF-8" },
	{ "en_DK", "en_DK.ISO8859-1" },
	{ "en_DKiso88591", "en_DK.ISO8859-1" },
	{ "en_DKutf8", "en_DK.UTF-8" },
	{ "en_GB", "en_GB.ISO8859-1" },
	{ "en_GB885915", "en_GB.ISO8859-15" },
	{ "en_GB885915euro", "en_GB.ISO8859-15" },
	{ "en_GBibm1252", "en_GB.ANSI1252" },
	{ "en_GBiso88591", "en_GB.ISO8859-1" },
	{ "en_GBiso885915", "en_GB.ISO8859-15" },
	{ "en_GBiso885915euro", "en_GB.ISO8859-15" },
	{ "en_GButf8", "en_GB.UTF-8" },
	{ "en_HK", "en_HK.ISO8859-15" },
	{ "en_HK885915", "en_HK.ISO8859-15" },
	{ "en_HKiso88591", "en_HK.ISO8859-1" },
	{ "en_HKutf8", "en_HK.UTF-8" },
	{ "en_IE", "en_IE.ISO8859-1" },
	{ "en_IE885915", "en_IE.ISO8859-15" },
	{ "en_IE885915euro", "en_IE.ISO8859-15" },
	{ "en_IEeuro", "en_IE.ISO8859-15" },
	{ "en_IEiso88591", "en_IE.ISO8859-1" },
	{ "en_IEiso885915euro", "en_IE.ISO8859-15" },
	{ "en_IEutf8", "en_IE.UTF-8" },
	{ "en_IN", "en_IN.ISO8859-15" },
	{ "en_IN885915", "en_IN.ISO8859-15" },
	{ "en_INutf8", "en_IN.UTF-8" },
	{ "en_NZ", "en_NZ.ISO8859-1" },
	{ "en_NZ885915", "en_NZ.ISO8859-15" },
	{ "en_NZiso88591", "en_NZ.ISO8859-1" },
	{ "en_NZutf8", "en_NZ.UTF-8" },
	{ "en_PH", "en_PH.ISO8859-15" },
	{ "en_PH885915", "en_PH.ISO8859-15" },
	{ "en_PHiso88591", "en_PH.ISO8859-1" },
	{ "en_PHutf8", "en_PH.UTF-8" },
	{ "en_SG", "en_SG.ISO8859-15" },
	{ "en_SG885915", "en_SG.ISO8859-15" },
	{ "en_SGiso88591", "en_SG.ISO8859-1" },
	{ "en_SGutf8", "en_SG.UTF-8" },
	{ "en_US", "en_US.ISO8859-1" },
	{ "en_US885915", "en_US.ISO8859-15" },
	{ "en_USiso88591", "en_US.ISO8859-1" },
	{ "en_USiso885915", "en_US.ISO8859-15" },
	{ "en_USutf8", "en_US.UTF-8" },
	{ "en_ZA", "en_ZA.ISO8859-15" },
	{ "en_ZA885915", "en_ZA.ISO8859-15" },
	{ "en_ZAiso88591", "en_ZA.ISO8859-1" },
	{ "en_ZAutf8", "en_ZA.UTF-8" },
	{ "en_ZW", "en_ZW.ISO8859-1" },
	{ "en_ZWiso88591", "en_ZW.ISO8859-1" },
	{ "en_ZWutf8", "en_ZW.UTF-8" },
	{ "es", "es_ES.ISO8859-1" },
	{ "es_AR", "es_AR.ISO8859-1" },
	{ "es_AR885915", "es_AR.ISO8859-15" },
	{ "es_ARiso88591", "es_AR.ISO8859-1" },
	{ "es_ARutf8", "es_AR.UTF-8" },
	{ "es_BO", "es_BO.ISO8859-1" },
	{ "es_BO885915", "es_BO.ISO8859-15" },
	{ "es_BOiso88591", "es_BO.ISO8859-1" },
	{ "es_BOutf8", "es_BO.UTF-8" },
	{ "es_CL", "es_CL.ISO8859-1" },
	{ "es_CL885915", "es_CL.ISO8859-15" },
	{ "es_CLiso88591", "es_CL.ISO8859-1" },
	{ "es_CLutf8", "es_CL.UTF-8" },
	{ "es_CO", "es_CO.ISO8859-1" },
	{ "es_CO885915", "es_CO.ISO8859-15" },
	{ "es_COiso88591", "es_CO.ISO8859-1" },
	{ "es_COutf8", "es_CO.UTF-8" },
	{ "es_CR", "es_CR.ISO8859-1" },
	{ "es_CR885915", "es_CR.ISO8859-15" },
	{ "es_CRiso88591", "es_CR.ISO8859-1" },
	{ "es_CRutf8", "es_CR.UTF-8" },
	{ "es_DO", "es_DO.ISO8859-15" },
	{ "es_DO885915", "es_DO.ISO8859-15" },
	{ "es_DOiso88591", "es_DO.ISO8859-1" },
	{ "es_DOutf8", "es_DO.UTF-8" },
	{ "es_EC", "es_EC.ISO8859-1" },
	{ "es_EC885915", "es_EC.ISO8859-15" },
	{ "es_ECiso88591", "es_EC.ISO8859-1" },
	{ "es_ECutf8", "es_EC.UTF-8" },
	{ "es_ES", "es_ES.ISO8859-1" },
	{ "es_ES885915", "es_ES.ISO8859-15" },
	{ "es_ES885915euro", "es_ES.ISO8859-15" },
	{ "es_ESeuro", "es_ES.ISO8859-15" },
	{ "es_ESibm1252", "es_ES.ANSI1252" },
	{ "es_ESiso88591", "es_ES.ISO8859-1" },
	{ "es_ESiso885915euro", "es_ES.ISO8859-15" },
	{ "es_ESutf8", "es_ES.UTF-8" },
	{ "es_ESutf8euro", "es_ES.UTF-8" },
	{ "es_GT", "es_GT.ISO8859-1" },
	{ "es_GT885915", "es_GT.ISO8859-15" },
	{ "es_GTiso88591", "es_GT.ISO8859-1" },
	{ "es_GTutf8", "es_GT.UTF-8" },
	{ "es_HN", "es_HN.ISO8859-15" },
	{ "es_HN885915", "es_HN.ISO8859-15" },
	{ "es_HNiso88591", "es_HN.ISO8859-1" },
	{ "es_HNutf8", "es_HN.UTF-8" },
	{ "es_MX", "es_MX.ISO8859-1" },
	{ "es_MX885915", "es_MX.ISO8859-15" },
	{ "es_MXiso88591", "es_MX.ISO8859-1" },
	{ "es_MXutf8", "es_MX.UTF-8" },
	{ "es_NI", "es_NI.ISO8859-1" },
	{ "es_NI885915", "es_NI.ISO8859-15" },
	{ "es_NIiso88591", "es_NI.ISO8859-1" },
	{ "es_NIutf8", "es_NI.UTF-8" },
	{ "es_PA", "es_PA.ISO8859-1" },
	{ "es_PA885915", "es_PA.ISO8859-15" },
	{ "es_PAiso88591", "es_PA.ISO8859-1" },
	{ "es_PAutf8", "es_PA.UTF-8" },
	{ "es_PE", "es_PE.ISO8859-1" },
	{ "es_PE885915", "es_PE.ISO8859-15" },
	{ "es_PEiso88591", "es_PE.ISO8859-1" },
	{ "es_PEutf8", "es_PE.UTF-8" },
	{ "es_PR", "es_PR.ISO8859-15" },
	{ "es_PR885915", "es_PR.ISO8859-15" },
	{ "es_PRiso88591", "es_PR.ISO8859-1" },
	{ "es_PRutf8", "es_PR.UTF-8" },
	{ "es_PY", "es_PY.ISO8859-1" },
	{ "es_PY885915", "es_PY.ISO8859-15" },
	{ "es_PYiso88591", "es_PY.ISO8859-1" },
	{ "es_PYutf8", "es_PY.UTF-8" },
	{ "es_SV", "es_SV.ISO8859-1" },
	{ "es_SV885915", "es_SV.ISO8859-15" },
	{ "es_SViso88591", "es_SV.ISO8859-1" },
	{ "es_SVutf8", "es_SV.UTF-8" },
	{ "es_US", "es_US.ISO8859-15" },
	{ "es_US885915", "es_US.ISO8859-15" },
	{ "es_USiso88591", "es_US.ISO8859-1" },
	{ "es_USutf8", "es_US.UTF-8" },
	{ "es_UY", "es_UY.ISO8859-1" },
	{ "es_UY885915", "es_UY.ISO8859-15" },
	{ "es_UYiso88591", "es_UY.ISO8859-1" },
	{ "es_UYutf8", "es_UY.UTF-8" },
	{ "es_VE", "es_VE.ISO8859-1" },
	{ "es_VE885915", "es_VE.ISO8859-15" },
	{ "es_VEiso88591", "es_VE.ISO8859-1" },
	{ "es_VEutf8", "es_VE.UTF-8" },
	{ "esiso885915", "es_ES.ISO8859-15" },
	{ "esutf8", "es_ES.UTF-8" },
	{ "et", "et_EE.ISO8859-15" },
	{ "et_EE", "et_EE.ISO8859-15" },
	{ "et_EEiso88591", "et_EE.ISO8859-1" },
	{ "et_EEiso885915", "et_EE.ISO8859-15" },
	{ "et_EEutf8", "et_EE.UTF-8" },
	{ "eu_ES", "eu_ES.ISO8859-1" },
	{ "eu_ESeuro", "eu_ES.ISO8859-15" },
	{ "eu_ESiso88591", "eu_ES.ISO8859-1" },
	{ "eu_ESiso885915euro", "eu_ES.ISO8859-15" },
	{ "eu_ESutf8", "eu_ES.UTF-8" },
	{ "fa_IR", "fa_IR.UTF-8" },
	{ "fa_IRutf8", "fa_IR.UTF-8" },
	{ "fi", "fi_FI.ISO8859-1" },
	{ "fi_FI", "fi_FI.ISO8859-1" },
	{ "fi_FI885915", "fi_FI.ISO8859-15" },
	{ "fi_FI885915euro", "fi_FI.ISO8859-15" },
	{ "fi_FIeuro", "fi_FI.ISO8859-15" },
	{ "fi_FIibm1252", "fi_FI.ANSI1252" },
	{ "fi_FIiso88591", "fi_FI.ISO8859-1" },
	{ "fi_FIiso885915euro", "fi_FI.ISO8859-15" },
	{ "fi_FIutf8", "fi_FI.UTF-8" },
	{ "fiiso885915", "fi_FI.ISO8859-15" },
	{ "fo_FO", "fo_FO.ISO8859-1" },
	{ "fo_FOiso88591", "fo_FO.ISO8859-1" },
	{ "fo_FOutf8", "fo_FO.UTF-8" },
	{ "fr", "fr_FR.ISO8859-1" },
	{ "fr_BE", "fr_BE.ISO8859-1" },
	{ "fr_BE885915", "fr_BE.ISO8859-15" },
	{ "fr_BE885915euro", "fr_BE.ISO8859-15" },
	{ "fr_BEeuro", "fr_BE.ISO8859-15" },
	{ "fr_BEibm1252", "fr_BE.ANSI1252" },
	{ "fr_BEiso88591", "fr_BE.ISO8859-1" },
	{ "fr_BEiso885915euro", "fr_BE.ISO8859-15" },
	{ "fr_BEutf8", "fr_BE.UTF-8" },
	{ "fr_BEutf8euro", "fr_BE.UTF-8" },
	{ "fr_CA", "fr_CA.ISO8859-1" },
	{ "fr_CA885915", "fr_CA.ISO8859-15" },
	{ "fr_CAiso88591", "fr_CA.ISO8859-1" },
	{ "fr_CAutf8", "fr_CA.UTF-8" },
	{ "fr_CH", "fr_CH.ISO8859-1" },
	{ "fr_CH885915", "fr_CH.ISO8859-15" },
	{ "fr_CHiso88591", "fr_CH.ISO8859-1" },
	{ "fr_CHutf8", "fr_CH.UTF-8" },
	{ "fr_FR", "fr_FR.ISO8859-1" },
	{ "fr_FR885915", "fr_FR.ISO8859-15" },
	{ "fr_FR885915euro", "fr_FR.ISO8859-15" },
	{ "fr_FReuro", "fr_FR.ISO8859-15" },
	{ "fr_FRibm1252", "fr_FR.ANSI1252" },
	{ "fr_FRiso88591", "fr_FR.ISO8859-1" },
	{ "fr_FRiso885915euro", "fr_FR.ISO8859-15" },
	{ "fr_FRutf8", "fr_FR.UTF-8" },
	{ "fr_FRutf8euro", "fr_FR.UTF-8" },
	{ "fr_LU", "fr_LU.ISO8859-15" },
	{ "fr_LU885915", "fr_LU.ISO8859-15" },
	{ "fr_LU885915euro", "fr_LU.ISO8859-15" },
	{ "fr_LUeuro", "fr_LU.ISO8859-15" },
	{ "fr_LUiso88591", "fr_LU.ISO8859-1" },
	{ "fr_LUiso885915euro", "fr_LU.ISO8859-15" },
	{ "fr_LUutf8", "fr_LU.UTF-8" },
	{ "friso885915", "fr_FR.ISO8859-15" },
	{ "frutf8", "fr_FR.UTF-8" },
	{ "fy_NL", "fy_NL.UTF-8" },
	{ "fy_NLutf8", "fy_NL.UTF-8" },
	{ "ga_IE", "ga_IE.ISO8859-1" },
	{ "ga_IEeuro", "ga_IE.ISO8859-15" },
	{ "ga_IEiso88591", "ga_IE.ISO8859-1" },
	{ "ga_IEiso885915euro", "ga_IE.ISO8859-15" },
	{ "ga_IEutf8", "ga_IE.UTF-8" },
	{ "gd_GB", "gd_GB.ISO8859-15" },
	{ "gd_GBiso885915", "gd_GB.ISO8859-15" },
	{ "gd_GButf8", "gd_GB.UTF-8" },
	{ "gl_ES", "gl_ES.ISO8859-1" },
	{ "gl_ESeuro", "gl_ES.ISO8859-15" },
	{ "gl_ESiso88591", "gl_ES.ISO8859-1" },
	{ "gl_ESiso885915euro", "gl_ES.ISO8859-15" },
	{ "gl_ESutf8", "gl_ES.UTF-8" },
	{ "gu_IN", "gu_IN.UTF-8" },
	{ "gu_INutf8", "gu_IN.UTF-8" },
	{ "gv_GB", "gv_GB.ISO8859-1" },
	{ "gv_GBiso88591", "gv_GB.ISO8859-1" },
	{ "gv_GButf8", "gv_GB.UTF-8" },
	{ "he", "he_IL.ISO8859-8" },
	{ "he_IL", "he_IL.ISO8859-8" },
	{ "he_ILiso88598", "he_IL.ISO8859-8" },
	{ "he_ILutf8", "he_IL.UTF-8" },
	{ "hi_IN", "hi_IN.UTF-8" },
	{ "hi_INutf8", "hi_IN.UTF-8" },
	{ "hr_HR", "hr_HR.ISO8859-2" },
	{ "hr_HRiso88592", "hr_HR.ISO8859-2" },
	{ "hr_HRutf8", "hr_HR.UTF-8" },
	{ "hu", "hu_HU.ISO8859-2" },
	{ "hu_HU", "hu_HU.ISO8859-2" },
	{ "hu_HUiso88592", "hu_HU.ISO8859-2" },
	{ "hu_HUutf8", "hu_HU.UTF-8" },
	{ "hy_AM", "hy_AM.UTF-8" },
	{ "hy_AMutf8", "hy_AM.UTF-8" },
	{ "id_ID", "id_ID.ISO8859-15" },
	{ "id_ID885915", "id_ID.ISO8859-15" },
	{ "id_IDiso88591", "id_ID.ISO8859-1" },
	{ "id_IDutf8", "id_ID.UTF-8" },
	{ "is_IS", "is_IS.ISO8859-1" },
	{ "is_IS885915", "is_IS.ISO8859-15" },
	{ "is_ISiso88591", "is_IS.ISO8859-1" },
	{ "is_ISutf8", "is_IS.UTF-8" },
	{ "it", "it_IT.ISO8859-1" },
	{ "it_CH", "it_CH.ISO8859-15" },
	{ "it_CH885915", "it_CH.ISO8859-15" },
	{ "it_CHiso88591", "it_CH.ISO8859-1" },
	{ "it_CHutf8", "it_CH.UTF-8" },
	{ "it_IT", "it_IT.ISO8859-1" },
	{ "it_IT885915", "it_IT.ISO8859-15" },
	{ "it_IT885915euro", "it_IT.ISO8859-15" },
	{ "it_ITeuro", "it_IT.ISO8859-15" },
	{ "it_ITibm1252", "it_IT.ANSI1252" },
	{ "it_ITiso88591", "it_IT.ISO8859-1" },
	{ "it_ITiso885915euro", "it_IT.ISO8859-15" },
	{ "it_ITutf8", "it_IT.UTF-8" },
	{ "it_ITutf8euro", "it_IT.UTF-8" },
	{ "itiso885915", "it_IT.ISO8859-15" },
	{ "itutf8", "it_IT.UTF-8" },
	{ "iw_IL", "iw_IL.ISO8859-8" },
	{ "iw_ILiso88598", "iw_IL.ISO8859-8" },
	{ "iw_ILutf8", "iw_IL.UTF-8" },
	{ "ja", "ja_JP.eucJP" },
	{ "ja_JP", "ja_JP.eucJP" },
	{ "ja_JPeucjp", "ja_JP.eucJP" },
	{ "ja_JPibmeucjp", "ja_JP.eucJP" },
	{ "ja_JPutf8", "ja_JP.UTF-8" },
	{ "ka_GEutf8", "ka_GE.UTF-8" },
	{ "kk_KZutf8", "kk_KZ.UTF-8" },
	{ "kl_GL", "kl_GL.ISO8859-1" },
	{ "kl_GLiso88591", "kl_GL.ISO8859-1" },
	{ "kl_GLutf8", "kl_GL.UTF-8" },
	{ "km_KH", "km_KH.UTF-8" },
	{ "km_KHutf8", "km_KH.UTF-8" },
	{ "kn_IN", "kn_IN.UTF-8" },
	{ "kn_INutf8", "kn_IN.UTF-8" },
	{ "ko", "ko_KR.EUC" },
	{ "ko_KR", "ko_KR.EUC" },
	{ "ko_KReuckr", "ko_KR.EUC" },
	{ "ko_KRibmeuckr", "ko_KR.EUC" },
	{ "ko_KRutf8", "ko_KR.UTF-8" },
	{ "koutf8", "ko_KR.UTF-8" },
	{ "ku_TR", "ku_TR.ISO8859-9" },
	{ "ku_TRiso88599", "ku_TR.ISO8859-9" },
	{ "ku_TRutf8", "ku_TR.UTF-8" },
	{ "kw_GB", "kw_GB.ISO8859-1" },
	{ "kw_GBiso88591", "kw_GB.ISO8859-1" },
	{ "kw_GButf8", "kw_GB.UTF-8" },
	{ "ky_KG", "ky_KG.UTF-8" },
	{ "ky_KGutf8", "ky_KG.UTF-8" },
	{ "lg_UGutf8", "lg_UG.UTF-8" },
	{ "lo_LA", "lo_LA.UTF-8" },
	{ "lo_LAutf8", "lo_LA.UTF-8" },
	{ "lt", "lt_LT.ISO8859-13" },
	{ "lt_LT", "lt_LT.ISO8859-13" },
	{ "lt_LTiso885913", "lt_LT.ISO8859-13" },
	{ "lt_LTutf8", "lt_LT.UTF-8" },
	{ "lv", "lv_LV.ISO8859-13" },
	{ "lv_LV", "lv_LV.ISO8859-13" },
	{ "lv_LViso885913", "lv_LV.ISO8859-13" },
	{ "lv_LVutf8", "lv_LV.UTF-8" },
	{ "mg_MG", "mg_MG.ISO8859-15" },
	{ "mg_MGiso885915", "mg_MG.ISO8859-15" },
	{ "mg_MGutf8", "mg_MG.UTF-8" },
	{ "mi_NZ", "mi_NZ.ISO8859-13" },
	{ "mi_NZiso885913", "mi_NZ.ISO8859-13" },
	{ "mi_NZutf8", "mi_NZ.UTF-8" },
	{ "mk_MK", "mk_MK.ISO8859-5" },
	{ "mk_MKiso88595", "mk_MK.ISO8859-5" },
	{ "mk_MKutf8", "mk_MK.UTF-8" },
	{ "ml_IN", "ml_IN.UTF-8" },
	{ "ml_INutf8", "ml_IN.UTF-8" },
	{ "mn_MN", "mn_MN.UTF-8" },
	{ "mn_MNutf8", "mn_MN.UTF-8" },
	{ "mr_IN", "mr_IN.UTF-8" },
	{ "mr_INutf8", "mr_IN.UTF-8" },
	{ "ms_MY", "ms_MY.ISO8859-15" },
	{ "ms_MY885915", "ms_MY.ISO8859-15" },
	{ "ms_MYiso88591", "ms_MY.ISO8859-1" },
	{ "ms_MYutf8", "ms_MY.UTF-8" },
	{ "mt_MT", "mt_MT.ISO8859-3" },
	{ "mt_MTiso88593", "mt_MT.ISO8859-3" },
	{ "mt_MTutf8", "mt_MT.UTF-8" },
	{ "nb_NO", "nb_NO.ISO8859-1" },
	{ "nb_NOiso88591", "nb_NO.ISO8859-1" },
	{ "nb_NOutf8", "nb_NO.UTF-8" },
	{ "ne_NP", "ne_NP.UTF-8" },
	{ "ne_NPutf8", "ne_NP.UTF-8" },
	{ "nl", "nl_NL.ISO8859-1" },
	{ "nl_BE", "nl_BE.ISO8859-1" },
	{ "nl_BE885915", "nl_BE.ISO8859-15" },
	{ "nl_BE885915euro", "nl_BE.ISO8859-15" },
	{ "nl_BEeuro", "nl_BE.ISO8859-15" },
	{ "nl_BEibm1252", "nl_BE.ANSI1252" },
	{ "nl_BEiso88591", "nl_BE.ISO8859-1" },
	{ "nl_BEiso885915euro", "nl_BE.ISO8859-15" },
	{ "nl_BEutf8", "nl_BE.UTF-8" },
	{ "nl_NL", "nl_NL.ISO8859-1" },
	{ "nl_NL885915", "nl_NL.ISO8859-15" },
	{ "nl_NL885915euro", "nl_NL.ISO8859-15" },
	{ "nl_NLeuro", "nl_NL.ISO8859-15" },
	{ "nl_NLibm1252", "nl_NL.ANSI1252" },
	{ "nl_NLiso88591", "nl_NL.ISO8859-1" },
	{ "nl_NLiso885915euro", "nl_NL.ISO8859-15" },
	{ "nl_NLutf8", "nl_NL.UTF-8" },
	{ "nliso885915", "nl_NL.ISO8859-15" },
	{ "nn_NO", "nn_NO.ISO8859-1" },
	{ "nn_NOiso88591", "nn_NO.ISO8859-1" },
	{ "nn_NOutf8", "nn_NO.UTF-8" },
	{ "no", "nb_NO.ISO8859-1" },
	{ "no_NO", "nb_NO.ISO8859-1" },
	{ "no_NO885915", "no_NO.ISO8859-15" },
	{ "no_NOiso88591", "no_NO.ISO8859-1" },
	{ "no_NOiso88591bokmal", "nb_NO.ISO8859-1" },
	{ "no_NOiso88591nynorsk", "nn_NO.ISO8859-1" },
	{ "no_NOutf8", "no_NO.UTF-8" },
	{ "no_NY", "nn_NO.ISO8859-1" },
	{ "nr_ZA", "nr_ZA.UTF-8" },
	{ "nr_ZAutf8", "nr_ZA.UTF-8" },
	{ "oc_FR", "oc_FR.ISO8859-1" },
	{ "oc_FRiso88591", "oc_FR.ISO8859-1" },
	{ "oc_FRutf8", "oc_FR.UTF-8" },
	{ "om_ET", "om_ET.UTF-8" },
	{ "om_ETutf8", "om_ET.UTF-8" },
	{ "om_KE", "om_KE.ISO8859-1" },
	{ "om_KEiso88591", "om_KE.ISO8859-1" },
	{ "om_KEutf8", "om_KE.UTF-8" },
	{ "or_IN", "or_IN.UTF-8" },
	{ "or_INutf8", "or_IN.UTF-8" },
	{ "pa_IN", "pa_IN.UTF-8" },
	{ "pa_INutf8", "pa_IN.UTF-8" },
	{ "pa_PK", "pa_PK.UTF-8" },
	{ "pa_PKutf8", "pa_PK.UTF-8" },
	{ "pl", "pl_PL.ISO8859-2" },
	{ "pl_PL", "pl_PL.ISO8859-2" },
	{ "pl_PLiso88592", "pl_PL.ISO8859-2" },
	{ "pl_PLutf8", "pl_PL.UTF-8" },
	{ "plutf8", "pl_PL.UTF-8" },
	{ "pt", "pt_PT.ISO8859-1" },
	{ "pt_BR", "pt_BR.ISO8859-1" },
	{ "pt_BR885915", "pt_BR.ISO8859-15" },
	{ "pt_BRiso88591", "pt_BR.ISO8859-1" },
	{ "pt_BRutf8", "pt_BR.UTF-8" },
	{ "pt_PT", "pt_PT.ISO8859-1" },
	{ "pt_PT885915", "pt_PT.ISO8859-15" },
	{ "pt_PT885915euro", "pt_PT.ISO8859-15" },
	{ "pt_PTeuro", "pt_PT.ISO8859-15" },
	{ "pt_PTibm1252", "pt_PT.ANSI1252" },
	{ "pt_PTiso88591", "pt_PT.ISO8859-1" },
	{ "pt_PTiso885915euro", "pt_PT.ISO8859-15" },
	{ "pt_PTutf8", "pt_PT.UTF-8" },
	{ "ptiso885915", "pt_PT.ISO8859-15" },
	{ "ro_RO", "ro_RO.ISO8859-2" },
	{ "ro_ROiso88592", "ro_RO.ISO8859-2" },
	{ "ro_ROutf8", "ro_RO.UTF-8" },
	{ "ru", "ru_RU.ISO8859-5" },
	{ "ru_RU", "ru_RU.ISO8859-5" },
	{ "ru_RUiso88595", "ru_RU.ISO8859-5" },
	{ "ru_RUkoi8r", "ru_RU.KOI8-R" },
	{ "ru_RUutf8", "ru_RU.UTF-8" },
	{ "ru_UA", "ru_UA.KOI8-U" },
	{ "ru_UAkoi8u", "ru_UA.KOI8-U" },
	{ "ru_UAutf8", "ru_UA.UTF-8" },
	{ "rukoi8r", "ru_RU.KOI8-R" },
	{ "ruutf8", "ru_RU.UTF-8" },
	{ "rw_RW", "rw_RW.UTF-8" },
	{ "rw_RWutf8", "rw_RW.UTF-8" },
	{ "se_NO", "se_NO.UTF-8" },
	{ "se_NOutf8", "se_NO.UTF-8" },
	{ "sh", "bs_BA.ISO8859-2" },
	{ "sh_BA", "bs_BA.ISO8859-2" },
	{ "sh_BAiso88592bosnia", "bs_BA.ISO8859-2" },
	{ "sh_BAutf8", "bs_BA.UTF-8" },
	{ "sh_SP", "sh_SP.ISO8859-2" },
	{ "sh_YU", "sh_YU.ISO8859-2" },
	{ "si_LK", "si_LK.UTF-8" },
	{ "si_LKutf8", "si_LK.UTF-8" },
	{ "sk_SK", "sk_SK.ISO8859-2" },
	{ "sk_SKiso88592", "sk_SK.ISO8859-2" },
	{ "sk_SKutf8", "sk_SK.UTF-8" },
	{ "sl_SI", "sl_SI.ISO8859-2" },
	{ "sl_SIiso88592", "sl_SI.ISO8859-2" },
	{ "sl_SIutf8", "sl_SI.UTF-8" },
	{ "so_DJ", "so_DJ.ISO8859-1" },
	{ "so_DJiso88591", "so_DJ.ISO8859-1" },
	{ "so_DJutf8", "so_DJ.UTF-8" },
	{ "so_ET", "so_ET.UTF-8" },
	{ "so_ETutf8", "so_ET.UTF-8" },
	{ "so_KE", "so_KE.ISO8859-1" },
	{ "so_KEiso88591", "so_KE.ISO8859-1" },
	{ "so_KEutf8", "so_KE.UTF-8" },
	{ "so_SO", "so_SO.ISO8859-1" },
	{ "so_SOiso88591", "so_SO.ISO8859-1" },
	{ "so_SOutf8", "so_SO.UTF-8" },
	{ "sq_AL", "sq_AL.ISO8859-2" },
	{ "sq_AL885915", "sq_AL.ISO8859-15" },
	{ "sq_ALiso88591", "sq_AL.ISO8859-1" },
	{ "sq_ALutf8", "sq_AL.UTF-8" },
	{ "sr_CS", "sr_RS.UTF-8" },
	{ "sr_CSiso88595", "sr_CS.ISO8859-5" },
	{ "sr_CSutf8", "sr_RS.UTF-8" },
	{ "sr_ME", "sr_ME.UTF-8" },
	{ "sr_MEutf8", "sr_ME.UTF-8" },
	{ "sr_RS", "sr_RS.UTF-8" },
	{ "sr_RSutf8", "sr_RS.UTF-8" },
	{ "sr_SP", "sr_RS.ISO8859-5" },
	{ "sr_YU", "sr_RS.ISO8859-5" },
	{ "sr_YUiso88595", "sr_RS.ISO8859-5" },
	{ "ss_ZA", "ss_ZA.UTF-8" },
	{ "ss_ZAutf8", "ss_ZA.UTF-8" },
	{ "st_ZA", "st_ZA.ISO8859-1" },
	{ "st_ZAiso88591", "st_ZA.ISO8859-1" },
	{ "st_ZAutf8", "st_ZA.UTF-8" },
	{ "sv", "sv_SE.ISO8859-1" },
	{ "sv_FI", "sv_FI.ISO8859-1" },
	{ "sv_FIeuro", "sv_FI.ISO8859-15" },
	{ "sv_FIiso88591", "sv_FI.ISO8859-1" },
	{ "sv_FIiso885915euro", "sv_FI.ISO8859-15" },
	{ "sv_FIutf8", "sv_FI.UTF-8" },
	{ "sv_SE", "sv_SE.ISO8859-1" },
	{ "sv_SE885915", "sv_SE.ISO8859-15" },
	{ "sv_SEiso88591", "sv_SE.ISO8859-1" },
	{ "sv_SEiso885915", "sv_SE.ISO8859-15" },
	{ "sv_SEiso885915euro", "sv_SE.ISO8859-15" },
	{ "sv_SEutf8", "sv_SE.UTF-8" },
	{ "sviso885915", "sv_SE.ISO8859-15" },
	{ "svutf8", "sv_SE.UTF-8" },
	{ "ta_IN", "ta_IN.UTF-8" },
	{ "ta_INutf8", "ta_IN.UTF-8" },
	{ "te_IN", "te_IN.UTF-8" },
	{ "te_INutf8", "te_IN.UTF-8" },
	{ "tg_TJ", "tg_TJ.KOI8-T" },
	{ "tg_TJkoi8t", "tg_TJ.KOI8-T" },
	{ "tg_TJutf8", "tg_TJ.UTF-8" },
	{ "th", "th_TH.TIS620" },
	{ "th_TH", "th_TH.TIS620" },
	{ "th_THiso885911", "th_TH.TIS620" },
	{ "th_THtis620", "th_TH.TIS620" },
	{ "th_THutf8", "th_TH.UTF-8" },
	{ "ti_ER", "ti_ER.UTF-8" },
	{ "ti_ERutf8", "ti_ER.UTF-8" },
	{ "ti_ET", "ti_ET.UTF-8" },
	{ "ti_ETutf8", "ti_ET.UTF-8" },
	{ "tl_PH", "tl_PH.ISO8859-1" },
	{ "tl_PHiso88591", "tl_PH.ISO8859-1" },
	{ "tl_PHutf8", "tl_PH.UTF-8" },
	{ "tn_ZA", "tn_ZA.UTF-8" },
	{ "tn_ZAutf8", "tn_ZA.UTF-8" },
	{ "tr", "tr_TR.ISO8859-9" },
	{ "tr_CY", "tr_CY.ISO8859-9" },
	{ "tr_CYiso88599", "tr_CY.ISO8859-9" },
	{ "tr_CYutf8", "tr_CY.UTF-8" },
	{ "tr_TR", "tr_TR.ISO8859-9" },
	{ "tr_TRiso88599", "tr_TR.ISO8859-9" },
	{ "tr_TRutf8", "tr_TR.UTF-8" },
	{ "ts_ZA", "ts_ZA.UTF-8" },
	{ "ts_ZAutf8", "ts_ZA.UTF-8" },
	{ "tt_RUutf8", "tt_RU.UTF-8" },
	{ "uk_UA", "uk_UA.KOI8-U" },
	{ "uk_UAkoi8u", "uk_UA.KOI8-U" },
	{ "uk_UAutf8", "uk_UA.UTF-8" },
	{ "ur_PK", "ur_PK.UTF-8" },
	{ "ur_PKutf8", "ur_PK.UTF-8" },
	{ "uz_UZ", "uz_UZ.ISO8859-1" },
	{ "uz_UZiso88591", "uz_UZ.ISO8859-1" },
	{ "ve_ZA", "ve_ZA.UTF-8" },
	{ "ve_ZAutf8", "ve_ZA.UTF-8" },
	{ "vi_VN", "vi_VN.UTF-8" },
	{ "vi_VNtcvn", "vi_VN.TCVN5712-1" },
	{ "vi_VNutf8", "vi_VN.UTF-8" },
	{ "wa_BE", "wa_BE.ISO8859-1" },
	{ "wa_BEeuro", "wa_BE.ISO8859-15" },
	{ "wa_BEiso88591", "wa_BE.ISO8859-1" },
	{ "wa_BEiso885915euro", "wa_BE.ISO8859-15" },
	{ "wa_BEutf8", "wa_BE.UTF-8" },
	{ "xh_ZA", "xh_ZA.ISO8859-1" },
	{ "xh_ZAiso88591", "xh_ZA.ISO8859-1" },
	{ "xh_ZAutf8", "xh_ZA.UTF-8" },
	{ "yi_US", "yi_US.ANSI1255" },
	{ "yi_UScp1255", "yi_US.ANSI1255" },
	{ "yi_USutf8", "yi_US.UTF-8" },
	{ "zh", "zh_CN.EUC" },
	{ "zh_CN", "zh_CN.EUC" },
	{ "zh_CNgb18030", "zh_CN.GB18030" },
	{ "zh_CNgb2312", "zh_CN.EUC" },
	{ "zh_CNgbk", "zh_CN.GBK" },
	{ "zh_CNibmeuccn", "zh_CN.EUC" },
	{ "zh_CNutf8", "zh_CN.UTF-8" },
	{ "zh_HK", "zh_HK.BIG5HK" },
	{ "zh_HKbig5hkscs", "zh_HK.BIG5HK" },
	{ "zh_HKutf8", "zh_HK.UTF-8" },
	{ "zh_SG", "zh_SG.EUC" },
	{ "zh_SGgb2312", "zh_SG.EUC" },
	{ "zh_SGgbk", "zh_SG.GBK" },
	{ "zh_SGutf8", "zh_SG.UTF-8" },
	{ "zh_TW", "zh_TW.EUC" },
	{ "zh_TWbig5", "zh_TW.BIG5" },
	{ "zh_TWeuctw", "zh_TW.EUC" },
	{ "zh_TWibmeuctw", "zh_TW.EUC" },
	{ "zh_TWutf8", "zh_TW.UTF-8" },
	{ "zhgbk", "zh_CN.GBK" },
	{ "zhutf8", "zh_CN.UTF-8" },
	{ "zu_ZA", "zu_ZA.ISO8859-1" },
	{ "zu_ZAiso88591", "zu_ZA.ISO8859-1" },
	{ "zu_ZAutf8", "zu_ZA.UTF-8" }
};

/*
 * The following two tables are used to map a canonical locale name into
 * obsoleted Solaris locale names for message object/catalog retrieval.
 * They are re-formulated by using the second list of the locale_alias(5)
 * man page.
 *
 * The __lc_obs_msg_index_list[] maps a canonical locale name into start and
 * end indices to the __lc_obs_msg_lc_list[] where the obsoleted Solaris locale
 * names are kept.
 */
static const lc_obs_msg_index_t __lc_obs_msg_index_list[] = {
	{ "ar_EG.ISO8859-6", 0, 0 },
	{ "bg_BG.ISO8859-5", 1, 1 },
	{ "bs_BA.ISO8859-2", 2, 4 },
	{ "bs_BA.UTF-8", 5, 5 },
	{ "ca_ES.ISO8859-1", 6, 7 },
	{ "ca_ES.ISO8859-15", 8, 8 },
	{ "cs_CZ.ISO8859-2", 9, 10 },
	{ "da_DK.ISO8859-1", 11, 12 },
	{ "da_DK.ISO8859-15", 13, 13 },
	{ "de_AT.ISO8859-1", 14, 14 },
	{ "de_AT.ISO8859-15", 15, 15 },
	{ "de_CH.ISO8859-1", 16, 16 },
	{ "de_DE.ISO8859-1", 17, 18 },
	{ "de_DE.ISO8859-15", 19, 20 },
	{ "de_DE.UTF-8", 21, 22 },
	{ "el_CY.UTF-8", 23, 23 },
	{ "el_GR.ISO8859-7", 24, 27 },
	{ "en_AU.ISO8859-1", 28, 28 },
	{ "en_CA.ISO8859-1", 29, 29 },
	{ "en_GB.ISO8859-1", 30, 30 },
	{ "en_IE.ISO8859-1", 31, 31 },
	{ "en_IE.ISO8859-15", 32, 32 },
	{ "en_NZ.ISO8859-1", 33, 33 },
	{ "en_US.ISO8859-1", 34, 34 },
	{ "es_AR.ISO8859-1", 35, 35 },
	{ "es_BO.ISO8859-1", 36, 36 },
	{ "es_CL.ISO8859-1", 37, 37 },
	{ "es_CO.ISO8859-1", 38, 38 },
	{ "es_CR.ISO8859-1", 39, 39 },
	{ "es_EC.ISO8859-1", 40, 40 },
	{ "es_ES.ISO8859-1", 41, 42 },
	{ "es_ES.ISO8859-15", 43, 44 },
	{ "es_ES.UTF-8", 45, 46 },
	{ "es_GT.ISO8859-1", 47, 47 },
	{ "es_MX.ISO8859-1", 48, 48 },
	{ "es_NI.ISO8859-1", 49, 49 },
	{ "es_PA.ISO8859-1", 50, 50 },
	{ "es_PE.ISO8859-1", 51, 51 },
	{ "es_PY.ISO8859-1", 52, 52 },
	{ "es_SV.ISO8859-1", 53, 53 },
	{ "es_UY.ISO8859-1", 54, 54 },
	{ "es_VE.ISO8859-1", 55, 55 },
	{ "et_EE.ISO8859-15", 56, 57 },
	{ "fi_FI.ISO8859-1", 58, 59 },
	{ "fi_FI.ISO8859-15", 60, 61 },
	{ "fr_BE.ISO8859-1", 62, 62 },
	{ "fr_BE.ISO8859-15", 63, 63 },
	{ "fr_BE.UTF-8", 64, 64 },
	{ "fr_CA.ISO8859-1", 65, 65 },
	{ "fr_CH.ISO8859-1", 66, 66 },
	{ "fr_FR.ISO8859-1", 67, 68 },
	{ "fr_FR.ISO8859-15", 69, 70 },
	{ "fr_FR.UTF-8", 71, 72 },
	{ "he_IL.ISO8859-8", 73, 74 },
	{ "hr_HR.ISO8859-2", 75, 75 },
	{ "hu_HU.ISO8859-2", 76, 77 },
	{ "is_IS.ISO8859-1", 78, 78 },
	{ "it_IT.ISO8859-1", 79, 80 },
	{ "it_IT.ISO8859-15", 81, 82 },
	{ "it_IT.UTF-8", 83, 84 },
	{ "ja_JP.eucJP", 85, 85 },
	{ "ko_KR.EUC", 86, 86 },
	{ "ko_KR.UTF-8", 87, 87 },
	{ "lt_LT.ISO8859-13", 88, 89 },
	{ "lv_LV.ISO8859-13", 90, 91 },
	{ "mk_MK.ISO8859-5", 92, 92 },
	{ "nb_NO.ISO8859-1", 93, 95 },
	{ "nl_BE.ISO8859-1", 96, 96 },
	{ "nl_BE.ISO8859-15", 97, 97 },
	{ "nl_NL.ISO8859-1", 98, 99 },
	{ "nl_NL.ISO8859-15", 100, 101 },
	{ "nn_NO.ISO8859-1", 102, 103 },
	{ "pl_PL.ISO8859-2", 104, 105 },
	{ "pl_PL.UTF-8", 106, 106 },
	{ "pt_BR.ISO8859-1", 107, 107 },
	{ "pt_PT.ISO8859-1", 108, 109 },
	{ "pt_PT.ISO8859-15", 110, 111 },
	{ "ro_RO.ISO8859-2", 112, 112 },
	{ "ru_RU.ISO8859-5", 113, 114 },
	{ "ru_RU.KOI8-R", 115, 115 },
	{ "ru_RU.UTF-8", 116, 116 },
	{ "sk_SK.ISO8859-2", 117, 117 },
	{ "sl_SI.ISO8859-2", 118, 118 },
	{ "sq_AL.ISO8859-2", 119, 119 },
	{ "sr_ME.ISO8859-5", 120, 122 },
	{ "sr_ME.UTF-8", 123, 124 },
	{ "sr_RS.ISO8859-5", 125, 127 },
	{ "sr_RS.UTF-8", 128, 129 },
	{ "sv_SE.ISO8859-1", 130, 131 },
	{ "sv_SE.ISO8859-15", 132, 132 },
	{ "sv_SE.UTF-8", 133, 133 },
	{ "th_TH.TIS620", 134, 136 },
	{ "tr_TR.ISO8859-9", 137, 138 },
	{ "zh_CN.EUC", 139, 139 },
	{ "zh_CN.GBK", 140, 140 },
	{ "zh_CN.UTF-8", 141, 141 },
	{ "zh_TW.EUC", 142, 142 }
};

const char * const __lc_obs_msg_lc_list[] = {
	"ar",				/* 0 */
	"bg_BG",			/* 1 */
	"sh",				/* 2 */
	"sh_BA",			/* 3 */
	"sh_BA.ISO8859-2@bosnia",	/* 4 */
	"sh_BA.UTF-8",			/* 5 */
	"ca",				/* 6 */
	"ca_ES",			/* 7 */
	"ca_ES.ISO8859-15@euro",	/* 8 */
	"cs",				/* 9 */
	"cs_CZ",			/* 10 */
	"da",				/* 11 */
	"da_DK",			/* 12 */
	"da.ISO8859-15",		/* 13 */
	"de_AT",			/* 14 */
	"de_AT.ISO8859-15@euro",	/* 15 */
	"de_CH",			/* 16 */
	"de",				/* 17 */
	"de_DE",			/* 18 */
	"de.ISO8859-15",		/* 19 */
	"de_DE.ISO8859-15@euro",	/* 20 */
	"de.UTF-8",			/* 21 */
	"de_DE.UTF-8@euro",		/* 22 */
	"el.UTF-8",			/* 23 */
	"el",				/* 24 */
	"el.sun_eu_greek",		/* 25 */
	"el_GR",			/* 26 */
	"el_GR.ISO8859-7@euro",		/* 27 */
	"en_AU",			/* 28 */
	"en_CA",			/* 29 */
	"en_GB",			/* 30 */
	"en_IE",			/* 31 */
	"en_IE.ISO8859-15@euro",	/* 32 */
	"en_NZ",			/* 33 */
	"en_US",			/* 34 */
	"es_AR",			/* 35 */
	"es_BO",			/* 36 */
	"es_CL",			/* 37 */
	"es_CO",			/* 38 */
	"es_CR",			/* 39 */
	"es_EC",			/* 40 */
	"es",				/* 41 */
	"es_ES",			/* 42 */
	"es.ISO8859-15",		/* 43 */
	"es_ES.ISO8859-15@euro",	/* 44 */
	"es.UTF-8",			/* 45 */
	"es_ES.UTF-8@euro",		/* 46 */
	"es_GT",			/* 47 */
	"es_MX",			/* 48 */
	"es_NI",			/* 49 */
	"es_PA",			/* 50 */
	"es_PE",			/* 51 */
	"es_PY",			/* 52 */
	"es_SV",			/* 53 */
	"es_UY",			/* 54 */
	"es_VE",			/* 55 */
	"et",				/* 56 */
	"et_EE",			/* 57 */
	"fi",				/* 58 */
	"fi_FI",			/* 59 */
	"fi.ISO8859-15",		/* 60 */
	"fi_FI.ISO8859-15@euro",	/* 61 */
	"fr_BE",			/* 62 */
	"fr_BE.ISO8859-15@euro",	/* 63 */
	"fr_BE.UTF-8@euro",		/* 64 */
	"fr_CA",			/* 65 */
	"fr_CH",			/* 66 */
	"fr",				/* 67 */
	"fr_FR",			/* 68 */
	"fr.ISO8859-15",		/* 69 */
	"fr_FR.ISO8859-15@euro",	/* 70 */
	"fr.UTF-8",			/* 71 */
	"fr_FR.UTF-8@euro",		/* 72 */
	"he",				/* 73 */
	"he_IL",			/* 74 */
	"hr_HR",			/* 75 */
	"hu",				/* 76 */
	"hu_HU",			/* 77 */
	"is_IS",			/* 78 */
	"it",				/* 79 */
	"it_IT",			/* 80 */
	"it.ISO8859-15",		/* 81 */
	"it_IT.ISO8859-15@euro",	/* 82 */
	"it.UTF-8",			/* 83 */
	"it_IT.UTF-8@euro",		/* 84 */
	"ja",				/* 85 */
	"ko",				/* 86 */
	"ko.UTF-8",			/* 87 */
	"lt",				/* 88 */
	"lt_LT",			/* 89 */
	"lv",				/* 90 */
	"lv_LV",			/* 91 */
	"mk_MK",			/* 92 */
	"no",				/* 93 */
	"no_NO",			/* 94 */
	"no_NO.ISO8859-1@bokmal",	/* 95 */
	"nl_BE",			/* 96 */
	"nl_BE.ISO8859-15@euro",	/* 97 */
	"nl",				/* 98 */
	"nl_NL",			/* 99 */
	"nl.ISO8859-15",		/* 100 */
	"nl_NL.ISO8859-15@euro",	/* 101 */
	"no_NO.ISO8859-1@nynorsk",	/* 102 */
	"no_NY",			/* 103 */
	"pl",				/* 104 */
	"pl_PL",			/* 105 */
	"pl.UTF-8",			/* 106 */
	"pt_BR",			/* 107 */
	"pt",				/* 108 */
	"pt_PT",			/* 109 */
	"pt.ISO8859-15",		/* 110 */
	"pt_PT.ISO8859-15@euro",	/* 111 */
	"ro_RO",			/* 112 */
	"ru",				/* 113 */
	"ru_RU",			/* 114 */
	"ru.koi8-r",			/* 115 */
	"ru.UTF-8",			/* 116 */
	"sk_SK",			/* 117 */
	"sl_SI",			/* 118 */
	"sq_AL",			/* 119 */
	"sr_SP",			/* 120 */
	"sr_YU",			/* 121 */
	"sr_YU.ISO8859-5",		/* 122 */
	"sr_CS",			/* 123 */
	"sr_CS.UTF-8",			/* 124 */
	"sr_SP",			/* 125 */
	"sr_YU",			/* 126 */
	"sr_YU.ISO8859-5",		/* 127 */
	"sr_CS",			/* 128 */
	"sr_CS.UTF-8",			/* 129 */
	"sv",				/* 130 */
	"sv_SE",			/* 131 */
	"sv.ISO8859-15",		/* 132 */
	"sv.UTF-8",			/* 133 */
	"th",				/* 134 */
	"th_TH",			/* 135 */
	"th_TH.ISO8859-11",		/* 136 */
	"tr",				/* 137 */
	"tr_TR",			/* 138 */
	"zh",				/* 139 */
	"zh.GBK",			/* 140 */
	"zh.UTF-8",			/* 141 */
	"zh_TW"				/* 142 */
};

#define	STRCPY(dst, src)	\
	{ \
		char	*p = (dst); \
		const char	*q = (src); \
		while (*p++ = *q++) \
			; \
	}

#define	FREEP(a) \
	{ \
		if (((a) != C) && ((a) != POSIX)) { \
			free((a)); \
		} \
	}

#define	FREE_ARRAY(X) \
	{ \
		int	cn; \
		for (cn = 0; cn <= _LastCategory; cn++) { \
			FREEP((X)[cn]); \
		} \
	}

#define	SET_TMP_LOCALE(COL, CTP, CHR, MON, NUM, TIM, MES, CORE) \
	{ \
		tmp_locale.lc_collate = (COL); \
		tmp_locale.lc_ctype = (CTP); \
		tmp_locale.lc_charmap = (CHR); \
		tmp_locale.lc_monetary = (MON); \
		tmp_locale.lc_numeric = (NUM); \
		tmp_locale.lc_time = (TIM); \
		tmp_locale.lc_messages = (MES); \
		tmp_locale.core.init = (CORE)->core.init; \
		tmp_locale.core.destructor = (CORE)->core.destructor; \
		tmp_locale.core.user_api = (CORE)->core.user_api; \
		tmp_locale.core.native_api = (CORE)->core.native_api; \
		tmp_locale.core.data = (CORE)->core.data; \
		tmp_locale.nl_lconv = &tmp_local_lconv; \
	}

#define	SET_GLOBAL_LOCALE_RO \
	{ \
		_LC_locale_t	*lp = locp->lp; \
		__lc_collate = lp->lc_collate; \
		__lc_ctype = lp->lc_ctype; \
		__lc_charmap = lp->lc_charmap; \
		__lc_monetary = lp->lc_monetary; \
		__lc_numeric = lp->lc_numeric; \
		__lc_time = lp->lc_time; \
		__lc_messages = lp->lc_messages; \
		__lc_locale = lp; \
	}

#define	SET_GLOBAL_LOCALE_RW \
	{ \
		struct lconv	*p; \
		__lc_collate = tmp_locale.lc_collate; \
		__lc_ctype = tmp_locale.lc_ctype; \
		__lc_charmap = tmp_locale.lc_charmap; \
		__lc_monetary = tmp_locale.lc_monetary; \
		__lc_numeric = tmp_locale.lc_numeric; \
		__lc_time = tmp_locale.lc_time; \
		__lc_messages = tmp_locale.lc_messages; \
		p = locp->lp->nl_lconv; \
		(void) memcpy((void *)locp->lp, (const void *)&tmp_locale, \
			sizeof (_LC_locale_t)); \
		(void) memcpy((void *)p, (const void *)&tmp_local_lconv, \
			sizeof (struct lconv)); \
		locp->lp->nl_lconv = p; \
		__lc_locale = locp->lp; \
	}

char *
setlocale(int category, const char *locname)
{
	int	i;
	int	composite = 0;
	int	add_locp = 0;

	char	*loc;
	loc_chain_t	*locp;
	_LC_locale_t	*lp;
	_LC_locale_t	tmp_locale, locale_local;
	struct lconv	tmp_local_lconv;
	char	*tmp_locale_names[_LastCategory+1];
	obj_chain_t	*lcobj;

	if (category < LC_CTYPE || category > LC_ALL) {
		return (NULL);
	}

	if (locname == NULL) {
		/* Query */
		return (curr_locale->names[category]);
	}

#ifdef	SETLOCALE_DEBUG
	if (strcmp(locname, "SHOW_LOCALE_CHAIN") == 0) {
		loc_chain_t *t;
		int i, j;

		printf("curr_locale = 0x%p\n", curr_locale);
		for (i = 0, t = chain_head; t != NULL; i++, t = t->next) {
			printf("(%d) LC_ALL=%s (0x%p)\n", i, t->names[LC_ALL],
			    t);
			for (j = 0; j <= _LastCategory; j++) {
				printf("\t%s=%s (0x%p)\n", category_name[j],
				    t->names[j], t->lc_objs[j]);
			}
			printf("\tcall_count=%d\n", t->call_count);
			printf("\tflag=0x%08x\n", t->flag);
			printf("\tlp=0x%p\n", t->lp);
			printf("\tprev=0x%p\n", t->prev);
			printf("\tnext=0x%p\n", t->next);
		}

		return (NULL);
	}

	if (strcmp(locname, "SHOW_LOCALE_OBJECT_CHAIN") == 0) {
		obj_chain_t *o;
		int i;

		for (i = 0, o = obj_chain_head; o != NULL; i++, o = o->next) {
			printf("[%d] obj_name=%s (0x%p)\n", i, o->name, o);
			printf("\tlp=0x%p\n", o->lp);
			printf("\thandle=0x%p\n", o->handle);
			printf("\tref_count=%d\n", o->ref_count);
			printf("\tprev=0x%p\n", o->prev);
			printf("\tnext=0x%p\n", o->next);
		}

		return (NULL);
	}
#endif

	/* Setting locale */
	if (category == LC_ALL) {
		/* load all locale categories */
		_LC_locale_t	*lp;
		char	*loc;
		int	reload = 0;

		loc = expand_locale_name(locname, tmp_locale_names, &composite);
		if (loc == NULL) {
			return (NULL);
		}
		if (composite == 0) {
			if (loc == C) {
				locp = &C_entry;
			} else if (loc == POSIX) {
				locp = &POSIX_entry;
			} else {
				/* non C/POSIX locale */
				locp = cache_check(loc);
				if (locp == NULL) {
					/* not found in cache */
					lp = load_locale(loc, &lcobj);
					if (lp == NULL) {
						free(loc);
						return (NULL);
					}
					locp = alloc_chain(lp);
					if (locp == NULL) {
						free(loc);
						return (NULL);
					}
					for (i = 0; i <= _LastCategory; i++) {
						locp->names[i] = loc;
						locp->lc_objs[i] = lcobj;
					}
					locp->names[LC_ALL] = loc;
					locp->flag |= _LC_LOC_LC_ALL;
					add_locp = 1;
				} else {
					/* found in cache */
					free(loc);
					lp = locp->lp;
				}
			}
			if (locp->lp != __lc_locale) {
				/* locale will change; need reload */
				reload = 1;
				lp = locp->lp;
			}
		} else {
			/* composite locale */
			locp = cache_check(loc);
			if (locp == NULL) {
				/* not found in cache */
				locp = alloc_chain(NULL);
				if (locp == NULL) {
					FREE_ARRAY(tmp_locale_names);
					free(loc);
					return (NULL);
				}
				lp = load_composite_locale(tmp_locale_names,
				    &locale_local, locp);
				if (lp == NULL) {
					FREE_ARRAY(tmp_locale_names);
					free(loc);
					free(locp->lp->nl_lconv);
					free(locp->lp);
					free(locp);
					return (NULL);
				}
				locp->names[LC_ALL] = loc;
				locp->flag |= _LC_LOC_ALLNAMES;
				add_locp = 1;
			} else {
				/* found in cache */
				FREE_ARRAY(tmp_locale_names);
				free(loc);
				lp = locp->lp;
			}
			/* composite locale always reloads */
			reload = 1;
		}
		if (reload) {
			SET_TMP_LOCALE(lp->lc_collate, lp->lc_ctype,
			    lp->lc_charmap, lp->lc_monetary, lp->lc_numeric,
			    lp->lc_time, lp->lc_messages, lp);

			if (lp->core.init == NULL ||
			    (*(lp->core.init))(&tmp_locale) ==
			    (_LC_locale_t *)-1) {
				if (add_locp) {
					free(locp->names[LC_ALL]);
					if (composite) {
						FREE_ARRAY(locp->names);
						free(locp->lp->nl_lconv);
						free(locp->lp);
					}
					free(locp);
				}
				return (NULL);
			}
		}
		if (strcmp(curr_locale->names[LC_MESSAGES],
		    locp->names[LC_MESSAGES]) != 0) {
			/*
			 * Inform the runtime linker that
			 * LC_MESSAGES has changed.
			 */
			informrtld(locp->names[LC_MESSAGES]);
		}
	} else {	/* category != LC_ALL */
		/* load a specific category of locale information */
		loc = locale_per_category(locname, category);
		if (loc == NULL) {
			return (NULL);
		}
		if (strcmp(loc, curr_locale->names[category]) == 0) {
			/* locale will not change */
			FREEP(loc);
			return (curr_locale->names[category]);
		}
		/* checks if this loc creates a composite locale */
		composite = 0;
		for (i = 0; i <= _LastCategory; i++) {
			if (i == category)
				continue;
			if (strcmp(curr_locale->names[i], loc) != 0) {
				composite = 1;
				break;
			}
		}
		if (composite == 0) {
			if (loc == C) {
				locp = &C_entry;
			} else if (loc == POSIX) {
				locp = &POSIX_entry;
			} else {
				locp = cache_check(loc);
				if (locp == NULL) {
					/* not found in cache */
					_LC_locale_t	*tlp;
					tlp = load_locale(loc, &lcobj);
					if (tlp == NULL) {
						if (category != LC_MESSAGES ||
						    check_msg(loc) == NULL) {
							free(loc);
							return (NULL);
						}
						/*
						 * Failed to load the locale
						 * object for loc.  But, the
						 * LC_MESSAGES directory for
						 * the locale exists.
						 * This makes a composite
						 * locale.
						 */
						composite = 1;
						goto set_composite;
					}
					locp = alloc_chain(tlp);
					if (locp == NULL) {
						free(loc);
						return (NULL);
					}
					locp->names[LC_ALL] = loc;
					locp->flag |= _LC_LOC_LC_ALL;
					for (i = 0; i <= _LastCategory; i++) {
						locp->names[i] = loc;
						locp->lc_objs[i] = lcobj;
					}
					add_locp = 1;
				} else {
					free(loc);
				}
			}
			lp = locp->lp;
			SET_TMP_LOCALE(lp->lc_collate, lp->lc_ctype,
			    lp->lc_charmap, lp->lc_monetary,
			    lp->lc_numeric, lp->lc_time,
			    lp->lc_messages, lp);
			if (lp->core.init == NULL ||
			    (*(lp->core.init))(&tmp_locale) ==
			    (_LC_locale_t *)-1) {
				if (add_locp) {
					free(locp->names[LC_ALL]);
					free(locp);
				}
				return (NULL);
			}
		} else {	/* composite != 0 */
			/* composite locale */
			char	*tloc;
			char	*tname[_LastCategory+1];
set_composite:
			for (i = 0; i <= _LastCategory; i++) {
				tname[i] = curr_locale->names[i];
			}
			tname[category] = loc;
			tloc = create_composite_locale(tname);
			if (tloc == NULL) {
				FREEP(loc);
				return (NULL);
			}

			locp = cache_check(tloc);
			if (locp) {
				/* found */
				_LC_locale_t	*lp = locp->lp;
				free(tloc);
				SET_TMP_LOCALE(lp->lc_collate, lp->lc_ctype,
				    lp->lc_charmap, lp->lc_monetary,
				    lp->lc_numeric, lp->lc_time,
				    lp->lc_messages, lp);
				if (lp->core.init == NULL ||
				    (*(lp->core.init))(&tmp_locale) ==
				    (_LC_locale_t *)-1) {
					FREEP(loc);
					return (NULL);
				}
			} else {
				/* not found in cache */
				_LC_locale_t	*lp;

				locp = alloc_chain(NULL);
				if (locp == NULL) {
					free(tloc);
					FREEP(loc);
					return (NULL);
				}
				locp->names[LC_ALL] = tloc;
				for (i = 0; i <= _LastCategory; i++) {
					locp->names[i] = tname[i];
					locp->lc_objs[i] =
					    curr_locale->lc_objs[i];
				}
				locp->flag |= (_LC_LOC_LC_ALL |
				    names_allocated[category]);
				add_locp = 1;

				if (loc == C || loc == POSIX) {
					lp = __C_locale;
					lcobj = NULL;
				} else {
					lp = load_locale(loc, &lcobj);
					if (lp == NULL) {
						if (category != LC_MESSAGES ||
						    (lp = check_msg(loc))
						    == NULL) {
							free(tloc);
							free(
							    locp->lp->nl_lconv);
							free(locp->lp);
							free(locp);
							free(loc);
							return (NULL);
						}
						/*
						 * Failed to load the locale
						 * object for loc.  But, the
						 * LC_MESSAGES directory for
						 * the locale exists.
						 * This makes a composite
						 * locale.  lp is set to
						 * __C_locale by check_msg()
						 * and lcobj is set to NULL by
						 * load_locale().
						 */
					}
				}
				locp->lc_objs[category] = lcobj;

				/* call init method for category changed */
				SET_TMP_LOCALE(__lc_collate, __lc_ctype,
				    __lc_charmap, __lc_monetary, __lc_numeric,
				    __lc_time, __lc_messages, lp);

				/* call init method for category changed */

				switch (category) {
				case LC_COLLATE:
					tmp_locale.lc_collate = lp->lc_collate;
					tmp_locale.lc_charmap = lp->lc_charmap;
					break;
				case LC_CTYPE:
					tmp_locale.lc_ctype = lp->lc_ctype;
					tmp_locale.lc_charmap = lp->lc_charmap;
					break;
				case LC_MONETARY:
					tmp_locale.lc_monetary =
					    lp->lc_monetary;
					break;
				case LC_NUMERIC:
					tmp_locale.lc_numeric = lp->lc_numeric;
					break;
				case LC_TIME:
					tmp_locale.lc_time = lp->lc_time;
					break;
				case LC_MESSAGES:
					tmp_locale.lc_messages =
					    lp->lc_messages;
					break;
				}

				if (lp->core.init == NULL ||
				    (*(lp->core.init))(&tmp_locale) ==
				    (_LC_locale_t *)-1) {
					free(tloc);
					free(locp->lp->nl_lconv);
					free(locp->lp);
					free(locp);
					FREEP(loc);
					return (NULL);
				}
			}
		}
		if (category == LC_MESSAGES) {
			/*
			 * Inform the runtime linker that
			 * LC_MESSAGES has changed.
			 */
			informrtld(loc);
		}
	}
	if (add_locp) {
		if (composite) {
			/* new composite locale */
			SET_GLOBAL_LOCALE_RW;
		} else {
			/* new simple locale */
			SET_GLOBAL_LOCALE_RO;
		}

		if (chain_head != NULL)
			chain_head->prev = locp;
		locp->prev = NULL;
		locp->next = chain_head;
		chain_head = locp;

		for (i = 0; i <= _LastCategory; i++) {
			if (locp->lc_objs[i] != NULL &&
			    locp->lc_objs[i]->ref_count < UINT_MAX)
				locp->lc_objs[i]->ref_count++;
		}

		if (obj_counter > _LC_MAX_OBJS)
			evict_locales();
	} else {
		/* composite locale or simple locale in cache */
		SET_GLOBAL_LOCALE_RO;
	}

	if (locp->call_count < UINT_MAX)
		locp->call_count++;
	curr_locale = locp;

	return (locp->names[category]);
}

static loc_chain_t *
alloc_chain(_LC_locale_t *lp)
{
	_LC_locale_t	*locale;
	struct lconv	*local_lconv;
	loc_chain_t	*locp;

	locp = malloc(sizeof (loc_chain_t));
	if (locp == NULL) {
		return (NULL);
	}
	locp->call_count = 0;

	if (lp) {
		/* simple locale */
		locp->lp = lp;
		locp->flag = 0;
		return (locp);
	}

	locale = malloc(sizeof (_LC_locale_t));
	if (locale == NULL) {
		free(locp);
		return (NULL);
	}
	local_lconv = malloc(sizeof (struct lconv));
	if (local_lconv == NULL) {
		free(locale);
		free(locp);
		return (NULL);
	}
	locale->nl_lconv = local_lconv;
	locp->lp = locale;
	locp->flag = _LC_LOC_LCONV;

	return (locp);
}

static loc_chain_t *
cache_check(char *locale)
{
	loc_chain_t	*lchain;

	lchain = chain_head;
	while (lchain) {
		if (strcmp(locale, lchain->names[LC_ALL]) == 0) {
			/* found */
			return (lchain);
		}
		lchain = lchain->next;
	}
	/* not found */
	return (NULL);
}

static _LC_locale_t *
check_msg(char *loc)
{
	char path[PATH_MAX + 1];
	char *p;
	struct stat sb;
	size_t len;
	size_t loc_len;

	len = get_locale_dir_n_suffix(path, NULL, NULL);
	loc_len = strlen(loc);
	if ((len + loc_len + _LC_LCMSG_LEN) > PATH_MAX)
		return (NULL);

	p = path + len;
	(void) memcpy(p, loc, loc_len);
	p += loc_len;
	*p++ = '/';

	(void) memcpy(p, _LC_LCMSG, _LC_LCMSG_LEN);

	/*
	 * Even if a loading of locale shared object fails,
	 * setting of LC_MESSAGES locale category must succeed if
	 * /usr/lib/locale/<loc>/LC_MESSAGES directory exists.
	 */
	if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode))
		return (__C_locale);

	return (NULL);
}

static void
informrtld(char *mesgloc)
{
	Lc_interface	reginfo[3];

	/*
	 * Do this only on the primary link map,
	 * to avoid giving ld.so.1 conflicting data.
	 */
	if (primary_link_map) {
		reginfo[0].ci_tag = CI_VERSION;
		reginfo[0].ci_un.ci_val = CI_V_CURRENT;
		reginfo[1].ci_tag = CI_LCMESSAGES;
		reginfo[1].ci_un.ci_ptr = mesgloc;
		reginfo[2].ci_tag = CI_NULL;
		reginfo[2].ci_un.ci_val = 0;
		_ld_libc(&reginfo);
	}
}

static char *
create_composite_locale(char *loc_name[])
{
	size_t	len;
	size_t	sz[_LastCategory + 1];
	int	i;
	char	*loc, *p;

	for (len = _LastCategory + 2, i = 0; i <= _LastCategory; i++) {
		sz[i] = strlen(loc_name[i]);
		len += sz[i];
	}

	loc = malloc(len);
	if (loc == NULL) {
		return (NULL);
	}

	p = loc;
	*p++ = '/';
	for (i = 0; i <= _LastCategory; i++) {
		(void) memcpy(p, loc_name[i], sz[i]);
		p += sz[i];
		*p++ = '/';
	}
	*(p - 1) = '\0';

	return (loc);
}

static _LC_locale_t *
load_composite_locale(char *comp_name[], _LC_locale_t *locale_local,
    loc_chain_t *locp)
{
	int	i;
	_LC_locale_t	*lp;
	_LC_locale_t	*locps[_LastCategory+1];

	for (i = 0; i <= _LastCategory; i++) {
		locp->names[i] = comp_name[i];
		if (comp_name[i] == C || comp_name[i] == POSIX) {
			lp = __C_locale;
			locp->lc_objs[i] = NULL;
		} else {
			lp = load_locale(comp_name[i], &(locp->lc_objs[i]));
			if (lp == NULL) {
				/*
				 * Some trick for LC_MESSAGES:
				 * Even if load of the locale object
				 * for LC_MESSAGES category failed,
				 * the setting of LC_MESSAGES category
				 * needs to succeed as long as
				 * /usr/lib/locale/<loc>/LC_MESSAGES
				 * directory exists.
				 */
				if (i != LC_MESSAGES ||
				    (lp = check_msg(comp_name[i])) == NULL) {
					return (NULL);
				}
			}
		}
		locps[i] = lp;
	}
	/* LC_COLLATE */
	locale_local->lc_collate = locps[LC_COLLATE]->lc_collate;

	/* LC_CTYPE */
	locale_local->lc_ctype = locps[LC_CTYPE]->lc_ctype;
	locale_local->lc_charmap = locps[LC_CTYPE]->lc_charmap;

	/* LC_MONETARY */
	locale_local->lc_monetary = locps[LC_MONETARY]->lc_monetary;

	/* LC_NUMERIC */
	locale_local->lc_numeric = locps[LC_NUMERIC]->lc_numeric;

	/* LC_TIME */
	locale_local->lc_time = locps[LC_TIME]->lc_time;

	/* LC_MESSAGES */
	locale_local->lc_messages = locps[LC_MESSAGES]->lc_messages;

	/* setup core part of locale container object */
	lp = locps[LC_CTYPE];

	locale_local->core.init = lp->core.init;
	locale_local->core.destructor = lp->core.destructor;
	locale_local->core.user_api = lp->core.user_api;
	locale_local->core.native_api = lp->core.native_api;
	locale_local->core.data = lp->core.data;

	return (locale_local);
}

/*
 * Returns:
 *	0	simple locale
 *	1	composite locale
 *	-1	error
 */
static int
check_composite(const char *locname, char *comp_name[])
{
	const char	*name, *head, *end;
	char	*p;
	int	i, j;
	int	composite;
	size_t	len;

	composite = 0;
	name = locname + 1;
	for (i = 0; i <= _LastCategory; i++) {
		head = name;
		end = strchr(name, '/');
		if (end == NULL) {
			if (i == _LastCategory) {
				end = head + strlen(head);
				name = end;
			} else {
				/* invalid composite locale */
				for (j = 0; j < i; j++) {
					FREEP(comp_name[j]);
				}
				return (-1);
			}
		} else {
			name = end + 1;
		}
		/* LINTED E_PTRDIFF_OVERFLOW */
		len = end - head;
		if (len == 1 && *head == 'C') {
			/* C locale */
			comp_name[i] = (char *)C;
		} else if (len == (sizeof (POSIX) - 1) &&
		    strncmp(head, POSIX, len) == 0) {
			/* POSIX locale */
			comp_name[i] = (char *)POSIX;
		} else {
			p = malloc(len + 1);
			if (p == NULL) {
				for (j = 0; j < i; j++) {
					FREEP(comp_name[j]);
				}
				return (-1);
			}
			(void) strncpy(p, head, len);
			*(p + len) = '\0';
			comp_name[i] = p;
		}
		if (i > 0 && !composite) {
			if (comp_name[i-1] != comp_name[i] &&
			    strcmp(comp_name[i-1], comp_name[i]) != 0) {
				composite = 1;
			}
		}
	}
	if (*name != '\0') {
		/* invalid composite locale */
		FREE_ARRAY(comp_name);
		return (-1);
	}
	if (!composite) {
		/* simple locale */
		p = comp_name[LC_CTYPE];
		for (i = 1; i <= _LastCategory; i++) {
			FREEP(comp_name[i]);
			comp_name[i] = p;
		}
	}
	return (composite);
}

static int
check_loc_name(const char *name, char **loc)
{
	if (name == NULL || *name == '\0' ||
	    (*name == 'C' && *(name + 1) == '\0')) {
		*loc = (char *)C;
		return (1);
	} else if (strcmp(name, POSIX) == 0) {
		*loc = (char *)POSIX;
		return (1);
	} else if (strchr(name, '/') != NULL) {
		*loc = (char *)NULL;
		return (-1);
	}
	*loc = (char *)name;
	return (0);
}

static char *
check_builtin_and_dup(const char *name, char **comp_name)
{
	int	i;
	char	*loc;

	if (check_loc_name(name, &loc) != 0) {
		/* name is either invalid or builtin */
		return (loc);
	}

	loc = strdup(name);
	if (comp_name != NULL && loc != NULL) {
		for (i = 0; i <= _LastCategory; i++) {
			comp_name[i] = loc;
		}
	}
	return (loc);
}

static char *
expand_locale_name(const char *locname, char *comp_name[], int *compp)
{
	char	*q;
	char	*loc;
	char	*lang_name;
	char	*lc_all;
	int	mark;
	int	invalid_lang;
	int	i, j;
	int	composite;

	if (*locname) {
		if (*locname == '/') {
			/*
			 * possible composite locale:
			 * check if locname contains a valid composite
			 * locale name.
			 * A locale name with allocated memory will be
			 * set into each comp_name[] element if necessary.
			 */
			*compp = check_composite(locname, comp_name);
			if (*compp > 0) {
				/* composite locale */
				loc = create_composite_locale(comp_name);
				if (loc == NULL) {
					FREE_ARRAY(comp_name);
					return (NULL);
				}
			} else if (*compp == 0) {
				/* simple locale */
				loc = comp_name[LC_CTYPE];
			} else {
				/* error */
				loc = NULL;
			}
			return (loc);
		} else {
			/* simple locale */
			/*
			 * In SVID, we don't support any simple locale names
			 * including '/' because '/' is used as a delimiter
			 * of a composite locale.
			 * If a simple locale name contains '/',
			 * we fail setlocale().
			 */
			return (check_builtin_and_dup(locname, comp_name));
		}
	} else {
		/* LC_ALL overrides all other environment variables */
		lc_all = getenv("LC_ALL");
		if (lc_all && *lc_all) {
			return (check_builtin_and_dup(lc_all, comp_name));
		}
		/* check environment variables */
		invalid_lang = 0;
		if (check_loc_name(getenv("LANG"), &lang_name) < 0)
			invalid_lang = 1;

		composite = 0;
		mark = 0;
		for (i = 0; i <= _LastCategory; i++) {
			char	*per_env;
			char	*tenv = getenv(category_name[i]);
			if (tenv && *tenv) {
				mark++;
				if (check_loc_name(tenv, &per_env) < 0)
					return (NULL);
				comp_name[i] = per_env;
			} else {
				comp_name[i] = lang_name;
			}
			if (!composite && mark && i > 0) {
				if (comp_name[i-1] != comp_name[i] &&
				    strcmp(comp_name[i-1], comp_name[i]) != 0) {
					composite = 1;
				}
			}
		}
		if (invalid_lang && mark != _LastCategory+1) {
			/* invalid lang */
			return (NULL);
		}

		*compp = composite;
		if (!mark || !composite) {
			/*
			 * no LC_* has been set or
			 * simple locale
			 */
			if (comp_name[LC_CTYPE] == C ||
			    comp_name[LC_CTYPE] == POSIX) {
				/* C or POSIX locale */
				return (comp_name[LC_CTYPE]);
			}
			loc = strdup(comp_name[LC_CTYPE]);
			if (loc == NULL)
				return (NULL);
			for (i = 0; i <= _LastCategory; i++) {
				comp_name[i] = loc;
			}
			return (loc);
		}

		/* composite locale */
		for (i = 0; i <= _LastCategory; i++) {
			if (comp_name[i] == C || comp_name[i] == POSIX)
				continue;
			q = malloc(strlen(comp_name[i]) + 1);
			if (q == NULL) {
				for (j = 0; j < i; j++) {
					FREEP(comp_name[j]);
				}
				return (NULL);
			}
			STRCPY(q, comp_name[i]);
			comp_name[i] = q;
		}
		loc = create_composite_locale(comp_name);
		if (loc == NULL) {
			FREE_ARRAY(comp_name);
			return (NULL);
		}
		/* we need to return non-NULL */
		return (loc);
	}
}

static char *
locale_per_category(const char *locname, int category)
{
	char	*lc_all;
	char	*env_name;

	if (*locname) {
		return (check_builtin_and_dup(locname, NULL));
	}
	/* LC_ALL overrides all other environment variables */
	lc_all = getenv("LC_ALL");
	if (lc_all != NULL && *lc_all != '\0') {
		return (check_builtin_and_dup(lc_all, NULL));
	}
	/* check LANG and LC_* environment */
	env_name = getenv(category_name[category]);
	if (env_name != NULL && *env_name != '\0') {
		return (check_builtin_and_dup(env_name, NULL));
	}
	/* LC_* has not been set */
	return (check_builtin_and_dup(getenv("LANG"), NULL));
}

/*
 * The load_locale() function below has to set the path prefix of the
 * localization libraries to the default path _DFLT_LOC_PATH="/usr/lib/locale/".
 *
 * Certain processes may need to prefix this path with an alternate root
 * directory, so this routine provides the mechanism to do so.
 *
 * (As an example, a branded process needs to get the Solaris localization
 * libraries from /native/usr/lib/locale/, in order to generate the appropriate
 * messages from the Solaris part of the process.
 */
static char l10n_alternate_path[PATH_MAX];	/* initialized to zeros */

int
set_l10n_alternate_root(char *fullpath)
{
	size_t len;

	/* make sure we have a full pathname */
	if (*fullpath != '/')
		return (EINVAL);

	len = strlen(fullpath);
	if (len >= (PATH_MAX - sizeof (_DFLT_LOC_PATH)))
		return (EINVAL);

	(void) memcpy(l10n_alternate_path, fullpath, len);
	(void) memcpy(l10n_alternate_path + len, _DFLT_LOC_PATH,
	    sizeof (_DFLT_LOC_PATH));	/* trailing \0 included */

	return (0);
}

static size_t
get_locale_dir_n_suffix(char *path, char *suffix, size_t *len_suffix)
{
	size_t len;

	if (*l10n_alternate_path != '\0') {
		len = strlen(l10n_alternate_path);
		(void) memcpy(path, l10n_alternate_path, len);
	} else {
		len = sizeof (_DFLT_LOC_PATH) - 1;
		(void) memcpy(path, _DFLT_LOC_PATH, len);
	}
	path[len] = '\0';

	/* .so.?? */
	if (suffix != NULL) {
		suffix[0] = '.';
		suffix[1] = 's';
		suffix[2] = 'o';
		suffix[3] = '.';
#if _LC_VERSION_MAJOR < 10
		suffix[4] = '0' + _LC_VERSION_MAJOR;
		suffix[5] = '\0';
		*len_suffix = 5;
#elif _LC_VERSION_MAJOR < 100
		suffix[4] = '0' + _LC_VERSION_MAJOR / 10;
		suffix[5] = '0' + _LC_VERSION_MAJOR % 10;
		suffix[6] = '\0';
		*len_suffix = 6;
#else
#error	"_LC_VERSION_MAJOR bigger than 99 at get_locale_dir_n_suffix()"
#endif
	}

	return (len);
}

static void
append_locale_obj_path(char *path, char *name, size_t len_name, char *suffix,
    size_t len_suffix, int prepend_locale_name)
{
	if (prepend_locale_name == 1) {
		(void) memcpy(path, name, len_name);
		path += len_name;
		*path++ = '/';
	}

#ifdef _LP64
	(void) memcpy(path, _MACH64_NAME, _MACH64_NAME_LEN);
	path += _MACH64_NAME_LEN;
	*path++ = '/';
#endif

	(void) memcpy(path, name, len_name);
	path += len_name;
	(void) memcpy(path, suffix, len_suffix);
	path += len_suffix;
	*path = '\0';
}

/*
 * Standard I/O functions call the following function to mark the current
 * locale and its locale shared objects to indicate that they are now bound to
 * an I/O stream orientation.
 */
void
mark_locale_as_oriented()
{
	int i;

	if (curr_locale == &C_entry || curr_locale == &POSIX_entry)
		return;

	curr_locale->flag |= _LC_LOC_ORIENTED;

	for (i = 0; i <= _LastCategory; i++)
		if (curr_locale->lc_objs[i] != NULL)
			curr_locale->lc_objs[i]->ref_count = UINT_MAX;
}

/*
 * Evict one or more locales from the locale chain and also dlclose
 * one or more locale shared objects from the locale shared object chain.
 * It may not remove any if there isn't any locale that can be evicted from
 * memory and process due to all the locales are bound to I/O stream
 * orientation, invalid, or too frequently used to evict; such locales are
 * marked with _LC_LOC_ORIENTED flag value, UINT_MAX call_count, or
 * UINT_MAX ref_count.
 *
 * This function can only be called from setlocale() after a new locale has
 * been added to the locale chain as the chain head.
 */
static void
evict_locales()
{
	loc_chain_t *t;
	loc_chain_t *r;
	obj_chain_t *o;
	uint_t count;
	int i;

	if (chain_head == NULL)
		return;

	do {
		t = chain_head->next;
		r = NULL;
		count = UINT_MAX - 1;

		while (t) {
			if (t->call_count <= count &&
			    (t->flag & _LC_LOC_ORIENTED) == 0) {
				r = t;
				count = t->call_count;
			}
			t = t->next;
		}

		if (r == NULL)
			return;

		for (i = 0; i <= _LastCategory; i++) {
			o = r->lc_objs[i];

			/*
			 * Skip locale shared object that is bound to
			 * an I/O stream, C/POSIX, invalid, or too frequently
			 * used to remove.
			 */
			if (o == NULL || o->ref_count == UINT_MAX)
				continue;

			o->ref_count--;

			if (o->ref_count == 0) {
				if (o->next != NULL)
					o->next->prev = o->prev;
				if (o->prev != NULL)
					o->prev->next = o->next;
				else
					obj_chain_head = o->next;

				(void) dlclose(o->handle);
				free(o->name);
				free(o);

				obj_counter--;
			}
		}

		if (r->next != NULL)
			r->next->prev = r->prev;

		if (r->prev != NULL)
			r->prev->next = r->next;
		else
			chain_head = r->next;

		for (i = 0; i <= (_LastCategory + 1); i++) {
			if ((r->flag & names_allocated[i]) != 0)
				FREEP(r->names[i]);
		}
		if ((r->flag & _LC_LOC_LCONV) != 0) {
			free(r->lp->nl_lconv);
			free(r->lp);
		}
		free(r);
	} while (obj_counter > _LC_MAX_OBJS);
}

static int
compare_locale_alias(const void *key, const void *domain)
{
	return (strcmp((char *)key, ((lc_name_list_comp_t *)domain)->alias));
}

static int
compare_canonical_for_obs_msg_locales(const void *key, const void *domain)
{
	return (strcmp((char *)key, ((lc_obs_msg_index_t *)domain)->lc_name));
}

/*
 * The following function retrieves the "canonical" locale name for a given
 * locale name "key" and also, if further requested, obsoleted Solaris locale
 * names for the given locale name. (The "start" and the "end" are the indices
 * to the obsoleted Solaris locale names in the __lc_obs_msg_lc_list[].)
 *
 * When there is no corresponding canonical locale name, "canonical" is
 * returned with NULL. When there is no corresponding indices to the obsoleted
 * Solaris locale names, "start" will have -1.
 */
void
alternative_locales(char *key, char **canonical, int *start, int *end,
    int search_obsoleted_message_locales)
{
	size_t l;
	char b[_LC_MAX_LOCALE_NAME_LENGTH];
	char *n;
	char *a;
	char c;
	ushort_t s;
	lc_name_list_comp_t *p;
	lc_obs_msg_index_t *q;

	*canonical = NULL;
	*start = -1;

	l = strlen(key);
	if (l < _LC_MIN_LOCALE_NAME_LENGTH || l >= _LC_MAX_LOCALE_NAME_LENGTH)
		return;

	n = b;
	a = key;
	while (*a != '\0' && *a != '.' && *a != '@')
		*n++ = *a++;

	while (*a != '\0') {
		if (*a >= 'A' && *a <= 'Z')
			*n++ = *a - 'A' + 'a';
		else if ((*a >= 'a' && *a <= 'z') || (*a >= '0' && *a <= '9'))
			*n++ = *a;
		a++;
	}
	*n = '\0';

	c = *b;
	if (c >= 'A' && c <= 'z') {
		c -= 'A';
		s = __lc_can_index_list[c].start;
		if (s != USHRT_MAX) {
			p = bsearch((const void *)b,
			    (const void *)(__lc_can_lc_list + s),
			    __lc_can_index_list[c].end - s + 1,
			    sizeof (lc_name_list_comp_t),
			    compare_locale_alias);
			if (p != NULL && strcmp(key, p->canonical) != 0)
				*canonical = (char *)p->canonical;
		}
	}

	if (search_obsoleted_message_locales == 0)
		return;

	q = bsearch((const void *)key, (const void *)__lc_obs_msg_index_list,
	    sizeof (__lc_obs_msg_index_list) / sizeof (lc_obs_msg_index_t),
	    sizeof (lc_obs_msg_index_t), compare_canonical_for_obs_msg_locales);
	if (q != NULL) {
		*start = (int)q->start;
		*end = (int)q->end;
	}
}

static _LC_locale_t *
load_locale(const char *name, obj_chain_t **lcobj)
{
	obj_chain_t *lchain;
	char path[PATH_MAX + 1];
	char suffix[_LC_MAX_SUFFIX_LEN];
	size_t len_locpath;
	size_t len_suffix;
	size_t len_name;
	size_t total_len;
	void *handle;
	char *canonical;
	int i;
	_LC_locale_t *(*init)(void);
	_LC_locale_t *lp;

#define	ADD_CACHE(a, h, c)	\
	{ \
		lchain->lp = (a); \
		lchain->handle = (h); \
		lchain->ref_count = (c); \
		lchain->next = obj_chain_head; \
		if (obj_chain_head != NULL) \
			obj_chain_head->prev = lchain; \
		lchain->prev = NULL; \
		obj_chain_head = lchain; \
	}

	lchain = obj_chain_head;
	while (lchain) {
		if (strcmp(name, lchain->name) == 0) {
			*lcobj = lchain;
			return (lchain->lp);
		}
		lchain = lchain->next;
	}

	*lcobj = NULL;

	/*
	 * 32-bit: /usr/lib/locale/<loc>/<loc>.so.??
	 * 64-bit: /usr/lib/locale/<loc>/<MACH64>/<loc>.so.??
	 */
	len_locpath = get_locale_dir_n_suffix(path, suffix, &len_suffix);
	len_name = strlen(name);

	total_len = len_locpath + len_name * 2 + 1 +
#ifdef _LP64
	    _MACH64_NAME_LEN + 1 +
#endif
	    len_suffix;
	if (total_len > PATH_MAX)
		return (NULL);

	append_locale_obj_path(path + len_locpath, (char *)name, len_name,
	    suffix, len_suffix, 1);

	handle = dlopen(path, RTLD_LAZY);
	if (handle == NULL) {
		alternative_locales((char *)name, &canonical, &i, &i, 0);
		if (canonical == NULL)
			return (NULL);

		len_name = strlen(canonical);

		total_len = len_locpath + len_name * 2 + 1 +
#ifdef _LP64
		    _MACH64_NAME_LEN + 1 +
#endif
		    len_suffix;
		if (total_len > PATH_MAX)
			return (NULL);

		append_locale_obj_path(path + len_locpath, canonical,
		    strlen(canonical), suffix, len_suffix, 1);

		handle = dlopen(path, RTLD_LAZY);
		if (handle == NULL)
			return (NULL);
	}

	lchain = malloc(sizeof (obj_chain_t));
	if (lchain == NULL) {
		(void) dlclose(handle);
		return (NULL);
	}

	lchain->name = strdup(name);
	if (lchain->name == NULL) {
		(void) dlclose(handle);
		free(lchain);
		return (NULL);
	}

	init = (_LC_locale_t *(*)(void))dlsym(handle, _LC_SYMBOL);
	if (init == NULL) {
		(void) dlclose(handle);
		/*
		 * The number of the locale objects that can be successfully
		 * dlopen'd is limited, because the number of the valid locales
		 * installed in the system is limited.  So, put this entry in
		 * the cache so that the following load_locale() calls with
		 * the same locale name can immediately return NULL without
		 * performing the dlopen of the object that would result in
		 * the failure of dlsym() again.
		 */
		ADD_CACHE(NULL, NULL, UINT_MAX);
		return (NULL);
	}

	lp = init();

	/* magic/version check */
	if (lp != NULL) {
		/* verify that what was returned was actually an object */
		_LC_core_locale_t	*core = (_LC_core_locale_t *)lp;
		if (core->hdr.magic == _LC_MAGIC &&
		    core->hdr.major_ver == _LC_VERSION_MAJOR &&
		    core->hdr.minor_ver <= (unsigned short)_LC_VERSION_MINOR) {
			ADD_CACHE(lp, handle, 0);
			*lcobj = lchain;
			obj_counter++;
			return (lp);
		}
	}
	/*
	 * init routine failed.
	 * magic number does not match with _LC_MAGIC.
	 * major_ver does not match with _LC_VERSION_MAJOR.
	 * minor_ver is larger than _LC_VERSION_MINOR.
	 */
	(void) dlclose(handle);
	ADD_CACHE(NULL, NULL, UINT_MAX);
	return (NULL);
}

static int
valid_locale_object(char *path, int keep)
{
	void *h;
	_LC_locale_t *(*init)(void);
	_LC_locale_t *lp;
	_LC_core_locale_t *core;

	h = dlopen(path, RTLD_LAZY);
	if (h == NULL)
		return (0);

	init = (_LC_locale_t *(*)(void))dlsym(h, _LC_SYMBOL);
	if (init != NULL) {
		lp = init();
		if (lp != NULL) {
			core = (_LC_core_locale_t *)lp;
			if (core->hdr.magic == _LC_MAGIC &&
			    core->hdr.major_ver == _LC_VERSION_MAJOR &&
			    core->hdr.minor_ver <=
			    (unsigned short)_LC_VERSION_MINOR) {
				if (keep == 0)
					(void) dlclose(h);

				return (1);
			}
		}
	}

	(void) dlclose(h);

	return (0);
}

static int
add_locale_to_list(lclist_t **list, char *loc, size_t *allocated, size_t *count)
{
	lclist_t *l;

	l = *list;

	if (*count >= *allocated) {
		*allocated += _LC_INC_NUM;
		l = realloc(l, sizeof (lclist_t) * *allocated);
		if (l == NULL) {
			errno = ENOMEM;
			return (0);
		}
	}

	*list = l;

	l[*count].locale = strdup(loc);
	if (l[*count].locale == NULL) {
		errno = ENOMEM;
		return (0);
	}

	return (++*count);
}

static void
internal_lclist_free(lclist_t *list, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		free(list[i].locale);
	free(list);
}

static int
compare_lclist_locale(const void *p1, const void *p2)
{
	return (strcmp(((lclist_t *)p1)->locale, ((lclist_t *)p2)->locale));
}

int
localelist(lclist_t **list, int flag)
{
	char path[PATH_MAX + 1];
	char suffix[_LC_MAX_SUFFIX_LEN];
	char *p;
	char *q;
	size_t len_path;
	size_t len_suffix;
	size_t len_name;
	size_t total_len;
	size_t allocated;
	size_t count;
	DIR *dp;
	struct dirent *dep;
	struct stat sb;
	lclist_t *l;
	int include_lc_messages;
	int keep;
	int validate;
	int (*lcliststat)(const char *restrict, struct stat *restrict);

	*list = NULL;

	if ((flag & LCLIST_QUERY) == 0)
		return (0);

	allocated = 0;
	count = 0;

	if (add_locale_to_list(list, (char *)C, &allocated, &count) == 0) {
		if (*list != NULL)
			free(*list);
		return (-1);
	}

	if ((flag & LCLIST_DO_NOT_INCLUDE_POSIX) == 0 &&
	    add_locale_to_list(list, (char *)POSIX, &allocated, &count) == 0) {
		internal_lclist_free(*list, count);
		return (-1);
	}

	len_path = get_locale_dir_n_suffix(path, suffix, &len_suffix);

	dp = opendir(path);
	if (dp == NULL) {
		(*list)[count].locale = NULL;
		return (count);
	}

	if ((flag & LCLIST_EXCLUDE_SYMBOLIC_LINKS) != 0)
		lcliststat = lstat;
	else
		lcliststat = stat;

	validate = flag & LCLIST_VALIDATE;
	keep = flag & LCLIST_KEEP;
	include_lc_messages = flag & LCLIST_INCLUDE_LC_MESSAGES;

	p = path + len_path;
	while ((dep = readdir(dp)) != NULL) {
		len_name = strlen(dep->d_name);

		total_len = len_path + len_name * 2 + 1 +
#ifdef _LP64
		    _MACH64_NAME_LEN + 1 +
#endif
		    len_suffix;
		if (total_len > PATH_MAX)
			continue;

		(void) memcpy(p, dep->d_name, len_name);
		q = p + len_name;
		*q = '\0';

		if (lcliststat(path, &sb) < 0 || S_ISDIR(sb.st_mode) == 0 ||
		    dep->d_name[0] == '.' || dep->d_name[0] == '\0' ||
		    (dep->d_name[0] == 'C' && dep->d_name[1] == '\0') ||
		    strcmp(dep->d_name, "POSIX") == 0)
			continue;

		*q++ = '/';
		append_locale_obj_path(q, dep->d_name, len_name, suffix,
		    len_suffix, 0);

		if (lcliststat(path, &sb) == 0 && S_ISLNK(sb.st_mode) == 0 &&
		    (validate == 0 || valid_locale_object(path, keep) == 1)) {
			if (add_locale_to_list(list, dep->d_name, &allocated,
			    &count) == 0) {
				internal_lclist_free(*list, count);
				(void) closedir(dp);
				return (-1);
			}
		} else if (include_lc_messages != 0) {
			(void) memcpy(q, _LC_LCMSG, _LC_LCMSG_LEN);

			if (lcliststat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
				if (add_locale_to_list(list, dep->d_name,
				    &allocated, &count) == 0) {
					internal_lclist_free(*list, count);
					(void) closedir(dp);
					return (-1);
				}
			}
		}
	}

	(void) closedir(dp);

	qsort((void *)*list, count, sizeof (lclist_t), compare_lclist_locale);

	if (count >= allocated) {
		l = *list;
		l = realloc(l, sizeof (lclist_t) * (allocated + 1));
		if (l == NULL) {
			internal_lclist_free(*list, count);
			errno = ENOMEM;
			return (-1);
		}
		*list = l;
	}
	(*list)[count].locale = NULL;

	return (count);
}

void
localelistfree(lclist_t *list)
{
	int i;

	if (list == NULL)
		return;

	for (i = 0; list[i].locale != NULL; i++)
		free(list[i].locale);
	free(list);
}
