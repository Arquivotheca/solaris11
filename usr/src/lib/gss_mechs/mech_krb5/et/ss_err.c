/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <locale.h>
const char *
ss_error_table(long errorno) {

switch (errorno) {
	case 0:
		return (dgettext(TEXT_DOMAIN,
		    "Subsystem aborted"));
	case 1:
		return (dgettext(TEXT_DOMAIN,
		    "Version mismatch"));
	case 2:
		return (dgettext(TEXT_DOMAIN,
		    "No current invocation"));
	case 3:
		return (dgettext(TEXT_DOMAIN,
		    "No info directory"));
	case 4:
		return (dgettext(TEXT_DOMAIN,
		    "Command not found"));
	case 5:
		return (dgettext(TEXT_DOMAIN,
		    "Command line aborted"));
	case 6:
		return (dgettext(TEXT_DOMAIN,
		    "End-of-file reached"));
	case 7:
		return (dgettext(TEXT_DOMAIN,
		    "Permission denied"));
	case 8:
		return (dgettext(TEXT_DOMAIN,
		    "Request table not found"));
	case 9:
		return (dgettext(TEXT_DOMAIN,
		    "No info available"));
	case 10:
		return (dgettext(TEXT_DOMAIN,
		    "Shell escapes are disabled"));
	case 11:
		return (dgettext(TEXT_DOMAIN,
		    "Sorry, this request is not yet implemented"));
	case 12:
		return (dgettext(TEXT_DOMAIN,
		    "libtecla returned an error"));
	case 13:
		return (dgettext(TEXT_DOMAIN,
		    "Command failed"));
	default:
		return ("unknown error");
	}
}
