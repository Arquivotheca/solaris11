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
# Tests that when migrating data from a share mounted in standby mode through
# libshadowfs, we get ESHADOW_BUSY instead of hanging.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir -p $TST_ROOT/dir || fail "failed to create dir"
echo foo > $TST_ROOT/dir/a || fail "failed to create file"

tst_create_legacy_dataset

mkdir -p $TST_SROOT || fail "failed to create mountpoint"
mount -F zfs -o shadow=standby $TST_DATASET $TST_SROOT || \
    fail "failed to mount dataset"

stmigrate $TST_SROOT 1 && fail "successfully migrated directory"

#
# Make sure we accumulate some pending list changes, to verify that loading the
# pending list doesn't run afoul of standby mode.  For this to be a problem, we
# need to have a parent directory with a shadow attribute (i.e. interrupted
# migration).  We simulate this by manually adding the attribute.
#
mount -F zfs -o remount,shadow=$TST_ROOT $TST_DATASET $TST_SROOT || \
    fail "failed to remount dataset"

ls $TST_SROOT/dir || fail "failed to list dir contents"

tst_shadow_disable

runat $TST_SROOT/dir "printf %s dir > SUNWshadow" || \
    fail "failed to create SUNWshadow attribute"

umount $TST_SROOT
tst_shadow_enable

mount -F zfs -o shadow=standby $TST_DATASET $TST_SROOT || \
    fail "failed to remount in standby mode"


stmigrate $TST_SROOT 1 && fail "successfully migrated directory"

tst_destroy_dataset

exit 0
