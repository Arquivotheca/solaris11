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

#ifndef _SYS_FM_IO_USB_H
#define	_SYS_FM_IO_USB_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	USB_ERROR_SUBCLASS	"usb"

/* Common USB ereport classes */
#define	USB_FM_CONN_FAIL	"con_fail"
#define	USB_FM_CONN_FAIL_POC	"con_fail_poc"	/* Hub power over current */
#define	USB_FM_CONN_FAIL_PSE	"con_fail_pse"	/* Other hub error */


#define	USB_FM_REQ_TO		"req_to"	/* command timed out */
#define	USB_FM_DTE		"dte"		/* d/t PID did not match */
#define	USB_FM_EPSE		"epse"		/* e/p returned stall PID */
#define	USB_FM_DNR		"dnr"		/* device not responding */
#define	USB_FM_PIDE		"pide"		/* receive PID was not valid */


#define	USB_FM_DUE		"due"		/* less data received */
#define	USB_FM_DOE		"doe"		/* data size exceeded */
#define	USB_FM_DE		"de"		/* data error */

#define	USB_FM_BUE		"bue"		/* buffer underrun */
#define	USB_FM_BOE		"boe" 		/* memory write can't keep up */
#define	USB_FM_BE		"be"		/* buffer error */

#define	USB_FM_USE		"use"		/* unspecified usba/hcd err */
#define	USB_FM_NAE		"nae"		/* Not accessed by hardware */
#define	USB_FM_NRE		"nre"		/* no resources	*/
#define	USB_FM_NSE		"nse"		/* command not supported */

#define	USB_FM_PE		"pe"		/* Pipe error */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FM_IO_USB_H */
