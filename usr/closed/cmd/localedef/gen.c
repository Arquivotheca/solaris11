/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
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
 * static char rcsid[] = "@(#)$RCSfile: gen.c,v $ $Revision: 1.5.5.6 $"
 *	" (OSF) $Date: 1992/10/27 01:54:21 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.11  com/cmd/nls/gen.c, cmdnls, bos320, 9137320a 9/4/91 13:44:10
 */
#include <stdio.h>
#include "locdef.h"

static void	gen_extern(FILE *);
static void	gen_ctype(FILE *, _LC_ctype_t *);
static void	gen_charmap(FILE *, _LC_charmap_t *, _LC_ctype_t *);
static void	gen_collate(FILE *, _LC_collate_t *);
static void	gen_monetary(FILE *, _LC_monetary_t *);
static void	gen_time(FILE *, _LC_time_t *);
static void	gen_numeric(FILE *, _LC_numeric_t *);
static void	gen_msg(FILE *, _LC_messages_t *);
static void	gen_locale(FILE *, _LC_charmap_t *, _LC_numeric_t *,
    _LC_monetary_t *, _LC_time_t *, _LC_messages_t *);
static void	gen_instantiate(FILE *);

/*
 * comp_map[] is used to map an index used by the previous version of
 * localedef for the method into the new index.
 * For example, the method function whose index was 0x01 in the previous
 * localedef was "fgetwc".  The "fgetwc" method function has the index
 * 0x41 in this new localedef.
 * This is to rearrange the list of the extern functions in the locale
 * C source so that the order of the functions becomes as similar to the
 * previously generated one as possible.
 */
static const int	comp_map[] = {
	0x42, 0x41, 0x02, 0x07, 0x01, 0x00, 0x03, 0x45, 0x46, 0x06,
	0xff, 0x49, 0x4a, 0x4c, 0x4d, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x56, 0x57, 0x12, 0x0e, 0x0b, 0x50, 0x52, 0x53,
	0x1d, 0x1e, 0x1f, 0x28, 0x29, 0x2a, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x2e, 0x2f, 0x23, 0x24, 0x33, 0x34, 0x35,
	0x36, 0x37, 0x38, 0x0a, 0x1c, 0x27, 0x2d, 0x22, 0x32, 0x11,
	0x0c, 0x0d, 0x51, 0x61, 0x70, 0x62, 0x65, 0x66, 0x72, 0x73,
	0x76, 0x69, 0x77, 0x6c, 0x6a, 0x6d, 0x71, 0x40, 0x4b, 0x04,
	0x05, 0x43, 0x47, 0x44, 0x48, 0x60, 0x6b, 0x63, 0x67, 0x64,
	0x68
};

static const int	charmap_meth[] = {
	CHARMAP_MBTOWC_AT_NATIVE, CHARMAP_MBSTOWCS_AT_NATIVE,
	CHARMAP_WCTOMB_AT_NATIVE, CHARMAP_WCSTOMBS_AT_NATIVE,
	CHARMAP_MBLEN, CHARMAP_WCSWIDTH_AT_NATIVE,
	CHARMAP_WCWIDTH_AT_NATIVE, CHARMAP_MBFTOWC_AT_NATIVE,
	CHARMAP_FGETWC_AT_NATIVE, CHARMAP_BTOWC_AT_NATIVE,
	CHARMAP_WCTOB_AT_NATIVE, CHARMAP_MBSINIT,
	CHARMAP_MBRLEN, CHARMAP_MBRTOWC_AT_NATIVE,
	CHARMAP_WCRTOMB_AT_NATIVE, CHARMAP_MBSRTOWCS_AT_NATIVE,
	CHARMAP_WCSRTOMBS_AT_NATIVE
};

static const int	ctype_meth[] = {
	CTYPE_WCTYPE, CTYPE_ISWCTYPE_AT_NATIVE,
	CTYPE_TOWUPPER_AT_NATIVE, CTYPE_TOWLOWER_AT_NATIVE,
	CTYPE_TRWCTYPE, CTYPE_WCTRANS,
	CTYPE_TOWCTRANS_AT_NATIVE
};

static const int	collate_meth[] = {
	COLLATE_STRCOLL, COLLATE_STRXFRM,
	COLLATE_WCSCOLL_AT_NATIVE, COLLATE_WCSXFRM_AT_NATIVE,
	COLLATE_FNMATCH, COLLATE_REGCOMP,
	COLLATE_REGERROR, COLLATE_REGEXEC,
	COLLATE_REGFREE
};

static const int	mon_meth[] = {
	MONETARY_STRFMON
};

static const int	time_meth[] = {
	TIME_STRFTIME, TIME_STRPTIME,
	TIME_GETDATE, TIME_WCSFTIME
};

/*
 *  FUNCTION: fp_putstr
 *
 *  DESCRIPTION:
 *  Standard print out routine for character strings in structure
 *  initialization data.
 */
static void
fp_putstr(FILE *fp, const char *s, int category)
{
	if (s == NULL)
		(void) fprintf(fp, "\t\"\",\n");
	else {
		if (copying[category]) {
			size_t	slen;
			char	*buf, *bptr;
			unsigned char	c;

			slen = strlen(s);
			buf = MALLOC(char, slen * 4 + 1);
			bptr = buf;
			while ((c = *s++) != 0) {
				if (c != '\\' && c != '"' &&
				    isascii(c) && isprint(c))
					*bptr++ = c;
				else
					bptr += sprintf(bptr, "\\x%02x", c);
			}
			*bptr = '\0';
			(void) fprintf(fp, "\t\"%s\",\n", buf);
			free(buf);
		} else
			(void) fprintf(fp, "\t\"%s\",\n", s);
	}
}

/*
 *  FUNCTION: fp_str
 *
 *  DESCRIPTION:
 *  Print out a string in the form of "\xXX\xYY"
 */
static void
fp_str(FILE *fp, char *s)
{
	(void) fprintf(fp, "\"");
	while (*s != '\0')
		(void) fprintf(fp, "\\x%2.2x", (unsigned char)*s++);
	(void) fprintf(fp, "\"");
}

/*
 *  FUNCTION: fp_wstr
 *
 *  DESCRIPTION:
 *  Print out a wchar string in the form of "\xXXXXXXXX\xYYYYYYYY"
 */
static void
fp_wstr(FILE *fp, wchar_t *ws)
{
	(void) fprintf(fp, "L\"");
	while (*ws != L'\0')
		(void) fprintf(fp, "\\x%8.8x", (unsigned int)*ws++);
	(void) fprintf(fp, "\"");
}

/*
 *  FUNCTION: fp_putsym
 *
 *  DESCRIPTION:
 *  Standard print out routine for symbols in structure initialization
 *  data.
 */
static void
fp_putsym(FILE *fp, char *s)
{
	if (s != NULL)
		(void) fprintf(fp, "\t%s,\n", s);
	else
		(void) fprintf(fp, "\t(void *)0,\n");
}


/*
 *  FUNCTION: fp_putdig
 *
 *  DESCRIPTION:
 *  Standard print out routine for integer valued structure initialization
 *  data.
 */
static void
fp_putdig(FILE *fp, int i)
{
	(void) fprintf(fp, "\t%d,\n", i);
}

static void
fp_putdig_c(FILE *fp, int i, const char *comment)
{
	(void) fprintf(fp, "\t%d,\t/* %s */\n", i, comment);
}

/*
 *  FUNCTION: fp_puthdr
 *
 *  DESCRIPTION:
 *  Standard print out routine for method headers.
 */
static void
fp_puthdr(FILE *fp, __lc_type_id_t type)
{
	static struct type_obj {
		const char	*typename;
		const char	*objname;
	} hdrstr[] = {
		{ NULL, NULL },				/* 0 */
		{ "_LC_CAR", NULL },			/* 1 */
		{ "_LC_LOCALE", "_LC_locale_t" },	/* 2 */
		{ "_LC_CHARMAP", "_LC_charmap_t" },	/* 3 */
		{ "_LC_CTYPE", "_LC_ctype_t" },		/* 4 */
		{ "_LC_COLLATE", "_LC_collate_t" },	/* 5 */
		{ "_LC_NUMERIC", "_LC_numeric_t" },	/* 6 */
		{ "_LC_MONETARY", "_LC_monetary_t" },	/* 7 */
		{ "_LC_TIME", "_LC_time_t" },		/* 8 */
		{ "_LC_MESSAGES", "_LC_messages_t" }	/* 9 */
	};
	(void) fprintf(fp,
	    "\t{ %s, _LC_MAGIC, _LC_VERSION_MAJOR, _LC_VERSION_MINOR, "
	    "sizeof (%s) },\n",
	    hdrstr[type].typename, hdrstr[type].objname);
}


/*
 *  FUNCTION: fp_putmeth
 *
 *  DESCRIPTION:
 *  Standard print out routine for method references.
 */
static void
fp_putmeth(FILE *fp, int i)
{
	fp_putsym(fp, METH_NAME(i));
}

/*
 *  FUNCTION: gen_hdr
 *
 *  DESCRIPTION:
 *  Generate the header file includes necessary to compile the generated
 *  C code.
 */
static void
gen_hdr(FILE *fp)
{
	/*
	 * New members added in the struct lconv by IEEE Std 1003.1-2001
	 * are always activated in the locale object.
	 * See <iso/locale_iso.h>.
	 */
	(void) fprintf(fp, "#define\t_LCONV_C99\n");
	(void) fprintf(fp, "#include <locale.h>\n");
	(void) fprintf(fp, "#include <sys/localedef.h>\n");
	(void) fprintf(fp, "\n");
}


/*
 *  FUNCTION: gen
 *
 *  DESCRIPTION:
 *  Common entry point to code generation.  This function calls each of the
 *  functions in turn which generate the locale C code from the in-memory
 *  tables built parsing the input files.
 */
void
gen(FILE *fp)
{
	gen_hdr(fp);
	gen_extern(fp);
	if (width_flag) {
		set_column_width();
	}
	if (copying[LC_CTYPE]) {
		gen_charmap(fp, &charmap, ctype_ptr);
	} else {
		gen_charmap(fp, &charmap, NULL);
	}
	if (lc_ctype_flag != 0)
		gen_ctype(fp, ctype_ptr);
	if (lc_collate_flag != 0)
		gen_collate(fp, collate_ptr);
	if (lc_monetary_flag != 0)
		gen_monetary(fp, monetary_ptr);
	if (lc_numeric_flag != 0)
		gen_numeric(fp, numeric_ptr);
	if (lc_time_flag != 0)
		gen_time(fp, lc_time_ptr);
	if (lc_message_flag != 0)
		gen_msg(fp, messages_ptr);
	gen_locale(fp, &charmap, numeric_ptr, monetary_ptr,
	    lc_time_ptr, messages_ptr);
	gen_instantiate(fp);
}


/*
 *  FUNCTION: gen_extern
 *
 *  DESCRIPTION:
 *  This function generates the externs which are necessary to reference the
 *  function names inside the locale objects.
 */
