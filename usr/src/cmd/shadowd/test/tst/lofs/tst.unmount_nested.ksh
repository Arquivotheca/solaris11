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
# Tests that we can force unmount the lofs root covering nested filesystems.
#

. $ST_TOOLS/utility.ksh

zfs create -o mountpoint=$TST_ROOT $TST_RDATASET || \
    fail "failed to create root"

touch $TST_ROOT/file1 || fail "failed to touch file1"
touch $TST_ROOT/file2 || fail "failed to touch file2"

mkdir $TST_ROOT/subdir

zfs create $TST_RDATASET/subdir || fail "failed to create sub-filesystem"

touch $TST_ROOT/subdir/file3 || fail "failed to touch file3"
touch $TST_ROOT/subdir/file4 || fail "failed to touch file4"

mkdir -p $TST_SROOT || fail "failed to mkdir $TST_SROOT"
mount -F lofs $TST_ROOT $TST_SROOT || fail "failed to mount lofs"

cd $TST_SROOT/subdir || fail "failed to cd to subdir"
ls

umount -f $TST_SROOT || fail "failed to unmount root"

ls
cd /

zfs destroy -r $TST_RDATASET || fail "failed to destroy dataset"
