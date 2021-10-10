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
# Tests that a shadowed filesystem can be renamed, and that its mountpoint can
# be changed and all is propagated appropriately.
#

. $ST_TOOLS/utility.ksh

tst_create_root
echo "this is file a" > $TST_ROOT/a || fail "failed to create a"
echo "this is file b" > $TST_ROOT/b || fail "failed to create b"
echo "this is file c" > $TST_ROOT/c || fail "failed to create c"

tst_create_dataset

zfs rename $TST_DATASET $TST_DATASET.2 || \
    fail "failed to rename $TST_DATASET"

cat $TST_SROOT/a

zfs set mountpoint=$TST_SROOT.2 $TST_DATASET.2 || \
    fail "failed to change mountpoint of $TST_DATASET.2"

cat $TST_SROOT.2/b

zfs rename $TST_DATASET.2 $TST_DATASET || \
    fail "failed to rename $TST_DATASET.2"
zfs set mountpoint=$TST_SROOT $TST_DATASET || \
    fail "failed to change mountpoint of $TST_DATASET"

cat $TST_SROOT/c

tst_destroy_dataset
