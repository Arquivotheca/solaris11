/*
 * Copyright (c) 1991, 2007, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Udp protocol header.
 * Per RFC 768, September, 1981.
 */

#ifndef	_NETINET_UDP_H
#define	_NETINET_UDP_H

/* udp.h 1.7 88/08/19 SMI; from UCB 7.1 6/5/86	*/

#ifdef	__cplusplus
extern "C" {
#endif

struct udphdr {
	in_port_t	uh_sport;		/* source port */
	in_port_t	uh_dport;		/* destination port */
	int16_t		uh_ulen;		/* udp length */
	uint16_t	uh_sum;			/* udp checksum */
};

/* Option definitions. */
#define	UDP_ANONPRIVBIND	0x0100		/* for internal use only */
#define	UDP_EXCLBIND		0x0101		/* for internal use only */
#define	UDP_RCVHDR		0x0102		/* for internal use only */
#define	UDP_NAT_T_ENDPOINT	0x0103		/* for internal use only */
/*
 * Following option in UDP_ namespace required to be exposed through
 * <xti.h> (It also requires exposing options not implemented). The options
 * with potential for conflicts use #ifndef guards.
 *
 */
#ifndef UDP_CHECKSUM
#define	UDP_CHECKSUM	0x0600
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _NETINET_UDP_H */
