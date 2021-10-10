#pragma ident	"%Z%%M%	%I%	%E% SMI"

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */
/*
 * psearch.c - Persistent search and "Entry Change Notification" support.
 */
#include "ldap-int.h"


int
LDAP_CALL
ldap_create_persistentsearch_control( LDAP *ld, int changetypes,
    int changesonly, int return_echg_ctls, char ctl_iscritical,
    LDAPControl **ctrlp )
{
    BerElement	*ber;
    int		rc;

    if ( !NSLDAPI_VALID_LDAP_POINTER( ld )) {
	return( LDAP_PARAM_ERROR );
    }

    if ( ctrlp == NULL || ( changetypes & ~LDAP_CHANGETYPE_ANY ) != 0 ) {
	rc = LDAP_PARAM_ERROR;
	goto report_error_and_return;
    }

    /*
     * create a Persistent Search control.  The control value looks like this:
     *
     *	PersistentSearch ::= SEQUENCE {
     *		changeTypes INTEGER,
     *		-- the changeTypes field is the logical OR of 
     *		-- one or more of these values: add (1), delete (2),
     *		-- modify (4), modDN (8).  It specifies which types of
     *		-- changes will cause an entry to be returned.
     *		changesOnly BOOLEAN, -- skip initial search?
     *		returnECs BOOLEAN,   -- return "Entry Change" controls?
     *	}
     */
    if (( nsldapi_alloc_ber_with_options( ld, &ber )) != LDAP_SUCCESS ) {
	rc = LDAP_NO_MEMORY;
	goto report_error_and_return;
    }

    if ( ber_printf( ber, "{ibb}", changetypes, changesonly,
	    return_echg_ctls ) == -1 ) {
	ber_free( ber, 1 );
	rc = LDAP_ENCODING_ERROR;
	goto report_error_and_return;
    }

    rc = nsldapi_build_control( LDAP_CONTROL_PERSISTENTSEARCH, ber, 1,
	    ctl_iscritical, ctrlp );

report_error_and_return:
    LDAP_SET_LDERRNO( ld, rc, NULL, NULL );
    return( rc );
}


int
LDAP_CALL
ldap_parse_entrychange_control( LDAP *ld, LDAPControl **ctrls, int *chgtypep,
    char **prevdnp, int *chgnumpresentp, ber_int_t *chgnump )
{
    BerElement		*ber;
    int			rc, i, changetype;
    ber_len_t		len;
    ber_int_t		along;
    char		*previousdn;

    if ( !NSLDAPI_VALID_LDAP_POINTER( ld )) {
	return( LDAP_PARAM_ERROR );
    }

    /*
     * find the entry change notification in the list of controls
     */
    for ( i = 0; ctrls != NULL && ctrls[i] != NULL; ++i ) {
	if ( strcmp( ctrls[i]->ldctl_oid, LDAP_CONTROL_ENTRYCHANGE ) == 0 ) {
	    break;
	}
    }

    if ( ctrls == NULL || ctrls[i] == NULL ) {
	rc = LDAP_CONTROL_NOT_FOUND;
	goto report_error_and_return;
    }

    /*
     * allocate a BER element from the control value and parse it.  The control
     * value should look like this:
     *
     *	EntryChangeNotification ::= SEQUENCE {
     *	     changeType ENUMERATED {
     *	 	add             (1),  -- these values match the
     *	 	delete          (2),  -- values used for changeTypes
     *	 	modify          (4),  -- in the PersistentSearch control.
     *	 	modDN           (8),
     *	     },
     *	     previousDN   LDAPDN OPTIONAL,     -- modDN ops. only
     *	     changeNumber INTEGER OPTIONAL,    -- if supported
     *	}
     */
    if (( ber = ber_init( &(ctrls[i]->ldctl_value))) == NULL ) {
	rc = LDAP_NO_MEMORY;
	goto report_error_and_return;
    }		

    if ( ber_scanf( ber, "{e", &along ) == LBER_ERROR ) {
	ber_free( ber, 1 );
	rc = LDAP_DECODING_ERROR;
	goto report_error_and_return;
    }
    changetype = (int)along;	/* XXX lossy cast */

    if ( changetype == LDAP_CHANGETYPE_MODDN ) {
	if ( ber_scanf( ber, "a", &previousdn ) == LBER_ERROR ) {
	    ber_free( ber, 1 );
	    rc = LDAP_DECODING_ERROR;
	    goto report_error_and_return;
	}
    } else {
	previousdn = NULL;
    }

    if ( chgtypep != NULL ) {
	*chgtypep = changetype;
    }
    if ( prevdnp != NULL ) {
	*prevdnp = previousdn;
    } else if ( previousdn != NULL ) {
	NSLDAPI_FREE( previousdn );
    }

    if ( chgnump != NULL ) {	/* check for optional changenumber */
	if ( ber_peek_tag( ber, &len ) == LBER_INTEGER
		&& ber_get_int( ber, chgnump ) != LBER_ERROR ) {
	    if ( chgnumpresentp != NULL ) {
		*chgnumpresentp = 1;
	    }
	} else {
	    if ( chgnumpresentp != NULL ) {
		*chgnumpresentp = 0;
	    }
	}
    }

    ber_free( ber, 1 );
    rc = LDAP_SUCCESS;

report_error_and_return:
    LDAP_SET_LDERRNO( ld, rc, NULL, NULL );
    return( rc );
}