static void
gen_extern(FILE *fp)
{
	int i, idx;
	char *s;
	char *p;		/* Prototype format string */

	for (i = 0; i < sizeof (comp_map) / sizeof (int); i++) {
		idx = comp_map[i];
		if (idx == 0xff)
			continue;
		if (single_layer == TRUE && ISUSER(idx))
			continue;

		s = METH_NAME(idx);
		if (s != NULL) {
			p = METH_PROTO(idx);

			if (p && *p) {
				/* There is a prototype string */
				(void) fprintf(fp, "extern ");
				/* LINTED E_SEC_PRINTF_VAR_FMT */
				(void) fprintf(fp, p, s);
				(void) fputs(";\n", fp);
			} else
				(void) fprintf(fp, "extern %s();\n", s);
		}
	}
}

/*
 *  FUNCTION: gen_charmap
 *
 *  DESCRIPTION:
 *  This function generates the C code which implements a _LC_charmap_t
 *  data structure corresponding to the in memory charmap build parsing the
 *  charmap sourcefile.
 */
#define	PLACEHOLDER_STRING	"(_LC_methods_func_t)0"
static void
gen_charmap(FILE *fp, _LC_charmap_t *lc_cmap, _LC_ctype_t *lc_ctype)
{
	int	i, j, k;
	_LC_charmap_t	*cmap_obj;
	_LC_euc_info_t	*euc_obj;

	if (lc_ctype) {
		cmap_obj = lc_ctype->cmapp;
		euc_obj = cmap_obj->cm_eucinfo;

		/*
		 * ctype information is copied from the existing locale
		 * by 'copy' keyword.  Need to check the information in
		 * the locale and the information from the extension/charmap
		 * file are consistent.
		 */
		if ((charmap.cm_fc_type == _FC_EUC && euc_obj == NULL) ||
		    (charmap.cm_fc_type != _FC_EUC && euc_obj != NULL)) {
			error(4, gettext(ERR_INVAL_EUCINFO));
			/* NOTREACHED */
		}

		if (charmap.cm_fc_type == _FC_EUC) {
			if ((euc_info.euc_bytelen1 != euc_obj->euc_bytelen1) ||
			    (euc_info.euc_bytelen2 != euc_obj->euc_bytelen2) ||
			    (euc_info.euc_bytelen3 != euc_obj->euc_bytelen3) ||
			    (euc_info.euc_scrlen1 != euc_obj->euc_scrlen1) ||
			    (euc_info.euc_scrlen2 != euc_obj->euc_scrlen2) ||
			    (euc_info.euc_scrlen3 != euc_obj->euc_scrlen3) ||
			    (euc_info.cs1_base != euc_obj->cs1_base) ||
			    (euc_info.cs2_base != euc_obj->cs2_base) ||
			    (euc_info.cs3_base != euc_obj->cs3_base) ||
			    (max_wchar_enc != euc_obj->dense_end) ||
			    (euc_info.cs1_adjustment !=
			    euc_obj->cs1_adjustment) ||
			    (euc_info.cs2_adjustment !=
			    euc_obj->cs2_adjustment) ||
			    (euc_info.cs3_adjustment !=
			    euc_obj->cs3_adjustment)) {
				error(4, gettext(ERR_INVAL_EUCINFO));
			}
		}

		if (strcmp(lc_cmap->cm_csname,
		    cmap_obj->cm_csname) != 0) {
			diag_error(gettext(ERR_CSNAME_MISMATCH),
			    cmap_obj->cm_csname, lc_cmap->cm_csname);
		}
		if (lc_cmap->cm_fc_type != cmap_obj->cm_fc_type) {
			error(4, gettext(ERR_FCTYPE_MISMATCH));
		}
		if (lc_cmap->cm_pc_type != cmap_obj->cm_pc_type) {
			error(4, gettext(ERR_PCTYPE_MISMATCH));
		}
		if (lc_cmap->cm_mb_cur_max !=
		    cmap_obj->cm_mb_cur_max) {
			error(4, gettext(ERR_MBMAX_MISMATCH));
		}
		if (lc_cmap->cm_mb_cur_min !=
		    cmap_obj->cm_mb_cur_min) {
			error(4, gettext(ERR_MBMIN_MISMATCH));
		}
	}

	if (charmap.cm_fc_type == _FC_EUC) {
		(void) fprintf(fp, "/*------------------------------"
		    "EUC INFO-----------------------------*/\n");
		(void) fprintf(fp,
		    "static const _LC_euc_info_t cm_eucinfo={\n");
		(void) fprintf(fp, "\t(char) 1,\n");
		(void) fprintf(fp, "\t(char) %d,\n", euc_info.euc_bytelen1);
		(void) fprintf(fp, "\t(char) %d,\n", euc_info.euc_bytelen2);
		(void) fprintf(fp, "\t(char) %d,\n", euc_info.euc_bytelen3);
		(void) fprintf(fp, "\t(char) 1,\n");
		(void) fprintf(fp, "\t(char) %d,\n", euc_info.euc_scrlen1);
		(void) fprintf(fp, "\t(char) %d,\n", euc_info.euc_scrlen2);
		(void) fprintf(fp, "\t(char) %d,\n", euc_info.euc_scrlen3);
		fp_putdig(fp, euc_info.cs1_base);
				/* CS1 dense code base value */
		fp_putdig(fp, euc_info.cs2_base);
		fp_putdig(fp, euc_info.cs3_base);
		fp_putdig(fp, max_wchar_enc);
				/* dense code last value */
		fp_putdig(fp, euc_info.cs1_adjustment);
				/* CS1 adjustment value */
		fp_putdig(fp, euc_info.cs2_adjustment);
		fp_putdig(fp, euc_info.cs3_adjustment);
		(void) fprintf(fp, "};\n\n");
	}
	(void) fprintf(fp,
"/*------------------------- WIDTH TABLE  -------------------------*/\n");
	if (lc_cmap->cm_tbl) {
		for (i = 0; i < lc_cmap->cm_tbl_ent; i++) {
			_LC_widthtabs_t	*cm_tbl = lc_cmap->cm_tbl;
			(void) fprintf(fp,
			    "static const _LC_width_range_t\tranges%d[] = {\n",
			    cm_tbl[i].width);
			for (j = 0; j < cm_tbl[i].entries; j++) {
				char	minstr[sizeof (uint64_t)];
				char	maxstr[sizeof (uint64_t)];
				int	rcmin, rcmax;
				(void) fprintf(fp,
				    "\t{ 0x%08x, 0x%08x }, \t/* ",
				    (int)cm_tbl[i].ranges[j].min,
				    (int)cm_tbl[i].ranges[j].max);
				rcmin = INT_METHOD(
				    (int (*)(_LC_charmap_t *, char *, wchar_t))
				    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))
				    (&charmap,
				    minstr, cm_tbl[i].ranges[j].min);
				rcmax = INT_METHOD(
				    (int (*)(_LC_charmap_t *, char *, wchar_t))
				    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))
				    (&charmap,
				    maxstr, cm_tbl[i].ranges[j].max);
				(void) fprintf(fp, "min: ");
				for (k = 0; k < rcmin; k++) {
					(void) fprintf(fp, "%02x ",
					    (unsigned char)minstr[k]);
				}
				(void) fprintf(fp, ", max: ");
				for (k = 0; k < rcmax; k++) {
					(void) fprintf(fp, "%02x ",
					    (unsigned char)maxstr[k]);
				}
				(void) fprintf(fp, " */\n");
			}
			(void) fprintf(fp, "};\n\n");
		}
		(void) fprintf(fp,
		    "static const _LC_widthtabs_t cm_tbl[] = {\n");
		for (i = 0; i < lc_cmap->cm_tbl_ent; i++) {
			_LC_widthtabs_t	*cm_tbl = lc_cmap->cm_tbl;

			(void) fprintf(fp,
			    "\t{ %d, %d, ranges%d },\n",
			    cm_tbl[i].width, cm_tbl[i].entries,
			    cm_tbl[i].width);
		}
		(void) fprintf(fp, "};\n");
	}
	(void) fprintf(fp,
"/*------------------------- CHARMAP OBJECT  -------------------------*/\n");
	(void) fprintf(fp,
"static const _LC_methods_charmap_t native_methods_charmap={\n");

	/*
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_cmap->core.native_api->nmethods);
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_cmap->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	(void) fprintf(fp, "\t(char *(*) ()) 0,\n");
			/* char *(*nl_langinfo)(); */
	for (i = 0; i < (sizeof (charmap_meth) / sizeof (int)); i++) {
		fp_putmeth(fp, charmap_meth[i]);
	}

	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	if (single_layer == FALSE) {	/* producing a non-native locale */
		(void) fprintf(fp,
"static const _LC_methods_charmap_t user_methods_charmap={\n");

		/*
		 * (void) fprintf(fp, "\t(short) %d,\n",
		 *	lc_cmap->core.native_api->nmethods);
		 * (void) fprintf(fp, "\t(short) %d,\n",
		 *	lc_cmap->core.native_api->ndefined);
		 */
		(void) fprintf(fp, "\t(short) 0,\n");
		(void) fprintf(fp, "\t(short) 0,\n");
		/* class methods */
		(void) fprintf(fp, "\t(char *(*) ()) 0,\n");
				/* char *(*nl_langinfo)(); */
		for (i = 0; i < (sizeof (charmap_meth) / sizeof (int)); i++) {
			fp_putmeth(fp, TOUSER(charmap_meth[i]));
		}

		/* for future use */
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
		(void) fprintf(fp, "};\n\n");
	}

	(void) fprintf(fp, "static const _LC_charmap_t lc_cmap = {{\n");

	/* class core */
	fp_puthdr(fp, _LC_CHARMAP);
	fp_putmeth(fp, CHARMAP_CHARMAP_INIT);
	(void) fprintf(fp, "\t0,\t\t\t\t/* charmap_destructor() */\n");
	/* fp_putmeth(fp, CHARMAP_CHARMAP_DESTRUCTOR); */
	if (single_layer == FALSE)
		fp_putsym(fp,
		    "(_LC_methods_charmap_t *)&user_methods_charmap");
	else
		fp_putsym(fp,
		    "(_LC_methods_charmap_t *)&native_methods_charmap");
	fp_putsym(fp,
	    "(_LC_methods_charmap_t *)&native_methods_charmap");
	fp_putmeth(fp, CHARMAP_EUCPCTOWC);
	fp_putmeth(fp, CHARMAP_WCTOEUCPC);
	fp_putsym(fp, 0);
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
	fp_putstr(fp, lc_cmap->cm_csname, 0);
	switch (lc_cmap->cm_fc_type) {
	case _FC_EUC:
		fp_putsym(fp, "_FC_EUC");
		break;
	case _FC_UTF8:
		fp_putsym(fp, "_FC_UTF8");
		break;
	case _FC_OTHER:
		fp_putsym(fp, "_FC_OTHER");
		break;
	default:
		fp_putsym(fp, "_FC_OTHER");
		break;
	}
	switch (lc_cmap->cm_pc_type) {
	case _PC_EUC:
		fp_putsym(fp, "_PC_EUC");
		break;
	case _PC_DENSE:
		fp_putsym(fp, "_PC_DENSE");
		break;
	case _PC_UCS4:
		fp_putsym(fp, "_PC_UCS4");
		break;
	default:
		fp_putsym(fp, "_PC_DENSE");
		break;
	}
	fp_putdig_c(fp, mb_cur_max, "cm_mb_cur_max");
	fp_putdig_c(fp, 1, "cm_mb_cur_min");
	fp_putdig_c(fp, 0, "cm_reserved");
	fp_putdig_c(fp, lc_cmap->cm_def_width, "cm_def_width");
	fp_putdig_c(fp, lc_cmap->cm_base_max, "cm_base_max");
	fp_putdig_c(fp, lc_cmap->cm_tbl_ent, "cm_tbl_ent");
	if (charmap.cm_fc_type == _FC_EUC)
		fp_putsym(fp, "(_LC_euc_info_t *)&cm_eucinfo");
	else
		(void) fprintf(fp, "\t(_LC_euc_info_t *) NULL,\n");

	if (lc_cmap->cm_tbl)
		fp_putsym(fp, "(_LC_widthtabs_t *)cm_tbl");
	else
		(void) fprintf(fp, "\tNULL,\n");

	(void) fprintf(fp, "};\n\n");
}


