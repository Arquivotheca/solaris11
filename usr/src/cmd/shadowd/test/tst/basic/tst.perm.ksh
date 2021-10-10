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
# Tests that basic permissions are preserved.
#

. $ST_TOOLS/utility.ksh

function create_file
{
	typeset mode=$1 file=$2

	touch $file || fail "failed to create $file"
	chmod $mode $file || fail "failed to chmod $file"
}

function print_mode
{
	typeset file=$1

	ls -ld $file > /dev/null || fail "failed to lookup $file"
	set -- $(ls -ld $file)

	echo $1
}

tst_create_root

create_file 0432 $TST_ROOT/a
create_file 5175 $TST_ROOT/b
create_file 4243 $TST_ROOT/c
create_file 7777 $TST_ROOT/d
chmod 0723 $TST_ROOT || fail "failed to chmod $TST_ROOT"

print_mode $TST_ROOT/a
print_mode $TST_ROOT/b
print_mode $TST_ROOT/c
print_mode $TST_ROOT/d
print_mode $TST_ROOT
echo

tst_create_dataset

print_mode $TST_SROOT/a
print_mode $TST_SROOT/b
print_mode $TST_SROOT/c
print_mode $TST_SROOT/d
print_mode $TST_SROOT
echo

tst_clear_shadow

print_mode $TST_SROOT/a
print_mode $TST_SROOT/b
print_mode $TST_SROOT/c
print_mode $TST_SROOT/d
print_mode $TST_SROOT

tst_destroy_dataset
