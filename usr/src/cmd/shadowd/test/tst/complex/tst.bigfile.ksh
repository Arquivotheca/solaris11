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
# Create a very large (holey) file and make sure that we can migrate it
# correctly, to make sure that all offsets are correctly 64-bits.
#

. $ST_TOOLS/utility.ksh

function print_file
{
	typeset file=$1

	holey $file || fail "failed to get file contents"
}

tst_create_root

mkfile -n 1000000000g $TST_ROOT/big || fail "failed to create file big"
echo test >> $TST_ROOT/big || fail "failed to append to file big"

print_file $TST_ROOT/big
echo

tst_create_dataset

print_file $TST_SROOT/big

tst_destroy_dataset
