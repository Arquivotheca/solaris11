#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# lib/libc_i18n/amd64/Makefile

include ../Makefile.com
include $(SRC)/lib/Makefile.lib.64

EXTN_CFLAGS=	$(CTF_FLAGS)
EXTN_CPPFLAGS=	-D$(MACH64) -D__$(MACH64)
EXTN_LINTFLAGS=	-erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

install: all $(ROOTLIB_PIC64)
