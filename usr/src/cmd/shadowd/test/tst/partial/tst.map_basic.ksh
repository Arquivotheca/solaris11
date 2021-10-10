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
# Tests basic operation of the space map, without collapsing it.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkfile 512k $TST_ROOT/file || fail "failed to create file"

tst_create_dataset

tst_spacemap_disable

#
# Space map should not exist, yet.
#
stspacemap $TST_SROOT/file 2>/dev/null && fail "successfully read space map"

# Add local record from 128k-256k
dd if=$TST_SROOT/file of=/dev/null bs=1 count=1 iseek=128k \
    conv=notrunc 2>/dev/null || fail "failed to read at 128k"
stspacemap $TST_SROOT/file || fail "failed to read space map"
echo

# And from 384k-512k, writing this time
dd if=/dev/zero of=$TST_SROOT/file bs=1 count=1 oseek=385k \
    conv=notrunc 2>/dev/null || fail "failed to write at 384kk"
stspacemap $TST_SROOT/file || fail "failed to read space map"

tst_spacemap_enable

tst_destroy_dataset
