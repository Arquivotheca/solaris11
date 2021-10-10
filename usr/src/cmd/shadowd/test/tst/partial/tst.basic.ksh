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
# Given a file consisting of 3 128k blocks, read from the middle block, disable
# shadow migration and check the hole status, then read from the other two.
#

. $ST_TOOLS/utility.ksh

tst_create_root

dd if=/dev/urandom of=$TST_ROOT/file bs=128k count=3 2>/dev/null || \
    fail "failed to create file"

tst_create_dataset

#
# Pick one byte at the beginning of the block.
#
dd if=$TST_SROOT/file of=/dev/null bs=1 count=1 iseek=128k conv=notrunc \
    2>/dev/null || fail "failed to read at 128k"

#
# At this point, we should have a file with a hole, a data section, and another
# hole.
#
tst_shadow_disable
holey $TST_SROOT/file || fail "failed to get file contents"
tst_shadow_enable

#
# Now read the entire 128k portion at the beginning.
#
dd if=$TST_SROOT/file of=/dev/null bs=128k count=1 conv=notrunc \
    2>/dev/null || fail "failed to read at 0"

#
# We should now have a 256k data section followed by a 128k hole.
#
tst_shadow_disable
echo
holey $TST_SROOT/file || fail "failed to get file contents"
tst_shadow_enable

#
# Write past the end of the file and make sure that don't migrate the third
# block.
#
dd if=/dev/zero of=$TST_SROOT/file bs=8k count=1 oseek=48 conv=notrunc \
    2>/dev/null || fail "failed to write at 384k"
tst_shadow_disable
echo
holey $TST_SROOT/file || fail "failed to get file contents"
tst_shadow_enable

#
# Read the contents of the third block comparing the file
#
dd if=/dev/zero of=$TST_ROOT/file bs=8k count=1 oseek=48 conv=notrunc \
    2>/dev/null || fail "failed to write at 384k"
diff $TST_ROOT/file $TST_SROOT/file > /dev/null || \
    fail "files do not match"

tst_shadow_disable
echo
holey $TST_SROOT/file || fail "failed to get file contents"
tst_shadow_enable

tst_destroy_dataset
exit 0
