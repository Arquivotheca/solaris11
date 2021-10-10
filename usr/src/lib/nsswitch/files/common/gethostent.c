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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <netdb.h>
#include "files_common.h"
#include <string.h>
#include <strings.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <assert.h>
#include <nss.h>

int check_name_host(nss_XbyY_args_t *, const char *, int,
    int, const char **, int *, void *, int *);
static char *do_aliases();
nss_status_t __nss_files_XY_hostbyname();
static int __nss_files_get_addr(int, const char *, int, void *, int, int *);

static uint_t
hash_string_tolower(const char *name, int namelen)
{
	uint_t	hash = 0;
	int	i;

	for (i = 0; i < namelen; i++) {
		hash = hash * 15 + tolower(name[i]);
	}

	return (hash);
}

static uint_t
hash_field_tolower(const char *line, int linelen, uint_t field,
    boolean_t *has_next)
{
	const char	*name = line;
	int		namelen = 0;

	while (field > 0 && linelen > 0) {
		/* trim whitespaces */
		while (linelen > 0 && isspace(*line)) {
			linelen--;
			line++;
		}

		/* set name when we have our field */
		if (field == 1)
			name = line;

		/* seek for the end of line or the next whitespace */
		while (linelen > 0 && !isspace(*line)) {
			linelen--;
			line++;
		}

		/* calculate size of found entry */
		if (field == 1) {
			namelen = line - name;
			break;
		}
		field--;
	}

	/* seek for the end of line or the next non-whitespace */
	while (linelen > 0 && isspace(*line)) {
		linelen--;
		line++;
	}
	*has_next = (linelen > 0);

	return (hash_string_tolower(name, namelen));
}

uint_t
hash_hostname(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	boolean_t	t;

	assert(keyhash == 1);

	return (hash_string_tolower(argp->key.name, strlen(argp->key.name)));
}

uint_t *
hash_arr_hostname(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	uint_t		*hash_arr, *new_arr;
	uint_t		field = 2;
	boolean_t	has_field = B_TRUE;

	assert(keyhash == 0);

	hash_arr = malloc(sizeof (uint_t));
	if (hash_arr == NULL)
		return (NULL);

	hash_arr[0] = 0;

	while (has_field &&
	    ((new_arr = realloc(hash_arr, field * sizeof (uint_t))) != NULL)) {
		hash_arr = new_arr;
		hash_arr[0]++;
		hash_arr[field - 1] = hash_field_tolower(line, linelen, field,
		    &has_field);
		field++;
	}

	if (new_arr == NULL) {
		free(hash_arr);
		return (NULL);
	}

	return (hash_arr);
}

/* convert_ip converts string to appropriate binary form */
int
convert_ip(int af, const char *line, int linelen, char *h_addrp, int *h_length)
{
	const char	*limit, *linep, *addrstart;
	int		v6flag = 0, addrlen;

	linep = line;
	limit = line + linelen;
	addrstart = linep;
	while (linep < limit && !isspace(*linep)) {
		if (*linep == ':')
			v6flag++;
		linep++;
	}
	addrlen = linep - addrstart;

	return (__nss_files_get_addr(af, addrstart, addrlen, h_addrp,
	    v6flag, h_length));
}

static uint_t
hash_ipv4(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	const uint_t	*value;
	uint_t		hash;
	int		h_length;
	struct in_addr	addr_ipv4;
	char		*h_addrp;

	if (keyhash) {
		(void) memcpy(&addr_ipv4, argp->key.hostaddr.addr,
		    sizeof (addr_ipv4));
	} else {
		h_addrp = (char *)&addr_ipv4;
		if (convert_ip(AF_INET, line, linelen, h_addrp, &h_length) == 0)
			return (0);
	}

	/* hash is equal to IPv4 address */
	hash = addr_ipv4.s_addr;

	return (hash);
}

/*
 * hash_hosts functions are used for single-value attribute
 * hash_arr_hosts are used for multi-value attribute (line in local file
 *     where hostnames can be canonical + aliases)
 */
