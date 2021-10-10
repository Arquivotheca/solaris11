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
# Create a ZFS shadow filesystem, remount it with shadow=standby, verify that
# the mount is indeed in standby mode, and that it can later be remounted in
# normal mode.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to mkdir 'dir'"
echo foo > $TST_ROOT/dir/foo || fail "failed to create 'dir/foo'"

tst_create_dataset

ls $TST_SROOT > /dev/null

zfs mount -o remount,shadow=standby $TST_DATASET || \
    fail "failed to remount in standby mode"

cat $TST_SROOT/dir/foo &
PID=$!

sleep 0.5

pflags $PID > /dev/null || fail "background process not waiting"

zfs mount -o remount $TST_DATASET || \
    fail "failed to remount in normal mode"

wait $PID

tst_destroy_dataset

exit 0
