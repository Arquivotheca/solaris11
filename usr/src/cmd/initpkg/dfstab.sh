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
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved
#
#  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

case "$MACH" in
  "u3b2"|"sparc"|"i386"|"ppc" )
	echo "
# Use the share(1m) or zfs(1m) commands for all share management
# This file is no longer supported or used in share management
# Configuration information can be imported into the system by running
# an existing dfstab file as a script.
#
" >dfstab
	;;
  * )
	echo "Unknown architecture."
	exit 1
	;;
esac
