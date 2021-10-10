/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#ifndef _IMPL_H
#define	_IMPL_H

#include <labeld.h>
#include <door.h>
#include <synch.h>
#include <thread.h>

#include <sys/types.h>
#include <sys/tsol/label_macro.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* shared labeld server variables */

extern mutex_t		gfi_lock;	/* gfi code lock */
extern int		debug;		/* debug flag, 0 if disabled */

/* labeld server manifest constants */

#define	MAX_THREADS	10		/* default max door threads */

#define	ENCODINGS_PATH	"/etc/security/tsol/"
#define	ENCODINGS_NAME	"label_encodings"
/* l_init manifest constants for field size checking */

#define	MAX_CLASS	255	/* maximum value a classification can have */
#define	MAX_COMPS	255	/* maximum value a compartment bit can have */
#define	MAX_MARKS	255	/* maximum value a marking bit can have */

/* shared local definitions variables (l_eof.c) */

#ifdef	_STD_LABELS_H
/* only if std_labels.h is included */

/* word to color name mapping */

typedef struct color_word_entry {
	struct color_word_entry *next;		/* link to next entry */
	COMPARTMENT_MASK	mask;		/* mask for this word */
	COMPARTMENTS		comps;		/* bits for this word */
	char			*color;		/* the color itself */
} cwe_t;

extern cwe_t	*color_word;		/* linked list of color words	*/

/* label to color name mapping */

typedef struct color_table_entry {
	struct color_table_entry *next;		/* link to next entry */
	_mac_label_impl_t	level;		/* label of this entry */
	char		*color;			/* the color itself */
} cte_t;

extern cte_t	**color_table;		/* array of color names		*/
					/* indexed by Classification	*/
extern char	*low_color;		/* color name for Admin Low	*/
extern char	*high_color;		/* color name for Admin High	*/

#endif	/* _STD_LABELS_H */

typedef struct _binary_cmw_label_impl bclabel_t; /* CMW Label */

/* label prototypes for comparison */

extern m_label_t m_low;		/* Admin Low label prototype */
extern m_label_t m_high;	/* Admin High label prototype */

/*
 * XXX these two should replace the temporary m_low/high above once
 * life settles down as m_label_t admin_low/high.
 */
extern bclabel_t admin_low;	/* the Admin Low label prototype */
extern bclabel_t admin_high;	/* the Admin High label prototype */

extern m_label_t  clear_low;	/* Admin Low Clearance prototype */
extern m_label_t  clear_high;	/* Admin High Clearance prototype */

/* guaranteed input translation for */

extern char *Admin_low;			/* Admin Low */
extern char *Admin_high;		/* Admin High */
extern int  Admin_low_size;		/* size of string */
extern int  Admin_high_size;		/* size of string */

/* names for label builder fields */

extern char *Class_name;		/* Classification field */
extern char *Comps_name;		/* Compartments field */
extern char *Marks_name;		/* Markings field */

/* translation flags */

extern int	view_label;	/* TRUE, external labels, FALSE, o'e */

extern ushort_t	default_flags;	/* default translation flags */
extern ushort_t	forced_flags;	/* forced translation flags */

/* default user label range */

extern bslabel_t	def_user_sl;	/* default user SL */
extern bclear_t		def_user_clear;	/* default user Clearance */

/* local definitions manifest constants (l_eof.c) */

#define	CLASS_NAME "CLASSIFICATION"
#define	COMPS_NAME "COMPARTMENTS"
#define	MARKS_NAME "MARKINGS"

/* utility function prototypes */

int check_bounds(const ucred_t *, _mac_label_impl_t *);
ushort_t get_gfi_flags(uint_t, const ucred_t *);

/* server function prototypes */

/* Miscellaneous */

