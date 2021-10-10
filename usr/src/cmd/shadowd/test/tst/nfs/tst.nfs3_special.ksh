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
# Tests migration of special files: character and block devices, and FIFOs.
#

NFS_VERSION=3
. $(dirname $0)/lib.nfs.ksh
TST_ROOT=$NFS_ROOT

function print_file
{
	typeset file=$1

	ls -l $file | cut -b -40 || fail "failed to get attributes of $file"
}

mkfifo $TST_ROOT/fifo || fail "failed to create fifo"
mknod $TST_ROOT/char c 215 0 || fail "failed to create character device"
chmod 0432 $TST_ROOT/char || fail "failed to chmod character device"
mknod $TST_ROOT/block b 216 1 || fail "failed to create block device"
chown bin:other $TST_ROOT/block || fail "failed to chown block device"

print_file $TST_ROOT/fifo
print_file $TST_ROOT/char
print_file $TST_ROOT/block
echo

tst_create_dataset

print_file $TST_SROOT/fifo
print_file $TST_SROOT/char
print_file $TST_SROOT/block

tst_destroy_dataset

nfs_unmount
