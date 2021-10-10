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

. $ST_TOOLS/utility.ksh

#
# Simple test to make sure we're correctly identifying completed migration.
#

tst_create_root

echo a > $TST_ROOT/a || fail "failed to create 'a'"
mkdir $TST_ROOT/dir1 || fail "failed to mkdir 'dir1'"
echo b > $TST_ROOT/dir1/b || fail "failed to create 'dir1/b'"

tst_create_dataset

stcomplete $TST_SROOT 4 || fail "failed to complete migration"

tst_destroy_dataset

exit 0
