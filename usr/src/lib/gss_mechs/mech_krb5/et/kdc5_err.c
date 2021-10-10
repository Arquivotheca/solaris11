/*
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <locale.h>
const char *
kdc5_error_table(long errorno) {

switch (errorno) {
	case 0:
		return ("$Header$");
	case 1:
		return (dgettext(TEXT_DOMAIN,
			"No server port found"));
	case 2:
		return (dgettext(TEXT_DOMAIN,
			"Network not initialized"));
	case 3:
		return (dgettext(TEXT_DOMAIN,
			"Short write while sending response"));
	default:
		return ("unknown error");
	}
}
