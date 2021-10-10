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
# Tests the basic functionality of 'standby' mode.  In this mode, we should be
# able to create a filesystem, mount it as 'shadow=stanbdy', fork a process in
# the background that will wait, and then set the shadow setting properly and
# see that it completes correctly.  Because we are not trying to test the zfs
# infrastructure, we manually create a UFS share and tweak the mountpoint
# appropriately.
# 

. $ST_TOOLS/utility.ksh

function tst_standby
{
	mount -F ufs -o remount,rw,shadow=standby /dev/zvol/dsk/$TST_DATASET \
	    $TST_SROOT || fail "failed to remount UFS filesystem"
}

function tst_activate
{
	mount -F ufs -o remount,rw,shadow=$TST_ROOT /dev/zvol/dsk/$TST_DATASET \
	    $TST_SROOT || fail "failed to remount UFS filesystem"
}

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to mkdir 'dir'"
echo foo > $TST_ROOT/dir/foo || fail "failed to create 'dir/foo'"

tst_create_ufs

ls $TST_SROOT > /dev/null

tst_standby

cat $TST_SROOT/dir/foo &
PID=$!

sleep 0.5

pflags $PID > /dev/null || fail "background process not waiting"

tst_activate

wait $PID

tst_destroy_ufs

exit 0
