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
# Create a sparse file and verify that it is copied over correctly with holes
# intact.
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

		dd if=/dev/random of=$file bs=1 count=$length oseek=$offset \
		    conv=notrunc 2>/dev/null || fail "failed to write data"	
	done
}

function print_file
{
	typeset file=$1

	holey $file || fail "failed to get file contents"
}

tst_create_root_dataset -o recordsize=8k

#
# Create a file of the form:
#
#	DATA	8k
#	HOLE	16k
#	DATA	16k
#
create_file $TST_ROOT/a 40k 0 8k 24k 16k

#
# And another file that starts with a hole.  Ideally we want to end with a
# hole, but it's a bit of a pain because for reasons that aren't clear, the
# last block doesn't get turned into a hole on ZFS.
#
#	HOLE	16k
#	DATA	8k
#	HOLE	8k
#	DATA	64k
#
create_file $TST_ROOT/b 64k 16k 8k 32k 32k

print_file $TST_ROOT/a
echo
print_file $TST_ROOT/b
echo

tst_create_dataset -o recordsize=8k

print_file $TST_SROOT/a
echo
print_file $TST_SROOT/b

tst_destroy_dataset
tst_destroy_root_dataset