/*
 *  FUNCTION: compress_mask
 *
 *  DESCRIPTION:
 *  Take all masks for codepoints above 255 and assign each unique mask
 *  into a secondary array.  Create an unsigned byte array of indices into
 *  the mask array for each of the codepoints above 255.
 */
static int
compress_masks(_LC_ctype_t *ctype)
{
	static int nxt_idx = 1;
	int    umasks;
	unsigned char *qidx;
	unsigned int	*qmask;
	int i, j;

	if (ctype->mask == NULL)
		return (0);

	/* allocate memory for masks and indices */
	ctype->qidx = qidx = MALLOC(unsigned char, max_wchar_enc - 256 + 1);
	ctype->qmask = qmask = MALLOC(unsigned int, 256);

	umasks = 1;
	for (i = 256; i <= max_wchar_enc; i++) { /* for each codepoint > 255 */
		/*
		 * Search for a mask in the 'qmask' array which matches
		 * the mask for the character corresponding to 'i'.
		 */
		for (j = 0; j < umasks; j++) {
			if (qmask[j] == ctype->mask[i]) {
				/*
				 * mask already exists, place index in
				 * qidx for character
				 */
				qidx[i - 256] = (unsigned char) j;
				break;
			}
		}

		if (j == umasks) {
			/*
			 * couldn't find mask which would work, so add
			 * new mask to 'qmask'
			 */
			qidx[i - 256] = (unsigned char) nxt_idx;
			qmask[nxt_idx] = ctype->mask[i];

			nxt_idx++;
			umasks++;
		}

		/* only support 256 unique masks for multi-byte characters */
		if (nxt_idx >= 256) {
			error(2, gettext(ERR_TOO_MANY_MASKS));
		}
	}

	return (nxt_idx);
}


/*
 *  FUNCTION: gen_ctype
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_ctype_t locale
 *  data structures.  These data structures include _LC_classnms_t,
 *  an array of wchars for transformations,
 *  and the container class _LC_ctype_t itself.
 */
