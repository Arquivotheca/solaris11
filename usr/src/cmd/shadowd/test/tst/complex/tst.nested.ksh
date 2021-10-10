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
# Create a nested filesystem and shadow a tree that spans both filesystems.
# This does not test for hard links within the tree.
#

. $ST_TOOLS/utility.ksh

tst_create_root_dataset

echo "this is file a" > $TST_ROOT/a || \
    fail "failed to create file a"

zfs create $TST_RDATASET/foo || fail "failed to create $TST_RDATASET/foo"

echo "this is file b" > $TST_ROOT/foo/b || \
    fail "failed to create file b"

cat $TST_ROOT/a || fail "failed to read file a"
cat $TST_ROOT/foo/b || fail "failed to read file b"
echo

tst_create_dataset

cat $TST_SROOT/a || fail "failed to read shadow a"
cat $TST_SROOT/foo/b || fail "failed to read shadow b"

tst_destroy_dataset

zfs destroy $TST_RDATASET/foo || fail "failed to destroy $TST_RDATASET/foo"
tst_destroy_root_dataset

exit 0
