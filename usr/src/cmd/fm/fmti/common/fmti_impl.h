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

#ifndef _FMTI_IMPL_H
#define	_FMTI_IMPL_H

#include <bay.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* sysevent */
#define	EC_CRO			"EC_cro"
#define	ESC_CRO_TOPOREFRESH	"ESC_cro_toporefresh"

/* light off; light on */
#define	LED_OFF			0x0
#define	LED_ON			0x1

/* Prototypes */
int   blink_locate(di_node_t, int, int, thread_t *);
void  dprintf(const char *, ...);
char *get_ch_label(void);
char *get_bay_label(di_node_t, int);
char *get_extch_prod(void);
char *get_extch_sn(void);
char *get_hba_label(di_node_t);
char *get_ich_sn(void);
int   get_product(char *, char *, char *);
void  fmti_pr_hdr(void);
void  fmti_pr_hdr1(char *);
void  hba_pr_hdr(char *, di_node_t);
void  pr_ebay_note(void);
void  pr_find_bay(void);
void  pr_summary(char *);
int   sort_hba_nodes(di_node_t *);
char *verify_ans(char *);
void  wr_config(bay_t *, char *);
int   wr_hdr(char *, char *);

#ifdef __cplusplus
}
#endif

#endif /* _FMTI_IMPL_H */