#define	N_PER_LINE	4
static void
gen_ctype(FILE *fp, _LC_ctype_t *lc_ctype)
{
	int i, j;
	int k;
	int n_idx;
	int bind_index;
	int sizeof_compat_is_table;
	int sizeof_compat_upper_table;
	int sizeof_compat_lower_table;
	int sizeof_valid_compat_is_table;
	int sizeof_valid_compat_upper_table;
	int sizeof_valid_compat_lower_table;
	char *lc_bind_tag_names[] = {
		"_LC_TAG_UNDEF",
		"_LC_TAG_TRANS",
		"_LC_TAG_CCLASS"
	};
	char *supper;
	char *slower;
	int line_count;
	int	no_of_tbls;
	_LC_transtabs_t	*transtabs;
	char	*transname;
	int	from, to;


	(void) fprintf(fp,
"/*------------------------- CHARACTER CLASSES -------------------------*/\n");

	(void) fprintf(fp, "static const _LC_bind_table_t bindtab[] ={\n");

	if (copying[LC_CTYPE]) {
		_LC_bind_table_t	*bd;

		bd = lc_ctype->bindtab;
		for (i = 0; i < lc_ctype->nbinds; i++) {
			(void) fprintf(fp, "\t{ \"%s\",\t%s,\t"
			    "(_LC_bind_value_t) 0x%08x },\n",
			    bd[i].bindname,
			    lc_bind_tag_names[bd[i].bindtag],
			    (int)bd[i].bindvalue);
		}
	} else {
		for (i = 0, bind_index = 0; i < lc_ctype->nbinds; i++) {
			struct lcbind_table	*p = &Lcbind_Table[i];
			if (p->defined == 1) {
				(void) fprintf(fp, "\t{ \"%s\",\t%s,\t"
				    "(_LC_bind_value_t) 0x%08x },\n",
				    p->lcbind.bindname,
				    lc_bind_tag_names[p->lcbind.bindtag],
				    p->nvalue);
				bind_index++;
			}
		}
	}
	(void) fprintf(fp, "};\n\n");

	if (!copying[LC_CTYPE]) {
		/*
		 * if upper or lower not specified then fill in defaults.
		 */

		slower = MALLOC(char, 2);
		supper = MALLOC(char, 2);
		slower[1] = supper[1] = '\0';
		if (lc_ctype->upper == NULL) {
			for (*supper = 'A', *slower = 'a'; *supper <= 'Z';
			    (*supper)++, (*slower)++) {
				item_t *iptr;

				sem_symbol(slower);
				iptr = create_item(SK_UINT64,
				    ((uint64_t)(*slower)));
				(void) sem_push(iptr);
				sem_symbol_def();
				sem_existing_symbol(slower);
				sem_char_ref();

				sem_symbol(supper);
				iptr = create_item(SK_UINT64,
				    ((uint64_t)(*supper)));
				(void) sem_push(iptr);
				sem_symbol_def();
				sem_existing_symbol(supper);
				sem_char_ref();

				sem_push_xlat();
			}
			add_transformation(lc_ctype, Lcbind_Table, "toupper");
		}

		if (lc_ctype->lower == NULL) {
			for (*supper = 'A', *slower = 'a'; *supper <= 'Z';
			    (*supper)++, (*slower)++) {
				item_t *iptr;

				sem_symbol(supper);
				iptr = create_item(SK_UINT64,
				    ((uint64_t)(*supper)));
				(void) sem_push(iptr);
				sem_symbol_def();
				sem_existing_symbol(supper);
				sem_char_ref();

				sem_symbol(slower);
				iptr = create_item(SK_UINT64,
				    ((uint64_t)(*slower)));
				(void) sem_push(iptr);
				sem_symbol_def();
				sem_existing_symbol(slower);
				sem_char_ref();

				sem_push_xlat();
			}
			add_transformation(lc_ctype, Lcbind_Table, "tolower");
		}

		free(slower);
		free(supper);

	} /* if (copying[LC_CTYPE]) { */

	if (copying[LC_CTYPE]) {
		from = 1;
		to = lc_ctype->ntrans;
	} else {
		from = 0;
		to = lc_ctype->ntrans - 1;
	}

	/*
	 * Generate transformation tables
	 */
	for (k = from; k <= to; k++) {

		(void) fprintf(fp,
		    "/*-------------------- %s --------------------*/\n",
		    lc_ctype->transname[k].name);
		(void) fprintf(fp,
		    "static const wchar_t transformation_%s[] = {\n",
		    lc_ctype->transname[k].name);

		line_count = 0;
#define	N_PER_LINE_TRANSFORMATIONS	8
		if ((strcmp("toupper", lc_ctype->transname[k].name) == 0) ||
		    (strcmp("tolower", lc_ctype->transname[k].name) == 0)) {
			i = 0;
			(void) fprintf(fp, "-1,\t/* %s[EOF] entry */\n",
			    lc_ctype->transname[k].name);
		} else {
			i = lc_ctype->transtabs[k].tmin;
		}

		for (; i <= lc_ctype->transtabs[k].tmax;
		    i += N_PER_LINE_TRANSFORMATIONS) {

			for (j = 0; (j < N_PER_LINE_TRANSFORMATIONS) &&
			    ((i + j) <= lc_ctype->transtabs[k].tmax); j++) {
				int	idx;
				idx = i + j - lc_ctype->transtabs[k].tmin;
				(void) fprintf(fp, "0x%04x, ",
				    (int)lc_ctype->transtabs[k].table[idx]);
			}

			line_count += N_PER_LINE_TRANSFORMATIONS;
			if ((i == 0) || (i == lc_ctype->transtabs[k].tmin) ||
			    (line_count % (N_PER_LINE_TRANSFORMATIONS * 2)))
				(void) fprintf(fp, "  /* 0x%04x */", i);
			(void) fprintf(fp, "\n");

		}
		(void) fprintf(fp, "};\n\n");

		transtabs = lc_ctype->transtabs[k].next;
		transname = lc_ctype->transname[k].name;
		no_of_tbls = 1;
		while (transtabs) {
			(void) fprintf(fp,
"static const wchar_t transformation_%s_%d[] = {\n",
			    transname, no_of_tbls);
			line_count = 0;
			i = transtabs->tmin;

			for (; i <= transtabs->tmax;
			    i += N_PER_LINE_TRANSFORMATIONS) {
				for (j = 0; (j < N_PER_LINE_TRANSFORMATIONS) &&
				    ((i + j) <= transtabs->tmax); j++) {
					int	idx;
					idx = i + j - transtabs->tmin;
					(void) fprintf(fp, "0x%04x, ",
					    (int)transtabs->table[idx]);
				}
				line_count += N_PER_LINE_TRANSFORMATIONS;
				if ((i == 0) ||
				    (i == transtabs->tmin) ||
				    (line_count %
				    (N_PER_LINE_TRANSFORMATIONS * 2))) {
					(void) fprintf(fp,
					    "  /* 0x%04x */", i);
				}
				(void) fprintf(fp, "\n");
			}
			(void) fprintf(fp, "};\n\n");
			transtabs = transtabs->next;
			no_of_tbls++;
		}
	}


	(void) fprintf(fp,
"/*------------------------- CHAR CLASS MASKS -------------------------*/\n");

	/*
	 * print the data for the standard linear array of class masks.
	 */
	(void) fprintf(fp, "static const unsigned int masks[] = {\n");
	(void) fprintf(fp, "0,\t/* masks[EOF] entry */\n");
	for (i = 0; i < 256; i += N_PER_LINE) {

		for (j = 0; j < N_PER_LINE && i + j < 256; j++)
			(void) fprintf(fp, "0x%08x, ",
			    ((lc_ctype->mask != NULL) ?
			    (lc_ctype->mask[i + j]) : 0));

		(void) fprintf(fp, "\n");
	}
	(void) fprintf(fp, "};\n\n");

	/*
	 * If there are more than 256 codepoints in the codeset, the
	 * implementation attempts to compress the masks into a two level
	 * array of indices into masks.
	 */
	if (max_wchar_enc > 255) {
		unsigned char	qmax = 0;
		unsigned char	qq;

		if (!copying[LC_CTYPE]) {
			n_idx = compress_masks(lc_ctype);
			lc_ctype->qidx_hbound = max_wchar_enc;
		}

		/* Print the index array 'qidx' */
		(void) fprintf(fp, "static const unsigned char qidx[] = {\n");
		for (i = 256; i <= lc_ctype->qidx_hbound; i += N_PER_LINE) {

			for (j = 0; j < N_PER_LINE &&
			    i + j <= lc_ctype->qidx_hbound; j++) {
				qq = lc_ctype->qidx[i + j - 256];
				if (copying[LC_CTYPE]) {
					if (qq > qmax) {
						qmax = qq;
					}
				}
				(void) fprintf(fp, "0x%02x, ", qq);
			}
			(void) fprintf(fp, "\n");
		}
		(void) fprintf(fp, "};\n\n");

		if (copying[LC_CTYPE]) {
			n_idx = qmax + 1;
		}
		/* Print the mask array 'qmask' */
		(void) fprintf(fp, "static const unsigned int qmask[] = {\n");
		for (i = 0; i < n_idx; i += N_PER_LINE) {

			for (j = 0; j < N_PER_LINE && i+j < n_idx; j++)
				(void) fprintf(fp,
				    "0x%04x, ", lc_ctype->qmask[i + j]);

		}
		(void) fprintf(fp, "};\n\n");
	} else
		n_idx = 0;

	/*
	 * Write out Solaris _ctype[] table for backwards compatibility.
	 */

	(void) fprintf(fp,
"/*-------------------- CTYPE COMPATIBILITY TABLE  --------------------*/\n\n");

	(void) fprintf(fp, "#define SZ_CTYPE	(257 + 257)\n");
		/* is* and to{upp,low}er */
	(void) fprintf(fp, "#define SZ_CODESET	7\n");
		/* codeset size information */
	(void) fprintf(fp, "#define SZ_TOTAL	(SZ_CTYPE + SZ_CODESET)\n");

	sizeof_compat_is_table = 255;
	sizeof_compat_upper_table = 255;
	sizeof_compat_lower_table = 255;

	if (lc_ctype->cmapp->cm_pc_type == _PC_UCS4) {
		/*
		 * locale using UCS4 as process code
		 * we assume only UTF-8 based locales use
		 * UCS4 as process code
		 */
		sizeof_valid_compat_is_table = 127;
		sizeof_valid_compat_upper_table = 127;
		sizeof_valid_compat_lower_table = 127;
	} else {
		sizeof_valid_compat_is_table = 255;
		sizeof_valid_compat_upper_table = 255;
		sizeof_valid_compat_lower_table = 255;
	}


	(void) fprintf(fp, "/*\n");
	(void) fprintf(fp, "sizeof_compat_is_table = %d\n",
	    sizeof_compat_is_table);
	(void) fprintf(fp, "sizeof_compat_upper_table = %d\n",
	    sizeof_compat_upper_table);
	(void) fprintf(fp, "sizeof_compat_lower_table = %d\n",
	    sizeof_compat_lower_table);
	(void) fprintf(fp, "*/\n");

	(void) fprintf(fp,
"static const unsigned char ctype_compat_table[SZ_TOTAL] = { 0,\n");

	for (i = 0; i <= sizeof_valid_compat_is_table; i++) {
		char ctype_mask[STRING_MAX] = "";

		if (lc_ctype->mask[i] & _ISUPPER)
			(void) strcat(ctype_mask, "_U|");
		if (lc_ctype->mask[i] & _ISLOWER)
			(void) strcat(ctype_mask, "_L|");
		if (lc_ctype->mask[i] & _ISDIGIT)
			(void) strcat(ctype_mask, "_N|");
		if (lc_ctype->mask[i] & _ISSPACE)
			(void) strcat(ctype_mask, "_S|");
		if (lc_ctype->mask[i] & _ISPUNCT)
			(void) strcat(ctype_mask, "_P|");
		if (lc_ctype->mask[i] & _ISCNTRL)
			(void) strcat(ctype_mask, "_C|");
		if (lc_ctype->mask[i] & _ISBLANK)
			if (i != 0x09) /* if tab then don't mark blank */
				(void) strcat(ctype_mask, "_B|");
		if (lc_ctype->mask[i] & _ISXDIGIT)
			(void) strcat(ctype_mask, "_X|");

		if (strlen(ctype_mask) == 0)
			(void) strcat(ctype_mask, "0|");
		ctype_mask[strlen(ctype_mask) - 1] = '\0';
		(void) fprintf(fp, "\t%s,", ctype_mask);
		if ((i + 1) % 8 == 0)
			(void) fprintf(fp, "\n");
	}
	for (; i <= sizeof_compat_is_table; i++) {
		(void) fprintf(fp, "\t0,");
		if ((i + 1) % 8 == 0)
			(void) fprintf(fp, "\n");
	}

	/*
	 * Now generate the toupper/tolower table.
	 */
	(void) fprintf(fp, "\n\t0,\n");
	for (i = 0; i <= sizeof_valid_compat_is_table; i++) {
		if (i <= sizeof_valid_compat_upper_table &&
		    lc_ctype->upper[i] != i)
			(void) fprintf(fp, "\t0x%02x,",
			    (int)lc_ctype->upper[i]);
		else if (i <= sizeof_valid_compat_lower_table &&
		    lc_ctype->lower[i] != i)
			(void) fprintf(fp, "\t0x%02x,",
			    (int)lc_ctype->lower[i]);
		else
			(void) fprintf(fp, "\t0x%02x,", i);
		if ((i + 1) % 8 == 0)
			(void) fprintf(fp, "\n");
	}
	for (; i <= sizeof_compat_is_table; i++) {
		(void) fprintf(fp, "\t0x%02x,", i);
		if ((i + 1) % 8 == 0)
			(void) fprintf(fp, "\n");
	}
	(void) fprintf(fp, "\n");

	/*
	 * Generate the width information.
	 */
	(void) fprintf(fp,
	    "\t/* multiple byte character width information */\n\n");
	(void) fprintf(fp, "\t%d,\t%d,\t%d,\t%d,\t%d,\t%d,\t%d\n",
	    euc_info.euc_bytelen1,
	    euc_info.euc_bytelen2,
	    euc_info.euc_bytelen3,
	    euc_info.euc_scrlen1,
	    euc_info.euc_scrlen2,
	    euc_info.euc_scrlen3,
	    mb_cur_max);

	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp,
"/*---------------------   TRANSFORMATION TABLES   --------------------*/\n");
	(void) fprintf(fp, "static const _LC_transnm_t transname[]={\n");
	(void) fprintf(fp, "\t{ NULL,\t0,\t0,\t0 },\n");
	for (i = from; i <= to; i++) {
		(void) fprintf(fp, "\t{ \"%s\",\t%d,\t0x%08x,\t0x%08x },\n",
		    lc_ctype->transname[i].name,
		    (copying[LC_CTYPE]) ? i : (i + 1),
		    (int)lc_ctype->transname[i].tmin,
		    (int)lc_ctype->transname[i].tmax);
	}
	(void) fprintf(fp, "};\n\n");

	for (i = from; i <= to; i++) {
		transtabs = lc_ctype->transtabs[i].next;
		transname = lc_ctype->transname[i].name;
		no_of_tbls = 1;
		while (transtabs) {
			if (transtabs->next) {
				(void) fprintf(fp,
"static const _LC_transtabs_t	transtabs_%s_%d;\n",
				    transname, no_of_tbls + 1);
			}
			(void) fprintf(fp,
"static const _LC_transtabs_t	transtabs_%s_%d = {\n",
			    transname, no_of_tbls);
			(void) fprintf(fp, "\ttransformation_%s_%d,\n",
			    transname, no_of_tbls);
			(void) fprintf(fp, "\t0x%08x, \t0x%08x,\n",
			    (int)transtabs->tmin, (int)transtabs->tmax);
			if (transtabs->next) {
				(void) fprintf(fp,
				    "\t(_LC_transtabs_t *)&transtabs_%s_%d\n",
				    transname, no_of_tbls + 1);
			} else {
				(void) fprintf(fp, "\tNULL\n");
			}
			(void) fprintf(fp, "};\n\n");
			transtabs = transtabs->next;
			no_of_tbls++;
		}
	}

	(void) fprintf(fp, "static const _LC_transtabs_t transtabs[]={\n");
	(void) fprintf(fp, "\t{ NULL, 0, 0, NULL },\n");
	for (i = from; i <= to; i++) {
		transname = lc_ctype->transname[i].name;
		transtabs = lc_ctype->transtabs + i;
		if ((strcmp("toupper", lc_ctype->transname[i].name) == 0) ||
		    (strcmp("tolower", lc_ctype->transname[i].name) == 0)) {
			(void) fprintf(fp, "\t{\n");
			(void) fprintf(fp, "\t\t&transformation_%s[1],\n",
			    lc_ctype->transname[i].name);
		} else {
			(void) fprintf(fp, "\t{\n");
			(void) fprintf(fp,
			    "\t\ttransformation_%s,\n", transname);
		}
		(void) fprintf(fp, "\t\t0x%08x, \t0x%08x,\n",
		    (int)transtabs->tmin, (int)transtabs->tmax);
		if (transtabs->next) {
			(void) fprintf(fp,
			    "\t\t(_LC_transtabs_t *)&transtabs_%s_1\n",
			    transname);
		} else {
			(void) fprintf(fp, "\t\tNULL\n");
		}
		(void) fprintf(fp, "\t},\n");
	}
	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp,
"/*-------------------------   CTYPE OBJECT   -------------------------*/\n");
	(void) fprintf(fp,
	    "static const _LC_methods_ctype_t native_methods_ctype={\n");

	/*
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_ctype->core.native_api->nmethods);
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_ctype->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	for (i = 0; i < (sizeof (ctype_meth) / sizeof (int)); i++) {
		fp_putmeth(fp, ctype_meth[i]);
	}

	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	if (single_layer == FALSE) {
		(void) fprintf(fp,
"static const _LC_methods_ctype_t user_methods_ctype={\n");

		/*
		 * (void) fprintf(fp, "\t(short) %d,\n",
		 * lc_ctype->core.native_api->nmethods);
		 * (void) fprintf(fp, "\t(short) %d,\n",
		 * lc_ctype->core.native_api->ndefined);
		 */
		(void) fprintf(fp, "\t(short) 0,\n");
		(void) fprintf(fp, "\t(short) 0,\n");
		/* class methods */
		for (i = 0; i < (sizeof (ctype_meth) / sizeof (int)); i++) {
			fp_putmeth(fp, TOUSER(ctype_meth[i]));
		}

		/* for future use */
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
		(void) fprintf(fp, "};\n\n");
	}

	(void) fprintf(fp, "static const _LC_ctype_t lc_ctype = {{\n");

	/* class core */
	fp_puthdr(fp, _LC_CTYPE);
	fp_putmeth(fp, CTYPE_CTYPE_INIT);
	(void) fprintf(fp, "\t0,\t\t\t\t/* ctype_destructor() */\n");
	/* fp_putmeth(fp, CTYPE_CTYPE_DESTRUCTOR); */
	if (single_layer == FALSE)
		fp_putsym(fp,
		    "(_LC_methods_ctype_t *)&user_methods_ctype");
	else
		fp_putsym(fp,
		    "(_LC_methods_ctype_t *)&native_methods_ctype");
	fp_putsym(fp,
	    "(_LC_methods_ctype_t *)&native_methods_ctype");
	fp_putsym(fp, 0);

	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
	fp_putsym(fp,
	    "(_LC_charmap_t *)&lc_cmap");	/* _LC_charmap_t *charmap; */
	/* max and min process code (required by toupper, et al) */
	fp_putdig(fp, 0);
	fp_putdig(fp, max_wchar_enc);
	fp_putdig(fp, lc_ctype->max_upper);
	fp_putdig(fp, lc_ctype->max_lower);

	/* case translation arrays */
	fp_putsym(fp, "&transformation_toupper[1]");
	fp_putsym(fp, "&transformation_tolower[1]");
	fp_putsym(fp, "&masks[1]");

	if (n_idx > 0) {
		fp_putsym(fp, "qmask");
		fp_putsym(fp, "qidx");
	} else {
		fp_putsym(fp, 0);
		fp_putsym(fp, 0);
	}
	fp_putdig(fp, lc_ctype->qidx_hbound);

	fp_putdig(fp, lc_ctype->nbinds);		/* nbinds */
	fp_putsym(fp, "(_LC_bind_table_t*)bindtab");	/* bindtab */
	/* trans name mapping */
	(void) fprintf(fp, "\t/*  transformations */\n");
	(void) fprintf(fp, "\t%d,\n", lc_ctype->ntrans);
	fp_putsym(fp, "(_LC_transnm_t *)transname");
	fp_putsym(fp, "transtabs");
	/* ctype[] table */
	(void) fprintf(fp, "\tSZ_TOTAL,\n");
	fp_putsym(fp, "ctype_compat_table");
	(void) fprintf(fp, "\t{\n");
	(void) fprintf(fp, "\t(void *)0,	"
	    "/* reserved for future use */\n");
	fp_putsym(fp, 0);
	fp_putsym(fp, 0);
	fp_putsym(fp, 0);
	fp_putsym(fp, 0);
	fp_putsym(fp, 0);
	fp_putsym(fp, 0);
	fp_putsym(fp, 0);
	(void) fprintf(fp, "\t}\n");
	(void) fprintf(fp, "};\n\n");
}


