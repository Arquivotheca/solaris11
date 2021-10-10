/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <string.h>
#include <time.h>
#include "kdb_ldap.h"
#include "ldap_misc.h"
#include "ldap_main.h"
#include "ldap_handle.h"
#include "ldap_err.h"
#include "ldap_principal.h"
#include "princ_xdr.h"
#include "ldap_pwd_policy.h"

/*
 * Solaris libldap does not provide the following functions which are in
 * OpenLDAP.  Note, Solaris Kerberos added the use_SSL to do a SSL init.  Also
 * added errstr to return specific error if it isn't NULL.  Yes, this is ugly
 * and no, the errstr should not be free()'ed.
 */
#ifndef HAVE_LDAP_INITIALIZE
int
ldap_initialize(LDAP **ldp, char *url, int use_SSL, char **errstr)
{
    int rc = LDAP_SUCCESS;
    LDAP *ld = NULL;
    LDAPURLDesc *ludp = NULL;

    /* For now, we don't use any DN that may be provided.  And on
       Solaris (based on Mozilla's LDAP client code), we need the
       _nodn form to parse "ldap://host" without a trailing slash.

       Also, this version won't handle an input string which contains
       multiple URLs, unlike the OpenLDAP ldap_initialize.  See
       https://bugzilla.mozilla.org/show_bug.cgi?id=353336#c1 .  */

    /* to avoid reinit and leaking handles, *ldp must be NULL */
    if (*ldp != NULL)
	return LDAP_SUCCESS;

#ifdef HAVE_LDAP_URL_PARSE_NODN
    rc = ldap_url_parse_nodn(url, &ludp);
#else
    rc = ldap_url_parse(url, &ludp);
#endif
    if (rc == 0) {
	if (use_SSL == SSL_ON)
	    ld = ldapssl_init(ludp->lud_host, ludp->lud_port, 1);
	else
	    ld = ldap_init(ludp->lud_host, ludp->lud_port);

	if (ld != NULL)
	    *ldp = ld;
	else {
	    if (errstr != NULL)
		*errstr = strerror(errno);
	    rc = LDAP_OPERATIONS_ERROR;
	}

	ldap_free_urldesc(ludp);
    } else {
	/* report error from ldap url parsing */
	if (errstr != NULL)
	    *errstr = ldap_err2string(rc);
	/* convert to generic LDAP error */
	rc = LDAP_OPERATIONS_ERROR;
    }
    return rc;
}
#endif /* HAVE_LDAP_INITIALIZE */
