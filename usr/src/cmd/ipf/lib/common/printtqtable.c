/*
 * Copyright (C) 2007 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include "ipf.h"

static char *states_desc[IPF_TCP_NSTATES] = {
	    "LISTEN",
	    "SYN SENT",
	    "SYN RECEIVED",
	    "HALF ESTAB",
	    "ESTABLISHED",
	    "CLOSE WAIT",
	    "FIN WAIT 1",
	    "CLOSING",
	    "LAST ACK",
	    "FIN WAIT 2",
	    "TIME WAIT",
	    "CLOSED"};

void
printtqtable(ipftq_t *table)
{
	int i;

	printf("TCP Entries per state\n");
	for (i = 0; i < IPF_TCP_NSTATES; i++)
		printf("%-12s: %5d\n", states_desc[i], table[i].ifq_ref - 1);
	printf("\n");
}
