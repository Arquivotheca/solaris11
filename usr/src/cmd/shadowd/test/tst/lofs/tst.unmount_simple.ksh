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
# Tests that we can unmount the zfs "backing" a lofs filesystem.
#

. $ST_TOOLS/utility.ksh

zfs create -o mountpoint=$TST_ROOT $TST_RDATASET || \
    fail "failed to create root"

zfs create $TST_RDATASET/subfs || \
    fail "failed to create sub-filesystem"

touch $TST_ROOT/subfs/file || fail "failed to touch file"

mkdir -p $TST_SROOT || fail "failed to mkdir $TST_SROOT"
mount -F lofs $TST_ROOT $TST_SROOT || fail "failed to mount lofs"

cd $TST_SROOT/subfs
ls

cd /

zfs unmount $TST_RDATASET/subfs || fail "failed to unmount sub-filesystem"

umount $TST_SROOT || fail "failed to unmount loopback mount"

zfs destroy -r $TST_RDATASET || fail "failed to destroy dataset"
