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
# This verifies that the SUNWshadow attribute is being set and removed
# correctly.  To do this, we create a directory, populate it, then
# disable the shadow mount option.  At this point, the attribute should exist
# on the unmigrated entries but not exist on the root.
#

. $ST_TOOLS/utility.ksh

function check_not_present
{
	typeset file=$1

	runat $file cat SUNWshadow > /dev/null 2>&1 && \
	    fail "SUNWshadow present for $file"
	runat $file cat SUNWshadow.amp > /dev/null 2>&1 && \
	    fail "SUNWshadow.map present for $file"
}

function print_shadow
{
	typeset file=$1

	runat $file cat SUNWshadow || fail "SUNWshadow missing for $file"
	echo
}

tst_create_root

mkdir $TST_ROOT/a || fail "failed to create a"
echo foo > $TST_ROOT/a/b || fail "failed to create b"
echo foo > $TST_ROOT/a/c || fail "failed to create c"

tst_create_dataset

ls $TST_SROOT/a > /dev/null || fail "failed to access a"
cat $TST_SROOT/a/b > /dev/null || fail "failed to access b"

tst_clear_shadow

check_not_present $TST_SROOT
check_not_present $TST_SROOT/a
check_not_present $TST_SROOT/a/b
print_shadow $TST_SROOT/a/c

tst_destroy_dataset
