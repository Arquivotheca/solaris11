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
# Tests shadowing of system attributes.  In particular, migrating a read only
# or immutable file works properly.
#

. $ST_TOOLS/utility.ksh

function print_attributes
{
	typeset file=$1

	ls -ld $file >/dev/null 2>&1 || fail "failed to read $file"
	ls -d -/ c $file | tail -1
}

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"
chmod S=c{AHRSadimu} $TST_ROOT/a || fail "failed to set attributes of a"
echo "this is file b" > $TST_ROOT/b || fail "failed to create file b"
chmod S=c{iR} $TST_ROOT/b || fail "failed to set attributes of b"
echo "this is file c" > $TST_ROOT/c || fail "failed to create file c"
chmod S=c{q} $TST_ROOT/c || fail "failed to set attributes of c"
chmod S=c{A} $TST_ROOT || fail "failed to set attributes of $TST_ROOT"

cat $TST_ROOT/a || fail "failed to read file a"
print_attributes $TST_ROOT/a
cat $TST_ROOT/b || fail "failed to read file b"
print_attributes $TST_ROOT/b
cat $TST_ROOT/c >/dev/null 2>&1 && fail "successfully read file c"
print_attributes $TST_ROOT/c
print_attributes $TST_ROOT
echo

tst_create_dataset

print_attributes $TST_SROOT/a
cat $TST_SROOT/a || fail "failed to read file a"
print_attributes $TST_SROOT/a
print_attributes $TST_SROOT/b
cat $TST_SROOT/b || fail "failed to read file b"
print_attributes $TST_SROOT/b
# quarantined files cannot be read and getting attributes apparently requires
# reading the extended attributes directory, so we can't do much of anything.
cat $TST_SROOT/c >/dev/null 2>&1 && fail "successfully read file c"
print_attributes $TST_SROOT

tst_destroy_dataset

exit 0
