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
# Tests that the read-only system attribute is preserved, even across multiple
# disjoint writes.
#

. $ST_TOOLS/utility.ksh

tst_create_root

dd if=/dev/urandom of=$TST_ROOT/file bs=384k count=1 2>/dev/null || \
    fail "failed to create file"
chmod S+c{R} $TST_ROOT/file || fail "failed to chmod file"

tst_create_dataset

dd if=/dev/zero of=$TST_SROOT/file bs=1 count=1 conv=notrunc 2>/dev/null && \
    fail "successfully wrote to file"
dd if=/dev/zero of=$TST_SROOT/file bs=1 count=1 conv=notrunc \
    oseek=128k 2>/dev/null && fail "successfully wrote to file again"

# modify the attribute locally
chmod S-c{R} $TST_SROOT/file || fail "failed to chmod file"

# now we should be able to write to it.
dd if=/dev/zero of=$TST_SROOT/file bs=1 count=1 conv=notrunc 2>/dev/null || \
    fail "failed to write to file"
dd if=/dev/zero of=$TST_SROOT/file bs=1 count=1 conv=notrunc \
    oseek=256k 2>/dev/null || fail "failed to write to file again"

tst_destroy_dataset

exit 0