static files_hash_func hash_hosts[2] = { hash_ipv4, hash_hostname };
static files_h_ar_func hash_arr_hosts[2] = { NULL, hash_arr_hostname };
static files_hash_t hashinfo = {
	DEFAULTMUTEX,
	sizeof (struct hostent),
	NSS_BUFLEN_HOSTS,
	2,
	hash_hosts,
	hash_arr_hosts
};

int
check_name_host(nss_XbyY_args_t *argp, const char *line, int linelen, int type,
    const char **namep, int *namelen, void *addrp, int *addrsize)
{
	const char	*limit, *linep, *keyp, *addrstart;
	int		v6flag = 0, addrlen;

	linep = line;
	limit = line + linelen;

	/* Address */
	addrstart = linep;
	while (linep < limit && !isspace(*linep)) {
		if (*linep == ':')
			v6flag++;
		linep++;
	}
	addrlen = linep - addrstart;

	/* skip the delimiting spaces */
	while (linep < limit && isspace(*linep))
		linep++;

	/* Canonical name */
	keyp = argp->key.name;
	*namep = linep;
	while (*keyp && linep < limit && !isspace(*linep) &&
	    tolower(*keyp) == tolower(*linep)) {
		keyp++;
		linep++;
	}
	if (*keyp == '\0' && (linep == limit || isspace(*linep))) {
		if (__nss_files_get_addr(type, addrstart, addrlen,
		    addrp, v6flag, addrsize)) {
			*namelen = linep - *namep;
			return (1);
		}
	}
	while (linep < limit && !isspace(*linep))
		linep++;
	*namelen = linep - *namep;

	/* Aliases */
	while (linep < limit) {
		/* skip the delimiting spaces */
		while (linep < limit && isspace(*linep))
			linep++;

		/* compare name (case insensitive) */
		keyp = argp->key.name;
		while (*keyp && linep < limit && !isspace(*linep) &&
		    tolower(*keyp) == tolower(*linep)) {
			keyp++;
			linep++;
		}
		if (*keyp == '\0' && (linep == limit || isspace(*linep)))
			return (__nss_files_get_addr(type, addrstart, addrlen,
			    addrp, v6flag, addrsize));

		/* skip remainder of alias, if any */
		while (linep < limit && !isspace(*linep))
			linep++;
	}
	return (0);

}

static int
check_name_hash(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	const char	*namep;
	int		namelen;
	struct in_addr	addr_ipv4;
	int		i;

	return (check_name_host(argp, line, linelen, AF_INET, &namep, &namelen,
	    &addr_ipv4, &i));
}

static void
find_canonical(const char *line, int linelen, char **namep, int *namelen)
{
	*namep = (char *)line;
	*namelen = 0;

	while (!isspace(**namep) && (**namep != '\0')) {
		(*namep)++;
	}

	while (isspace(**namep) && (**namep != '\0')) {
		(*namep)++;
	}

	if (**namep == '\0')
		return;

	while (!isspace(*(*namep + *namelen)) &&
	    (*(*namep + *namelen) != '\0')) {
		(*namelen)++;
	}
}
/*
 * finish_hostname and add_host_line construct buffer used by nscd
 * for hosts/ipnodes entries:
 *
 * IP1 canonical <aliases>\n
 * IP2\n
 * IP3\n
 * etc.
 */
static int
add_host_line(char **buffer_end, char **alias_end, int *buflenp,
    line_matches_t *linem)
{
	int		erange = 0;
	char		*namep;
	int		namelen;
	char		*ap;
	int		alen;
	const char	*line;
	int		linelen;

	line = linem->line;
	linelen = linem->len;

	if (linem->next == NULL) {
		if (linelen + 1 >= *buflenp) {
			erange = 1;
		} else {
			(void) memcpy(*buffer_end, line, linelen);
			*buflenp -= linelen;
			*buffer_end += linelen;
			*alias_end = *buffer_end;
			**buffer_end = '\0';
			(*buflenp)--;
		}
	} else {
		erange = add_host_line(buffer_end, alias_end, buflenp,
		    linem->next);

		if (erange == 0) {
			find_canonical(line, linelen, &namep, &namelen);

			if (*namep != NULL) {
				/* get to the start of alias */
				ap = (char *)namep + namelen;
				/* see if there's any alias */
				if (ap == line + linelen)
					alen = 0;
				else
					alen = linelen - (ap - line);

				if (alen + 1 + (namep - line) >= *buflenp) {
					erange = 1;
				} else {
					if (alen != 0) {
						/* make room for the aliases */
						(void) memmove(
						    *alias_end + alen,
						    *alias_end,
						    *buffer_end - *alias_end);
						/* copy in the alias */
						(void) memmove(*alias_end, ap,
						    alen);
						*alias_end += alen;
						*buflenp -= alen;
						*buffer_end += alen;
					}
					/* copy IP */
					(void) memcpy(*buffer_end + 1, line,
					    namep - line);

					/* add delimiter */
					**buffer_end = '\n';
					*buflenp -= namep - line + 1;
					*buffer_end += namep - line + 1;
					**buffer_end = '\0';
				}
			}
		}
	}

	free(linem->line);
	free(linem);
	return (erange);
}

