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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <libintl.h>

#include "libcpc.h"

/*
 * Takes a string and converts it to a cpc_set_t.
 *
 * While processing the string using getsubopt(), we will use an array of
 * requests to hold the data, and a proprietary representation of attributes
 * which allow us to avoid a realloc()/bcopy() dance every time we come across
 * a new attribute.
 *
 * Not until after the string has been processed in its entirety do we
 * allocate and specify a request set properly.
 */

/*
 * Leave enough room in token strings for picn, nousern, or sysn where n is
 * picnum.
 */
#define	TOK_SIZE	10

typedef struct __tmp_attr {
	char			*name;
	uint64_t		val;
	struct __tmp_attr	*next;
} tmp_attr_t;

typedef struct __tok_info {
	char			*name;
	int			picnum;
} tok_info_t;

typedef struct __request_t {
	char			cr_event[CPC_MAX_EVENT_LEN];
	uint_t			cr_flags;
	uint_t			cr_nattrs;	/* # CPU-specific attrs */
} request_t;

/*
 * Thread-safe tokenizing context
 */
typedef struct __tctx {
	request_t	*reqs;
	int		nreqs;
	int		ncounters;
	tmp_attr_t	**attrs;
	char		**toks;
	int		ntoks;
	tok_info_t	*tok_info;
	int		(*(*tok_funcs))(struct __tctx *, int, char *);
	char		**attrlist;
	int		nattrs;
	cpc_t		*cpc;
} tctx_t;

typedef int (*(tok_funcp))(struct __tctx *, int, char *);

typedef struct __walker_arg {
	char	*event;
	int	found;
	int	i;
	tctx_t	*tctx;
} walker_arg_t;

static void strtoset_cleanup(tctx_t *tctx);
static void smt_special(tctx_t *tctx, int picnum);
static void *emalloc(size_t n);

/*
 * Clients of cpc_strtoset may set this to specify an error handler during
 * string parsing.
 */
cpc_errhndlr_t		*strtoset_errfn = NULL;

static void
strtoset_err(const char *fmt, ...)
{
	va_list ap;

	if (strtoset_errfn == NULL)
		return;

	va_start(ap, fmt);
	(*strtoset_errfn)("cpc_strtoset", -1, fmt, ap);
	va_end(ap);
}

/*ARGSUSED*/
static void
event_walker(void *arg, uint_t picno, const char *event)
{
	walker_arg_t *warg = (walker_arg_t *)arg;

	if (strncmp(warg->event, event, CPC_MAX_EVENT_LEN) == 0)
		warg->found = 1;
}

static int
event_valid(tctx_t *tctx, int picnum, char *event)
{
	walker_arg_t warg;
	char *end_event;
	int err;

	warg.event = event;
	warg.found = 0;

	cpc_walk_events_pic(tctx->cpc, picnum, &warg, event_walker);

	if (warg.found)
		return (1);

	cpc_walk_generic_events_pic(tctx->cpc, picnum, &warg, event_walker);

	if (warg.found)
		return (1);

	/*
	 * Before assuming this is an invalid event, see if we have been given
	 * a raw event code.
	 * Check the second argument of strtol() to ensure invalid events
	 * beginning with number do not go through.
	 */
	err = errno;
	errno = 0;
	(void) strtol(event, &end_event, 0);
	if ((errno == 0) && (*end_event == '\0')) {
		/*
		 * Success - this is a valid raw code in hex, decimal, or octal.
		 */
		errno = err;
		return (1);
	}

	errno = err;
	return (0);
}

/*
 * An unknown token was encountered; check here if it is an implicit event
 * name. We allow users to omit the "picn=" portion of the event spec, and
 * assign such events to available pics in order they are returned from
 * getsubopt(3C). We start our search for an available pic _after_ the highest
 * picnum to be assigned. This ensures that the event spec can never be out of
 * order; i.e. if the event string is "eventa,eventb" we must ensure that the
 * picnum counting eventa is less than the picnum counting eventb.
 */
