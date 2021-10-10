/*
 * Copyright 2001-2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"


/*
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation. Portions created by Netscape are
 * Copyright (C) 1998-1999 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s):
 */
/*
 *  Copyright (c) 1996 Regents of the University of Michigan.
 *  All rights reserved.
 *
 */
/*  LIBLDAP url.c -- LDAP URL related routines
 *
 *  LDAP URLs look like this:
 *    l d a p : / / hostport / dn [ ? attributes [ ? scope [ ? filter ] ] ]
 *
 *  where:
 *   attributes is a comma separated list
 *   scope is one of these three strings:  base one sub (default=base)
 *   filter is an string-represented filter as in RFC 1558
 *
 *  e.g.,  ldap://ldap.itd.umich.edu/c=US?o,description?one?o=umich
 *
 *  We also tolerate URLs that look like: <ldapurl> and <URL:ldapurl>
 */

#if 0
#ifndef lint 
static char copyright[] = "@(#) Copyright (c) 1996 Regents of the University of Michigan.\nAll rights reserved.\n";
#endif
#endif

#include "ldap-int.h"


static int skip_url_prefix( const char **urlp, int *enclosedp, int *securep );


int
LDAP_CALL
ldap_is_ldap_url( const char *url )
{
	int	enclosed, secure;

	return( url != NULL
	    && skip_url_prefix( &url, &enclosed, &secure ));
}


static int
skip_url_prefix( const char **urlp, int *enclosedp, int *securep )
{
/*
 * return non-zero if this looks like a LDAP URL; zero if not
 * if non-zero returned, *urlp will be moved past "ldap://" part of URL
 * The data that *urlp points to is not changed by this function.
 */
	if ( *urlp == NULL ) {
		return( 0 );
	}

	/* skip leading '<' (if any) */
	if ( **urlp == '<' ) {
		*enclosedp = 1;
		++*urlp;
	} else {
		*enclosedp = 0;
	}

	/* skip leading "URL:" (if any) */
	if ( strlen( *urlp ) >= LDAP_URL_URLCOLON_LEN && strncasecmp(
	    *urlp, LDAP_URL_URLCOLON, LDAP_URL_URLCOLON_LEN ) == 0 ) {
		*urlp += LDAP_URL_URLCOLON_LEN;
	}

	/* check for an "ldap://" prefix */
	if ( strlen( *urlp ) >= LDAP_URL_PREFIX_LEN && strncasecmp( *urlp,
	    LDAP_URL_PREFIX, LDAP_URL_PREFIX_LEN ) == 0 ) {
		/* skip over URL prefix and return success */
		*urlp += LDAP_URL_PREFIX_LEN;
		*securep = 0;
		return( 1 );
	}

	/* check for an "ldaps://" prefix */
	if ( strlen( *urlp ) >= LDAPS_URL_PREFIX_LEN && strncasecmp( *urlp,
	    LDAPS_URL_PREFIX, LDAPS_URL_PREFIX_LEN ) == 0 ) {
		/* skip over URL prefix and return success */
		*urlp += LDAPS_URL_PREFIX_LEN;
		*securep = 1;
		return( 1 );
	}

	return( 0 );	/* not an LDAP URL */
}


int
LDAP_CALL
ldap_url_parse( const char *url, LDAPURLDesc **ludpp )
{
/*
 *  Pick apart the pieces of an LDAP URL.
 */
	int	rc;

	if (( rc = nsldapi_url_parse( url, ludpp, 1 )) == 0 ) {
		if ( (*ludpp)->lud_scope == -1 ) {
			(*ludpp)->lud_scope = LDAP_SCOPE_BASE;
		}
		if ( (*ludpp)->lud_filter == NULL ) {
			(*ludpp)->lud_filter = "(objectclass=*)";
		}
		if ( *((*ludpp)->lud_dn) == '\0' ) {
			(*ludpp)->lud_dn = NULL;
		}
	}

	return( rc );
}

/* same as ldap_url_parse(), but dn is not require */
int
LDAP_CALL
ldap_url_parse_nodn(const char *url, LDAPURLDesc **ludpp)
{
/*
 *  Pick apart the pieces of an LDAP URL.
 */
	int	rc;

	if ((rc = nsldapi_url_parse(url, ludpp, 0)) == 0) {
		if ((*ludpp)->lud_scope == -1) {
			(*ludpp)->lud_scope = LDAP_SCOPE_BASE;
		}
		if ((*ludpp)->lud_filter == NULL) {
			(*ludpp)->lud_filter = "(objectclass=*)";
		}
		if ((*ludpp)->lud_dn && *((*ludpp)->lud_dn) == '\0') {
			(*ludpp)->lud_dn = NULL;
		}
	}

	return (rc);
}


