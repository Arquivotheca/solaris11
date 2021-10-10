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
# Tests that progress is tracked correctly.  We cannot verify our off-the-wall
# estimate, but we can make sure the total size migrated is correct, even in
# the presence of holes.
#

. $ST_TOOLS/utility.ksh

function create_file
{
	typeset file=$1 size=$2
	shift 2

	mkfile -n $size $file || fail "failed to create $file"

	while [[ $# -gt 0 ]]; do
		typeset offset=$1 length=$2
		shift 2

		dd if=/dev/urandom of=$file bs=1024 count=$length \
		    oseek=$offset conv=notrunc 2>/dev/null || \
		    fail "failed to write data"
	done
}

tst_create_root_dataset -o recordsize=8k

#
# Create a file of the form:
#
#	DATA	128k
#	HOLE	256k
#	DATA	256k
#
create_file $TST_ROOT/a 640k 0 128 384 256

#
# Create a normal file
#
dd if=/dev/urandom of=$TST_ROOT/c bs=1023 count=1 2>/dev/null || \
    fail "failed to create $TST_ROOT/c"

tst_create_dataset -o recordsize=8k

stprogress $TST_SROOT || fail "failed to migrate filesystem"

tst_destroy_dataset
tst_destroy_root_dataset
