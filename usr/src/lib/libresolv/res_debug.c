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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/nameser.h>

extern char *p_cdname(), *p_rr(), *p_type(), *p_class(), *p_time();
extern char *inet_ntoa();
void fp_query(char *msg, FILE *file);

char *_res_opcodes[] = {
	"QUERY",
	"IQUERY",
	"CQUERYM",
	"CQUERYU",
	"4",
	"5",
	"6",
	"7",
	"8",
	"UPDATEA",
	"UPDATED",
	"UPDATEDA",
	"UPDATEM",
	"UPDATEMA",
	"ZONEINIT",
	"ZONEREF",
};

char *_res_resultcodes[] = {
	"NOERROR",
	"FORMERR",
	"SERVFAIL",
	"NXDOMAIN",
	"NOTIMP",
	"REFUSED",
	"6",
	"7",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"14",
	"NOCHANGE",
};

void
p_query(msg)
	char *msg;
{
	fp_query(msg, stdout);
}

/*
 * Print the contents of a query.
 * This is intended to be primarily a debugging routine.
 */
void
fp_query(msg, file)
	char *msg;
	FILE *file;
{
	register char *cp;
	register HEADER *hp;
	register int n;

	/*
	 * Print header fields.
	 */
	hp = (HEADER *)msg;
	cp = msg + sizeof (HEADER);
	fprintf(file, "HEADER:\n");
	fprintf(file, "\topcode = %s", _res_opcodes[hp->opcode]);
	fprintf(file, ", id = %d", ntohs(hp->id));
	fprintf(file, ", rcode = %s\n", _res_resultcodes[hp->rcode]);
	fprintf(file, "\theader flags: ");
	if (hp->qr)
		fprintf(file, " qr");
	if (hp->aa)
		fprintf(file, " aa");
	if (hp->tc)
		fprintf(file, " tc");
	if (hp->rd)
		fprintf(file, " rd");
	if (hp->ra)
		fprintf(file, " ra");
	if (hp->pr)
		fprintf(file, " pr");
	fprintf(file, "\n\tqdcount = %d", ntohs(hp->qdcount));
	fprintf(file, ", ancount = %d", ntohs(hp->ancount));
	fprintf(file, ", nscount = %d", ntohs(hp->nscount));
	fprintf(file, ", arcount = %d\n\n", ntohs(hp->arcount));
	/*
	 * Print question records.
	 */
	if (n = ntohs(hp->qdcount)) {
		fprintf(file, "QUESTIONS:\n");
		while (--n >= 0) {
			fprintf(file, "\t");
			cp = p_cdname(cp, msg, file);
			if (cp == NULL)
				return;
			fprintf(file, ", type = %s", p_type(_getshort(cp)));
			cp += sizeof (u_short);
			fprintf(file, ", class = %s\n\n",
						p_class(_getshort(cp)));
			cp += sizeof (u_short);
		}
	}
	/*
	 * Print authoritative answer records
	 */
	if (n = ntohs(hp->ancount)) {
		fprintf(file, "ANSWERS:\n");
		while (--n >= 0) {
			fprintf(file, "\t");
			cp = p_rr(cp, msg, file);
			if (cp == NULL)
				return;
		}
	}
	/*
	 * print name server records
	 */
	if (n = ntohs(hp->nscount)) {
		fprintf(file, "NAME SERVERS:\n");
		while (--n >= 0) {
			fprintf(file, "\t");
			cp = p_rr(cp, msg, file);
			if (cp == NULL)
				return;
		}
	}
	/*
	 * print additional records
	 */
	if (n = ntohs(hp->arcount)) {
		fprintf(file, "ADDITIONAL RECORDS:\n");
		while (--n >= 0) {
			fprintf(file, "\t");
			cp = p_rr(cp, msg, file);
			if (cp == NULL)
				return;
		}
	}
}

char *
p_cdname(cp, msg, file)
	char *cp, *msg;
	FILE *file;
{
	char name[MAXDNAME];
	int n;

	if ((n = dn_expand(msg, msg + 512, cp, name, sizeof (name))) < 0)
		return (NULL);
	if (name[0] == '\0') {
		name[0] = '.';
		name[1] = '\0';
	}
	fputs(name, file);
	return (cp + n);
}

/*
 * Print resource record fields in human readable form.
 */