/*
 * like ldap_url_parse() with a few exceptions:
 *   1) if dn_required is zero, a missing DN does not generate an error
 *	(we just leave the lud_dn field NULL)
 *   2) no defaults are set for lud_scope and lud_filter (they are set to -1
 *	and NULL respectively if no SCOPE or FILTER are present in the URL).
 *   3) when there is a zero-length DN in a URL we do not set lud_dn to NULL.
 *   4) if an LDAPv3 URL extensions are included, 
 */
int
nsldapi_url_parse( const char *url, LDAPURLDesc **ludpp, int dn_required )
{

	LDAPURLDesc	*ludp;
	char		*urlcopy, *attrs, *scope, *extensions = NULL, *p, *q;
	int		enclosed, secure, i, nattrs, at_start;

	LDAPDebug( LDAP_DEBUG_TRACE, "nsldapi_url_parse(%s)\n", url, 0, 0 );

	if ( url == NULL || ludpp == NULL ) {
		return( LDAP_URL_ERR_PARAM );
	}

	*ludpp = NULL;	/* pessimistic */

	if ( !skip_url_prefix( &url, &enclosed, &secure )) {
		return( LDAP_URL_ERR_NOTLDAP );
	}

	/* allocate return struct */
	if (( ludp = (LDAPURLDesc *)NSLDAPI_CALLOC( 1, sizeof( LDAPURLDesc )))
	    == NULLLDAPURLDESC ) {
		return( LDAP_URL_ERR_MEM );
	}

	if ( secure ) {
		ludp->lud_options |= LDAP_URL_OPT_SECURE;
	}

	/* make working copy of the remainder of the URL */
	if (( urlcopy = nsldapi_strdup( url )) == NULL ) {
		ldap_free_urldesc( ludp );
		return( LDAP_URL_ERR_MEM );
	}

	if ( enclosed && *((p = urlcopy + strlen( urlcopy ) - 1)) == '>' ) {
		*p = '\0';
	}

	/* initialize scope and filter */
	ludp->lud_scope = -1;
	ludp->lud_filter = NULL;

	/* lud_string is the only malloc'd string space we use */
	ludp->lud_string = urlcopy;

	/* scan forward for '/' that marks end of hostport and begin. of dn */
	if (( ludp->lud_dn = strchr( urlcopy, '/' )) == NULL ) {
		if ( dn_required ) {
			ldap_free_urldesc( ludp );
			return( LDAP_URL_ERR_NODN );
		}
	} else {
		/* terminate hostport; point to start of dn */
		*ludp->lud_dn++ = '\0';
	}


	if ( *urlcopy == '\0' ) {
		ludp->lud_host = NULL;
	} else {
		ludp->lud_host = urlcopy;
		nsldapi_hex_unescape( ludp->lud_host );

		/*
		 * Locate and strip off optional port number (:#) in host
		 * portion of URL.
		 *
		 * If more than one space-separated host is listed, we only
		 * look for a port number within the right-most one since
		 * ldap_init() will handle host parameters that look like
		 * host:port anyway.
		 */
		if (( p = strrchr( ludp->lud_host, ' ' )) == NULL ) {
			p = ludp->lud_host;
		} else {
			++p;
		}
                if ( *p == '[' && ( q = strchr( p, ']' )) != NULL ) {
                         /* square brackets present -- skip past them */
                        p = q++;
                }
		if (( p = strchr( p, ':' )) != NULL ) {
			*p++ = '\0';
			ludp->lud_port = atoi( p );
			if ( *ludp->lud_host == '\0' ) {
				/*
				 * no hostname and a port: invalid hostcode
				 * according to RFC 1738
				 */
				ldap_free_urldesc(ludp);
				return (LDAP_URL_ERR_HOSTPORT);
			}
		}
	}

	/* scan for '?' that marks end of dn and beginning of attributes */
	attrs = NULL;
	if ( ludp->lud_dn != NULL &&
	    ( attrs = strchr( ludp->lud_dn, '?' )) != NULL ) {
		/* terminate dn; point to start of attrs. */
		*attrs++ = '\0';

		/* scan for '?' that marks end of attrs and begin. of scope */
		if (( p = strchr( attrs, '?' )) != NULL ) {
			/*
			 * terminate attrs; point to start of scope and scan for
			 * '?' that marks end of scope and begin. of filter
			 */
			*p++ = '\0';
                        scope = p;

                        if (( p = strchr( scope, '?' )) != NULL ) {
                                /* terminate scope; point to start of filter */
                                *p++ = '\0';
                                if ( *p != '\0' ) {
                                        ludp->lud_filter = p;
                                        /*
                                         * scan for the '?' that marks the end
                                         * of the filter and the start of any
                                         * extensions
                                         */
                                        if (( p = strchr( ludp->lud_filter, '?' ))
                                            != NULL ) {
                                                *p++ = '\0'; /* term. filter */
                                                extensions = p;
                                        }
                                        if ( *ludp->lud_filter == '\0' ) {
                                                ludp->lud_filter = NULL;
                                        } else {
                                                nsldapi_hex_unescape( ludp->lud_filter );
                                        }
                                }
                        }


                        if ( strcasecmp( scope, "one" ) == 0 ) {
                                ludp->lud_scope = LDAP_SCOPE_ONELEVEL;
                        } else if ( strcasecmp( scope, "base" ) == 0 ) {
                                ludp->lud_scope = LDAP_SCOPE_BASE;
                        } else if ( strcasecmp( scope, "sub" ) == 0 ) {
                                ludp->lud_scope = LDAP_SCOPE_SUBTREE;
                        } else if ( *scope != '\0' ) {
                                ldap_free_urldesc( ludp );
                                return( LDAP_URL_ERR_BADSCOPE );
                        }
		}
	}

	if ( ludp->lud_dn != NULL ) {
		nsldapi_hex_unescape( ludp->lud_dn );
	}

	/*
	 * if attrs list was included, turn it into a null-terminated array
	 */
	if ( attrs != NULL && *attrs != '\0' ) {
		nsldapi_hex_unescape( attrs );
		for ( nattrs = 1, p = attrs; *p != '\0'; ++p ) {
		    if ( *p == ',' ) {
			    ++nattrs;
		    }
		}

		if (( ludp->lud_attrs = (char **)NSLDAPI_CALLOC( nattrs + 1,
		    sizeof( char * ))) == NULL ) {
			ldap_free_urldesc( ludp );
			return( LDAP_URL_ERR_MEM );
		}

		for ( i = 0, p = attrs; i < nattrs; ++i ) {
			ludp->lud_attrs[ i ] = p;
			if (( p = strchr( p, ',' )) != NULL ) {
				*p++ ='\0';
			}
			nsldapi_hex_unescape( ludp->lud_attrs[ i ] );
		}
	}

        /* if extensions list was included, check for critical ones */
        if ( extensions != NULL && *extensions != '\0' ) {
                /* Note: at present, we do not recognize ANY extensions */
                at_start = 1;
                for ( p = extensions; *p != '\0'; ++p ) {
                        if ( at_start ) {
                                if ( *p == '!' ) {      /* critical extension */
                                        ldap_free_urldesc( ludp );
                                        /* this is what iplanet did *
                                        return( LDAP_URL_UNRECOGNIZED_CRITICAL_EXTENSION );
                                         * and this is what we do */
                                        return( LDAP_URL_ERR_PARAM );
                                }
                                at_start = 0;
                        } else if ( *p == ',' ) {
                                at_start = 1;
                        }
                }
        }


	*ludpp = ludp;

	return( 0 );
}