nss_status_t
finish_hostname(line_matches_t *linem, nss_XbyY_args_t *args,
    nss_status_t res)
{
	char		*alias_end;
	char		*buffer;
	int		buflen;

	if (linem == NULL)
		return (res);

	args->returnval = args->buf.buffer;
	args->returnlen = args->buf.buflen;
	buffer = args->buf.buffer;
	buflen = args->buf.buflen;

	if (add_host_line(&buffer, &alias_end, &buflen, linem) != 0) {
		args->erange = 1;
		return (NSS_NOTFOUND);
	}

	args->returnlen -= buflen;

	return (NSS_SUCCESS);
}

/*
 * For non-nscd and libnsl API there is non-hash function with different output.
 */
static nss_status_t
getbyname(files_backend_ptr_t be, void *a)
{
	nss_XbyY_args_t		*argp = (nss_XbyY_args_t *)a;
	nss_status_t		res;

	be->flags = FC_FLAG_HOSTNAME;
	/*
	 * For systems without nscd and for libnsl API there is non-hash
	 * function with different output buffer.
	 */
	if (argp->buf.result != NULL)
		res = __nss_files_XY_hostbyname(be, argp, argp->key.name,
		    AF_INET);
	else
		res = _nss_files_XY_hash(be, argp, 1, &hashinfo, 1,
		    check_name_hash);
	if (res != NSS_SUCCESS)
		argp->h_errno = __nss2herrno(res);

	return (res);
}

static int
__nss_files_get_addr(int af, const char *addrstart, int addrlen,
	void *addrp, int v6flag, int *h_length)
{
	struct in_addr	addr_ipv4;
	struct in6_addr	*addrpv6;
	in_addr_t	*addrpv4;
	char		addrbuf[INET6_ADDRSTRLEN + 1];

	if (addrlen >= sizeof (addrbuf))
		return (0);
	(void) memcpy(addrbuf, addrstart, addrlen);
	addrbuf[addrlen] = '\0';

	if (af == AF_INET) {
		addrpv4 = (in_addr_t *)addrp;
		if ((*addrpv4 = inet_addr(addrbuf)) == 0xffffffffU)
			return (0);
		*h_length = sizeof (in_addr_t);
	} else if (af == AF_INET6) {
		addrpv6 = (struct in6_addr *)addrp;
		if (v6flag) {
			if (inet_pton(af, addrbuf, addrpv6) != 1)
				return (0);
		} else {
			if ((addr_ipv4.s_addr = inet_addr(addrbuf)) ==
			    0xffffffffU)
				return (0);
			IN6_INADDR_TO_V4MAPPED(&addr_ipv4, addrpv6);
		}
		*h_length = sizeof (struct in6_addr);
	} else {
		return (0);
	}
	return (1);
}


int
__nss_files_check_addr(int af, nss_XbyY_args_t *argp, const char *line,
		int linelen)
{
	int		h_length;
	in_addr_t	addr_ipv4;
	struct in6_addr	addr_ipv6;
	char		*h_addrp;

	/* Compare the address type */
	if (argp->key.hostaddr.type != af)
		return (0);

	/* Retrieve the address */
	if (af == AF_INET)
		h_addrp = (char *)&addr_ipv4;
	else
		h_addrp = (char *)&addr_ipv6;

	if (convert_ip(af, line, linelen, h_addrp, &h_length) == 0)
		return (0);

	/* Compare the address */
	return (h_length == argp->key.hostaddr.len &&
	    memcmp(h_addrp, argp->key.hostaddr.addr,
	    argp->key.hostaddr.len) == 0);
}

