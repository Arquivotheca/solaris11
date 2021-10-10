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
# Tests that many hard links are migrated correctly.  V4 implements
# hard links with shadow vnodes that have a bit different behavior
# for the VOP_OPEN() case.  This only tests links within a single filesystem.
#

. $(dirname $0)/lib.nfs.ksh
TST_ROOT=$NFS_ROOT

function make_file
{
	dd if=/dev/random count=4 of=$1 > /dev/null 2>& 1
}

function make_filesI
{
	make_file $TST_ROOT/es/one || fail "links test setup I"
	ln $TST_ROOT/es/one $TST_ROOT/es/1 || fail "links test setup I"
	ln $TST_ROOT/es/one $TST_ROOT/es/uno || fail "links test setup I"
	make_file $TST_ROOT/es/two || fail "links test setup I"
	ln $TST_ROOT/es/two $TST_ROOT/es/2 || fail "links test setup I"
	ln $TST_ROOT/es/two $TST_ROOT/es/dos || fail "links test setup I"
	make_file $TST_ROOT/es/three || fail "links test setup I"
	ln $TST_ROOT/es/three $TST_ROOT/es/3 || fail "links test setup I"
	ln $TST_ROOT/es/three $TST_ROOT/es/tres || fail "links test setup I"
	make_file $TST_ROOT/es/four || fail "links test setup I"
	ln $TST_ROOT/es/four $TST_ROOT/es/4 || fail "links test setup I"
	ln $TST_ROOT/es/four $TST_ROOT/es/quatro || fail "links test setup I"
	make_file $TST_ROOT/es/five || fail "links test setup I"
	ln $TST_ROOT/es/five $TST_ROOT/es/5 || fail "links test setup I"
	ln $TST_ROOT/es/five $TST_ROOT/es/cinco || fail "links test setup I"
}

function make_filesII
{
	ln $TST_ROOT/es/one $TST_ROOT/g/eins || fail "links test setup II"
	ln $TST_ROOT/es/two $TST_ROOT/g/zwei || fail "links test setup II"
	ln $TST_ROOT/es/three $TST_ROOT/g/drei || fail "links test setup II"
	ln $TST_ROOT/es/four $TST_ROOT/g/vier || fail "links test setup II"
	ln $TST_ROOT/es/five $TST_ROOT/g/funf || fail "links test setup II"
	make_file $TST_ROOT/g/sechs || fail "links test setup II"
	ln $TST_ROOT/g/sechs $TST_ROOT/g/6 || fail "links test setup II"
	make_file $TST_ROOT/g/sieben || fail "links test setup II"
	ln $TST_ROOT/g/sieben $TST_ROOT/g/7 || fail "links test setup II"
	make_file $TST_ROOT/g/acht || fail "links test setup II"
	ln $TST_ROOT/g/acht $TST_ROOT/g/8 || fail "links test setup II"
	make_file $TST_ROOT/g/neun || fail "links test setup II"
	make_file $TST_ROOT/g/zehn || fail "links test setup II"
}

mkdir $TST_ROOT/es || fail "failed to mkdir es"
mkdir $TST_ROOT/g || fail "failed to mkdir g"
make_filesI
make_filesII

tst_create_dataset

find $TST_SROOT/es -exec sum {} \; > /dev/null 2>& 1 &
find $TST_SROOT/g -exec sum {} \; > /dev/null 2>& 1 

ls -il $TST_SROOT/es $TST_SROOT/g | egrep -v 'total|tst|^$' | \
    nawk '{print $1, $NF}' | sort -n | \
    nawk ' \
	BEGIN {started=0; prev=0;}
	{ if (prev != $1) { \
		if (started != 0) { \
			printf("\n"); \
		} \
	} \
	started = 1; \
	printf(" %s",$NF); \
	prev=$1; \
	} \
	END {printf("\n")}' | sort

tst_destroy_dataset

nfs_unmount
