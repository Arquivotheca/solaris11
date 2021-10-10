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

#
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#
#	This makefile drives the production of the Neterion X3100
#	Series 10G Ethernet (VXGE) driver module in x86 systems
#

#
#	Paths to the base of the uts directory trees
#
UTSBASE   = ../..

#
#	Define the module and object file sets.
#
MODULE		= vxge
OBJECTS		= $(VXGE_HAL_OBJS:%=$(OBJS_DIR)/%) $(VXGE_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(VXGE_HAL_OBJS:%.o=$(LINTS_DIR)/%.ln) $(VXGE_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io/vxge/drv

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint

INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	GENERAL PURPOUSE HAL FLAGS: Tuning HAL for Solaris specific modes
#
HAL_CFLAGS	 = -DVXGE_HAL_TX_MULTI_POST_IRQ \
		   -DVXGE_HAL_BP_POST \
		   -DVXGE_HAL_OID_DB \
		   -DVXGE_HAL_VP_CHANNELS \
		   -DVXGE_HAL_PENDING_CHANNELS \
		   -DVXGE_HAL_VP_CBS

#
#	TRACE SECTION: Possible values for MODULE, TRACE and ERR masks
#
#	VXGE_COMPONENT_HAL_DEVICE	0x00000001
#	VXGE_COMPONENT_HAL_DEVICE_IRQ	0x00000002
#	VXGE_COMPONENT_HAL_VPATH	0x00000004
#	VXGE_COMPONENT_HAL_VPATH_IRQ	0x00000008
#	VXGE_COMPONENT_HAL_CONFIG	0x00000010
#	VXGE_COMPONENT_HAL_MM		0x00000020
#	VXGE_COMPONENT_HAL_POOL		0x00000040
#	VXGE_COMPONENT_HAL_QUEUE	0x00000080
#	VXGE_COMPONENT_HAL_BITMAP	0x00000100
#	VXGE_COMPONENT_HAL_CHANNEL	0x00000200
#	VXGE_COMPONENT_HAL_FIFO		0x00000400
#	VXGE_COMPONENT_HAL_RING		0x00000800
#	VXGE_COMPONENT_HAL_DMQ		0x00001000
#	VXGE_COMPONENT_HAL_UMQ		0x00002000
#	VXGE_COMPONENT_HAL_SQ		0x00004000
#	VXGE_COMPONENT_HAL_SRQ		0x00008000
#	VXGE_COMPONENT_HAL_CQRQ		0x00010000
#	VXGE_COMPONENT_HAL_NCE		0x00020000
#	VXGE_COMPONENT_HAL_STAG		0x00040000
#	VXGE_COMPONENT_HAL_TCP		0x00080000
#	VXGE_COMPONENT_HAL_LRO		0x00100000
#	VXGE_COMPONENT_HAL_SPDM		0x00200000
#	VXGE_COMPONENT_HAL_SESSION	0x00400000
#	VXGE_COMPONENT_HAL_STATS	0x00800000
#	VXGE_COMPONENT_OSDEP		0x10000000
#	VXGE_COMPONENT_LL		0x20000000
#	VXGE_COMPONENT_ALL		0xffffffff


# all debug & trace
#TRACE_CFLAGS	= -DVXGE_DEBUG_MODULE_MASK=0xffffffff
#
# all debug & trace except mm and pool
#TRACE_CFLAGS	= -DVXGE_DEBUG_MODULE_MASK=0xffffffe5
#
# all debug & trace except queue, mm and data path
#TRACE_CFLAGS	= -DVXGE_DEBUG_MODULE_MASK=0xffffff9f
#
# minimal tracing
#TRACE_CFLAGS	= -DVXGE_DEBUG_MODULE_MASK=0x30000030
#
# No tracing
TRACE_CFLAGS	= -DVXGE_DEBUG_MODULE_MASK=0

VXGE_CFLAGS	= $(HAL_CFLAGS) $(TRACE_CFLAGS) $(CCVERBOSE) \
		-I$(UTSBASE)/common/io/vxge/hal/include \
		-I$(UTSBASE)/common/io/vxge/hal/vxgehal \
		-I$(UTSBASE)/common/io/vxge/drv

CFLAGS		+= $(VXGE_CFLAGS) -xO4 -xc99=%all
CFLAGS64	+= $(VXGE_CFLAGS) -xO4 -xc99=%all

#
#	Driver depends on MAC & IP
#
LDFLAGS		+=  -dy -N misc/mac -N drv/ip

#	Lint flag
#
LINTFLAGS	+= $(VXGE_CFLAGS) -Xc99=%all

#
#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSBASE)/intel/Makefile.targ

