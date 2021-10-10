/*
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <locale.h>
const char *
pty_error_table(long errorno) {

switch (errorno) {
	case 0:
		return (dgettext(TEXT_DOMAIN,
		    "Failed to unlock or grant streams pty."));
	case 1:
		return (dgettext(TEXT_DOMAIN,
		    "fstat of master pty failed"));
	case 2:
		return (dgettext(TEXT_DOMAIN,
		    "All terminal ports in use"));
	case 3:
		return (dgettext(TEXT_DOMAIN,
		    "buffer to hold slave pty name is too short"));
	case 4:
		return (dgettext(TEXT_DOMAIN,
		    "Failed to open slave side of pty"));
	case 5:
		return (dgettext(TEXT_DOMAIN,
		    "Failed to chmod slave side of pty"));
	case 6:
		return (dgettext(TEXT_DOMAIN,
		    "Unable to set controlling terminal"));
	case 7:
		return (dgettext(TEXT_DOMAIN,
		    "Failed to chown slave side of pty"));
	case 8:
		return (dgettext(TEXT_DOMAIN,
		    "Call to line_push failed to push streams on slave pty"));
	case 9:
		return (dgettext(TEXT_DOMAIN,
		    "Failed to push stream on slave side of pty"));
	case 10:
		return (dgettext(TEXT_DOMAIN,
		    "Failed to revoke slave side of pty"));
	case 11:
		return (dgettext(TEXT_DOMAIN,
		    "bad process type passed to pty_update_utmp"));
	case 12:
		return (dgettext(TEXT_DOMAIN,
		    "Slave pty name is zero-length"));
	default:
		return ("unknown error");
	}
}
