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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <utility.h>
#include <sys/param.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <netdb.h>

extern int errno, h_errno;

typedef union {
	HEADER  hdr;
	uchar_t buf[PACKETSZ];
} ans_t;

typedef struct {
	int priority;	/* 0 == highest */
	int weight;	/* 0 == lowest */
	char *host;
} sort_t;

static int
sorter(const void *p1, const void *p2)
{
	sort_t *s1, *s2;

	s1 = (sort_t *)p1;
	s2 = (sort_t *)p2;

	if (s1->priority > s2->priority)
		return (1);
	if (s1->priority < s2->priority)
		return (-1);
	if (s1->weight > s2->weight)
		return (-1);
	if (s1->weight < s2->weight)
		return (1);
	return (0);
}

#ifdef DEBUG
static void
hexdump(char *data, int datalen)
{
	char *p;
	ushort_t *p16 = (ushort_t *)data;
	char *p8 = data;
	int i, left, len;
	int chunk = 16;  /* 16 bytes per line */

	printf("\n");

	for (p = data; p < data + datalen; p += chunk) {
		printf("\t%4d: ", (int)(p - data));
		left = (data + datalen) - p;
		len = MIN(chunk, left);
		for (i = 0; i < (len / 2); i++)
			printf("%04x ", ntohs(*p16++) & 0xffff);
		if (len % 2) {
			printf("%02x   ", *((unsigned char *)p16));
		}
		for (i = 0; i < (chunk - left) / 2; i++)
			printf("     ");

		printf("   ");
		for (i = 0; i < len; i++, p8++)
			printf("%c", isprint(*p8) ? *p8 : '.');
		printf("\n");
	}

	printf("\n");
}
#endif

/*
 * Look up an RFC 2782 SRV record; the form is:
 *
 * _Service._Proto.Name TTL Class SRV Priority Weight Port Target
 * e.g.
 * _nfs4._domainroot._tcp IN SRV 0 0 2049 nfs1tr.example.net.
 *
 * The "prefix" arg is "_nfs4._domainroot._tcp"
 * The "domain" arg is "example.net"
 */
