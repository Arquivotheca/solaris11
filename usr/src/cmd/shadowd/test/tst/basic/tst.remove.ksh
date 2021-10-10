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
# Tests that we can open and remove a file from a shadowed directory.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"
echo "this is file b" > $TST_ROOT/b || fail "failed to create file b"

tst_create_dataset

[[ -f $TST_SROOT/a ]] || fail "file a doesn't exist"
rm $TST_SROOT/a || fail "failed to remove file a"
rm $TST_SROOT/b || fail "failed to remove file b"
[[ -f $TST_SROOT/a ]] && fail "faile a still exists"
[[ -f $TST_SROOT/b ]] && fail "faile b still exists"

tst_clear_shadow

[[ -f $TST_SROOT/a ]] && fail "faile a still exists"
[[ -f $TST_SROOT/b ]] && fail "faile b still exists"

tst_destroy_dataset

[[ -f $TST_ROOT/a ]] || fail "original file a doesn't exist"
[[ -f $TST_ROOT/b ]] || fail "original file b doesn't exist"
