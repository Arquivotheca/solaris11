#!/bin/ksh -p
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

#
# Tests that UFS migration of ACLs (and therefore NFSv3 POSIX draft ACLs) works
# correctly.
#

. $ST_TOOLS/utility.ksh

tst_create_ufs_root

mkdir $TST_ROOT/subdir || fail "failed to create subdir"
echo hello > $TST_ROOT/subdir/a || fail "failed to create subdir/a"

setfacl -m user:daemon:r-- $TST_ROOT/subdir/a || fail "failed to setfacl"
cd $TST_ROOT/subdir && getfacl a || fail "failed to getfacl"
cat $TST_ROOT/subdir/a

tst_create_ufs

cd $TST_SROOT/subdir && getfacl a || fail "failed to getfacl"
cat $TST_SROOT/subdir/a

tst_destroy_ufs
tst_destroy_ufs_root