void
LDAP_CALL
ldap_free_urldesc( LDAPURLDesc *ludp )
{
	if ( ludp != NULLLDAPURLDESC ) {
		if ( ludp->lud_string != NULL ) {
			NSLDAPI_FREE( ludp->lud_string );
		}
		if ( ludp->lud_attrs != NULL ) {
			NSLDAPI_FREE( ludp->lud_attrs );
		}
		NSLDAPI_FREE( ludp );
	}
}


int
LDAP_CALL
ldap_url_search( LDAP *ld, const char *url, int attrsonly )
{
	int		err, msgid;
	LDAPURLDesc	*ludp;
	BerElement	*ber;
	LDAPServer	*srv;
	char		*host;

	if ( !NSLDAPI_VALID_LDAP_POINTER( ld )) {
		return( -1 );		/* punt */
	}

	if ( ldap_url_parse( url, &ludp ) != 0 ) {
		LDAP_SET_LDERRNO( ld, LDAP_PARAM_ERROR, NULL, NULL );
		return( -1 );
	}

	LDAP_MUTEX_LOCK( ld, LDAP_MSGID_LOCK );
	msgid = ++ld->ld_msgid;
	LDAP_MUTEX_UNLOCK( ld, LDAP_MSGID_LOCK );

	if ( nsldapi_build_search_req( ld, ludp->lud_dn, ludp->lud_scope,
	    ludp->lud_filter, ludp->lud_attrs, attrsonly, NULL, NULL,
	    -1, -1, msgid, &ber ) != LDAP_SUCCESS ) {
		return( -1 );
	}

	err = 0;

	if ( ludp->lud_host == NULL ) {
		host = ld->ld_defhost;
	} else {
		host = ludp->lud_host;
	}

	if (( srv = (LDAPServer *)NSLDAPI_CALLOC( 1, sizeof( LDAPServer )))
	    == NULL || ( host != NULL &&
	    ( srv->lsrv_host = nsldapi_strdup( host )) == NULL )) {
		if ( srv != NULL ) {
			NSLDAPI_FREE( srv );
		}
		LDAP_SET_LDERRNO( ld, LDAP_NO_MEMORY, NULL, NULL );
		err = -1;
	} else {
                if ( ludp->lud_port != 0 ) {
                        /* URL includes a port - use it */
                         srv->lsrv_port = ludp->lud_port;
                } else if ( ludp->lud_host == NULL ) {
                        /* URL has no port or host - use port from ld */
                        srv->lsrv_port = ld->ld_defport;
                } else if (( ludp->lud_options & LDAP_URL_OPT_SECURE ) == 0 ) {
                        /* ldap URL has a host but no port - use std. port */
                        srv->lsrv_port = LDAP_PORT;
                } else {
                        /* ldaps URL has a host but no port - use std. port */
                        srv->lsrv_port = LDAPS_PORT;
                }
	}

	if (( ludp->lud_options & LDAP_URL_OPT_SECURE ) != 0 ) {
		srv->lsrv_options |= LDAP_SRV_OPT_SECURE;
	}

	if ( err != 0 ) {
		ber_free( ber, 1 );
	} else {
		err = nsldapi_send_server_request( ld, ber, msgid, NULL, srv,
		    NULL, NULL, 1 );
	}

	ldap_free_urldesc( ludp );
	return( err );
}


