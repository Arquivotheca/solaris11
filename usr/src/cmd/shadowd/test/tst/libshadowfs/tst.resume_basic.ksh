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
# Tests that we correctly read the pending list when resuming a partial
# migration.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to mkdir dir"
echo a > $TST_ROOT/a || fail "failed to create a"
echo b > $TST_ROOT/dir/b || fail "failed to create b"

stsuspend

tst_create_dataset

ls $TST_SROOT > /dev/null || fail "failed to read root"

stmigrate $TST_SROOT 0 | sort || fail "failed to read pending list"

strotate $TST_SROOT || fail "failed to rotate list"
strotate $TST_SROOT || Fail "failed to rotate list"

echo
stmigrate $TST_SROOT 0 | sort || fail "failed to read pending list"

ls $TST_SROOT/dir > /dev/null || fail "failed to read dir"

strotate $TST_SROOT || fail "failed to rotate list"
strotate $TST_SROOT || Fail "failed to rotate list"

echo
stmigrate $TST_SROOT 0 | sort || fail "failed to read pending list"

tst_destroy_dataset
stresume
