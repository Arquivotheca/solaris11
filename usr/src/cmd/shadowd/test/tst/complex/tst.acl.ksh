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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# Tests that NFSv4 ACLs are migrated appropriately.
#

. $ST_TOOLS/utility.ksh

tst_create_root

touch $TST_ROOT/a

chmod A+user:root:write_attributes/write_acl/write_owner:allow \
    $TST_ROOT/a || fail "failed to chmod a"
chmod A+group:other:synchronize:deny \
    $TST_ROOT/a || fail "failed to chmod a"

chmod A+group@:delete_child:deny \
    $TST_ROOT || fail "failed to chmod $TST_ROOT"

ls -lV $TST_ROOT/a | tail +2
echo
ls -lVd $TST_ROOT | tail +2
echo

tst_create_dataset

ls -lV $TST_SROOT/a | tail +2
echo
ls -lVd $TST_SROOT | tail +2

tst_destroy_dataset
