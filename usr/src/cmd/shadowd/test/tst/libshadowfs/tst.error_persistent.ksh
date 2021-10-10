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
# Tests that a filesystem with persistent errors is correctly detected, and
# that when that problem is repaired we can continue migration.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo a > $TST_ROOT/a || fail "failed to create a"
echo b > $TST_ROOT/b || fail "failed to create b"

stsuspend

tst_create_dataset

ls $TST_SROOT> /dev/null || fail "failed to list root"
mv $TST_ROOT/a $TST_ROOT/a.renamed || fail "failed to rename a"

#
# Test once with the entry only on one pending list, and then cheat to create
# the same entry on both lists and make sure it still works.
#
stpersistent $TST_SROOT || fail "persistent errors not present"
cp $TST_SROOT/.SUNWshadow/pending/0 $TST_SROOT/.SUNWshadow/pending/1 || \
    fail "failed to copy pending list"
stpersistent $TST_SROOT || fail "persistent errors not present"

#
# We should have migrated 'b' regardless.
#
tst_shadow_disable
cat $TST_SROOT/b || fail "failed to cat b"
tst_shadow_enable

mv $TST_ROOT/a.renamed $TST_ROOT/a || fail "failed ot rename b back"

stpersistent $TST_SROOT && fail "still found persistent errors"

tst_destroy_dataset
stresume

exit 0
