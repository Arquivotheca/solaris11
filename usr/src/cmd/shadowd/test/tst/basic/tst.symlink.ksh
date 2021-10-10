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
# Creates a basic symlink structure and verifies that it is copied correctly.
#

. $ST_TOOLS/utility.ksh

function print_link
{
	typeset root=$1 file=$2

	ls -l $root/$file > /dev/null || fail "failed to lookup $file"
	set -- $(cd $root && ls -l $file)

	echo $1 $9 ${11}
}

tst_create_root

mkdir $TST_ROOT/a
ln -s ../nothing $TST_ROOT/a/b
ln -s bar $TST_ROOT/a/foo

print_link $TST_ROOT a/b
print_link $TST_ROOT a/foo
echo

tst_create_dataset

print_link $TST_SROOT a/b
print_link $TST_SROOT a/foo
echo

tst_clear_shadow

print_link $TST_SROOT a/b
print_link $TST_SROOT a/foo

tst_destroy_dataset
