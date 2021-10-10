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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# Tests migration of extended attributes on files and directories.  Extended
# attributes must be regular files, but can have attributes that control access
# (owner, permissions, ACLs, etc).
#

. $ST_TOOLS/utility.ksh

function print_owner
{
	typeset file=$1 xattr=$2

	runat $file ls -l $xattr > /dev/null || \
	    fail "failed to lookup $file@$xattr"
	set -- $(runat $file ls -l $xattr)

	echo $3 $4
}

function print_mode
{
	typeset file=$1 xattr=$2

	runat $file ls -l $xattr > /dev/null || \
	    fail "failed to lookup $file@$xattr"
	set -- $(runat $file ls -l $xattr)

	echo $1
}

tst_create_root

#
# Create the following hierarchy:
#
#	@xattr	
#	a
#	a@xattr
#	a@xattr2
#	dir
#	dir@xattr
#	dir/b
#
echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"
mkdir $TST_ROOT/dir || fail "failed to create dir"
echo "this is file b" > $TST_ROOT/dir/b || fail "failed to create file b"
runat $TST_ROOT "echo 'this is a root xattr' > xattr" || \
    fail "failed to create root xattr"
runat $TST_ROOT/a "echo 'this is a file xattr' > xattr" || \
    fail "failed to create file xattr"
runat $TST_ROOT/a chmod 0432 xattr ||  \
    fail "failed to chmod xattr" 
runat $TST_ROOT/a "echo 'this is another file xattr' > xattr2" || \
    fail "failed to create file xattr2"
runat $TST_ROOT/a chown bin:other xattr2 || \
    fail "failed to chown xattr2"
runat $TST_ROOT/dir "echo 'this is a directory xattr' > xattr" || \
    fail "failed to create file xattr"

runat $TST_ROOT cat xattr || fail "failed to cat root xattr"
cat $TST_ROOT/a || fail "failed to cat file a"
runat $TST_ROOT/a cat xattr || fail "failed to cat a@xattr"
print_owner $TST_ROOT/a xattr2
print_mode $TST_ROOT/a xattr
runat $TST_ROOT/a cat xattr2 || fail "failed to cat a@xattr2"
runat $TST_ROOT/dir cat xattr || fail "failed to cat dir@xattr"
cat $TST_ROOT/dir/b || fail "failed to cat file dir/b"
echo

tst_create_dataset

runat $TST_SROOT cat xattr || fail "failed to cat shadow root xattr"
cat $TST_SROOT/a
runat $TST_SROOT/a cat xattr || fail "failed to cat shadow a@xattr"
print_owner $TST_SROOT/a xattr2
print_mode $TST_SROOT/a xattr
runat $TST_SROOT/a cat xattr2 || fail "failed to cat shadow a@xattr2"
runat $TST_SROOT/dir cat xattr || fail "failed to cat shadow dir@xattr"
cat $TST_SROOT/dir/b || fail "failed to cat file b"

tst_destroy_dataset
