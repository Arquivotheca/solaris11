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
# Tests a simplistic directory hierarchy.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/a
mkdir $TST_ROOT/b
touch $TST_ROOT/c
mkdir $TST_ROOT/a/a
mkdir $TST_ROOT/a/b
touch $TST_ROOT/a/c
mkdir $TST_ROOT/a/b/c

(cd $TST_ROOT && find . -type d | sort) || fail "find failed"
(cd $TST_ROOT && find . -type f | sort) || fail "find failed"
echo

tst_create_dataset

(cd $TST_SROOT && find . -type d | grep -v ".SUNWshadow" | sort) || \
    fail "find failed"
(cd $TST_SROOT && find . -type f | grep -v ".SUNWshadow" | sort) || \
    fail "find failed"
echo

tst_clear_shadow

(cd $TST_SROOT && find . -type d | grep -v ".SUNWshadow" | sort) || \
    fail "find failed"
(cd $TST_SROOT && find . -type f | grep -v ".SUNWshadow" | sort) || \
    fail "find failed"

tst_destroy_dataset
