#!/bin/sh
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
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
#

#
# This script is invoked by python-governed executable wrappers from within
# s10-branded zones.  It uses the native python and native linker.
# The only consumer is currently the zfs command which uses python for
# a few of its subcommands.
#
# All native executables must be run using the native linker.
# By default, the kernel loads the linker at /lib/ld.so.1, which
# in an s10 zone is the s10 linker.  Hence when we run the native
# executable below, we explicitly specify /.SUNWnative/lib/ld.so.1 as our 32-
# bit linker and /.SUNWnative/lib/64/ld.so.1 as our 64-bit linker.
# For convience we define "n" to be the native path prefix.
#
pyname=$0
n=/.SUNWnative

PYTHONPATH=/.SUNWnative/usr/lib/python2.6/vendor-packages
export PYTHONPATH

# This wrapper is running in the S10 zone so there is no L10N for the
# following error msg.
if [ ! -f $n$pyname ]; then
	echo "Error: \"$pyname\" is not installed in the global zone"
	exit 1
fi

exec $n/usr/lib/brand/solaris10/s10_native \
    $n/lib/ld.so.1 \
    -e LD_NOENVIRON=1 \
    -e LD_NOCONFIG=1 \
    -e LD_PRELOAD_32=s10_npreload.so.1 \
    -e LD_PRELOAD_64=s10_npreload.so.1 \
    -e LD_LIBRARY_PATH_32="$n/lib:$n/usr/lib:$n/usr/lib/mps" \
    -e LD_LIBRARY_PATH_64="$n/lib/64:$n/usr/lib/64:$n/usr/lib/mps/64" \
    $n/usr/bin/python2.6 $n$pyname "$@"
