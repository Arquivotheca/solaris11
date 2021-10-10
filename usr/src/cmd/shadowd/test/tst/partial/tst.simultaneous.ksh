#!/bin/ksh -p

#
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
# Tests that simultaneous writes to the same region will be recognized and the
# data will not be migrated twice.  To do this, we set the filesystem into
# "spin" mode, which will pause the migrations while the lock isn't held.  We
# can then spawn multiple writers, sleep to make sure they reach the spin loop,
# and then disable the spin mode.
#

. $ST_TOOLS/utility.ksh

tst_create_root

dd if=/dev/urandom of=$TST_ROOT/file bs=384k count=1 2>/dev/null || \
    fail "failed to create file"

tst_create_dataset

stspin $TST_SROOT on || fail "failed to enable spin mode"

dd if=$TST_ROOT/file of=$TST_SROOT/file bs=4k count=1 oseek=128k \
    conv=notrunc 2>/dev/null &
ONE=$!

dd if=$TST_ROOT/file of=$TST_SROOT/file bs=4k count=1 oseek=141k \
    conv=notrunc 2>/dev/null &
TWO=$!

sleep 0.5

stspin $TST_SROOT off || fail "failed to disable spin mode"

# wait for both to complete
wait $ONE
wait $TWO

# now migrate entire file
dd if=$TST_SROOT/file of=/dev/null bs=128k 2>/dev/null || \
    fail "failed to read entire file"

# make same modifications to original
dd if=$TST_ROOT/file of=$TST_ROOT/file bs=4k count=1 oseek=128k \
    conv=notrunc 2>/dev/null || fail "failed to modify original"
dd if=$TST_ROOT/file of=$TST_ROOT/file bs=4k count=1 oseek=141k \
    conv=notrunc 2>/dev/null || fail "failed to modify original"

diff $TST_ROOT/file $TST_SROOT/file || fail "files are different"

tst_destroy_dataset
