/*
 * Copyright (c) 1998-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * Copyright (c) 1992-1998 NCR Corporation, Dayton, Ohio USA
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/mkdev.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/log.h>
#include <sys/stropts.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/stat.h>
#include <sys/ddi.h>

#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/strlog.h>
#include <sys/log.h>
#include <sys/llc2.h>
#include "ild.h"
#include "llc2k.h"
#include "ildlock.h"
#include <sys/dlpi.h>

#define	Kernel_sprintf	sprintf
#define	E_TextFormat	"%d|%s|%c|%c|%c|%c|%d|%d|%c|%d|%d|%s|%d|%d|%d|%d#%s"
#define	E_LAN_TAGS	60000000
#define	E_LEVEL_OS	'O'
#define	E_MID_Error	0x8000

/*
 * ======================= Log Function =======================
 *
 *
 * The expected invocation is:
 *
 * ild_log(unsigned char link_no, char *sw_module, unsigned int tag_err,
 *	unsigned char cause, unsigned char severity, char *specific_part)
 *
 * where:
 *
 *	link_no		- link number -- this is used to derive
 *					E_BusSlotNumber in the E_Header.
 *					(e.g., 00-07)
 *	sw_module	- pointer to software module -- this is
 *					E_SoftwareModule in the E_Header.
 *					(e.g., ild, dlpi, llc2)
 *	tag_err		- error number within LAN subsystem -- this is eeeee
 *					in the E_Tag field of the E_Header.
 *					(e.g., 0000-9999)
 *	cause		- cause of the error -- this is E_Cause in the
 *					E_Header.
 *					(e.g., E, H, M, N, O, S, U)
 *	severity	- severity of error -- this is E_Severity in the
 *					E_Header.
 *					(e.g., C, D, F, W, I)
 *	*specific_part	- pointer to the specific portion of the log message --
 *					In all cases it would be extremely
 *					helpful to the log reader to see a
 *					brief explanation, or more information
 *					in cases where the E_Header didn't
 *					cover it completely.
 *					(e.g., No response to link_up.)
 */

void
ild_log(uint_t link_no, uint_t slot_no, char *sw_module, uint_t tag_err,
	uchar_t cause, uchar_t severity, char *file, uint_t lineno, char *msg,
	uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4)
{
	char str[1024];
	char usrstr[512];
	char fileline[256];
	int l1, l2;

	/*
	 * Build the Specific Portion.
	 */

	(void) Kernel_sprintf(fileline, "NCRLLC2: %s-%d: PPA %d: ", file,
				lineno, link_no);
	l1 = strlen(fileline);
	(void) Kernel_sprintf(usrstr,
			msg,			/* message */
			arg1,			/* user arg */
			arg2,			/* user arg */
			arg3,			/* user arg */
			arg4			/* user arg */);
	l2 = strlen(usrstr);
	if ((l1+l2) <= sizeof (fileline)) {
		(void) strcat(fileline, usrstr);

		(void) Kernel_sprintf(str,
				E_TextFormat,
				E_LAN_TAGS | tag_err,	/* E_Tag */
				"comm",			/* E_Type */
				'1',			/* E_Version */
				cause,			/* E_Cause */
				severity,		/* E_Severity */
				E_LEVEL_OS,		/* E_Level */
				0,			/* E_NodeNumber */
				0,			/* E_ProcessorNumber */
				'M',			/* E_BusType */
				0,			/* E_BusNumber */
				slot_no,		/* E_BusSlotNumber */
				sw_module,		/* E_SoftwareModule */
				0,			/* E_Temperature */
				0,			/* E_Voltage */
				0,			/* E_Speed */
				1,			/* E_Count */
				fileline		/* user message */);

		/*
		 * Call the Log Routine.
		 */
		(void) strlog((E_MID_Error|LANSubsystemID), tag_err, 0,
			SL_ERROR|SL_NOTE, str);

	}
}