char *
p_rr(cp, msg, file)
	char *cp, *msg;
	FILE *file;
{
	int type, class, dlen, n, c;
	struct in_addr inaddr;
	char *cp1, *cp2;

	if ((cp = p_cdname(cp, msg, file)) == NULL)
		return (NULL);			/* compression error */
	fprintf(file, "\n\ttype = %s", p_type(type = _getshort(cp)));
	cp += sizeof (u_short);
	fprintf(file, ", class = %s", p_class(class = _getshort(cp)));
	cp += sizeof (u_short);
	fprintf(file, ", ttl = %s", p_time(_getlong(cp)));
	cp += sizeof (u_long);
	fprintf(file, ", dlen = %d\n", dlen = _getshort(cp));
	cp += sizeof (u_short);
	cp1 = cp;
	/*
	 * Print type specific data, if appropriate
	 */
	switch (type) {
	case T_A:
		switch (class) {
		case C_IN:
		case C_HS:
#ifdef SYSV
			memcpy((void *)&inaddr, (void *)cp, sizeof (inaddr));
#else
			bcopy(cp, (char *)&inaddr, sizeof (inaddr));
#endif
			if (dlen == 4) {
				fprintf(file, "\tinternet address = %s\n",
					inet_ntoa(inaddr));
				cp += dlen;
			} else if (dlen == 7) {
				fprintf(file, "\tinternet address = %s",
					inet_ntoa(inaddr));
				fprintf(file, ", protocol = %d", cp[4]);
				fprintf(file, ", port = %d\n",
					(cp[5] << 8) + cp[6]);
				cp += dlen;
			}
			break;
		default:
			cp += dlen;
		}
		break;
	case T_CNAME:
	case T_MB:
	case T_MG:
	case T_MR:
	case T_NS:
	case T_PTR:
		fprintf(file, "\tdomain name = ");
		cp = p_cdname(cp, msg, file);
		fprintf(file, "\n");
		break;

	case T_HINFO:
		if (n = *cp++) {
			fprintf(file, "\tCPU=%.*s\n", n, cp);
			cp += n;
		}
		if (n = *cp++) {
			fprintf(file, "\tOS=%.*s\n", n, cp);
			cp += n;
		}
		break;

	case T_SOA:
		fprintf(file, "\torigin = ");
		cp = p_cdname(cp, msg, file);
		fprintf(file, "\n\tmail addr = ");
		cp = p_cdname(cp, msg, file);
		fprintf(file, "\n\tserial = %ld", _getlong(cp));
		cp += sizeof (u_long);
		fprintf(file, "\n\trefresh = %s", p_time(_getlong(cp)));
		cp += sizeof (u_long);
		fprintf(file, "\n\tretry = %s", p_time(_getlong(cp)));
		cp += sizeof (u_long);
		fprintf(file, "\n\texpire = %s", p_time(_getlong(cp)));
		cp += sizeof (u_long);
		fprintf(file, "\n\tmin = %s\n", p_time(_getlong(cp)));
		cp += sizeof (u_long);
		break;

	case T_MX:
		fprintf(file, "\tpreference = %ld,", _getshort(cp));
		cp += sizeof (u_short);
		fprintf(file, " name = ");
		cp = p_cdname(cp, msg, file);
		break;

	case T_TXT:
		(void) fputs("\t\"", file);
		cp2 = cp1 + dlen;
		while (cp < cp2) {
			if (n = (unsigned char) *cp++) {
				for (c = n; c > 0 && cp < cp2; c--)
					if (*cp == '\n') {
					    (void) putc('\\', file);
					    (void) putc(*cp++, file);
					} else
					    (void) putc(*cp++, file);
			}
		}
		(void) fputs("\"\n", file);
		break;

	case T_MINFO:
		fprintf(file, "\trequests = ");
		cp = p_cdname(cp, msg, file);
		fprintf(file, "\n\terrors = ");
		cp = p_cdname(cp, msg, file);
		break;

	case T_UINFO:
		fprintf(file, "\t%s\n", cp);
		cp += dlen;
		break;

	case T_UID:
	case T_GID:
		if (dlen == 4) {
			fprintf(file, "\t%ld\n", _getlong(cp));
			cp += sizeof (int);
		}
		break;

	case T_WKS:
		if (dlen < sizeof (u_long) + 1)
			break;
#ifdef SYSV
		memcpy((void *)&inaddr, (void *)cp, sizeof (inaddr));
#else
		bcopy(cp, (char *)&inaddr, sizeof (inaddr));
#endif
		cp += sizeof (u_long);
		fprintf(file, "\tinternet address = %s, protocol = %d\n\t",
			inet_ntoa(inaddr), *cp++);
		n = 0;
		while (cp < cp1 + dlen) {
			c = *cp++;
			do {
				if (c & 0200)
					fprintf(file, " %d", n);
				c <<= 1;
			} while (++n & 07);
		}
		putc('\n', file);
		break;

#ifdef ALLOW_T_UNSPEC
	case T_UNSPEC:
		{
			int NumBytes = 8;
			char *DataPtr;
			int i;

			if (dlen < NumBytes) NumBytes = dlen;
			fprintf(file, "\tFirst %d bytes of hex data:",
				NumBytes);
			for (i = 0, DataPtr = cp; i < NumBytes; i++, DataPtr++)
				fprintf(file, " %x", *DataPtr);
			fputs("\n", file);
			cp += dlen;
		}
		break;
#endif /* ALLOW_T_UNSPEC */

	default:
		fprintf(file, "\t???\n");
		cp += dlen;
	}
	if (cp != cp1 + dlen) {
		fprintf(file, "packet size error (%#x != %#x)\n", cp, cp1+dlen);
		cp = NULL;
	}
	fprintf(file, "\n");
	return (cp);
}

