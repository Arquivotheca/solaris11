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
# Tests that if we take a snapshot of a ZFS filesystem during migration, we get
# empty files within the snapshot.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to create dir"
echo "this is file a" > $TST_ROOT/dir/a || fail "failed to create dir/a"
echo "this is file b" > $TST_ROOT/dir/b || fail "failed to create dir/b"

tst_create_dataset

cat $TST_SROOT/dir/b || fail "failed to read dir/b"

zfs snapshot $TST_DATASET@test || fail "failed to snapshot dataset"

cat $TST_SROOT/.zfs/snapshot/test/dir/a && fail "successfully read snapshot a"
cat $TST_SROOT/.zfs/snapshot/test/dir/b || fail "failed to read snapshot b"

cat $TST_SROOT/dir/a || fail "failed to read dir/a"

cat $TST_SROOT/.zfs/snapshot/test/dir/a && fail "successfully read snapshot a"
cat $TST_SROOT/.zfs/snapshot/test/dir/b || fail "failed to read snapshot b"

tst_destroy_dataset

