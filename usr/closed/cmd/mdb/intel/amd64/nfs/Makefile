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
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

MODULE = nfs.so
MDBTGT = kvm

MODSRCS = nfs.c rfs4_db_mdb.c nfs4_diag.c nfs4_exp_mdb.c nfs4_rnode_mdb.c \
	atable.c atable4.c nfs4_clnt.c nfs4_data.c nfs4_idmap.c \
	nfs_info.c nfs_mntinfo.c nfs_stat.c nlm_info.c rtable.c rtable4.c \
	rfs4_fsh.c rfs_inst.c

include $(SRC)/cmd/Makefile.cmd
include $(SRC)/cmd/Makefile.cmd.64
include ../../../common/Makefile.closed
include $(SRC)/cmd/mdb/intel/Makefile.amd64
include $(SRC)/cmd/mdb/Makefile.module

CPPFLAGS += -I../../../../../../src/uts/common