static int
find_event(tctx_t *tctx, char *event)
{
	int i;

	/*
	 * Event names cannot have '=' in them. If present here, it means we
	 * have encountered an unknown token (foo=bar, for example).
	 */
	if (strchr(event, '=') != NULL)
		return (0);

	/*
	 * Find the first unavailable pic, after which we must start our search.
	 */
	for (i = tctx->ncounters - 1; i >= 0; i--) {
		if (tctx->reqs[i].cr_event[0] != '\0')
			break;
	}
	/*
	 * If the last counter has been assigned, we cannot place this event.
	 */
	if (i == tctx->ncounters - 1)
		return (0);

	/*
	 * If none of the counters have been assigned yet, i is -1 and we will
	 * begin our search at 0. Else we begin our search at the counter after
	 * the last one currently assigned.
	 */
	i++;

	for (; i < tctx->ncounters; i++) {
		if (event_valid(tctx, i, event) == 0)
			continue;

		tctx->nreqs++;
		(void) strncpy(tctx->reqs[i].cr_event, event,
		    CPC_MAX_EVENT_LEN);
		return (1);
	}

	return (0);
}

static int
pic(tctx_t *tctx, int tok, char *val)
{
	int picnum;

	if (!tctx || !tctx->tok_info)
		return (-1);

	picnum = tctx->tok_info[tok].picnum;

	/*
	 * Make sure the each pic only appears in the spec once.
	 */
	if (tctx->reqs[picnum].cr_event[0] != '\0') {
		strtoset_err(gettext("repeated 'pic%d' token\n"), picnum);
		return (-1);
	}

	if (val == NULL || val[0] == '\0') {
		strtoset_err(gettext("missing 'pic%d' value\n"), picnum);
		return (-1);
	}

	if (event_valid(tctx, picnum, val) == 0) {
		strtoset_err(gettext("pic%d cannot measure event '%s' on this "
		    "cpu\n"), picnum, val);
		return (-1);
	}

	tctx->nreqs++;
	(void) strncpy(tctx->reqs[picnum].cr_event, val, CPC_MAX_EVENT_LEN);
	return (0);
}

/*
 * We explicitly ignore any value provided for these tokens, as their
 * mere presence signals us to turn on or off the relevant flags.
 */
/*ARGSUSED*/
static int
flag(tctx_t *tctx, int tok, char *val)
{
	int i;
	int picnum;

	if (!tctx || !tctx->tok_info)
		return (-1);

	picnum = tctx->tok_info[tok].picnum;
	/*
	 * If picnum is -1, this flag should be applied to all reqs.
	 */
	for (i = (picnum == -1) ? 0 : picnum; i < tctx->ncounters; i++) {
		if (strcmp(tctx->tok_info[tok].name, "nouser") == 0)
			tctx->reqs[i].cr_flags &= ~CPC_COUNT_USER;
		else if (strcmp(tctx->tok_info[tok].name, "sys") == 0)
			tctx->reqs[i].cr_flags |= CPC_COUNT_SYSTEM;
		else
			return (-1);

		if (picnum != -1)
			break;
	}

	return (0);
}

static int
doattr(tctx_t *tctx, int tok, char *val)
{
	int		i;
	int		picnum;
	tmp_attr_t	*tmp;
	char		*endptr;

	if (!tctx || !tctx->tok_info)
		return (-1);

	picnum = tctx->tok_info[tok].picnum;

	/*
	 * If picnum is -1, this attribute should be applied to all reqs.
	 */
	for (i = (picnum == -1) ? 0 : picnum; i < tctx->ncounters; i++) {
		tmp = (tmp_attr_t *)emalloc(sizeof (tmp_attr_t));
		tmp->name = tctx->tok_info[tok].name;
		if (val != NULL) {
			tmp->val = strtoll(val, &endptr, 0);
			if (endptr == val) {
				strtoset_err(gettext("invalid value '%s' for "
				    "attribute '%s'\n"), val, tmp->name);
				free(tmp);
				return (-1);
			}
		} else
			/*
			 * No value was provided for this attribute,
			 * so specify a default value of 1.
			 */
			tmp->val = 1;

		tmp->next = tctx->attrs[i];
		tctx->attrs[i] = tmp;
		tctx->reqs[i].cr_nattrs++;

		if (picnum != -1)
			break;
	}

	return (0);
}

