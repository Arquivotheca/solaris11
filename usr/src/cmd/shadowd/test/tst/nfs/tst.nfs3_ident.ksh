#!/bin/ksh -p

#
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
# Tests that file ownership is correctly migrated when using NFSv3.
#

NFS_VERSION=3
. $(dirname $0)/lib.nfs.ksh
TST_ROOT=$NFS_ROOT

function print_owner
{
	typeset file=$1

	ls -lnd $file > /dev/null 2>&1 || fail "failed to lookup $file"
	set -- $(ls -lnd $file)

	echo $3 $4
}

echo foo > $TST_ROOT/file
chown dladm:sys $TST_ROOT/file || fail "failed to change ownership"
echo foo > $TST_ROOT/file2
chown 2000:3000 $TST_ROOT/file2 || fail "failed to change ownership"

print_owner $TST_ROOT/file
print_owner $TST_ROOT/file2

echo

tst_create_dataset

print_owner $TST_SROOT/file
print_owner $TST_SROOT/file2

tst_destroy_dataset
nfs_unmount