static int
check_addr(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	return (__nss_files_check_addr(AF_INET, argp, line, linelen));
}

static nss_status_t
getbyaddr(files_backend_ptr_t be, void *a)
{
	nss_XbyY_args_t		*argp	= (nss_XbyY_args_t *)a;
	nss_status_t		res;

	be->flags = FC_FLAG_IP;
	res = _nss_files_XY_hash(be, argp, 1, &hashinfo, 0, check_addr);
	if (res != NSS_SUCCESS)
		argp->h_errno = __nss2herrno(res);
	return (res);
}

/*
 * filter_ipv6
 *
 * Return - NSS_STR_PARSE_SUCCESS: An IPv4 address
 *          NSS_STR_PARSE_PARSE: An IPv6 address or other errors
 */
static int
filter_ipv6(char *instr, int lenstr) {
	char	*p, *addrstart, *limit, c;
	int	rc;
	struct in_addr	addr;

	p = instr;
	limit = p + lenstr;

	addrstart = p;

	/* parse IP address */
	while (p < limit && !isspace(*p)) {
		if (*p == ':')
			/* IPv6 */
			return (NSS_STR_PARSE_PARSE);
		else
			p++;
	}

	if (p >= limit)
		/* invalid IP */
		return (NSS_STR_PARSE_PARSE);

	/* extract IP address */
	c = *p;
	*p = '\0';
	rc = inet_aton(addrstart, &addr);
	*p = c;

	if (rc == 0)
		/* invalid IP */
		return (NSS_STR_PARSE_PARSE);
	else
		/* IPv4 */
		return (NSS_STR_PARSE_SUCCESS);


}
static nss_status_t
getent_hosts(files_backend_ptr_t be, void *a)
{
	nss_XbyY_args_t	*args = (nss_XbyY_args_t *)a;
	nss_status_t	rc = NSS_SUCCESS;

	if (args->buf.result != NULL) {
		return (_nss_files_XY_all(be, args, 1, 0, 0));
	} else {
		/*
		 * Called by nscd. It does not use in-memory mapped file
		 * but on-disk file directly.
		 */
		for (;;) {
			rc = _nss_files_XY_all(be, args, 1, 0, 0);
			/*
			 * NSS_NOTFOUND, end of file or other errors.
			 */
			if (rc != NSS_SUCCESS)
				break;
			/*
			 * /etc/hosts and /etc/ipnodes are merged and
			 * /etc/hosts can contain IPv6 addresses.
			 * These addresses have to be filtered.
			 */
			if (filter_ipv6(args->returnval, args->returnlen)
			    == NSS_STR_PARSE_SUCCESS)
				break;
			/*
			 * The entry is an IPv6 address or other errors.
			 * Skip it and continue to find next one.
			 */
			args->returnval = NULL;
			args->returnlen = 0;

		}
		return (rc);
	}

}

static files_backend_op_t host_ops[] = {
	_nss_files_destr,
	_nss_files_endent,
	_nss_files_setent,
	getent_hosts,
	getbyname,
	getbyaddr,
};

/*ARGSUSED*/
nss_backend_t *
_nss_files_hosts_constr(dummy1, dummy2, dummy3)
	const char	*dummy1, *dummy2, *dummy3;
{
	return (_nss_files_constr(host_ops,
	    sizeof (host_ops) / sizeof (host_ops[0]),
	    _PATH_HOSTS,
	    NSS_LINELEN_HOSTS,
	    &hashinfo, 0));
}


/*
 * XXX - this duplicates code from files_common.c because we need to keep
 * going after we've found a match to satisfy the multihomed host case.
 * It is used only by non-nscd calls.
 */
