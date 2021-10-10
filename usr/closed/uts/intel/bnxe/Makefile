#
# Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
#       Path to the base of the uts directory tree.
#
ROOT_SRC  = ../../../../src
UTSCLOSED = ../..
UTSBASE   = $(ROOT_SRC)/uts
DRVBASE   = $(UTSCLOSED)/common/io/bnxe

#
#	Define the module and object file sets.
#
MODULE		= bnxe
OBJECTS		= $(BNXE_OBJS:%=$(OBJS_DIR)/%) \
		$(BNXE_COM_OBJS:%=$(OBJS_DIR)/%) \
		$(BNXE_DRV_OBJS:%=$(OBJS_DIR)/%) \
		$(BNXE_FW_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(BNXE_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		$(BNXE_COM_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		$(BNXE_DRV_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		$(BNXE_FW_OBJS:%=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR     = $(UTSCLOSED)/common/io/bnxe


#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

#
#	Define driver version
#
MAJVERSION = 6
MINVERSION = 4
REVVERSION = 37
SUNVERSION = 1

BRCMVERSION = $(MAJVERSION).$(MINVERSION).$(REVVERSION).$(SUNVERSION)

DRV_FLAGS = -D_KERNEL \
	-D__SunOS \
	-D__Solaris_11 \
	-D__inline=inline \
	-D_inline=inline \
	-DLM_RXPKT_NON_CONTIGUOUS \
	-DELINK_ENHANCEMENTS \
	-DELINK_57711E_SUPPORT \
	-DELINK_DEBUG \
	-DCLC_ENHANCEMENTS \
	-DCLC_57711E_SUPPORT \
	-UCLC_DEBUG \
	-DBRCMVERSION="\"v$(BRCMVERSION)\""

INCLUDES = -I$(DRVBASE)/577xx/include \
	-I$(DRVBASE)/577xx/drivers/common/ecore \
	-I$(DRVBASE)/577xx/drivers/common/include \
	-I$(DRVBASE)/577xx/drivers/common/include/l4 \
	-I$(DRVBASE)/577xx/drivers/common/include/l5 \
	-I$(DRVBASE)/577xx/drivers/common/lm_device \
	-I$(DRVBASE)/577xx/drivers/common/lm_device/Include \
	-I$(DRVBASE)/577xx/drivers/common/lm_device/fw \
	-I$(DRVBASE)/577xx/drivers/common/lm_device/l4 \
	-I$(DRVBASE)/577xx/drivers/common/lm_device/l4/Include \
	-I$(DRVBASE)/577xx/drivers/common/lm_device/l5 \
	-I$(DRVBASE)/577xx/drivers/common/lm_device/l5/Include \
	-I$(DRVBASE)/577xx/hsi/hw/include \
	-I$(DRVBASE)/577xx/hsi/mcp \
	-I$(DRVBASE)/brcmfcoeadm \
	-I$(DRVBASE)/src \
	-I$(ROOT_SRC)/head

ENDIAN_I386 = -DLITTLE_ENDIAN -DLITTLE_ENDIAN_HOST -D__LITTLE_ENDIAN

CFLAGS		+= ${DRV_FLAGS} ${INCLUDES} $(ENDIAN_I386) -xc99=%all

#
#       Lint flag
#

CERRWARN	+= -erroff=E_INTEGER_OVERFLOW_DETECTED \
		   -erroff=E_ENUM_VAL_OVERFLOWS_INT_MAX \
		   -erroff=E_CONST_PROMOTED_UNSIGNED_LL \
		   -erroff=E_NO_IMPLICIT_DECL_ALLOWED \
		   -erroff=E_STATEMENT_NOT_REACHED \
		   -erroff=E_OLD_STYLE_DECL_OR_BAD_TYPE


#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(CONFMOD)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#       Driver depends on GLD & IP
#
LDFLAGS         += -dy -Qn -B reduce -r -N drv/ip -N misc/mac


#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:

modlintlib:

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)


#
#	Include common targets.
#
include $(UTSCLOSED)/intel/Makefile.targ
