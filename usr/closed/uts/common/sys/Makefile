#
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/uts/Makefile.uts
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

FILEMODE=644

#
#	Note that the following headers are present in the kernel but
#	neither installed or shipped as part of the product:
#		cpuid_drv.h:		Private interface for cpuid consumers
#		unix_bb_info.h:		Private interface to kcov
#

i386_HDRS=

CHKHDRS=			\
	bmc_cmd.h		\
	llc2.h

HDRS=	$(CHKHDRS)

SCSICADHDRS=		\
	glmreg.h	\
	glmvar.h	\
	mptreg.h	\
	mptvar.h

# SVVS headers and other excluded headers
XHDRS=			\
	svvslo.h	\
	tidg.h		\
	tivc.h		\
	tmux.h

include $(SRC)/uts/common/sys/Makefile.syshdrs

CHECKHDRS=						\
	$($(MACH)_HDRS:%.h=%.check)			\
	$(CHKHDRS:%.h=%.check)				\
	$(XHDRS:%.h=%.check)


.KEEP_STATE:

.PARALLEL:			\
	$(CHECKHDRS)		\
	$(ROOTHDRS)		\
	$($(MACH)_ROOTHDRS)


install_h:			\
	$(ROOTDIRS)		\
	.WAIT			\
	$(ROOTHDRS)		\
	$($(MACH)_ROOTHDRS)	\
	svvs_h

svvs_h: FRC
	@if [ -f svvslo.h  ]; then \
		$(MAKE) install_X_h; \
	else \
		true; \
	fi

all_h: $(GENHDRS)

install_X_h: $(ROOTXHDRS)

check:	$(CHECKHDRS)

FRC:
