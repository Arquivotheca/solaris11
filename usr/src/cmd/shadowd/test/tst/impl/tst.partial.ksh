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
# Tests migrating just the metadata for a file.  Create a file in a directory
# with some attributes and content, and do an 'ls' in the directory.  Then turn
# off shadow migration.  The resulting file should have a full set of
# attributes but no contents.
#

. $ST_TOOLS/utility.ksh

function print_attributes
{
	typeset file=$1 output=$2

	(cd $(dirname $file) && ls -l $(basename $file) > $output) || \
	    fail "failed to get attributes of $file"
}

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"
chmod 431 $TST_ROOT/a || fail "fail failed to chmod file a"
chown daemon:other $TST_ROOT/a || fail "failed to chown file a"

print_attributes $TST_ROOT/a $TST_ROOT/attr

tst_create_dataset

ls $TST_SROOT || fail "failed to list directory contents"

tst_clear_shadow

print_attributes $TST_SROOT/a $TST_ROOT/attr.shadow
(cat $TST_SROOT/a | grep "file a") && fail "file a has real contents"

diff $TST_ROOT/attr $TST_ROOT/attr.shadow || fail "attributes differ"

tst_destroy_dataset
