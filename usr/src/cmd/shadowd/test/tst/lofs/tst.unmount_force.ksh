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
# Just do a simple lofs unmount while files are held open and verify that we
# get EBUSY.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo hi > $TST_ROOT/a

mkdir -p $TST_SROOT || fail "failed to mkdir $TST_SROOT"
mount -F lofs $TST_ROOT $TST_SROOT || fail "failed lofs mount"

cat $TST_SROOT/a || fail "failed to cat file a"

stkeepopen $TST_SROOT/a &
PID=$!

sleep 0.1

umount -f $TST_SROOT || fail "successfully unmounted lofs mount"

cat $TST_SROOT/a && fail "successfully cat file a"

kill $PID
wait $PID

exit 0
