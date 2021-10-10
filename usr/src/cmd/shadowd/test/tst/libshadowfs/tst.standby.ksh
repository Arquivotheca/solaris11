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
# Tests that we can correctly open a shadow handle even when the filesystem is
# mounted in standby mode.  The real problem comes from when we are resuming a
# migration with a pending FID list, and we attempt to get the remote path
# information for an item on the list.  And even then, we have to have a
# partially migrated item on the pending list, or have died before we had a
# chance to remove it from the list.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir -p $TST_ROOT/dir || fail "failed to create dir"
echo foo > $TST_ROOT/dir/a || fail "failed to create file"

stsuspend

tst_create_legacy_dataset

mkdir -p $TST_SROOT || fail "failed to create mountpoint"
mount -F zfs -o shadow=$TST_ROOT $TST_DATASET $TST_SROOT || \
    fail "failed to mount dataset"

ls $TST_SROOT/dir

#
# We need to unmount the thing, but we are also going to collapse our pending
# list in the process.  In order to simulate a hard failure, we copy the
# pending list somewhere else, then copy it back after mounting the filesystem.
#
tst_shadow_disable

cp $TST_SROOT/.SUNWshadow/pending/0 $TST_SROOT/pending.0 || \
    fail "failed to save pending list"

umount $TST_SROOT || fail "failed to unmount dataset"
mount -F zfs -o shadow=standby $TST_DATASET $TST_SROOT || \
    fail "failed to remount dataset"

cp $TST_SROOT/pending.0 $TST_SROOT/.SUNWshadow/pending/0 || \
    fail "failed to restore pending list"

runat $TST_SROOT/dir "echo dir > SUNWshadow" || fail \
    fail "failed to set shadow attribute"

#
# Finally, we should have a pending list that contains 'dir', and it will have a
# shadow attribute set.  At this point, we want to make sure we don't hang.
#
tst_shadow_enable

stmigrate $TST_SROOT -1 || fail "failed to open shadow handle"

tst_destroy_dataset
stresume