int
LDAP_CALL
ldap_url_search_st( LDAP *ld, const char *url, int attrsonly,
	struct timeval *timeout, LDAPMessage **res )
{
	int	msgid;

	/*
	 * It is an error to pass in a zero'd timeval.
	 */
	if ( timeout != NULL && timeout->tv_sec == 0 &&
	    timeout->tv_usec == 0 ) {
		if ( ld != NULL ) {
			LDAP_SET_LDERRNO( ld, LDAP_PARAM_ERROR, NULL, NULL );
		}
		if ( res != NULL ) {
			*res = NULL;
		}
                return( LDAP_PARAM_ERROR );
        }

	if (( msgid = ldap_url_search( ld, url, attrsonly )) == -1 ) {
		return( LDAP_GET_LDERRNO( ld, NULL, NULL ) );
	}

	if ( ldap_result( ld, msgid, 1, timeout, res ) == -1 ) {
		return( LDAP_GET_LDERRNO( ld, NULL, NULL ) );
	}

	if ( LDAP_GET_LDERRNO( ld, NULL, NULL ) == LDAP_TIMEOUT ) {
		(void) ldap_abandon( ld, msgid );
		LDAP_SET_LDERRNO( ld, LDAP_TIMEOUT, NULL, NULL );
		return( LDAP_TIMEOUT );
	}

	return( ldap_result2error( ld, *res, 0 ));
}


int
LDAP_CALL
ldap_url_search_s( LDAP *ld, const char *url, int attrsonly, LDAPMessage **res )
{
	int	msgid;

	if (( msgid = ldap_url_search( ld, url, attrsonly )) == -1 ) {
		return( LDAP_GET_LDERRNO( ld, NULL, NULL ) );
	}

	if ( ldap_result( ld, msgid, 1, (struct timeval *)NULL, res ) == -1 ) {
		return( LDAP_GET_LDERRNO( ld, NULL, NULL ) );
	}

	return( ldap_result2error( ld, *res, 0 ));
}

