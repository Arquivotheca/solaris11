#! /usr/bin/ksh
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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# Generate a minimal set of closed binaries from a proto area.  Useful
# when building just the open tree.
#

usage="mkclosed isa root closed-root"

if [ $# -ne 3 ]; then
	print -u2 "usage: $usage"
	exit 1
fi

isa=$1
case "$isa" in
i386)	plat64=amd64;;
sparc)	plat64=sparcv9;;
*)	print -u2 "unknown isa: $isa"
	exit 1
	;;
esac

protoroot=$2
closedroot=$3

#
# Make closedroot an absolute path if it isn't already.  This is
# needed for the cpio invocation below.
#
[[ $closedroot = /* ]] || closedroot=`pwd`/$closedroot

# Check arguments before modifying filesystem.
cd $protoroot || exit 1

mkdir -p $closedroot || exit 1

#
# Use the system "cp" utility, not the shell's
#
CP=/usr/bin/cp

#
# Copy files from the proto area to the new closed tree.  We use cpio
# rather than a tar pipeline to make it easier to detect errors.
#
# We need /lib/libc_i18n.a & /lib/{sparcv9,amd64}/libc_i18n.a
#

mkdir -p $closedroot/lib/$plat64
$CP lib/libc_i18n.a $closedroot/lib
$CP lib/$plat64/libc_i18n.a $closedroot/lib/$plat64

#
# libkmsagent is closed due to gSOAP licensing, but it is
# a dependency of pkcs11/pkcs11_kms in the open area so 
# it must be available for the open_src build.
#
mkdir -p $closedroot/usr/lib/$plat64
$CP usr/lib/libkmsagent.so.1 $closedroot/usr/lib
$CP -rP usr/lib/libkmsagent.so $closedroot/usr/lib
$CP usr/lib/$plat64/libkmsagent.so.1 $closedroot/usr/lib/$plat64
$CP -rP usr/lib/$plat64/libkmsagent.so $closedroot/usr/lib/$plat64

