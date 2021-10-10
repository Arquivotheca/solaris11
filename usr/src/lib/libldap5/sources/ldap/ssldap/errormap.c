/*
 * Copyright (c) 2001 by Sun Microsystems, Inc.
 * All rights reserved.
 */

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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * errormap.c - map NSPR and OS errors to strings
 *
 * CONFIDENTIAL AND PROPRIETARY SOURCE CODE OF NETSCAPE COMMUNICATIONS
 * CORPORATION 
 * 
 * Copyright (C) 1998-9 Netscape Communications Corporation. All Rights Reserved. 
 *
 * Use of this Source Code is subject to the terms of the applicable license
 * agreement from Netscape Communications Corporation. 
 *
 * The copyright notice(s) in this Source Code does not indicate actual or
 * intended publication of this Source Code. 
 */

/* XXX ceb
 * This code was stolen from Directory server.  
 * ns/netsite/ldap/servers/slapd/errormap.c
 * OS errors are not handled, so the os error has been removed.
 */


#if defined( _WINDOWS )
#include <windows.h>
#include "proto-ntutil.h"
#endif

#include <nspr.h>
#include <ssl.h>

#include <ldap.h>

#ifdef _SOLARIS_SDK
#include <synch.h>
#include <libintl.h>
#endif /* _SOLARIS_SDK */


/*
 * function protoypes
 */
static const char *SECU_Strerror(PRErrorCode errNum);



/*
 * return the string equivalent of an NSPR error
 */

const char *
LDAP_CALL
ldapssl_err2string( const int prerrno )
{
    const char	*s;

    if (( s = SECU_Strerror( (PRErrorCode)prerrno )) == NULL ) {
	s = dgettext(TEXT_DOMAIN, "unknown");
    }

    return( s );
}

/*
 ****************************************************************************
 * The code below this point was provided by Nelson Bolyard <nelsonb> of the
 *	Netscape Certificate Server team on 27-March-1998.
 *	Taken from the file ns/security/cmd/lib/secerror.c on NSS_1_BRANCH.
 *	Last updated from there: 24-July-1998 by Mark Smith <mcs>
 *	Last updated from there: 14-July-1999 by chuck boatwright <cboatwri>
 *      
 *
 * All of the Directory Server specific changes are enclosed inside
 *	#ifdef NS_DIRECTORY.
 ****************************************************************************
 */
#include "nspr.h"

/*
 * XXXceb as a hack, we will locally define NS_DIRECTORY
 */
#define NS_DIRECTORY 1

struct tuple_str {
    PRErrorCode	 errNum;
    const char * errString;
};

typedef struct tuple_str tuple_str;

#ifndef _SOLARIS_SDK
#define ER2(a,b)   {a, b},
#define ER3(a,b,c) {a, c},
#else
#define ER2(a,b)   {a, NULL},
#define ER3(a,b,c) {a, NULL},
#endif

#include "secerr.h"
#include "sslerr.h"

#ifndef _SOLARIS_SDK
const tuple_str errStrings[] = {
#else
tuple_str errStrings[] = {
#endif

/* keep this list in asceding order of error numbers */
#ifdef NS_DIRECTORY
#include "sslerrstrs.h"
#include "secerrstrs.h"
#include "prerrstrs.h"
/* 
 * XXXceb -- LDAPSDK won't care about disconnect 
#include "disconnect_error_strings.h"
 */

#else /* NS_DIRECTORY */
#include "SSLerrs.h"
#include "SECerrs.h"
#include "NSPRerrs.h"
#endif /* NS_DIRECTORY */

};

const PRInt32 numStrings = sizeof(errStrings) / sizeof(tuple_str);

/* Returns a UTF-8 encoded constant error string for "errNum".
 * Returns NULL of errNum is unknown.
 */
#ifndef _SOLARIS_SDK
#ifdef NS_DIRECTORY
static
#endif /* NS_DIRECTORY */
const char *
SECU_Strerror(PRErrorCode errNum) {
    PRInt32 low  = 0;
    PRInt32 high = numStrings - 1;
    PRInt32 i;
    PRErrorCode num;
    static int initDone;

    /* make sure table is in ascending order.
     * binary search depends on it.
     */
    if (!initDone) {
	PRErrorCode lastNum = 0x80000000;
    	for (i = low; i <= high; ++i) {
	    num = errStrings[i].errNum;
	    if (num <= lastNum) {

/*
 * XXXceb
 * We aren't handling out of sequence errors.
 */


#if 0
#ifdef NS_DIRECTORY
		LDAPDebug( LDAP_DEBUG_ANY,
			"sequence error in error strings at item %d\n"
			"error %d (%s)\n",
			i, lastNum, errStrings[i-1].errString );
		LDAPDebug( LDAP_DEBUG_ANY,
			"should come after \n"
			"error %d (%s)\n",
			num, errStrings[i].errString, 0 );
#else /* NS_DIRECTORY */
	    	fprintf(stderr, 
"sequence error in error strings at item %d\n"
"error %d (%s)\n"
"should come after \n"
"error %d (%s)\n",
		        i, lastNum, errStrings[i-1].errString, 
			num, errStrings[i].errString);
#endif /* NS_DIRECTORY */
#endif /* 0 */
	    }
	    lastNum = num;
	}
	initDone = 1;
    }

    /* Do binary search of table. */
    while (low + 1 < high) {
    	i = (low + high) / 2;
	num = errStrings[i].errNum;
	if (errNum == num) 
	    return errStrings[i].errString;
        if (errNum < num)
	    high = i;
	else 
	    low = i;
    }
    if (errNum == errStrings[low].errNum)
    	return errStrings[low].errString;
    if (errNum == errStrings[high].errNum)
    	return errStrings[high].errString;
    return NULL;
}
#else /* _SOLARIS_SDK */
#undef ER3
#define	ER3(x, y, z) case (x):		\
			s = (z);	\
			break;
#undef ER2
#define	ER2(x, y) case (x):		\
			s = (y);	\
			break;

static mutex_t		err_mutex = DEFAULTMUTEX;

static const char *
getErrString(PRInt32 i, PRErrorCode errNum)
{
	char *s;

	mutex_lock(&err_mutex);

	if (errStrings[i].errString != NULL) {
		mutex_unlock(&err_mutex);
		return (errStrings[i].errString);
	}

	switch (errNum) {
#include "sslerrstrs.h"
#include "secerrstrs.h"
#include "prerrstrs.h"
		default:
			s = NULL;
			break;
	}
	errStrings[i].errString = s;
	mutex_unlock(&err_mutex);
	return (s);
}

static
const char *
SECU_Strerror(PRErrorCode errNum) {
    PRInt32 low  = 0;
    PRInt32 high = numStrings - 1;
    PRInt32 i;
    PRErrorCode num;

    /* ASSUME table is in ascending order.
     * binary search depends on it.
     */

    /* Do binary search of table. */
    while (low + 1 < high) {
    	i = (low + high) / 2;
	num = errStrings[i].errNum;
	if (errNum == num) 
	    return getErrString(i, errNum);
        if (errNum < num)
	    high = i;
	else 
	    low = i;
    }
    if (errNum == errStrings[low].errNum)
    	return getErrString(low, errNum);
    if (errNum == errStrings[high].errNum)
    	return getErrString(high, errNum);
    return NULL;
}
#endif