static void
fp_putcolflag(FILE *fp, int flags)
{
	int first = 1;

#define	PF(x) \
	if (flags & x) { \
		(void) fprintf(fp, #x); \
		flags &= ~x; \
		continue; \
	}

	while (flags != 0) {
		if (!first)
			(void) fprintf(fp, "|");
		first = 0;
		PF(_COLL_FORWARD_MASK)
		PF(_COLL_FORWARD_MASK)
		PF(_COLL_BACKWARD_MASK)
		PF(_COLL_NOSUBS_MASK)
		PF(_COLL_POSITION_MASK)
		PF(_COLL_SUBS_MASK)
		PF(_COLL_WGT_WIDTH1)
		PF(_COLL_WGT_WIDTH2)
		PF(_COLL_WGT_WIDTH3)
		PF(_COLL_WGT_WIDTH4)
		INTERNAL_ERROR;
	}
	if (first)
		(void) fprintf(fp, "0");
	(void) fprintf(fp, ",");
#undef PF
}

/*
 * FUNCTION: fp_putsubsflag
 *
 *	Prints the collating substitution flags symbolically
 */
static void
fp_putsubsflag(FILE *fp, int flags)
{
	int	first = 1;

	if (flags == 0) {
		(void) fputs("0,", fp);
		return;
	}
	while (flags != 0) {
		if (!first)
			(void) fputs("|", fp);
		if (flags & _SUBS_ACTIVE) {
			(void) fputs("_SUBS_ACTIVE", fp);
			flags &= ~_SUBS_ACTIVE;
			first = 0;
		} else if (flags & _SUBS_REGEXP) {
			(void) fputs("_SUBS_REGEXP", fp);
			flags &= ~_SUBS_REGEXP;
			first = 0;
		} else {
			INTERNAL_ERROR;
		}
	}
	(void) fputs(",", fp);
}

/*
 * Generate substitue table, which used to be "substrs", and now "elsubs"
 * for many-to-many/one mappings and "weightstr" for one-to-many mappings.
 */
static void
gen_subtable(FILE *fp, _LC_collate_t *coll)
{
	int	num_order = coll->co_nord;
	int	max_order = coll->co_nord + coll->co_r_order;
	int	i, j, k;
	wchar_t	*ws;
	size_t	l, wslen;
	_LC_collextinfo_t *ext;
	_LC_exsubs_t *subs;

	/*
	 * Remember, we may be "copy"ing locale element. Then the
	 * original locale may not have extended info. We only
	 * handles co_ext == 1. Don't touch any future version.
	 */
	ext = NULL;
	if (coll->co_ext == 1)
		ext = (_LC_collextinfo_t *)coll->co_extinfo;
	/*
	 * create elsub table for many-to-many/one mappings
	 */
	if (ext != NULL && ext->ext_nsubs != 0) {
		/*
		 * We have some substring/substitute entries.
		 */
		/*
		 * Create index and size table per orders which will
		 * be pointed by ext_hsuboff and ext_hsubsz.
		 */
		(void) fprintf(fp,
		    "static const unsigned int hsuboff[] = {\n");
		for (i = 0; i <= max_order; i++) {
			(void) fprintf(fp, "%d, ", ext->ext_hsuboff[i]);
		}
		(void) fprintf(fp, "\n};\n");

		(void) fprintf(fp,
		    "static const unsigned int hsubsz[] = {\n");
		for (i = 0; i <= max_order; i++) {
			(void) fprintf(fp, "%d, ", ext->ext_hsubsz[i]);
		}
		(void) fprintf(fp, "\n};\n");

		/*
		 * Generate the entries for single byte character's
		 * table used by strxfrm/strcoll.
		 */
		(void) fprintf(fp, "static const _LC_exsubs_t exsubs[] = {\n");
		subs = ext->ext_hsubs;
		for (i = 0; i < ext->ext_nsubs; i++) {
			if (verbose > 1)
				(void) fprintf(fp, "/* %2d */ ", i);
			(void) fprintf(fp, "{ %d, %d, ",
			    subs[i].ess_order,  subs[i].ess_srclen);
			fp_str(fp, subs[i].ess_src.sp);
			(void) fprintf(fp, ", %d},\n", subs[i].ess_wgt.wgtidx);
		}
		(void) fprintf(fp, "};\n");

		/*
		 * The next bunch is the entries for wide characters
		 * used by wcsxfrm/wcscoll.
		 */
		(void) fprintf(fp, "static const _LC_exsubs_t wexsubs[] = {\n");
		subs = ext->ext_hwsubs;
		for (i = 0; i < ext->ext_nsubs; i++) {
			/* wide char source entries */
			if (verbose > 1)
				(void) fprintf(fp, "/* %2d */ ", i);
			(void) fprintf(fp, "{ %d, %d, (const char *)",
			    subs[i].ess_order,  subs[i].ess_srclen);
			fp_wstr(fp, subs[i].ess_src.wp);
			(void) fprintf(fp, ", %d},\n", subs[i].ess_wgt.wgtidx);
		}
		(void) fprintf(fp, "};\n");
	}

	/*
	 * Generate weight string table.
	 */
	if (ext != NULL && ext->ext_wgtstrsz != 0) {
		/*
		 * We have weight string table
		 */
		/*
		 * generate weightstr[] (weight string table)
		 */
		(void) fprintf(fp, "static const wchar_t weightstr[] = {\n");
		(void) fprintf(fp, "0,\n");
		i = 1;
		ws = ext->ext_wgtstr;
		wslen = ext->ext_wgtstrsz;
		while (i < wslen) {
			/* The first entry is the size */
			l = ws[i] + 1;
			if (verbose > 1)
				(void) fprintf(fp, "/* %d */ ", i);
			for (j = 0; j < l; j++)
				(void) fprintf(fp, "%d, ", (int)ws[i + j]);
			(void) fprintf(fp, "\n");
			i += j;
		}
		(void) fprintf(fp, "};\n");
	}

	/*
	 * Generate the substitution quick lookup table (ie submap). The
	 * first entry is the flag indicates if those tables are valid.
	 */
	if (ext != NULL && ext->ext_submap != NULL) {

		(void) fprintf(fp, "static const char subs_map[]={\n");
		(void) fprintf(fp, "0x%02x,\n", ext->ext_submap[0]);

		j = coll->co_hbound + 1;
		for (i = 1; i <= j; i += k) {
			if (verbose > 1)
				(void) fprintf(fp, "/* 0x%04x */ ", i - 1);
			for (k = 0; k < 8 && (i + k) <= j; k++)
				(void) fprintf(fp, "0x%02x, ",
				    ext->ext_submap[i + k]);
			(void) fprintf(fp, "\n");
		}
		(void) fprintf(fp, "};\n");
	}

	/*
	 * Generate substitution action arrays (old subs table).
	 * New runtime won't use this table, but generate it for
	 * the old runtime.
	 */
	if (coll->co_nsubs != 0) {
		/*
		 * Generate weights (flags)
		 */
		(void) fprintf(fp, "static const wchar_t subs_wgts[][%d]={\n",
		    coll->co_nord + 1);
		for (i = 0; i < (int)coll->co_nsubs; i++) {
			for (j = 0; j <= num_order; j++)
				fp_putsubsflag(fp, coll->co_subs[i].ss_act[j]);
			(void) fprintf(fp, "\n");
		}
		(void) fprintf(fp, "};\n");
		/*
		 * Generate real table.
		 */
		(void) fprintf(fp, "static const _LC_subs_t substrs[] = {\n");
		for (i = 0; i < (int)coll->co_nsubs; i++) {
			(void) fprintf(fp,
			    "\t{ (const _LC_weight_t)subs_wgts[%d], "
			    "\"%s\", \"%s\" },\n", i + 1,
			    coll->co_subs[i].ss_src,
			    coll->co_subs[i].ss_tgt);
		}
		(void) fprintf(fp, "};\n\n");
	}

	if (ext != NULL) {
		/*
		 * Generate _LC_collextinfo_t
		 */
		(void) fprintf(fp,
		    "static const _LC_collextinfo_t collextinfo[] = {\n");

		if (ext->ext_wgtstrsz != 0) {
			(void) fprintf(fp, "\t%d,\n", ext->ext_wgtstrsz);
			(void) fprintf(fp, "\tweightstr,\n");
		} else {
			(void) fprintf(fp, "\t0, NULL,\n");
		}

		if (ext->ext_submap != NULL)
			(void) fprintf(fp, "\tsubs_map,\n");
		else
			(void) fprintf(fp, "\tNULL,\n");

		if (ext->ext_nsubs != 0) {
			(void) fprintf(fp, "\t%d,\n", ext->ext_nsubs);
			(void) fprintf(fp, "\texsubs,\n");
			(void) fprintf(fp, "\twexsubs,\n");
			(void) fprintf(fp, "\thsuboff,\n");
			(void) fprintf(fp, "\thsubsz,\n");
		} else {
			(void) fprintf(fp, "\t0, NULL, NULL, NULL, NULL,\n");
		}
		fp_putdig(fp, ext->ext_col_max);
		(void) fprintf(fp, "};\n");
	}
}

/*
 *  FUNCTION: gen_collate
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_collate_t locale
 *  data structure.
 */
static void
gen_collate(FILE *fp, _LC_collate_t *coll)
{
	int	i, j;
	int	num_order = coll->co_nord + coll->co_r_order;

	/* Generate code to implement collation elements. */

	coll->co_hbound = max_wchar_enc;	/* Save upper bound. */

	/*
	 * Generate subs table at first, which could alter co_coltbl.
	 */
	if (coll->co_coltbl != NULL) {
		(void) fprintf(fp,
"/*------------------------- SUBSTITUTION STR-------------------------*/\n");

		gen_subtable(fp, coll);
	}

	/*
	 * Generate the collation table.
	 */
	if (coll->co_coltbl != NULL) {
		(void) fprintf(fp,
"/*------------------------- COLLTBL WEIGHTS -------------------------*/\n");

		for (j = 0; j <= num_order; j++) {

			(void) fprintf(fp,
			    "static const wchar_t ct_wgts%d[]={\n", j);

			for (i = 0; i <= coll->co_hbound; i++) {
				if (copying[LC_COLLATE]) {
					(void) fprintf(fp, "%d,\n",
					    (int)coll->co_coltbl[j][i]);
				} else {
					(void) fprintf(fp, "%d,",
					    (int)coll->co_coltbl[i][j]);
					if (verbose > 1) {
						(void) fprintf(fp,
						    " /* %s */\n",
						    char_info(i));
					} else {
						(void) fputc('\n', fp);
					}
				}
			}

			(void) fprintf(fp, "};\n\n");
		}

		(void) fprintf(fp,
"/*------------------------- COLLTBL ---------------------------------*/\n");

		(void) fprintf(fp, "static const _LC_weight_t colltbl[] ={\n");

		for (i = 0; i <= num_order; i++)
			(void) fprintf(fp, "(_LC_weight_t)ct_wgts%d,\n", i);

		(void) fprintf(fp, "};\n\n");
	}

	(void) fprintf(fp,
"/*------------------------- COLLATE OBJECT  -------------------------*/\n");
	/* Generate sort order weights if necessary */
	if (coll->co_coltbl != NULL) {
		(void) fprintf(fp, "static const wchar_t sort[] = {\n");
		/* sort order does not have a relative order */
		for (i = 0; i <= coll->co_nord; i++) {
			fp_putcolflag(fp, coll->co_sort[i]);
			(void) fprintf(fp, "\n");
		}

		(void) fprintf(fp, "};\n");
	}

	(void) fprintf(fp,
"static const _LC_methods_collate_t native_methods_collate={\n");

	/*
	 * (void) fprintf(fp, "\t(short) %d,\n",
	 * coll->core.native_api->nmethods);
	 * (void) fprintf(fp, "\t(short) %d,\n",
	 * coll->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	for (i = 0; i < (sizeof (collate_meth) / sizeof (int)); i++) {
		fp_putmeth(fp, collate_meth[i]);
	}

	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	if (single_layer == FALSE) {
		(void) fprintf(fp,
"static const _LC_methods_collate_t user_methods_collate={\n");

		/*
		 * (void) fprintf(fp,
		 * "\t(short) %d,\n", coll->core.native_api->nmethods);
		 * (void) fprintf(fp,
		 * "\t(short) %d,\n", coll->core.native_api->ndefined);
		 */
		(void) fprintf(fp, "\t(short) 0,\n");
		(void) fprintf(fp, "\t(short) 0,\n");
		/* class methods */
		for (i = 0; i < (sizeof (collate_meth) / sizeof (int)); i++) {
			fp_putmeth(fp, TOUSER(collate_meth[i]));
		}

		/* for future use */
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
		(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
		(void) fprintf(fp, "};\n\n");
	}

	(void) fprintf(fp, "static const _LC_collate_t lc_coll = {{\n");

	/* class core */
	fp_puthdr(fp, _LC_COLLATE);
	fp_putmeth(fp, COLLATE_COLLATE_INIT); /* _LC_collate_t *(*init)(); */
	(void) fprintf(fp, "\t0,\t\t\t\t/* collate_destructor() */\n");
	/* fp_putmeth(fp, COLLATE_COLLATE_DESTRUCTOR); */
	if (single_layer == FALSE)
		fp_putsym(fp,
		    "(_LC_methods_collate_t *)&user_methods_collate");
	else
		fp_putsym(fp,
		    "(_LC_methods_collate_t *)&native_methods_collate");
	fp_putsym(fp,
	    "(_LC_methods_collate_t *)&native_methods_collate");
	fp_putsym(fp, 0);	/* void *data; */
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
	fp_putsym(fp,
	    "(_LC_charmap_t *)&lc_cmap");	/* _LC_charmap_t *charmap; */
	fp_putdig(fp, coll->co_nord);
	fp_putdig(fp, coll->co_r_order); 	/* relative order */
	fp_putdig(fp, 1); 			/* extended */
	(void) fprintf(fp, "\t{ 0 },\n"); 	/* padding */
	fp_putsym(fp, "(_LC_weight_t)sort");

	fp_putdig(fp, 0);	/* wchar_t co_wc_min; */
	fp_putdig(fp, max_wchar_enc);	/* wchar_t co_wc_max; */
	fp_putdig(fp, coll->co_hbound);	/* wchar_t co_hbound; */

	if (coll->co_coltbl != NULL) {

		(void) fprintf(fp, "\t0x%04x,\n",
		    (int)coll->co_col_min);	/* wchar_t co_col_min; */
		(void) fprintf(fp, "\t0x%04x,\n",
		    (int)coll->co_col_max);	/* wchar_t co_col_max; */

		fp_putsym(fp, "colltbl"); /* _LC_weight_t *coltbl; */
		/*
		 * We no longer create cetbl.
		 */
		fp_putsym(fp, 0);	/* _LC_collel_t *cetbl; */
	} else {

		(void) fprintf(fp, "\t0x%04x,\n", 0); /* wchar_t co_col_min; */
		(void) fprintf(fp, "\t0x%04x,\n",
		    (int)max_wchar_enc);	/* wchar_t co_col_max; */

		fp_putsym(fp, 0);	/* _LC_weight_t *coltbl; */
		fp_putsym(fp, 0);	/* _LC_collel_t *cetbl; */
	}

	fp_putdig(fp, coll->co_nsubs);		/* co_nsubs */
	if (coll->co_nsubs != 0)
		fp_putsym(fp, "substrs");	/* co_subs */
	else
		fp_putsym(fp, 0);
	if (coll->co_ext == 1 && coll->co_extinfo != NULL)
		fp_putsym(fp, "collextinfo");	/* co_extinfo */
	else
		fp_putsym(fp, 0);

	(void) fprintf(fp, "};\n\n");
}


/*
 *  FUNCTION: gen_monetary
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_monetary_t locale
 *  data structure.
 */
static void
gen_monetary(FILE *fp, _LC_monetary_t *lc_mon)
{
	int	i;

	(void) fprintf(fp,
"/*------------------------- MONETARY OBJECT  -------------------------*/\n");

	(void) fprintf(fp,
"static const _LC_methods_monetary_t native_methods_monetary={\n");

	/*
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_mon->core.native_api->nmethods);
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_mon->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	(void) fprintf(fp, "\t(char *(*) ()) 0,\n");
				/* char *(*nl_langinfo)(); */
	for (i = 0; i < (sizeof (mon_meth) / sizeof (int)); i++) {
		fp_putmeth(fp, mon_meth[i]);
	}

	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp, "static const _LC_monetary_t lc_mon={{\n");

	/* class core */
	fp_puthdr(fp, _LC_MONETARY);
	fp_putmeth(fp, MONETARY_MONETARY_INIT);
	(void) fprintf(fp, "\t0,\t\t\t\t/* monetary_destructor() */\n");
	/* fp_putmeth(fp, MONETARY_MONETARY_DESTRUCTOR); */
	fp_putsym(fp,
	    "(_LC_methods_monetary_t *)&native_methods_monetary");
	fp_putsym(fp,
	    "(_LC_methods_monetary_t *)&native_methods_monetary");
	fp_putsym(fp, 0);
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
#define	FP_PUTSTR(f, s)	fp_putstr(f, s, LC_MONETARY)
	FP_PUTSTR(fp, lc_mon->int_curr_symbol);   /* char *int_curr_symbol; */
	FP_PUTSTR(fp, lc_mon->currency_symbol);   /* char *currency_symbol; */
	FP_PUTSTR(fp, lc_mon->mon_decimal_point); /* char *mon_decimal_point; */
	FP_PUTSTR(fp, lc_mon->mon_thousands_sep); /* char *mon_thousands_sep; */
	FP_PUTSTR(fp, lc_mon->mon_grouping);	    /* char *mon_grouping; */
	FP_PUTSTR(fp, lc_mon->positive_sign);	    /* char *positive_sign; */
	FP_PUTSTR(fp, lc_mon->negative_sign);	    /* char *negative_sign; */
#undef FP_PUTSTR
	fp_putdig(fp, lc_mon->int_frac_digits);
				/* signed char int_frac_digits; */
	fp_putdig(fp, lc_mon->frac_digits); /* signed char frac_digits; */
	fp_putdig(fp, lc_mon->p_cs_precedes); /* signed char p_cs_precedes; */
	fp_putdig(fp, lc_mon->p_sep_by_space); /* signed char p_sep_by_space; */
	fp_putdig(fp, lc_mon->n_cs_precedes); /* signed char n_cs_precedes; */
	fp_putdig(fp, lc_mon->n_sep_by_space); /* signed char n_sep_by_space; */
	fp_putdig(fp, lc_mon->p_sign_posn); /* signed char p_sign_posn; */
	fp_putdig(fp, lc_mon->n_sign_posn); /* signed char n_sign_posn; */

	/* signed char int_p_cs_precedes; */
	fp_putdig(fp, lc_mon->int_p_cs_precedes);

	/* signed char int_p_sep_by_space; */
	fp_putdig(fp, lc_mon->int_p_sep_by_space);

	/* signed char int_n_cs_precedes; */
	fp_putdig(fp, lc_mon->int_n_cs_precedes);

	/* signed char int_n_sep_by_space; */
	fp_putdig(fp, lc_mon->int_n_sep_by_space);

	/* signed char int_p_sign_posn; */
	fp_putdig(fp, lc_mon->int_p_sign_posn);

	/* signed char int_n_sign_posn; */
	fp_putdig(fp, lc_mon->int_n_sign_posn);

	(void) fprintf(fp, "};\n");
}


