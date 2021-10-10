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
 *  Copyright (c) 1990 Regents of the University of Michigan.
 *  All rights reserved.
 */
/*
 *  getentry.c
 */

#if 0
#ifndef lint 
static char copyright[] = "@(#) Copyright (c) 1990 Regents of the University of Michigan.\nAll rights reserved.\n";
#endif
#endif

#include "ldap-int.h"

LDAPMessage *
LDAP_CALL
ldap_first_entry( LDAP *ld, LDAPMessage *chain )
{
	if ( !NSLDAPI_VALID_LDAP_POINTER( ld ) || chain == NULLMSG ) {
		return( NULLMSG );
	}

	if ( chain->lm_msgtype == LDAP_RES_SEARCH_ENTRY ) {
		return( chain );
	}

	return( ldap_next_entry( ld, chain ));
}


LDAPMessage *
LDAP_CALL
ldap_next_entry( LDAP *ld, LDAPMessage *entry )
{
	if ( !NSLDAPI_VALID_LDAP_POINTER( ld ) || entry == NULLMSG ) {
		return( NULLMSG );
	}

	for ( entry = entry->lm_chain; entry != NULLMSG;
	    entry = entry->lm_chain ) {
		if ( entry->lm_msgtype == LDAP_RES_SEARCH_ENTRY ) {
			return( entry );
		}
	}

	return( NULLMSG );
}

int
LDAP_CALL
ldap_count_entries( LDAP *ld, LDAPMessage *chain )
{
	int	i;

	if ( !NSLDAPI_VALID_LDAP_POINTER( ld )) {
		return( -1 );
	}

	for ( i = 0; chain != NULL; chain = chain->lm_chain ) {
		if ( chain->lm_msgtype == LDAP_RES_SEARCH_ENTRY ) {
			++i;
		}
	}

	return( i );
}


int
LDAP_CALL
ldap_get_entry_controls( LDAP *ld, LDAPMessage *entry,
	LDAPControl ***serverctrlsp )
{
	int		rc;
	BerElement	tmpber;

	LDAPDebug( LDAP_DEBUG_TRACE, "ldap_get_entry_controls\n", 0, 0, 0 );

	if ( !NSLDAPI_VALID_LDAP_POINTER( ld )) {
		return( LDAP_PARAM_ERROR );
	}

	if ( !NSLDAPI_VALID_LDAPMESSAGE_ENTRY_POINTER( entry )
	    || serverctrlsp == NULL ) {
		rc = LDAP_PARAM_ERROR;
		goto report_error_and_return;
	}

	*serverctrlsp = NULL;
	tmpber = *entry->lm_ber;	/* struct copy */

	/* skip past dn and entire attribute/value list */ 
	if ( ber_scanf( &tmpber, "{xx" ) == LBER_ERROR ) {
		rc = LDAP_DECODING_ERROR;
		goto report_error_and_return;
	}

	rc = nsldapi_get_controls( &tmpber, serverctrlsp );

report_error_and_return:
	LDAP_SET_LDERRNO( ld, rc, NULL, NULL );
	return( rc );
}
