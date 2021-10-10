/*
 * All Rights Reserved, Copyright (c) FUJITSU LIMITED 2006
 */

#ifndef _SCFIO32_H
#define	_SCFIO32_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types32.h>

/* SCFIOCWRLCD 32bit */
typedef struct scfwrlcd32 {
	int		lcd_type;
	int		length;
	caddr32_t	string;
} scfwrlcd32_t;


/* SCFIOCGETREPORT 32bit */
typedef struct scfreport32 {
	int		flag;
	unsigned int	rci_addr;
	unsigned char	report_sense[4];
	time32_t	timestamp;
} scfreport32_t;


/* SCFIOCGETEVENT 32bit */
typedef struct scfevent32 {
	int		flag;
	unsigned int	rci_addr;
	unsigned char	code;
	unsigned char	size;
	unsigned char	rsv[2];
	unsigned char	event_sense[24];
	time32_t	timestamp;
} scfevent32_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SCFIO32_H */
