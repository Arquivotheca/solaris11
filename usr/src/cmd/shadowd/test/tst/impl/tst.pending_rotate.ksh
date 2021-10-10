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
# Identical to tst.pending_remove.ksh, except that we let the normal pending
# log rotation process occur.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"
mkdir -p $TST_ROOT/dir1/dir2 || failed "failed to mkdir dir1/dir2"
echo "this is file b" > $TST_ROOT/dir1/b || fail "failed to create file b"
echo "this is file c" > $TST_ROOT/dir1/dir2/c || fail "failed to create file c"

#
# If the timeout is not large enough, this test can fail. The test
# requires that pending files don't rotate before the test expects
# a rotation.
#
TIMEOUT=8

tst_set_timeout $TIMEOUT

tst_create_dataset

#
# Touch file 'a' in the root.  This will migrate the root as well as the file
# 'a'.
#
touch $TST_SROOT/a || fail "failed to touch a"
tst_shadow_disable
stpending 0 $TST_SROOT $TST_SROOT/a $TST_SROOT/dir1 || \
    fail "mismatched pending list 0"

#
# After rotating the pending list, the original should be the same, but the new
# one should have only 'dir1' left.
#
sleep $TIMEOUT
stpending 0 $TST_SROOT/dir1 || fail "mismatched pending list 0 after rotate"

tst_shadow_enable

find $TST_SROOT/dir1 -exec cat {} \; > /dev/null 2>& 1

stpending 0 $TST_SROOT/dir1 || \
    fail "pending list 0 changed unexpectedly"
stpending 1 $TST_SROOT/dir1/b $TST_SROOT/dir1/dir2 \
    $TST_SROOT/dir1/dir2/c || fail "mismatched pending list 1"

sleep $TIMEOUT

stpending -e 1 $TST_SROOT || fail "non-empty pending list 1"
stpending 0 $TST_SROOT/dir1 || \
    fail "pending list 0 changed unexpectedly"

sleep $TIMEOUT

stpending -e 0 $TST_SROOT || fail "non-empty pending list 0 after completion"
stpending -e 1 $TST_SROOT || fail "non-empty pending list 1 after completion"

tst_destroy_dataset
tst_set_timeout 60

exit 0