/*ARGSUSED*/
static void
attr_count_walker(void *arg, const char *attr)
{
	/*
	 * We don't allow picnum to be specified by the user.
	 */
	if (strncmp(attr, "picnum", 7) == 0)
		return;
	(*(int *)arg)++;
}

static int
cpc_count_attrs(cpc_t *cpc)
{
	int nattrs = 0;

	cpc_walk_attrs(cpc, &nattrs, attr_count_walker);

	return (nattrs);
}

static void
attr_walker(void *arg, const char *attr)
{
	walker_arg_t *warg = (walker_arg_t *)arg;

	if (strncmp(attr, "picnum", 7) == 0)
		return;

	if ((warg->tctx->attrlist[warg->i] = strdup(attr)) == NULL) {
		strtoset_err(gettext("no memory available\n"));
		exit(0);
	}
	warg->i++;
}

cpc_set_t *
cpc_strtoset(cpc_t *cpcin, const char *spec, int smt)
{
	cpc_set_t		*set;
	cpc_attr_t		*req_attrs;
	tmp_attr_t		*tmp;
	size_t			toklen;
	int			i;
	int			j;
	int			x;
	char			*opts;
	char			*val;
	tctx_t			*tctx;
	walker_arg_t		warg;

	tctx = emalloc(sizeof (tctx_t));

	tctx->cpc = cpcin;

	tctx->nattrs = 0;

	tctx->ncounters = cpc_npic(tctx->cpc);
	tctx->reqs = emalloc(tctx->ncounters * sizeof (request_t));
	tctx->attrs = emalloc(tctx->ncounters * sizeof (tmp_attr_t *));

	for (i = 0; i < tctx->ncounters; i++) {
		tctx->reqs[i].cr_event[0] = '\0';
		tctx->reqs[i].cr_flags = CPC_COUNT_USER;
		/*
		 * Each pic will have at least one attribute: the physical pic
		 * assignment via the "picnum" attribute. Set that up here for
		 * each request.
		 */
		tctx->reqs[i].cr_nattrs = 1;
		tctx->attrs[i] = emalloc(sizeof (tmp_attr_t));
		tctx->attrs[i]->name = "picnum";
		tctx->attrs[i]->val = i;
		tctx->attrs[i]->next = NULL;
	}

	/*
	 * Build up a list of acceptable tokens.
	 *
	 * Permitted tokens are
	 * picn=event
	 * nousern
	 * sysn
	 * attrn=val
	 * nouser
	 * sys
	 * attr=val
	 *
	 * Where n is a counter number, and attr is any attribute supported by
	 * the current processor.
	 *
	 * If a token appears without a counter number, it applies to all
	 * counters in the request set.
	 *
	 * The number of tokens is:
	 *
	 * picn: ncounters
	 * generic flags: 2 * ncounters (nouser, sys)
	 * attrs: nattrs * ncounters
	 * attrs with no picnum: nattrs
	 * generic flags with no picnum: 2 (nouser, sys)
	 * NULL token to signify end of list to getsubopt(3C).
	 *
	 * Matching each token's index in the token table is a function which
	 * process that token; these are in tok_funcs.
	 */

	/*
	 * Count the number of valid attributes.
	 * Set up the attrlist array to point to the attributes in attrlistp.
	 */
	tctx->nattrs = cpc_count_attrs(tctx->cpc);
	tctx->attrlist = (char **)emalloc(tctx->nattrs * sizeof (char *));

	warg.tctx = tctx;
	warg.i = 0;

	cpc_walk_attrs(tctx->cpc, &warg, attr_walker);

	tctx->ntoks = tctx->ncounters +
	    (2 * tctx->ncounters) +
	    (tctx->nattrs * tctx->ncounters) + tctx->nattrs + 3;

	tctx->toks = emalloc(tctx->ntoks * sizeof (char *));
	tctx->tok_info = emalloc(tctx->ntoks * sizeof (tok_info_t));
	tctx->tok_funcs = emalloc(tctx->ntoks * sizeof (tok_funcp));

	for (i = 0; i < tctx->ntoks; i++) {
		tctx->toks[i] = NULL;
		tctx->tok_funcs[i] = NULL;
	}

	x = 0;
	for (i = 0; i < tctx->ncounters; i++) {
		tctx->toks[x] = emalloc(TOK_SIZE);
		(void) snprintf(tctx->toks[x], TOK_SIZE, "pic%d", i);
		tctx->tok_info[x].name = "pic";
		tctx->tok_info[i].picnum = i;
		tctx->tok_funcs[x] = pic;
		x++;
	}

	for (i = 0; i < tctx->ncounters; i++) {
		tctx->toks[x] = emalloc(TOK_SIZE);
		(void) snprintf(tctx->toks[x], TOK_SIZE, "nouser%d", i);
		tctx->tok_info[x].name = "nouser";
		tctx->tok_info[x].picnum = i;
		tctx->tok_funcs[x] = flag;
		x++;
	}

	for (i = 0; i < tctx->ncounters; i++) {
		tctx->toks[x] = emalloc(TOK_SIZE);
		(void) snprintf(tctx->toks[x], TOK_SIZE, "sys%d", i);
		tctx->tok_info[x].name = "sys";
		tctx->tok_info[x].picnum = i;
		tctx->tok_funcs[x] = flag;
		x++;
	}
	for (j = 0; j < tctx->nattrs; j++) {
		toklen = strlen(tctx->attrlist[j]) + 3;
		for (i = 0; i < tctx->ncounters; i++) {
			tctx->toks[x] = emalloc(toklen);
			(void) snprintf(tctx->toks[x], toklen, "%s%d",
			    tctx->attrlist[j], i);
			tctx->tok_info[x].name = tctx->attrlist[j];
			tctx->tok_info[x].picnum = i;
			tctx->tok_funcs[x] = doattr;
			x++;
		}

		/*
		 * Now create a token for this attribute with no picnum;
		 * if used it will be applied to all reqs.
		 */
		tctx->toks[x] = emalloc(toklen);
		(void) snprintf(tctx->toks[x], toklen, "%s",
		    tctx->attrlist[j]);
		tctx->tok_info[x].name = tctx->attrlist[j];
		tctx->tok_info[x].picnum = -1;
		tctx->tok_funcs[x] = doattr;
		x++;
	}

	tctx->toks[x] = strdup("nouser");
	tctx->tok_info[x].name = strdup("nouser");
	tctx->tok_info[x].picnum = -1;
	tctx->tok_funcs[x] = flag;
	x++;

	tctx->toks[x] = strdup("sys");
	tctx->tok_info[x].name = strdup("sys");
	tctx->tok_info[x].picnum = -1;
	tctx->tok_funcs[x] = flag;
	x++;

	tctx->toks[x] = NULL;

	opts = strdup(spec);

	while (*opts != '\0') {
		int idx = getsubopt(&opts, tctx->toks, &val);

		if (idx == -1) {
			if (find_event(tctx, val) == 0) {
				strtoset_err(gettext("bad token '%s'\n"), val);
				goto inval;
			} else
				continue;
		}

		if (tctx->tok_funcs[idx](tctx, idx, val) == -1)
			goto inval;
	}

	/*
	 * The string has been processed. Now count how many PICs were used,
	 * create a request set, and specify each request properly.
	 */
	if ((set = cpc_set_create(tctx->cpc)) == NULL) {
		strtoset_err(gettext("no memory available\n"));
		exit(0);
	}

	for (i = 0; i < tctx->ncounters; i++) {
		if (tctx->reqs[i].cr_event[0] == '\0')
			continue;

		/*
		 * If the caller wishes to measure events on the physical CPU,
		 * we need to add SMT attributes to each request.
		 */
		if (smt)
			smt_special(tctx, i);

		req_attrs = emalloc(tctx->reqs[i].cr_nattrs *
		    sizeof (cpc_attr_t));

		j = 0;
		for (tmp = tctx->attrs[i]; tmp != NULL; tmp = tmp->next) {
			req_attrs[j].ca_name = tmp->name;
			req_attrs[j].ca_val = tmp->val;
			j++;
		}

		if (cpc_set_add_request(tctx->cpc, set, tctx->reqs[i].cr_event,
		    0, tctx->reqs[i].cr_flags,
		    tctx->reqs[i].cr_nattrs, req_attrs) == -1) {
			free(req_attrs);
			(void) cpc_set_destroy(tctx->cpc, set);
			strtoset_err(
			    gettext("cpc_set_add_request() failed: %s\n"),
			    strerror(errno));
			goto inval;
		}

		free(req_attrs);
	}

	strtoset_cleanup(tctx);

	return (set);

inval:
	strtoset_cleanup(tctx);
	errno = EINVAL;
	return (NULL);
}

