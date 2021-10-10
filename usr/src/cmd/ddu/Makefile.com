#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Definitions common to ddu source.
#

include		$(SRC)/cmd/Makefile.cmd

FILEMODE=	0555
PYTHON=		$(PYTHON_26)

ASFLAGS +=	-P
CPPFLAGS +=	-I$(SRC)/cmd/ddu/include
LDLIBS +=	-L$(SRC)/cmd/ddu/lib

ROOTUSRBIN=		$(ROOT)/usr/bin
ROOTUSRLIBPY_2.6_DDU=	$(ROOT)/usr/lib/python2.6/vendor-packages/DDU
ROOTUSRSHAREPIX=	$(ROOT)/usr/share/pixmaps
ROOTUSRSHAREAPP=	$(ROOT)/usr/share/applications

ROOTDDU=		$(ROOT)/usr/ddu
ROOTDDUBINMACH=		$(ROOTDDU)/bin/$(MACH)
ROOTDDUDATA=		$(ROOTDDU)/data
ROOTDDUTEXTUTILS=	$(ROOTDDU)/ddu-text/utils
ROOTDDUHELP_XML=	$(ROOTDDU)/help/gnome/help/ddu/C
ROOTDDUHELP_FIGS=	$(ROOTDDU)/help/gnome/help/ddu/C/figures
ROOTDDULIB=		$(ROOTDDU)/lib
ROOTDDUSCRIPTS=		$(ROOTDDU)/scripts
ROOTDDUUTILS=		$(ROOTDDU)/utils

ROOTDDUCONFFILES=	$(CONFFILES:%=$(ROOTDDU)/%)
ROOTDDUPYFILES=		$(PYFILES:%=$(ROOTDDU)/%)
ROOTUSRLIBPY_2.6_DDUPYFILES=	$(PYFILES:%=$(ROOTUSRLIBPY_2.6_DDU)/%)
ROOTDDUBINMACHPROG=	$(PROG:%=$(ROOTDDUBINMACH)/%)
ROOTUSRSHAREPIXFILE=	$(PIXFILE:%=$(ROOTUSRSHAREPIX)/%)
ROOTUSRSHAREAPPFILE=	$(APPFILE:%=$(ROOTUSRSHAREAPP)/%)
ROOTDDUDATAFILES=	$(FILES:%=$(ROOTDDUDATA)/%)
ROOTDDUTEXTUTILSPYFILES=	$(PYFILES:%=$(ROOTDDUTEXTUTILS)/%)
ROOTDDUHELP_XMLFILES=	$(XMLFILES:%=$(ROOTDDUHELP_XML)/%)
ROOTDDUHELP_FIGSFILES=	$(PNGFILES:%=$(ROOTDDUHELP_FIGS)/%)
ROOTDDULIBDYNLIB=	$(DYNLIB:%=$(ROOTDDULIB)/%)
ROOTDDUSCRIPTSSHFILES=	$(SHFILES:%=$(ROOTDDUSCRIPTS)/%)
ROOTDDUSCRIPTSPYFILES=	$(PYFILES:%=$(ROOTDDUSCRIPTS)/%)
ROOTDDUSCRIPTSFILES=	$(FILES:%=$(ROOTDDUSCRIPTS)/%)
ROOTDDUUTILSPYFILES=	$(PYFILES:%=$(ROOTDDUUTILS)/%)

$(ROOTDDU)/%: %
	$(INS.file)

$(ROOTUSRLIBPY_2.6_DDU)/%: %
	$(INS.file)

$(ROOTDDUBINMACH)/%: %
	$(INS.file)

$(ROOTUSRSHAREPIX)/%: %
	$(INS.file)

$(ROOTUSRSHAREAPP)/%: %
	$(INS.file)

$(ROOTDDUDATA)/%: %
	$(INS.file)

$(ROOTDDUTEXTUTILS)/%: %
	$(INS.file)

$(ROOTDDUHELP_XML)/%: %
	$(INS.file)

$(ROOTDDUHELP_FIGS)/%: %
	$(INS.file)

$(ROOTDDULIB)/%: %
	$(INS.file)

$(ROOTDDUSCRIPTS)/%: %
	$(INS.file)

$(ROOTDDUUTILS)/%: %
	$(INS.file)
