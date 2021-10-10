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
# Induce an error and make sure that the attribute still exists on the file.
#

. $ST_TOOLS/utility.ksh

function print_shadow
{
	typeset file=$1

	runat $file cat SUNWshadow || fail "SUNWshadow missing for $file"
	echo
}

tst_create_root

echo foo > $TST_ROOT/a || fail "failed to create a"

tst_create_dataset

ls $TST_SROOT
rm $TST_ROOT/a
cat $TST_SROOT/a > /dev/null 2>&1 && fail "successfully read a"

tst_clear_shadow

print_shadow $TST_SROOT/a

tst_destroy_dataset
