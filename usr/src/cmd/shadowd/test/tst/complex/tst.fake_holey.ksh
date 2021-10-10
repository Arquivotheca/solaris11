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
# Create a sparse file on a filesystem that doesn't support holes (or a ZFS
# filesystem with blocksize larger than the holes), and then shadow the result.
# We should automatically detect the holes anyway and propagate those to the
# shadowed filesystem.  Note that the holes must align on vfs_shadow_min_xfer
# (128k) block size, independent of the underlying filesystem block size.
#

. $ST_TOOLS/utility.ksh

function create_file
{
	typeset file=$1 size=$2
	shift 2

	mkfile $size $file || fail "failed to create file"

	while [[ $# -gt 0 ]]; do
		typeset offset=$1 length=$2
		shift 2

		dd if=/dev/urandom of=$file bs=1024 count=$length \
		    oseek=$offset conv=notrunc 2>/dev/null || \
		    fail "failed to write data"	
	done
}

function print_file
{
	typeset file=$1

	truss holey $file || fail "failed to get file contents"
}

tst_create_root_dataset -o recordsize=128k

#
# Create a file of the form:
#
#	DATA	128k
#	HOLE	256k
#	DATA	256k
#
create_file $TST_ROOT/a 440k 0 128 384 256

#
# And another file that starts and ends with a hole:
#
#	HOLE	256k
#	DATA	128k
#	HOLE	128k
#	DATA	512k
#	HOLE	128k
#
create_file $TST_ROOT/b 1152k 256 128 512 512

print_file $TST_ROOT/a
echo
print_file $TST_ROOT/b
echo

tst_create_dataset -o recordsize=128k

print_file $TST_SROOT/a
echo
print_file $TST_SROOT/b

tst_destroy_dataset
tst_destroy_root_dataset