#ifdef _SOLARIS_SDK
/*
 * Locate the LDAP URL associated with a DNS domain name.
 *
 * The supplied DNS domain name is converted into a distinguished
 * name. The directory entry specified by that distinguished name
 * is searched for a labeledURI attribute. If successful then the
 * LDAP URL is returned. If unsuccessful then that entry's parent
 * is searched and so on until the target distinguished name is
 * reduced to only two nameparts.
 *
 * For example, if 'ny.eng.wiz.com' is the DNS domain then the
 * following entries are searched until one succeeds:
 *              dc=ny,dc=eng,dc=wiz,dc=com
 *              dc=eng,dc=wiz,dc=com
 *              dc=wiz,dc=com
 *
 * If dns_name is NULL then the environment variable LOCALDOMAIN is used.
 * If attrs is not NULL then it is appended to the URL's attribute list.
 * If scope is not NULL then it overrides the URL's scope.
 * If filter is not NULL then it is merged with the URL's filter.
 *
 * If an error is encountered then zero is returned, otherwise a string
 * URL is returned. The caller should free the returned string if it is
 * non-zero.
 */

char *
ldap_dns_to_url(
        LDAP    *ld,
        char    *dns_name,
        char    *attrs,
        char    *scope,
        char    *filter
)
{
        char            *dn;
        char            *url = 0;
        char            *url2 = 0;
        LDAPURLDesc     *urldesc;
        char            *cp;
        char            *cp2;
        size_t          attrs_len = 0;
        size_t          scope_len = 0;
        size_t          filter_len = 0;
        int             nameparts;
        int             no_attrs = 0;
        int             no_scope = 0;

        if (dns_name == 0) {
                dns_name = (char *)getenv("LOCALDOMAIN");
        }

        if ((ld == NULL) || ((dn = ldap_dns_to_dn(dns_name, &nameparts)) ==
            NULL))
                return (0);

        if ((url = ldap_dn_to_url(ld, dn, nameparts)) == NULL) {
                free(dn);
                return (0);
        }
        free(dn);

        /* merge filter and/or scope and/or attributes with URL */
        if (attrs || scope || filter) {

                if (attrs)
                        attrs_len = strlen(attrs) + 2; /* for comma and NULL */

                if (scope)
                        scope_len = strlen(scope) + 1; /* for NULL */

                if (filter)
                        filter_len = strlen(filter) + 4;
                            /* for ampersand, parentheses and NULL */

                if (ldap_is_ldap_url(url)) {

                        if ((url2 = (char *)malloc(attrs_len + scope_len +
                            filter_len + strlen(url) + 1)) == NULL) {
                                return (0);
                        }
                        cp = url;
                        cp2 = url2;

                        /* copy URL scheme, hostname, port number and DN */
                        while (*cp && (*cp != '?')) {
                                *cp2++ = *cp++;
                        }

                        /* handle URL attributes */

                        if (*cp == '?') {       /* test first '?' */
                                *cp2++ = *cp++; /* copy first '?' */

                                if (*cp == '?') {       /* test second '?' */

                                        /* insert supplied attributes */
                                        if (attrs) {
                                                while (*attrs) {
                                                        *cp2++ = *attrs++;
                                                }
                                        } else {
                                                no_attrs = 1;
                                        }

                                } else {

                                        /* copy URL attributes */
                                        while (*cp && (*cp != '?')) {
                                                *cp2++ = *cp++;
                                        }

                                        /* append supplied attributes */
                                        if (attrs) {
                                                *cp2++ = ',';
                                                while (*attrs) {
                                                        *cp2++ = *attrs++;
                                                }
                                        }
                                }

                        } else {
                                /* append supplied attributes */
                                if (attrs) {
                                        *cp2++ = '?';
                                        while (*attrs) {
                                                *cp2++ = *attrs++;
                                       }
                                } else {
                                        no_attrs = 1;
                                }
                        }

                        /* handle URL scope */

                        if (*cp == '?') {       /* test second '?' */
                                *cp2++ = *cp++; /* copy second '?' */

                                if (*cp == '?') {       /* test third '?' */

                                        /* insert supplied scope */
                                        if (scope) {
                                                while (*scope) {
                                                        *cp2++ = *scope++;
                                                }
                                        } else {
                                                no_scope = 1;
                                        }

                                } else {

                                        if (scope) {
                                                /* skip over URL scope */
                                                while (*cp && (*cp != '?')) {
                                                        *cp++;
                                                }
                                                /* insert supplied scope */
                                                while (*scope) {
                                                        *cp2++ = *scope++;
                                                }
                                        } else {

                                                /* copy URL scope */
                                                while (*cp && (*cp != '?')) {
                                                        *cp2++ = *cp++;
                                                }
                                        }
                                }

                        } else {
                                /* append supplied scope */
                                if (scope) {
                                        if (no_attrs) {
                                                *cp2++ = '?';
                                        }
                                        *cp2++ = '?';
                                        while (*scope) {
                                                *cp2++ = *scope++;
                                        }
                                } else {
                                        no_scope = 1;
                                }
                        }

                        /* handle URL filter */

                        if (*cp == '?') {       /* test third '?' */
                                *cp2++ = *cp++; /* copy third '?' */

                                if (filter) {

                                        /* merge URL and supplied filters */

                                        *cp2++ = '(';
                                        *cp2++ = '&';
                                        /* copy URL filter */
                                        while (*cp) {
                                                *cp2++ = *cp++;
                                        }
                                        /* append supplied filter */
                                        while (*filter) {
                                                *cp2++ = *filter++;
                                        }
                                        *cp2++ = ')';
                                } else {

                                        /* copy URL filter */
                                        while (*cp) {
                                                *cp2++ = *cp++;
                                        }
                                }

                        } else {
                                /* append supplied filter */
                                if (filter) {
                                        if (no_scope) {
                                                if (no_attrs) {
                                                        *cp2++ = '?';
                                                }
                                                *cp2++ = '?';
                                        }
                                        *cp2++ = '?';
                                        while (*filter) {
                                                *cp2++ = *filter++;
                                        }
                                }
                        }

                        *cp2++ = '\0';
                        free (url);
                        url = url2;

                } else {
                        return (0);     /* not an LDAP URL */
                }
        }
        return (url);
}