static void
fp_puttime(FILE *fp, char *s)
{
	if (s == NULL)
		(void) fprintf(fp, "\t\"\"");
	else {
		if (copying[LC_TIME]) {
			size_t	slen;
			char	*buf, *bptr;
			unsigned char	c;

			slen = strlen(s);
			buf = MALLOC(char, slen * 4 + 1);
			bptr = buf;
			while ((c = *s++) != 0) {
				if (c != '\\' && c != '"' &&
				    isascii(c) && isprint(c))
					*bptr++ = c;
				else
					bptr += sprintf(bptr, "\\x%02x", c);
			}
			*bptr = '\0';
			(void) fprintf(fp, "\t\"%s\"", buf);
			free(buf);
		} else
			(void) fprintf(fp, "\t\"%s\"", s);
	}
}

/*
 *  FUNCTION: gen_time
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_time_t locale data structure.
 */
static void
gen_time(FILE *fp, _LC_time_t *lc_time)
{
	int i;

	if (lc_time->era) {
		(void) fprintf(fp, "static const char *era_strings[] = {\n");
		for (i = 0; lc_time->era[i]; i++) {
			fp_puttime(fp, lc_time->era[i]);
			if (lc_time->era[i + 1] != NULL)
				(void) fprintf(fp, "\n\t\""
				    ";\"\n");
			else
				(void) fprintf(fp, ",\n");
		}
		(void) fprintf(fp, "(char *)0 };\n");
			/* Terminate with NULL */
	}

	(void) fprintf(fp,
"/*-------------------------   TIME OBJECT   -------------------------*/\n");

	(void) fprintf(fp,
	    "static const _LC_methods_time_t native_methods_time={\n");

	/*
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_time->core.native_api->nmethods);
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_time->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	(void) fprintf(fp, "\t(char *(*) ()) 0,\n");
				/* char *(*nl_langinfo)(); */
	for (i = 0; i < (sizeof (time_meth) / sizeof (int)); i++) {
		fp_putmeth(fp, time_meth[i]);
	}

	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp, "static const _LC_time_t lc_time={{\n");

	/* class core */
	fp_puthdr(fp, _LC_TIME);
	fp_putmeth(fp, TIME_TIME_INIT); /* _LC_time_t *(*init)() */
	(void) fprintf(fp, "\t0,\t\t\t\t/* time_destructor() */\n");
	/* fp_putmeth(fp, TIME_TIME_DESTRUCTOR); */
	fp_putsym(fp,
	    "(_LC_methods_time_t *)&native_methods_time");
	fp_putsym(fp,
	    "(_LC_methods_time_t *)&native_methods_time");
	fp_putsym(fp, 0);	/* void *data; */
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
#define	FP_PUTSTR(f, s)	fp_putstr(f, s, LC_TIME)

	FP_PUTSTR(fp, lc_time->d_fmt);	/* char *d_fmt; */
	FP_PUTSTR(fp, lc_time->t_fmt);	/* char *t_fmt; */
	FP_PUTSTR(fp, lc_time->d_t_fmt);	/* char *d_t_fmt; */
	FP_PUTSTR(fp, lc_time->t_fmt_ampm);	/* char *t_fmt_ampm; */
	(void) fprintf(fp, "\t{\n");
	for (i = 0; i < 7; i++)
		FP_PUTSTR(fp, lc_time->abday[i]);    /* char *abday[7]; */
	(void) fprintf(fp, "\t},\n");
	(void) fprintf(fp, "\t{\n");
	for (i = 0; i < 7; i++)
		FP_PUTSTR(fp, lc_time->day[i]);	/* char *day[7]; */
	(void) fprintf(fp, "\t},\n");
	(void) fprintf(fp, "\t{\n");
	for (i = 0; i < 12; i++)
		FP_PUTSTR(fp, lc_time->abmon[i]);    /* char *abmon[12]; */
	(void) fprintf(fp, "\t},\n");
	(void) fprintf(fp, "\t{\n");
	for (i = 0; i < 12; i++)
		FP_PUTSTR(fp, lc_time->mon[i]);	/* char *mon[12]; */
	(void) fprintf(fp, "\t},\n");
	(void) fprintf(fp, "\t{\n");
	for (i = 0; i < 2; i++)
		FP_PUTSTR(fp, lc_time->am_pm[i]);    /* char *am_pm[2]; */
	(void) fprintf(fp, "\t},\n");

	if (lc_time->era)
		fp_putsym(fp,
		    "era_strings"); /* There is an array of eras */
	else
		fp_putsym(fp, 0);	/* No eras available */

	FP_PUTSTR(fp, lc_time->era_d_fmt);	/* char *era_d_fmt; */

	if (lc_time->alt_digits == (char *)NULL)  /* char *alt_digits */
		(void) fprintf(fp, "\t\"\",\n");
	else {
		if (copying[LC_TIME]) {
			unsigned char	c;

			(void) fprintf(fp, "\t\"");
			for (i = 0; ((c = lc_time->alt_digits[i]) != 0); i++) {
				if (c == ';') {
					(void) fprintf(fp,
					    "\"\n\t\t\";\"\n\t\t\"");
					continue;
				}
				if (c != '\\' && c != '"' &&
				    isascii(c) && isprint(c)) {
					(void) fputc(c, fp);
				} else {
					(void) fprintf(fp, "\\x%02x", c);
					(void) fprintf(fp, "\"\"");
				}
			}
			(void) fprintf(fp, "\""
			    ",\n");
		} else {
			(void) fprintf(fp, "\t\"");
			for (i = 0; lc_time->alt_digits[i] != NULL; i++) {
				if (lc_time->alt_digits[i] != ';')
					(void) fputc(lc_time->alt_digits[i],
					    fp);
				else
					(void) fprintf(fp,
					    "\"\n\t\t\";\"\n\t\t\"");
			}
			(void) fprintf(fp, "\""
			    ",\n");
		}
	}
	FP_PUTSTR(fp, lc_time->era_d_t_fmt);	/* char *era_d_t_fmt */
	FP_PUTSTR(fp, lc_time->era_t_fmt);	/* char *era_t_fmt */
	FP_PUTSTR(fp, lc_time->date_fmt);	/* char *date_fmt */
	(void) fprintf(fp, "};\n");
