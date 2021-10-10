#!/usr/bin/ksh
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

# Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.

#	compress - compress the spell program log

H_SPELL=${H_SPELL:-$HOME/.spellhist}
trap 'rm -f /usr/tmp/spellhist;exit' 1 2 3 15
echo "COMPRESSED `date`" > /usr/tmp/spellhist
grep -v ' ' $H_SPELL | sort -fud >> /usr/tmp/spellhist
cp /usr/tmp/spellhist $H_SPELL
rm -f /usr/tmp/spellhist
