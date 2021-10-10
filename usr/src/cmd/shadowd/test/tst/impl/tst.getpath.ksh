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
# Tests that the SHADOW_IOC_GETPATH ioctl() works.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo foo > $TST_ROOT/a || fail "failed to touch a"
mkdir $TST_ROOT/dir || fail "failed to mkdir dir"
echo foo > $TST_ROOT/dir/b || fail "failed to touch b"

tst_create_dataset

mv $TST_SROOT/dir $TST_SROOT/dir2 || fail "failed to rename dir"

stpath $TST_SROOT/a || fail "failed to get path for a"
stpath $TST_SROOT/dir2 || fail "failed to get path for dir2"
stpath $TST_SROOT/dir2/b || fail "failed to get path for dir2/b"

stpath $TST_SROOT/dir2 || fail "Failed to get path for dir2 again"

tst_destroy_dataset

stpath $TST_ROOT/a && fail "successfully got path for underlying file a"

exit 0