#undef	FP_PUTSTR
}


/*
 *  FUNCTION: gen_numeric
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_numeric_t locale
 *  data structure.
 */
static void
gen_numeric(FILE *fp, _LC_numeric_t *lc_num)
{
	(void) fprintf(fp,
"/*------------------------- NUMERIC OBJECT  -------------------------*/\n");

	(void) fprintf(fp,
"static const _LC_methods_numeric_t native_methods_numeric={\n");

	/*
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_num->core.native_api->nmethods);
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_num->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	(void) fprintf(fp, "\t(char *(*) ()) 0,\n");
				/* char *(*nl_langinfo)(); */
	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp, "static const _LC_numeric_t lc_num={{\n");

	/* class core */
	fp_puthdr(fp, _LC_NUMERIC);
	fp_putmeth(fp, NUMERIC_NUMERIC_INIT);
	(void) fprintf(fp, "\t0,\t\t\t\t/* numeric_destructor() */\n");
	/* fp_putmeth(fp, NUMERIC_NUMERIC_DESTRUCTOR); */
	fp_putsym(fp,
	    "(_LC_methods_numeric_t *)&native_methods_numeric");
	fp_putsym(fp,
	    "(_LC_methods_numeric_t *)&native_methods_numeric");
	fp_putsym(fp, 0);			   /* void *data; */
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
#define	FP_PUTSTR(f, s)	fp_putstr(f, s, LC_NUMERIC)
	FP_PUTSTR(fp, lc_num->decimal_point);
	FP_PUTSTR(fp, lc_num->thousands_sep);	    /* char *thousands_sep; */
	FP_PUTSTR(fp, lc_num->grouping);
#undef	FP_PUTSTR
	(void) fprintf(fp, "};\n\n");
}


/*
 *  FUNCTION: gen_msg
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_messages_t locale data structure.
 */
static void
gen_msg(FILE *fp, _LC_messages_t *lc_messages)
{
	(void) fprintf(fp,
"/*------------------------- MESSAGE OBJECT  -------------------------*/\n");

	(void) fprintf(fp,
"static const _LC_methods_messages_t native_methods_messages={\n");

	/*
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_messages->core.native_api->nmethods);
	 * (void) fprintf(fp,
	 * "\t(short) %d,\n", lc_messages->core.native_api->ndefined);
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	(void) fprintf(fp, "\t(char *(*) ()) 0,\n");
			/* char *(*nl_langinfo)(); */
	/* fore future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp, "static const _LC_messages_t lc_messages={{\n");

	/* class core */
	fp_puthdr(fp, _LC_MESSAGES);
	fp_putmeth(fp, MESSAGES_MESSAGES_INIT);
	(void) fprintf(fp, "\t0,\t\t\t\t/* messages_destructor() */\n");
	/* fp_putmeth(fp, MESSAGES_MESSAGES_DESTRUCTOR); */
	fp_putsym(fp,
	    "(_LC_methods_messages_t *)&native_methods_messages");
	fp_putsym(fp,
	    "(_LC_methods_messages_t *)&native_methods_messages");
	fp_putsym(fp, 0);		/* void *data; */
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* class extension */
#define	FP_PUTSTR(f, s)	fp_putstr(f, s, LC_MESSAGES)
	FP_PUTSTR(fp, lc_messages->yesexpr);	/* char *yesexpr; */
	FP_PUTSTR(fp, lc_messages->noexpr);	/* char *noexpr; */
	FP_PUTSTR(fp, lc_messages->yesstr);	/* char *yesstr; */
	FP_PUTSTR(fp, lc_messages->nostr);	/* char *nostr; */
#undef	FP_PUTSTR
	(void) fprintf(fp, "};\n\n");
}


/*
 *  FUNCTION: gen_locale
 *
 *  DESCRIPTION:
 *  Generate C code which implements the _LC_locale_t locale
 *  data structures. This routine collects the data from the various
 *  child classes of _LC_locale_t, and outputs the pieces from the child
 *  classes as appropriate.
 */
