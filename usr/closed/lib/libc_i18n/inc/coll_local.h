/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	__H_COLL_LOCAL
#define	__H_COLL_LOCAL

#include <sys/localedef.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	COLL_OUT_AUTOSZ	4

typedef struct coll_output {
	wchar_t	*out;		/* output buffer */
	size_t	olen;		/* length of weights */
	size_t	obsize;		/* size of buffer */
	int	nignore;	/* number of IGNOREs in the output buffer */
	int	pos;		/* string position where wgts were taken */
	char	count_only;	/* count weights only */
	char	pad[3];
	int	error;		/* error indicator */
	wchar_t	sbuf[COLL_OUT_AUTOSZ];
				/* scratch buffer for small output */
} coll_output_t;

#define	CLF_INIT_SUBMAP	0x0001
#define	CLF_EXTINFO	0x0002
#define	CLF_SUBS	0x0004
#define	CLF_WGTSTR	0x0008
#define	CLF_SIMPLE	0x0010
#define	CLF_SUBCHK	0x0020

typedef struct coll_locale {
	_LC_collate_t	*hdl;
	uint_t		flag;
	const _LC_collextinfo_t *extinfo;	/* extended info */
	const char	*submap;	/* quick lookup map */
	const char	*wgtstrmap;	/* quick weightstr map */
	const wchar_t	*wgtstr;	/* weight string */
	char		map[256];	/* scratch buffer for lookup map */
} coll_locale_t;

#define	CCF_ALLOC	0x0001
#define	CCF_CONVMB	0x0002
#define	CCF_CONVWC	0x0004
#define	CCF_MBSTR	0x0008
#define	CCF_WIDE	0x0010
#define	CCF_BC		0x0020
#define	CCF_NOSUB	0x0040
#define	CCF_SIMPLE	0x0080
#define	CCF_ATTACHED	0x0100

typedef struct coll_cookie {
	coll_locale_t	*loc;
	coll_output_t	co;		/* output stream */
	uint_t		flag;		/* flags */
	size_t		allocsz;	/* size allocaed for buffer below */
	union {
		const char	*str;
		const wchar_t	*wstr;
	} data;
	size_t		inlen;		/* length of attached string */
} coll_cookie_t;

extern int coll_compare_wc(coll_cookie_t *, coll_cookie_t *, int);
extern int coll_compare_sb(coll_cookie_t *, coll_cookie_t *, int);
extern int coll_compare_std(coll_cookie_t *, coll_cookie_t *, int);
extern int coll_wgt_comp(coll_output_t *, coll_output_t *);
extern int coll_wgt_pos_comp(coll_output_t *, coll_output_t *);
extern int coll_wgtcmp(const wchar_t *, const wchar_t *);

extern int coll_str2weight_sb(coll_cookie_t *, int);
extern int coll_str2weight_std(coll_cookie_t *, int);
extern int coll_chr2weight_sb(coll_cookie_t *, const char *, int);
extern int coll_chr2weight_std(coll_cookie_t *, const char *, int);

extern int coll_wstr2weight(coll_cookie_t *, int);
extern int coll_wchr2weight(coll_cookie_t *, const wchar_t *, int);

#define	MIN_WEIGHT	0x01010101
#define	WEIGHT_IGNORE	L'\0'
#define	WGTSTR_IDX(x)	(-(x))
#define	_STACK_THR	(512)


extern int coll_format_collate(coll_output_t *, int);
extern void coll_output_init(coll_output_t *);
extern int coll_output_add(coll_output_t *, wchar_t);
extern int coll_output_add_slow(coll_output_t *, wchar_t);
extern void coll_output_clean(coll_output_t *);
extern void coll_output_fini(coll_output_t *);
extern void coll_output_shift(coll_output_t *, int);
extern int coll_all_ignore(coll_output_t *);

#ifndef lint
#define	coll_all_ignore(op)	((op)->nignore == (op)->olen)

#define	coll_output_add(op, wt)	\
	(op->count_only ? \
		((wt != WEIGHT_IGNORE) ? (op->olen++, 0) : 0) : \
	(op->olen == op->obsize ? coll_output_add_slow(op, wt) : \
		((op->nignore += (wt == WEIGHT_IGNORE)), \
		op->out[op->olen++] = wt, 0)))

#define	coll_output_clean(x)	((x)->olen = (x)->nignore = 0)
#endif

extern void coll_locale_init(coll_locale_t *, _LC_collate_t *);
extern void coll_cookie_init(coll_cookie_t *, coll_locale_t *, int);
extern void coll_cookie_fini(coll_cookie_t *);

extern size_t coll_conv_calc_size(coll_cookie_t *);
extern const wchar_t *coll_conv_input_real(coll_cookie_t *, void *);
extern const wchar_t *coll_conv_input(coll_cookie_t *);

#ifndef lint
#define	coll_conv_input(c) \
	coll_conv_input_real((c), \
	(coll_conv_calc_size(c) != 0 ? \
	malloc((c)->allocsz) : \
	((c)->allocsz == 0 ? (void *)((c)->data.str) : \
	alloca((c)->allocsz))))
#endif

extern size_t coll_wgt_width(coll_locale_t *);

extern size_t coll_store_weight(int nbpw, char *strout,
    size_t cur, size_t max, coll_output_t *co, boolean_t output_wide);

#ifdef	__cplusplus
}
#endif

#endif	/* __H_COLL_LOCAL */
