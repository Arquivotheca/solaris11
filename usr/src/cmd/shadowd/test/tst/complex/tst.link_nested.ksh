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
# Create hard links in multiple nested filesystems, and make sure that we
# correctly migrate them.
#

. $ST_TOOLS/utility.ksh

tst_create_root_dataset

echo "this is file a" > $TST_ROOT/a || \
    fail "failed to create file a"
mkdir $TST_ROOT/dir || \
    fail "failed to mkdir dir"
ln $TST_ROOT/a $TST_ROOT/dir/b || \
    fail "failed to link a to b"

zfs create $TST_RDATASET/foo || fail "failed to create $TST_RDATASET/foo"

echo "this is file c" > $TST_ROOT/foo/c || \
    fail "failed to create file c"
mkdir $TST_ROOT/foo/dir || \
    fail "failed to mkdir foo/dir"
ln $TST_ROOT/foo/c $TST_ROOT/foo/dir/d || \
    fail "failed to link c to do"

cat $TST_ROOT/a
ls -l $TST_ROOT/a | awk '{print $2}'
cat $TST_ROOT/dir/b
ls -l $TST_ROOT/dir/b | awk '{print $2}'
cat $TST_ROOT/foo/c
ls -l $TST_ROOT/foo/c | awk '{print $2}'
cat $TST_ROOT/foo/dir/d
ls -l $TST_ROOT/foo/dir/d | awk '{print $2}'
echo 

tst_create_dataset

cat $TST_SROOT/a
ls -l $TST_SROOT/a | awk '{print $2}'
cat $TST_SROOT/dir/b
ls -l $TST_SROOT/dir/b | awk '{print $2}'
cat $TST_SROOT/foo/c
ls -l $TST_SROOT/foo/c | awk '{print $2}'

#
# We remount the dataset, which should trigger the code to read in the old
# indices and correctly get the updated link count.
#
tst_unmount_dataset
tst_mount_dataset

cat $TST_SROOT/foo/dir/d
ls -l $TST_SROOT/foo/dir/d | awk '{print $2}'
ls -l $TST_SROOT/foo/c | awk '{print $2}'

tst_destroy_dataset

zfs destroy $TST_RDATASET/foo || fail "failed to destroy $TST_RDATASET/foo"
tst_destroy_root_dataset

exit 0
