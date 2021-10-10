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
# Tests the mtime is set to the same value as the source and remains that way
# until a local modification is done to the file to change it.
#

. $ST_TOOLS/utility.ksh

tst_create_root

dd if=/dev/urandom of=$TST_ROOT/foo bs=384k count=1 2>/dev/null || \
    fail "failed to create file"

SRC_MTIME=$(ls -lE $TST_ROOT/foo | awk '{print $6 " " $7}')

tst_create_dataset

DST_MTIME=$(ls -lE $TST_SROOT/foo | awk '{print $6 " " $7}')

[[ $SRC_MTIME == $DST_MTIME ]] ||
    fail "initial timestamps don't match ($SRC_MTIME != $DST_MTIME)"

dd if=$TST_SROOT/foo of=/dev/null bs=1 count=1 conv=notrunc 2>/dev/null || \
    fail "failed to read from file"

DST_MTIME=$(ls -lE $TST_SROOT/foo | awk '{print $6 " " $7}')

[[ $SRC_MTIME == $DST_MTIME ]] ||
    fail "timestamps after read don't match ($SRC_MTIME != $DST_MTIME)"

dd if=/dev/zero of=$TST_SROOT/foo bs=1 count=1 conv=notrunc oseek=128k \
    2>/dev/null || fail "failed to write to file"

DST_MTIME=$(ls -lE $TST_SROOT/foo | awk '{print $6 " " $7}')

[[ $SRC_MTIME == $DST_MTIME ]]  &&
    fail "timestamps match after write ($SRC_MTIME == $DST_MTIME)"

dd if=$TST_SROOT/foo of=/dev/null bs=1 count=1 conv=notrunc iseek=256k \
    2>/dev/null || fail "failed to read from file"

DST_MTIME=$(ls -lE $TST_SROOT/foo | awk '{print $6 " " $7}')

[[ $SRC_MTIME == $DST_MTIME ]]  &&
    fail "timestamps match after write/read ($SRC_MTIME == $DST_MTIME)"

tst_destroy_dataset

exit 0