static void
strtoset_cleanup(tctx_t *tctx)
{
	int		i;
	tmp_attr_t	*tmp, *p;

	if (tctx->reqs) {
		free(tctx->reqs);
		tctx->reqs = NULL;
	}

	if (tctx->attrs) {
		for (i = 0; i < tctx->ncounters; i++)
			for (tmp = tctx->attrs[i]; tmp; tmp = p) {
				p = tmp->next;
				free(tmp);
			}
		free(tctx->attrs);
		tctx->attrs = NULL;
	}

	if (tctx->attrlist) {
		for (i = 0; i < tctx->nattrs; i++)
			if (tctx->attrlist[i])
				free(tctx->attrlist[i]);
		free(tctx->attrlist);
		tctx->attrlist = NULL;
	}

	if (tctx->toks) {
		for (i = 0; i < tctx->ntoks; i++)
			if (tctx->toks[i])
				free(tctx->toks[i]);
		free(tctx->toks);
		tctx->toks = NULL;
	}

	if (tctx->tok_info) {
		free(tctx->tok_info);
		tctx->tok_info = NULL;
	}

	if (tctx->tok_funcs) {
		free(tctx->tok_funcs);
		tctx->tok_funcs = NULL;
	}
}

/*
 * The following is called to modify requests so that they count events on
 * behalf of a physical processor, instead of a logical processor. It duplicates
 * the request flags for the sibling processor (i.e. if the request counts user
 * events, add an attribute to count user events on the sibling processor also).
 */
static void
smt_special(tctx_t *tctx, int picnum)
{
	tmp_attr_t *attr;

	if (!tctx || !tctx->reqs)
		return;

	if (tctx->reqs[picnum].cr_flags & CPC_COUNT_USER) {
		attr = emalloc(sizeof (tmp_attr_t));
		attr->name = "count_sibling_usr";
		attr->val = 1;
		attr->next = tctx->attrs[picnum];
		tctx->attrs[picnum] = attr;
		tctx->reqs[picnum].cr_nattrs++;
	}

	if (tctx->reqs[picnum].cr_flags & CPC_COUNT_SYSTEM) {
		attr = emalloc(sizeof (tmp_attr_t));
		attr->name = "count_sibling_sys";
		attr->val = 1;
		attr->next = tctx->attrs[picnum];
		tctx->attrs[picnum] = attr;
		tctx->reqs[picnum].cr_nattrs++;
	}
}

/*
 * If we ever fail to get memory, we print an error message and exit.
 */
static void *
emalloc(size_t n)
{
	void *p = malloc(n);

	if (p == NULL) {
		strtoset_err(gettext("no memory available\n"));
		exit(0);
	}

	return (p);
}