int
dns_srv_list(char *prefix, char *domain, char ***listp)
{
	struct __res_state res;
	char *key;
	int class = C_IN;
	int type = T_SRV;
	ans_t answer = {0};
	int n, answer_len, dlen;
	uint_t cnt, qd_cnt, an_cnt;
	HEADER *hp;
	uchar_t *p, *buf, *eom, *srv, name[NS_MAXCDNAME + 1];
	int num = 0, priority, weight, i;
	sort_t *nlist, *list = NULL;
	char **rlist = NULL;

	memset(&res, 0, sizeof (res));
	n = h_errno = errno = 0;
	if ((n = res_ninit(&res)) < 0) {
		printf("dns_srv_list: res_ninit() err %d\n", h_errno);
		return (0);
	}

	key = malloc(strlen(prefix) + strlen(domain) + 3);
	if (key == NULL) {
		res_ndestroy(&res);
		return (0);
	}
	sprintf(key, "%s.%s.", prefix, domain);
#ifdef DEBUG
	printf("dns_srv_list: key for DNS query is %s\n", key);
#endif

	answer_len = h_errno = errno = 0;
	if ((answer_len = res_nsearch(&res, key, class, type,
	    answer.buf, sizeof (answer))) < 0) {
		printf("dns_srv_list: res_nsearch() err %d\n", h_errno);
		free(key);
		res_ndestroy(&res);
		return (0);
	}
	free(key);

#ifdef DEBUG
	printf("dns_srv_list: res_nsearch() returned %d,buf:\n", answer_len);
	hexdump((char *)answer.buf, answer_len);
#endif

	buf = (uchar_t *)&answer.buf;
	hp = (HEADER *)&answer.hdr;
	eom = (uchar_t *)(buf + answer_len);
	if (hp->rcode != NOERROR) {
#ifdef DEBUG
		printf("dns_srv_list: bad response code, errno %s, h_errno %s",
		    strerror(errno), hstrerror(h_errno));
#endif
		res_ndestroy(&res);
		return (0);
	}
	qd_cnt = ntohs(hp->qdcount);
	an_cnt = ntohs(hp->ancount);

	/*
	 * skip query entries
	 */
	p = (uchar_t *)(buf + HFIXEDSZ);
	errno = 0;
	while (qd_cnt-- > 0) {
		n = dn_skipname(p, eom);
		if (n < 0) {
			printf("dn_skipname() failure %s", strerror(errno));
			return (0);
		}
		p += n;
		p += INT16SZ;   /* type */
		p += INT16SZ;   /* class */
	}
	n = h_errno = errno = 0;
	n = dn_expand(buf, eom, p, (char *)name, sizeof (name));
	if (n < 0) {
#ifdef DEBUG
		printf("dn_expand() failed, errno: %s, h_errno: %s",
		    strerror(errno), hstrerror(h_errno));
#endif
		res_ndestroy(&res);
		return (0);
	}
	/*
	 * printf("Query was %s\n", name);
	 */

	/*
	 * Process actual answer(s).
	 */
#ifdef DEBUG
	printf("dns_srv_list: %d answers found\n", an_cnt);
#endif
	cnt = an_cnt;
	while (cnt-- > 0 && p < eom) {
		/* skip the name field */
		n = dn_expand(buf, eom, p, (char *)name, sizeof (name));
		if (n < 0) {
#ifdef DEBUG
			printf("dn_expand() failed, errno: %s, h_errno: %s",
			    strerror(errno), hstrerror(h_errno));
#endif
			res_ndestroy(&res);
			return (0);
		}
		p += n;

		if ((p + 3 * INT16SZ + INT32SZ) > eom) {
			res_ndestroy(&res);
			return (0);
		}

		type = ns_get16(p);
		p += INT16SZ;
		p += INT16SZ + INT32SZ; /* skip class & ttl */
		dlen = ns_get16(p);
		p += INT16SZ;

		if ((p + dlen) > eom) {
			res_ndestroy(&res);
			return (0);
		}

		switch (type) {
			case T_SRV:
#ifdef DEBUG
				printf("dns_srv_list: "
				    "server record found, buf:\n");
				hexdump((char *)p, dlen);
#endif

				priority = ns_get16(p);
				weight = ns_get16(p + INT16SZ);
				srv = p + (3 * INT16SZ);
				n = h_errno = errno = 0;
				name[0] = '\0';
				n = dn_expand(buf, eom, srv,
				    (char *)name, sizeof (name));
#ifdef DEBUG
				printf("server %s, prio %d, weight %d\n",
				    name, priority, weight);
#endif
				nlist = (sort_t *)realloc(list,
				    ++num * sizeof (sort_t));
				if (nlist == NULL) {
					num = 0;
					goto errout;
				}
				list = nlist;
				list[num - 1].host = strdup((char *)name);
				if (list[num - 1].host == NULL)
					goto errout;
				list[num - 1].priority = priority;
				list[num - 1].weight = weight;
				p += dlen;
				break;

			default:
				/*
				 * Advance to next answer record for any
				 * other record types.
				 */
				p += dlen;
				break;
		}
	}

	res_ndestroy(&res);

	/*
	 * Sort list by priority (smaller first) and weight (larger first)
	 */
	qsort(list, num, sizeof (sort_t), sorter);

#ifdef DEBUG
	printf("dns_srv_list: after sort:\n");
#endif
	/*
	 * Copy hostnames only to return to the caller
	 */
	rlist = malloc(num * sizeof (char *));
	if (rlist == NULL)
		goto errout;

	for (i = 0; i < num; i++) {
		rlist[i] = list[i].host;
#ifdef DEBUG
		printf("server %s, prio %d, weight %d\n",
		    list[i].host, list[i].priority, list[i].weight);
#endif
	}

	free(list);
	*listp = rlist;
	return (num);

errout:
	for (i = 0; i < num; i++)
		free(list[i].host);
	free(list);
	*listp = NULL;
	return (0);
}

void
free_srv_list(char **hostlist, int num)
{
	int i;

	if (hostlist == NULL)
		return;
	for (i = 0; i < num; i++)
		free(hostlist[i]);
	free(hostlist);
}
