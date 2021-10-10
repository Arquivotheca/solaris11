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
# Tests that owner and group settings are preserved.
#

. $ST_TOOLS/utility.ksh

function create_file
{
	typeset owner=$1 group=$2 file=$3
	touch $file || fail "failed to create $file"
	chown $owner:$group $file || fail "failed to chown $file"
}

function print_owner
{
	typeset file=$1

	ls -ld $file > /dev/null 2>&1 || fail "failed to lookup $file"
	set -- $(ls -ld $file)

	echo $3 $4
}

tst_create_root

create_file bin other $TST_ROOT
create_file dladm daemon $TST_ROOT/a

print_owner $TST_ROOT
print_owner $TST_ROOT/a
echo

tst_create_dataset

print_owner $TST_SROOT
print_owner $TST_SROOT/a
echo

tst_clear_shadow

print_owner $TST_SROOT
print_owner $TST_SROOT/a

tst_destroy_dataset
