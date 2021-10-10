#!/bin/ksh93
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
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

# Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.

usage="usage: diff3 file1 file2 file3"

# mktmpdir - Create a private (mode 0700) temporary directory inside of /tmp
# for this process's temporary files.  We set up a trap to remove the 
# directory on exit (trap 0), and also on SIGHUP, SIGINT, SIGQUIT, SIGPIPE,
# and SIGTERM.

mktmpdir() {
	tmpdir=/tmp/diff3.$$
	trap '/usr/bin/rm -rf $tmpdir' 0 1 2 3 13 15
	/usr/bin/mkdir -m 700 $tmpdir || exit 1
}
mktmpdir

e=
case $1 in
-*)
	e=$1
	shift;;
esac
if [ $# != 3 ]; then
	echo ${usage} 1>&2
	exit 1
fi

isdir() {
	echo "diff3: $1: is a directory" 1>&2
	exit 1
}
[ ! -d "$1" ] || isdir "$1"
[ ! -d "$2" ] || isdir "$2"
[ ! -d "$3" ] || isdir "$3"

notfound() {
	echo "diff3: $1: not found" 1>&2
	exit 1
}
[ -e "$1" ] || notfound "$1"
[ -e "$2" ] || notfound "$2"
[ -e "$3" ] || notfound "$3"

badfiletype() {
	echo "diff3: $1: is not a regular or character special file" 1>&2
	exit 1
}
[ -f "$1" -o -c "$1" ] || badfiletype "$1"
[ -f "$2" -o -c "$2" ] || badfiletype "$2"
[ -f "$3" -o -c "$3" ] || badfiletype "$3"

f1=$1 f2=$2 f3=$3
if [ -c $f1 ]
then
	/usr/bin/cat $f1 > $tmpdir/d3c$$
	f1=$tmpdir/d3c$$
fi
if [ -c $f2 ]
then
	/usr/bin/cat $f2 > $tmpdir/d3d$$
	f2=$tmpdir/d3d$$
fi
if [ -c $f3 ]
then
	/usr/bin/cat $f3 > $tmpdir/d3e$$
	f3=$tmpdir/d3e$$
fi

/usr/bin/diff $f1 $f3 > $tmpdir/d3a$$ 2> $tmpdir/d3a$$.err
STATUS=$?
if [ $STATUS -eq 1 ]
then
	/usr/xpg4/bin/grep -q "^[<>]" $tmpdir/d3a$$
	RET=$?
	if [ $RET -eq 1 ]
	then
		/usr/bin/cat $tmpdir/d3a$$
		exit $STATUS
	fi

	if [ $RET -gt 1 ]
	then
		echo "diff3 failed" 1>&2
		exit $STATUS
	fi
fi

if [ $STATUS -gt 1 ]
then
	/usr/bin/cat $tmpdir/d3a$$.err
	exit $STATUS
fi

/usr/bin/diff $f2 $f3 > $tmpdir/d3b$$ 2> $tmpdir/d3b$$.err
STATUS=$?
if [ $STATUS -eq 1 ]
then
	/usr/xpg4/bin/grep -q "^[<>]" $tmpdir/d3b$$
	RET=$?
	if [ $RET -eq 1 ]
	then
		/usr/bin/cat $tmpdir/d3b$$
		exit $STATUS
	fi

	if [ $RET -gt 1 ]
	then
		echo "diff3 failed" 1>&2
		exit $STATUS
	fi
fi

if [ $STATUS -gt 1 ]
then
	/usr/bin/cat $tmpdir/d3b$$.err
	exit $STATUS
fi

/usr/lib/diff3prog $e $tmpdir/d3[ab]$$ $f1 $f2 $f3
