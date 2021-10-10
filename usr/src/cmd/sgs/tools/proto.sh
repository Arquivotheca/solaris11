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

#
# Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Generate a proto area suitable for the current architecture ($(MACH))
# sufficient to support the sgs build.
#
# Currently, the following releases are supported:
#	5.11, 5.10.
#

if [ "X$CODEMGR_WS" = "X" -o "X$MACH" = "X" ] ; then
	echo "usage: CODEMGR_WS and MACH environment variables must be set"
	exit 1
fi

RELEASE=$1

if [ "X$RELEASE" = "X" ] ; then
	echo "usage: proto release"
	exit 1;
fi

case $RELEASE in
	"5.11") break;;
	"5.10") break;;
	*)
	echo "usage: unsupported release $RELEASE specified"
	exit 1;;
esac

#
# 64-bit architecture
#
MACH64=""
if [ $MACH = "sparc" ]; then
    MACH64="sparcv9";
fi
if [ $MACH = "i386" ]; then
    MACH64="amd64";
fi

# If there is no proto, then have usr/src/Makefile create and populate it.
# The standard rule for establishing a workspace for building is 'make setup'
# in usr/src. However, that does far more work than we need, so we instead run
# the subrules that do what we need:
#	rootdirs -Creates the real and stub roots, and installs the standard
#		symlinks into them.
#	libstubs - Produces a full complement of stub objects under $STUBROOT.
# This stuff is a bit slow to run on every make invocation, and is also
# quite static since we are working only under sgs, so only run it when no
# proto is present.
#
PROTO_ROOT="$CODEMGR_WS/proto/root_$MACH"
PROTO_STUBROOT="${PROTO_ROOT}_stub"

if [ ! -d $PROTO_ROOT ]; then
	(cd $SRC; make rootdirs; make libstubs)
fi
if [ ! -d $PROTO_ROOT/opt/SUNWonld ]; then
	# Directories that we add to the standard proto
	dirs="	$PROTO_ROOT/opt/SUNWonld \
		$PROTO_ROOT/opt/SUNWonld/bin \
		$PROTO_ROOT/opt/SUNWonld/bin/$MACH64 \
		$PROTO_ROOT/opt/SUNWonld/doc \
		$PROTO_ROOT/opt/SUNWonld/lib \
		$PROTO_ROOT/opt/SUNWonld/lib/$MACH64 \
		$PROTO_ROOT/opt/SUNWonld/man \
		$PROTO_ROOT/opt/SUNWonld/man/man1 \
		$PROTO_ROOT/opt/SUNWonld/man/man1l \
		$PROTO_ROOT/opt/SUNWonld/man/man3t \
		$PROTO_ROOT/opt/SUNWonld/man/man3l \
		$PROTO_ROOT/opt/SUNWonld/man/man3x"

	for dir in `echo $dirs`
	do
		if [ ! -d $dir ] ; then
			echo $dir
			mkdir $dir
			chmod 777 $dir
		fi
	
		# And create the same in the parallel stub proto
		stubdir=`echo $dir | sed s:$PROTO_ROOT:$PROTO_STUBROOT:`
		if [ ! -d $stubdir ] ; then
			echo $stubdir
			mkdir $stubdir
			chmod 777 $stubdir
		fi
	done
fi

# We need a local copy of libc_pic.a.  We should probably build the code under
# usr/lib/libc, but in the interest of speed, take a stashed copy from the
# linkers server. If LINKERS_EXPORT is defined, we use it. Failing that, we
# fall over to linkers-data.us.oracle.com.
#
if [ "$LINKERS_EXPORT" = "" ]; then
	LINKERS_EXPORT=/net/linkers-data.us.oracle.com/export/linkers
fi

if [ ! -f $STUBROOT/lib/libc_pic.a ]; then
	cp $LINKERS_EXPORT/big/libc_pic/$RELEASE/$MACH/libc_pic.a \
		$STUBROOT/lib/libc_pic.a
fi
if [ ! -f $STUBROOT/lib/$MACH64/libc_pic.a ]; then
	cp $LINKERS_EXPORT/big/libc_pic/$RELEASE/$MACH64/libc_pic.a \
		$STUBROOT/lib/$MACH64/libc_pic.a
fi
