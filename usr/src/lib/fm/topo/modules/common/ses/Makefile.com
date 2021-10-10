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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

MODULE = ses
CLASS = common
SHAREDMODULE = disk

MODULESRCS = ses.c ses_facility.c
SHAREDSRCS = disk_common.c


include $(SRC)/lib/fm/topo/modules/Makefile.com

CPPFLAGS += -I$(SRC)/lib/fm/topo/modules/common/disk/common

DYNFLAGS32 += -R/usr/lib/scsi
DYNFLAGS64 += -R/usr/lib/scsi/$(MACH64)

LDLIBS += -ldevinfo -ldevid -ldiskstatus -lcontract -lsysevent -lses
LDLIBS32 += -L$(ROOT)/usr/lib/scsi -R/usr/lib/scsi
LDLIBS64 += -L$(ROOT)/usr/lib/scsi/$(MACH64) -R/usr/lib/scsi/$(MACH64)