static	char nbuf[40];

/*
 * Return a string for the type
 */
char *
p_type(type)
	int type;
{
	switch (type) {
	case T_A:
		return ("A");
	case T_NS:		/* authoritative server */
		return ("NS");
	case T_CNAME:		/* canonical name */
		return ("CNAME");
	case T_SOA:		/* start of authority zone */
		return ("SOA");
	case T_MB:		/* mailbox domain name */
		return ("MB");
	case T_MG:		/* mail group member */
		return ("MG");
	case T_MR:		/* mail rename name */
		return ("MR");
	case T_NULL:		/* null resource record */
		return ("NULL");
	case T_WKS:		/* well known service */
		return ("WKS");
	case T_PTR:		/* domain name pointer */
		return ("PTR");
	case T_HINFO:		/* host information */
		return ("HINFO");
	case T_MINFO:		/* mailbox information */
		return ("MINFO");
	case T_MX:		/* mail routing info */
		return ("MX");
	case T_TXT:		/* text */
		return ("TXT");
	case T_AXFR:		/* zone transfer */
		return ("AXFR");
	case T_MAILB:		/* mail box */
		return ("MAILB");
	case T_MAILA:		/* mail address */
		return ("MAILA");
	case T_ANY:		/* matches any type */
		return ("ANY");
	case T_UINFO:
		return ("UINFO");
	case T_UID:
		return ("UID");
	case T_GID:
		return ("GID");
#ifdef ALLOW_T_UNSPEC
	case T_UNSPEC:
		return ("UNSPEC");
#endif /* ALLOW_T_UNSPEC */
	default:
		(void) sprintf(nbuf, "%d", type);
		return (nbuf);
	}
}

/*
 * Return a mnemonic for class
 */
char *
p_class(class)
	int class;
{

	switch (class) {
	case C_IN:		/* internet class */
		return ("IN");
	case C_HS:		/* hesiod class */
		return ("HS");
	case C_ANY:		/* matches any class */
		return ("ANY");
	default:
		(void) sprintf(nbuf, "%d", class);
		return (nbuf);
	}
}

/*
 * Return a mnemonic for a time to live
 */
char *
p_time(value)
	u_long value;
{
	int secs, mins, hours;
	register char *p;

	if (value == 0) {
		strcpy(nbuf, "0 secs");
		return (nbuf);
	}

	secs = value % 60;
	value /= 60;
	mins = value % 60;
	value /= 60;
	hours = value % 24;
	value /= 24;

#define	PLURALIZE(x)	x, (x == 1) ? "" : "s"
	p = nbuf;
	if (value) {
		(void) sprintf(p, "%d day%s", PLURALIZE(value));
		while (*++p);
	}
	if (hours) {
		if (value)
			*p++ = ' ';
		(void) sprintf(p, "%d hour%s", PLURALIZE(hours));
		while (*++p);
	}
	if (mins) {
		if (value || hours)
			*p++ = ' ';
		(void) sprintf(p, "%d min%s", PLURALIZE(mins));
		while (*++p);
	}
	if (secs || ! (value || hours || mins)) {
		if (value || hours || mins)
			*p++ = ' ';
		(void) sprintf(p, "%d sec%s", PLURALIZE(secs));
	}
	return (nbuf);
}
