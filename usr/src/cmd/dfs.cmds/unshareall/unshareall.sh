#!/usr/sbin/sh
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
# Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.

# unshareall  -- unshare resources

USAGE="unshareall [-F fsys[,fsys...]]"
fsys=
set -- `getopt F: $*`
if [ $? != 0 ]		# invalid options
	then
	echo $USAGE >&2
	exit 1
fi
for i in $*		# pick up the options
do
	case $i in
	-F)  fsys=$2; shift 2;;
	--)  shift; break;;
	esac
done

if [ $# -gt 0 ]		# accept no arguments
then
	echo $USAGE >&2
	exit 1
fi

if [ "$fsys" ]		# for each file system ...
then
	fsys=`echo $fsys|tr ',' ' '`
	for i in $fsys
	do
		/usr/sbin/unshare -F $fsys -a
	done
else			# for every file system ...
	/usr/sbin/unshare -a
fi

