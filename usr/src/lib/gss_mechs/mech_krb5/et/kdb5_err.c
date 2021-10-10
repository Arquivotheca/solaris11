/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <locale.h>
const char *
kdb5_error_table(long errorno) {

switch (errorno) {
	case 0:
		return (
		    "$Id: kdb5_err.et,v 5.18 1995/11/03 21:52:42 eichin Exp $");
	case 1:
		return (dgettext(TEXT_DOMAIN,
		    "Entry already exists in database"));
	case 2:
		return (dgettext(TEXT_DOMAIN,
		    "Database store error"));
	case 3:
		return (dgettext(TEXT_DOMAIN,
		    "Database read error"));
	case 4:
		return (dgettext(TEXT_DOMAIN,
		    "Insufficient access to perform requested operation"));
	case 5:
		return (dgettext(TEXT_DOMAIN,
		    "No such entry in the database"));
	case 6:
		return (dgettext(TEXT_DOMAIN,
		    "Illegal use of wildcard"));
	case 7:
		return (dgettext(TEXT_DOMAIN,
		    "Database is locked or in use--try again later"));
	case 8:
		return (dgettext(TEXT_DOMAIN,
		    "Database was modified during read"));
	case 9:
		return (dgettext(TEXT_DOMAIN,
		    "Database record is incomplete or corrupted"));
	case 10:
		return (dgettext(TEXT_DOMAIN,
		    "Attempt to lock database twice"));
	case 11:
		return (dgettext(TEXT_DOMAIN,
		    "Attempt to unlock database when not locked"));
	case 12:
		return (dgettext(TEXT_DOMAIN,
		    "Invalid kdb lock mode"));
	case 13:
		return (dgettext(TEXT_DOMAIN,
		    "Database has not been initialized"));
	case 14:
		return (dgettext(TEXT_DOMAIN,
		    "Database has already been initialized"));
	case 15:
		return (dgettext(TEXT_DOMAIN,
		    "Bad direction for converting keys"));
	case 16:
		return (dgettext(TEXT_DOMAIN,
		    "Cannot find master key record in database"));
	case 17:
		return (dgettext(TEXT_DOMAIN,
		    "Master key does not match database"));
	case 18:
		return (dgettext(TEXT_DOMAIN,
		    "Key size in database is invalid"));
	case 19:
		return (dgettext(TEXT_DOMAIN,
		    "Cannot find/read stored master key"));
	case 20:
		return (dgettext(TEXT_DOMAIN,
		    "Stored master key is corrupted"));
	case 21:
		return (dgettext(TEXT_DOMAIN,
		    "Cannot find active master key"));
	case 22:
		return (dgettext(TEXT_DOMAIN,
		    "KVNO of new master key does not match expected value"));
	case 23:
		return (dgettext(TEXT_DOMAIN,
		    "Stored master key is not current"));
	case 24:
		return (dgettext(TEXT_DOMAIN,
		    "Insufficient access to lock database"));
	case 25:
		return (dgettext(TEXT_DOMAIN,
		    "Database format error"));
	case 26:
		return (dgettext(TEXT_DOMAIN,
		    "Unsupported version in database entry"));
	case 27:
		return (dgettext(TEXT_DOMAIN,
		    "Unsupported salt type"));
	case 28:
		return (dgettext(TEXT_DOMAIN,
		    "Unsupported encryption type"));
	case 29:
		return (dgettext(TEXT_DOMAIN,
		    "Bad database creation flags"));
	case 30: /* KRB5_KDB_NO_PERMITTED_KEY */
		return (dgettext(TEXT_DOMAIN,
		    "No matching key in entry having a permitted enctype"));
	case 31: /* KRB5_KDB_NO_MATCHING_KEY */
		return (dgettext(TEXT_DOMAIN,
		    "No matching key in entry"));
	case 32: /* KRB5_KDB_DBTYPE_NOTFOUND */
		return (dgettext(TEXT_DOMAIN,
		    "Unable to find requested database type"));
	case 33: /* KRB5_KDB_DBTYPE_NOSUP */
		return (dgettext(TEXT_DOMAIN, "Database type not supported"));
	case 34: /* KRB5_KDB_DBTYPE_INIT */
		return (dgettext(TEXT_DOMAIN,
		    "Database library failed to initialize"));
	case 35: /* KRB5_KDB_SERVER_INTERNAL_ERR */
		return (dgettext(TEXT_DOMAIN, "Server error"));
	case 36: /* KRB5_KDB_ACCESS_ERROR */
		return (dgettext(TEXT_DOMAIN,
		    "Unable to access Kerberos database"));
	case 37: /* KRB5_KDB_INTERNAL_ERROR */
		return (dgettext(TEXT_DOMAIN,
		    "Kerberos database internal error"));
	case 38: /* KRB5_KDB_CONSTRAINT_VIOLATION */
		return (dgettext(TEXT_DOMAIN,
		    "Kerberos database constraints violated"));
	case 39: /* KRB5_LOG_CONV */
		return (dgettext(TEXT_DOMAIN, "Update log conversion error"));
	case 40: /* KRB5_LOG_UNSTABLE */
		return (dgettext(TEXT_DOMAIN, "Update log is unstable"));
	case 41: /* KRB5_LOG_CORRUPT */
		return (dgettext(TEXT_DOMAIN, "Update log is corrupt"));
	case 42: /* KRB5_LOG_ERROR */
		return (dgettext(TEXT_DOMAIN, "Generic update log error"));
	default:
		return ("unknown error");
	}
}