void inset(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void slvalid(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void clearvalid(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void info(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void vers(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void color(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* Binary to String Label Translation */

void sltos(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void cleartos(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* String to Binary Label Translation */

void stosl(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void stoclear(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* Dimming List Routines; Contract private for label builders */

void slcvt(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void clearcvt(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void fields(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);
void udefs(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* File labeling routines based on mount points */

void setflbl(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* New stable interfaces */

/* DIA printer banner labels */
void prtos(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* DIA label to string */
void ltos(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* DIA string to label */
void stol(labeld_call_t *, labeld_ret_t *, size_t *, const ucred_t *);

/* used by server side label translation interfaces */

/* short hands */

#define	IS_ADMIN_LOW(sl) \
	((strncasecmp(sl, ADMIN_LOW, (sizeof (ADMIN_LOW) - 1)) == 0) || \
	(strncasecmp(sl, Admin_low, Admin_low_size) == 0))

#define	IS_ADMIN_HIGH(sh) \
	((strncasecmp(sh, ADMIN_HIGH, (sizeof (ADMIN_HIGH) - 1)) == 0) || \
	(strncasecmp(sh, Admin_high, Admin_high_size) == 0))


#define	GFI_FLAGS(f) (((((f->paf_label_xlate)>>XLATE_SHIFT)&GFI_FLAG_MASK)? \
	(((f->paf_label_xlate)>>XLATE_SHIFT)&GFI_FLAG_MASK): \
	default_flags)|forced_flags)

#define	VIEW(f)	(((f)&(LABELS_VIEW_EXTERNAL|LABELS_VIEW_INTERNAL))? \
	((f)&LABELS_VIEW_EXTERNAL):(view_label))

/* Promote / Demote SL, CLR */

#define	PSL(sl) _PSL((_bslabel_impl_t *)(sl))
#define	_PSL(sl) LCLASS_SET((sl), l_lo_sensitivity_label->l_classification); \
		(sl)->_comps = *(Compartments_t *) \
				l_lo_sensitivity_label->l_compartments;

#define	DSL(sl) _DSL((_bslabel_impl_t *)(sl))
#define	_DSL(sl) LCLASS_SET((sl), l_hi_sensitivity_label->l_classification); \
		(sl)->_comps = *(Compartments_t *) \
				l_hi_sensitivity_label->l_compartments;

#define	PIL(il) _PIL((_bilabel_impl_t *)(il))
#define	_PIL(il) ICLASS_SET((il), *l_min_classification); \
		(il)->_icomps = *(Compartments_t *) \
				l_li_compartments; \
		(il)->_imarks = *(Markings_t *) \
				l_li_markings;
#define	DIL(il) _DIL((_bilabel_impl_t *)(il))
#define	_DIL(il)	ICLASS_SET((il), \
				l_hi_sensitivity_label->l_classification); \
		(il)->_icomps = *(Compartments_t *) \
				l_hi_sensitivity_label->l_compartments; \
		(il)->_imarks = *(Markings_t *)l_hi_markings;
#define	PCLR(clr) _PCLR((_bclear_impl_t *)(clr))
#define	_PCLR(clr) LCLASS_SET((clr), l_lo_clearance->l_classification); \
			(clr)->_comps = *(Compartments_t *) \
				l_lo_clearance->l_compartments;
#define	DCLR(clr) _DCLR((_bclear_impl_t *)(clr))
#define	_DCLR(clr) LCLASS_SET((clr), \
				l_hi_sensitivity_label->l_classification); \
			(clr)->_comps = *(Compartments_t *) \
				l_hi_sensitivity_label->l_compartments;

/* Create Manifest Labels */

/* Write a System_High CMW Label into this memory. */
#define	BCLHIGH(l) (BSLHIGH(BCLTOSL(l)), BILHIGH(BCLTOIL(l)))

/* Write a System_High Information Label into this memory. */
#define	BILHIGH(l) _BILHIGH((_bilabel_impl_t *)(l))

#define	_BILHIGH(l) \
	((l)->_iid = SUN_IL_ID, (l)->_i_c_len = _C_LEN, \
	ICLASS_SET(l, HIGH_CLASS), \
	(l)->_icomps.c1 = (l)->_icomps.c2 = (l)->_icomps.c3 = \
	(l)->_icomps.c4 = (l)->_icomps.c5 = (l)->_icomps.c6 = \
	(l)->_icomps.c7 = (l)->_icomps.c8 = UNIVERSAL_SET, \
	(l)->_imarks.m1 = (l)->_imarks.m2 = (l)->_imarks.m3 = \
	(l)->_imarks.m4 = (l)->_imarks.m5 = (l)->_imarks.m6 = \
	(l)->_imarks.m7 = (l)->_imarks.m8 = UNIVERSAL_SET)

#ifdef	__cplusplus
}
#endif

#endif	/* _IMPL_H */
