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
# Verifies that we do not migrate a file on a fop_fid() request.  Besides being
# unnecessary, this is required because otherwise the NFS server will panic in
# rfs4_op_readdir() when makefh4() fails.
#

. $ST_TOOLS/utility.ksh

function print_attributes
{
	typeset file=$1 output=$2

	(cd $(dirname $file) && ls -l $(basename $file) > $output) || \
	    fail "failed to get attributes of $file"
}

stsuspend

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"

tst_create_dataset

ls -l $TST_SROOT || fail "failed to list directory contents"

#
# This will implicitly call fop_fid() for the file 'a' but nothing else.
#

stpending 0 $TST_SROOT $TST_SROOT/a

tst_clear_shadow

print_attributes $TST_SROOT/a /dev/null
(cat $TST_SROOT/a | grep "file a") && fail "file a has real contents"

tst_destroy_dataset

stresume