/*
 * Locate the LDAP URL associated with a distinguished name.
 *
 * The number of nameparts in the supplied distinguished name must be
 * provided. The specified directory entry is searched for a labeledURI
 * attribute. If successful then the LDAP URL is returned. If unsuccessful
 * then that entry's parent is searched and so on until the target
 * distinguished name is reduced to only two nameparts.
 *
 * For example, if 'l=ny,ou=eng,o=wiz,c=us' is the distinguished name
 * then the following entries are searched until one succeeds:
 *              l=ny,ou=eng,o=wiz,c=us
 *              ou=eng,o=wiz,c=us
 *              o=wiz,c=us
 *
 * If an error is encountered then zero is returned, otherwise a string
 * URL is returned. The caller should free the returned string if it is
 * non-zero.
 */

char *
ldap_dn_to_url(
        LDAP    *ld,
        char    *dn,
        int     nameparts
)
{
        char            *next_dn = dn;
        char            *url = 0;
        char            *attrs[2] = {"labeledURI", 0};
        LDAPMessage     *res, *e;
        char            **vals;

        /*
         * Search for a URL in the named entry or its parent entry.
         * Continue until only 2 nameparts remain.
         */
        while (dn && (nameparts > 1) && (! url)) {

                /* search for the labeledURI attribute */
                if (ldap_search_s(ld, dn, LDAP_SCOPE_BASE,
                    "(objectClass=*)", attrs, 0, &res) == LDAP_SUCCESS) {

                        /* locate the first entry returned */
                        if ((e = ldap_first_entry(ld, res)) != NULL) {

                                /* locate the labeledURI attribute */
                                if ((vals =
                                    ldap_get_values(ld, e, "labeledURI")) !=
                                    NULL) {

                                        /* copy the attribute value */
                                        if ((url = strdup((char *)vals[0])) !=
                                            NULL) {
                                                ldap_value_free(vals);
                                        }
                                }
                        }
                        /* free the search results */
			if (res != NULL) {
                        	ldap_msgfree(res);
			}
                }

                if (! url) {
                        /* advance along the DN by one namepart */
                        if (next_dn = strchr(dn, ',')) {
                                next_dn++;
                                dn = next_dn;
                                nameparts--;
                        }
                }
        }

        return (url);
}

#endif /* _SOLARIS_SDK */
