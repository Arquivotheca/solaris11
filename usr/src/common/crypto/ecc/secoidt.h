/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Netscape security libraries.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1994-2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Dr Vipul Gupta <vipul.gupta@sun.com>, Sun Microsystems Laboratories
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Sun elects to use this software under the MPL license.
 */

#ifndef _SECOIDT_H_
#define _SECOIDT_H_

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * secoidt.h - public data structures for ASN.1 OID functions
 *
 * $Id: secoidt.h,v 1.23 2007/05/05 22:45:16 nelson%bolyard.com Exp $
 */

typedef struct SECOidDataStr SECOidData;
typedef struct SECAlgorithmIDStr SECAlgorithmID;

/*
** An X.500 algorithm identifier
*/
struct SECAlgorithmIDStr {
    SECItem algorithm;
    SECItem parameters;
};

#define SEC_OID_SECG_EC_SECP192R1 SEC_OID_ANSIX962_EC_PRIME192V1
#define SEC_OID_SECG_EC_SECP256R1 SEC_OID_ANSIX962_EC_PRIME256V1
#define SEC_OID_PKCS12_KEY_USAGE  SEC_OID_X509_KEY_USAGE

/* fake OID for DSS sign/verify */
#define SEC_OID_SHA SEC_OID_MISS_DSS

typedef enum {
    INVALID_CERT_EXTENSION = 0,
    UNSUPPORTED_CERT_EXTENSION = 1,
    SUPPORTED_CERT_EXTENSION = 2
} SECSupportExtenTag;

struct SECOidDataStr {
    SECItem            oid;
    ECCurveName        offset;
    const char *       desc;
    unsigned long      mechanism;
    SECSupportExtenTag supportedExtension;	
    				/* only used for x.509 v3 extensions, so
				   that we can print the names of those
				   extensions that we don't even support */
};

#endif /* _SECOIDT_H_ */
