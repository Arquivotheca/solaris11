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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /usr/lib/brand/solaris/common.ksh

m_usage=$(gettext  "detach [-n ].")

noexecute=0

# Other brand detach options are invalid for this brand.
while getopts "nR:z:" opt; do
	case $opt in
		n)	noexecute=1 ;;
		R)	ZONEPATH="$OPTARG" ;;
		z)	ZONENAME="$OPTARG" ;;
		?)	fail_usage "" ;;
		*)	fail_usage "";;
	esac
done
shift $((OPTIND-1))

init_zone zone "$ZONENAME" "$ZONEPATH"
eval $(bind_legacy_zone_globals zone)

if (( $noexecute == 1 )); then
	cat /etc/zones/$ZONENAME.xml
	exit $ZONE_SUBPROC_OK
fi

# All of the hard stuff is done in commmon code.
detach_zone zone

exit $ZONE_SUBPROC_OK