static void
gen_locale(FILE *fp, _LC_charmap_t *lc_cmap, _LC_numeric_t *lc_num,
    _LC_monetary_t *lc_mon, _LC_time_t *lc_time, _LC_messages_t *lc_messages)
{
	int i;

	(void) fprintf(fp,
"/*--------------------------- LOCALE DEFS --------------------------*/\n");
	(void) fprintf(fp, "typedef struct {\n");
	(void) fprintf(fp, "	_LC_core_locale_t	core;\n\n");
	(void) fprintf(fp, "	struct lconv	*nl_lconv;\n\n");
	(void) fprintf(fp, "	_LC_charmap_t	*lc_charmap;\n");
	(void) fprintf(fp, "	_LC_collate_t	*lc_collate;\n");
	(void) fprintf(fp, "	_LC_ctype_t	*lc_ctype;\n");
	(void) fprintf(fp, "	_LC_monetary_t	*lc_monetary;\n");
	(void) fprintf(fp, "	_LC_numeric_t	*lc_numeric;\n");
	(void) fprintf(fp, "	_LC_messages_t	*lc_messages;\n");
	(void) fprintf(fp, "	_LC_time_t	*lc_time;\n\n");
	(void) fprintf(fp, "	int	no_of_items;\n");
	(void) fprintf(fp, "	const char	*nl_info[_NL_NUM_ITEMS];\n");
	(void) fprintf(fp, "} _lc_locale_t;\n\n");

	if ((lc_numeric_flag != 0) &&
	    (lc_monetary_flag != 0)) {
		(void) fprintf(fp,
"/*--------------------------- LCONV STRUCT --------------------------*/\n");

		(void) fprintf(fp,
		    "static const struct lconv	lc_lconv = {\n");
		fp_putstr(fp, lc_num->decimal_point, LC_NUMERIC);
		fp_putstr(fp, lc_num->thousands_sep, LC_NUMERIC);
		fp_putstr(fp, lc_num->grouping, LC_NUMERIC);
		fp_putstr(fp, lc_mon->int_curr_symbol, LC_MONETARY);
		fp_putstr(fp, lc_mon->currency_symbol, LC_MONETARY);
		fp_putstr(fp, lc_mon->mon_decimal_point, LC_MONETARY);
		fp_putstr(fp, lc_mon->mon_thousands_sep, LC_MONETARY);
		fp_putstr(fp, lc_mon->mon_grouping, LC_MONETARY);
		fp_putstr(fp, lc_mon->positive_sign, LC_MONETARY);
		fp_putstr(fp, lc_mon->negative_sign, LC_MONETARY);
		fp_putdig(fp, lc_mon->int_frac_digits);
		fp_putdig(fp, lc_mon->frac_digits);
		fp_putdig(fp, lc_mon->p_cs_precedes);
		fp_putdig(fp, lc_mon->p_sep_by_space);
		fp_putdig(fp, lc_mon->n_cs_precedes);
		fp_putdig(fp, lc_mon->n_sep_by_space);
		fp_putdig(fp, lc_mon->p_sign_posn);
		fp_putdig(fp, lc_mon->n_sign_posn);
		fp_putdig(fp, lc_mon->int_p_cs_precedes);
		fp_putdig(fp, lc_mon->int_p_sep_by_space);
		fp_putdig(fp, lc_mon->int_n_cs_precedes);
		fp_putdig(fp, lc_mon->int_n_sep_by_space);
		fp_putdig(fp, lc_mon->int_p_sign_posn);
		fp_putdig(fp, lc_mon->int_n_sign_posn);
		(void) fprintf(fp, "};\n\n");
	}

	(void) fprintf(fp,
"/*-------------------------- LOCALE OBJECT --------------------------*/\n");

	(void) fprintf(fp,
	    "static const _LC_methods_locale_t native_methods_locale={\n");

	/*
	 * (void) fprintf(fp, "\t(short) 7,\n");
	 * (void) fprintf(fp, "\t(short) 2,\n");
	 */
	(void) fprintf(fp, "\t(short) 0,\n");
	(void) fprintf(fp, "\t(short) 0,\n");
	/* class methods */
	fp_putmeth(fp, LOCALE_NL_LANGINFO);
	fp_putmeth(fp, LOCALE_LOCALECONV);
	/* for future use */
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s,\n", PLACEHOLDER_STRING);
	(void) fprintf(fp, "\t%s\n",  PLACEHOLDER_STRING);
	(void) fprintf(fp, "};\n\n");

	(void) fprintf(fp, "static const _lc_locale_t lc_loc={{\n");

	/* class core */
	fp_puthdr(fp, _LC_LOCALE);
	fp_putmeth(fp, LOCALE_LOCALE_INIT);
	(void) fprintf(fp, "\t0,\t\t\t\t/* locale_destructor() */\n");
	/* fp_putmeth(fp, LOCALE_LOCALE_DESTRUCTOR); */
	fp_putsym(fp,
	    "(_LC_methods_locale_t *)&native_methods_locale");
	fp_putsym(fp,
	    "(_LC_methods_locale_t *)&native_methods_locale");
	fp_putsym(fp, 0);
	(void) fprintf(fp, "\t},\t/* End core */\n");

	/* -- lconv structure -- */
	if ((lc_numeric_flag != 0) &&
	    (lc_monetary_flag != 0)) {
		fp_putsym(fp,
"(struct lconv *)&lc_lconv");	/* struct lconv *lc_lconv; */
	} else {
		fp_putsym(fp, 0);
	}

	/* pointers to other classes */
	fp_putsym(fp, "(_LC_charmap_t *)&lc_cmap");
			/* _LC_charmap_t *charmap; */
	if (lc_collate_flag != 0) {
		fp_putsym(fp, "(_LC_collate_t *)&lc_coll");
			/* _LC_collate_t *collate; */
	} else {
		fp_putsym(fp, 0);
	}
	if (lc_ctype_flag != 0) {
		fp_putsym(fp, "(_LC_ctype_t *)&lc_ctype");
			/* _LC_ctype_t *ctype; */
	} else {
		fp_putsym(fp, 0);
	}
	if (lc_monetary_flag != 0) {
		fp_putsym(fp, "(_LC_monetary_t *)&lc_mon");
			/* _LC_monetary_t *monetary; */
	} else {
		fp_putsym(fp, 0);
	}
	if (lc_numeric_flag != 0) {
		fp_putsym(fp, "(_LC_numeric_t *)&lc_num");
			/* _LC_numeric_t *numeric; */
	} else {
		fp_putsym(fp, 0);
	}

	if (lc_message_flag != 0) {
		fp_putsym(fp, "(_LC_messages_t *)&lc_messages");
			/* _LC_messages_t *messages; */
	} else {
		fp_putsym(fp, 0);
	}
	if (lc_time_flag != 0) {
		fp_putsym(fp, "(_LC_time_t *)&lc_time");
			/* _LC_time_t *time; */
	} else {
		fp_putsym(fp, 0);
	}

	/* class extension */
	(void) fprintf(fp, "\t%d,\n", _NL_NUM_ITEMS);

#define	PUT_NL(flag, str, cat) \
	{ \
		if (flag != 0) { \
			fp_putstr(fp, str, cat); \
		} else { \
			fp_putsym(fp, 0); \
		} \
	}

	(void) fprintf(fp, "\t{");
			/* Bracket array of nl_langinfo stuff */
	(void) fprintf(fp, "\"\",			/* not used */\n");
	(void) fprintf(fp, "\t/* DAY_1 - DAY_7 */\n");
	for (i = 0; i < 7; i++) {
		PUT_NL(lc_time_flag, lc_time->day[i], LC_TIME);
	}

	(void) fprintf(fp, "\t/* ABDAY_1 - ABDAY_7 */\n");
	for (i = 0; i < 7; i++) {
		PUT_NL(lc_time_flag, lc_time->abday[i], LC_TIME);
	}

	(void) fprintf(fp, "\t/* MON_1 - MON_12 */\n");
	for (i = 0; i < 12; i++) {
		PUT_NL(lc_time_flag, lc_time->mon[i], LC_TIME);
	}

	(void) fprintf(fp, "\t/* ABMON_1 - ABMON_12 */\n");
	for (i = 0; i < 12; i++) {
		PUT_NL(lc_time_flag, lc_time->abmon[i], LC_TIME);
	}

	(void) fprintf(fp, "\t/* RADIXCHAR */\n");
	PUT_NL(lc_numeric_flag, lc_num->decimal_point, LC_NUMERIC);

	(void) fprintf(fp, "\t/* THOUSEP */\n");
	PUT_NL(lc_numeric_flag, lc_num->thousands_sep, LC_NUMERIC);

	(void) fprintf(fp, "\t/* YESSTR */\n");
	PUT_NL(lc_message_flag, lc_messages->yesstr, LC_MESSAGES);

	(void) fprintf(fp, "\t/* NOSTR */\n");
	PUT_NL(lc_message_flag, lc_messages->nostr, LC_MESSAGES);

	(void) fprintf(fp, "\t/* CRNCYSTR */\n");
	PUT_NL(lc_monetary_flag, lc_mon->currency_symbol, LC_MONETARY);

	(void) fprintf(fp, "\t/* D_T_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->d_t_fmt, LC_TIME);

	(void) fprintf(fp, "\t/* D_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->d_fmt, LC_TIME);

	(void) fprintf(fp, "\t/* T_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->t_fmt, LC_TIME);

	(void) fprintf(fp, "\t/* AM_STR/PM_STR */\n");
	for (i = 0; i < 2; i++) {
		PUT_NL(lc_time_flag, lc_time->am_pm[i], LC_TIME);
	}

	(void) fprintf(fp, "\t/* CODESET */\n");
	fp_putstr(fp, lc_cmap->cm_csname, 0);

	(void) fprintf(fp, "\t/* T_FMT_AMPM */\n");
	PUT_NL(lc_time_flag, lc_time->t_fmt_ampm, LC_TIME);

	(void) fprintf(fp, "\t/* ERA */\n");
	if (lc_time_flag != 0) {
		if (lc_time->era[0] == NULL) {
			(void) fprintf(fp, "\t\"\",\n");
		} else {
			for (i = 0; lc_time->era[i]; i++) {
				fp_puttime(fp, lc_time->era[i]);
				if (lc_time->era[i + 1] != NULL)
					(void) fprintf(fp, "\n\t\""
					    ";\"\n");
				else
					(void) fprintf(fp, ",\n");
			}
		}
	} else {
		fp_putsym(fp, 0);
	}

	(void) fprintf(fp, "\t/* ERA_D_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->era_d_fmt, LC_TIME);

	(void) fprintf(fp, "\t/* ERA_D_T_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->era_d_t_fmt, LC_TIME);

	(void) fprintf(fp, "\t/* ERA_T_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->era_t_fmt, LC_TIME);

	(void) fprintf(fp, "\t/* ALTDIGITS */\n");
	if (lc_time_flag != 0) {
		if (lc_time->alt_digits == NULL) {
			(void) fprintf(fp, "\t\"\",\n");
		} else {
			if (copying[LC_TIME]) {
				unsigned char	c;

				(void) fprintf(fp, "\t\"");
				for (i = 0;
				    ((c = lc_time->alt_digits[i]) != 0); i++) {
					if (c == ';') {
						(void) fprintf(fp,
						    "\"\n\t\t\";\"\n\t\t\"");
						continue;
					}
					if (c != '\\' && c != '"' &&
					    isascii(c) && isprint(c)) {
						(void) fputc(c, fp);
					} else {
						(void) fprintf(fp,
						    "\\x%02x", c);
						(void) fprintf(fp, "\"\"");
					}
				}
				(void) fprintf(fp, "\""
				    ",\n");
			} else {
				(void) fprintf(fp, "\t\"");
				for (i = 0; lc_time->alt_digits[i] != NULL;
				    i++) {
					if (lc_time->alt_digits[i] != ';')
						(void) fputc(
						    lc_time->alt_digits[i],
						    fp);
					else
						(void) fprintf(fp,
						    "\"\n\t\t\";\"\n\t\t\"");
				}
				(void) fprintf(fp, "\""
				    ",\n");
			}
		}
	} else {
		fp_putsym(fp, 0);
	}

	(void) fprintf(fp, "\t/* YESEXPR */\n");
	PUT_NL(lc_message_flag, lc_messages->yesexpr, LC_MESSAGES);

	(void) fprintf(fp, "\t/* NOEXPR */\n");
	PUT_NL(lc_message_flag, lc_messages->noexpr, LC_MESSAGES);

	(void) fprintf(fp, "\t/* _DATE_FMT */\n");
	PUT_NL(lc_time_flag, lc_time->date_fmt, LC_TIME);

	(void) fprintf(fp, "\t},\n");

	(void) fprintf(fp, "};\n\n");
}


/*
 *  FUNCTION: gen_instantiate
 *
 *  DESCRIPTION:
 *  Generates C code which returns address of lc_locale and serves as
 *  entry point to object.
 */
static void
gen_instantiate(FILE *fp)
{
	(void) fprintf(fp,
	    "_LC_locale_t *instantiate(void)\n{\n");
	(void) fprintf(fp,
	    "\treturn ((_LC_locale_t *)&lc_loc);\n}\n");
}