nss_status_t
__nss_files_XY_hostbyname(be, args, filter, type)
	files_backend_ptr_t be;
	nss_XbyY_args_t *args;
	const char *filter;		/* hint for name string */
	int type;
{
	nss_status_t	res;
	char		*abuf = NULL, *abuf_start = NULL, *abuf_end;
	char		*first, *last, *buffer;
	int		parsestat, i, nhosts = 0, buflen;
	const char	*namep;
	char		*h_name;
	int		h_namelen, namelen;
	struct hostent	*hp;
	in_addr_t	*taddr = NULL;
	struct in6_addr	*taddr6 = NULL;
	size_t		ntaddr;
	void		*addrp;
	char		*alias_end = NULL;

	if (be->buf == 0 && (be->buf = malloc(be->minbuf)) == 0) {
		return (NSS_UNAVAIL);
	}

	if (be->f == 0) {
		if ((res = _nss_files_setent(be, 0)) != NSS_SUCCESS)
			return (res);
	}

	ntaddr = MAXADDRS;
	if (type == AF_INET) {
		taddr = (in_addr_t *)calloc(ntaddr, sizeof (*taddr));
		if (taddr == NULL)
			return (NSS_UNAVAIL);
	} else {
		taddr6 = (struct in6_addr *)calloc(ntaddr, sizeof (*taddr6));
		if (taddr6 == NULL)
			return (NSS_UNAVAIL);
	}

	res = NSS_NOTFOUND;
	args->returnval = (char *)0;
	args->returnlen = 0;
	hp = (struct hostent *)args->buf.result;
	buffer = args->buf.buffer;
	buflen = args->buf.buflen;
	h_namelen = 0;
	h_name = NULL;

	for (;;) {
		char *instr = be->buf;
		int linelen;

		if ((linelen = _nss_files_read_line(be,
		    instr, be->minbuf)) < 0) {
			break;		/* EOF */
		}

		/*
		 * This check avoids a malloc()/free() for the common
		 * case. Also, if we're trying to match an alias and an
		 * already matched entry doesn't share a canonical name
		 * with the current one, bail.
		 */
		if (nhosts == 0 && strcasestr(instr, filter) == 0) {
			continue;
		}

		if ((last = strchr(instr, '#')) == 0)
			last = instr + linelen;
		*last-- = '\0';
		for (first = instr;  isspace(*first);  first++)
			;
		/* Ignore blank and comment lines */
		if (*first == '\0')
			continue;

		while (isspace(*last))
			--last;
		linelen = last - first + 1;
		if (first != instr)
			instr = first;

		/* Bail out if the canonical name does not match */
		if (nhosts && strcasestr(instr, h_name) == 0) {
			continue;
		}

		/*
		 * Still need to check, strcasestr() above is just a hint.
		 */
		addrp = (type == AF_INET) ?
		    (void *)&taddr[nhosts] : (void *)&taddr6[nhosts];

		if (check_name_host(args, instr, linelen, type, &namep,
		    &namelen, addrp, &i)) {

			/*
			 * If we've already matched once and have a possible
			 * match on this line, copy the aliases where they're
			 * safe from being overwritten when we look at the
			 * next entry. They're saved as a string of blank
			 * separated names for the alias parser. On errors,
			 * we return failure whether or not we have already
			 * obtained a valid address.
			 */
			if (nhosts == 1 && hp) {
				if (h_namelen + 1 > args->buf.buflen) {
					args->erange = 1;
					res = NSS_NOTFOUND;
					break;
				}
				abuf = (char *)malloc(args->buf.buflen);
				if (abuf == NULL) {
					res = NSS_UNAVAIL;
					break;
				}
				abuf_start = abuf;
				abuf_end = abuf_start + args->buf.buflen;
				(void) memcpy(abuf, h_name, h_namelen);
				abuf += h_namelen;
				*abuf = '\0';
				abuf = do_aliases(hp, abuf, abuf_end);
				if (abuf == NULL) {
					args->erange = 1;
					res = NSS_NOTFOUND;
					break;
				}
			}

			if (hp != NULL) {
				/* inside the application */
				parsestat = (*args->str2ent)(instr, linelen,
						hp, buffer, buflen);
				if (parsestat != NSS_STR_PARSE_SUCCESS) {
					if (parsestat == NSS_STR_PARSE_ERANGE)
						args->erange = 1;
					(void) memset(buffer, 0, buflen);
					continue;
				}
			} else {
				/* nscd call should not be here */
				res = NSS_ERROR;
				break;
			}

			/*
			 * If this is the first one, save the canonical
			 * name for future matches and continue.
			 */
			if (++nhosts == 1) {
				h_name = malloc(namelen + 1);
				if (h_name == NULL) {
					res = NSS_UNAVAIL;
					break;
				}
				res = NSS_SUCCESS;
				(void) memcpy(h_name, namep, namelen);
				h_name[namelen] = '\0';
				h_namelen = namelen;
				if (hp)
					args->returnval = hp;
				continue;
			}


			/* Extend the array */
			if (nhosts >= ntaddr) {
				ntaddr *= 2;
				if (type == AF_INET) {
					addrp = realloc(taddr,
						sizeof (*taddr) * ntaddr);
					if (addrp == NULL) {
						res = NSS_UNAVAIL;
						break;
					}
					taddr = (in_addr_t *)addrp;
				} else {
					addrp = realloc(taddr6,
						sizeof (*taddr6) * ntaddr);
					if (addrp == NULL) {
						res = NSS_UNAVAIL;
						break;
					}
					taddr6 = (struct in6_addr *)addrp;
				}
			}

			/*
			 * For non-nscd, save aliases in a temporary buffer
			 * Don't have to do this for nscd as 'buffer' already
			 * contains the required data in the appropriate
			 * format
			 */
			if (hp) {
				abuf = do_aliases(hp, abuf, abuf_end);
				if (abuf == NULL) {
					args->erange = 1;
					res = NSS_NOTFOUND;
					break;
				}
			}
		}
	}

	if (abuf && res == NSS_SUCCESS) {

		/* abuf != NULL implies hp and abuf_start != NULL */

		struct in_addr *addrp;
		struct in6_addr *addrp6;

		if (type == AF_INET) {
			addrp = (struct in_addr *)(ROUND_DOWN(args->buf.buffer +
			    args->buf.buflen, sizeof (*addrp)));
			hp->h_addr_list = (char **)(ROUND_DOWN(addrp -
			    ((nhosts + 1) * sizeof (char *) +
			    (nhosts * sizeof (*addrp))), sizeof (char *)));
			for (i = 0, --addrp; i < nhosts; i++, --addrp) {
				(*(in_addr_t *)addrp) = taddr[i];
				hp->h_addr_list[i] = (char *)addrp;
			}
		} else {
			addrp6 = (struct in6_addr *)
			(ROUND_DOWN(args->buf.buffer + args->buf.buflen,
			sizeof (*addrp6)));
			hp->h_addr_list = (char **)(ROUND_DOWN(addrp6 -
			    ((nhosts + 1) * sizeof (char *) +
			    (nhosts * sizeof (*addrp6))), sizeof (char *)));
			for (i = 0, --addrp6; i < nhosts; i++, --addrp6) {
				(void) memcpy(addrp6, &taddr6[i],
						sizeof (struct in6_addr));
				hp->h_addr_list[i] = (char *)addrp6;
			}
		}

		hp->h_addr_list[nhosts] = 0;
		hp->h_aliases = _nss_netdb_aliases(abuf_start,
		    abuf - abuf_start, args->buf.buffer,
		    (char *)hp->h_addr_list - args->buf.buffer);
		if (hp->h_aliases == 0) {
			args->erange = 1;
			res = NSS_NOTFOUND;
		} else {
			hp->h_name = hp->h_aliases[0];
			hp->h_aliases++;
		}
	}

	/*
	 * stayopen is set to 0 by default in order to close the opened
	 * file.  Some applications may break if it is set to 1.
	 */
	if (!args->stayopen)
		(void) _nss_files_endent(be, 0);

	if (taddr)
		free(taddr);
	if (taddr6)
		free(taddr6);
	if (h_name)
		free(h_name);
	if (abuf_start)
		free(abuf_start);

	return (res);
}


static char *
do_aliases(struct hostent *hp, char *abuf, char *end)
{
	char	**cp;
	size_t	len;

	if ((cp = hp->h_aliases) == NULL)
		return (abuf);

	for (; *cp; cp++) {
		len = strlen(*cp);
		if (abuf+len+1 >= end) {
			return (NULL);
		}
		*abuf++ = ' ';
		(void) memcpy(abuf, *cp, len);
		abuf += len;
	}
	*abuf = '\0';

	return (abuf);
}
