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
# Create a ZFS pool with a shadowed filesystem, and make sure 'zpool destroy'
# correctly tears down the filesystem, and shadow mount.
#

. $ST_TOOLS/utility.ksh

POOL1=shadowtest.$(basename $0).$$.1
POOL2=shadowtest.$(basename $0).$$.2
mkfile -n 128m $ST_ROOT/data1 || fail "failed to create $ST_ROOT/data1"
mkfile -n 128m $ST_ROOT/data2 || fail "failed to create $ST_ROOT/data2"
zpool create -O mountpoint=none $POOL1 $ST_ROOT/data1 || \
    fail "failed to create pool $POOL1"
zpool create -O mountpoint=none $POOL2 $ST_ROOT/data2 || \
    fail "failed to create pool $POOL2"

zfs create -o mountpoint=$TST_ROOT $POOL1/src || \
    fail "failed to create dataset $POOL1/src"
zfs create -o mountpoint=$TST_SROOT -o shadow=file://$TST_ROOT $POOL2/dst || \
    fail "failed to create dataset $POOL2/dst"

zpool destroy $POOL2 || fail "failed to destroy $POOL2"
zpool destroy $POOL1 || fail "failed to destroy $POOL1"
rm -f $ST_ROOT/data1 || fail "failed to remove $ST_ROOT/data1"
rm -f $ST_ROOT/data2 || fail "failed to remove $ST_ROOT/data2"
