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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

. /usr/lib/brand/solaris/common.ksh

# Allows developer to override some things like PATH and PYTHONPATH
. /usr/lib/brand/solaris/developerenv.ksh

EXIT_CODE=$ZONE_SUBPROC_USAGE
f_noscprofile=$(gettext "Unable to apply system configuration profile to attached zone.")

zone=
init_zone zone "$1" "$2"
# Set ZONEPATH, etc.
eval $(bind_legacy_zone_globals zone)

shift; shift	# remove ZONENAME and ZONEPATH from arguments array

unset sc_config
noexecute=0

# Other brand attach options are invalid for this brand.
# Brand opts were already checked in attach and zone is already
# successfully attached at this time.  If a strange option
# error occurs then print a warning that no profile was applied
# and exit success.
while getopts "a:c:d:n:uUvz:" opt; do
	case $opt in
		a)	;;
		c)	sc_config="$OPTARG" ;;
		d) 	;;
		n)	noexecute=1 ;;
		u)	;;
		U)	;;
		v)	;;
		z)	;;
		?)	error "$f_noscprofile"
			exit $ZONE_SUBPROC_OK ;;
	esac
done
shift $((OPTIND-1))

if [[ $noexecute -eq 1 ]]; then
	EXIT_CODE=$ZONE_SUBPROC_OK
	exit $EXIT_CODE
fi

[[ -n $sc_config ]] && reconfigure_zone $sc_config

EXIT_CODE=$ZONE_SUBPROC_OK
exit $ZONE_SUBPROC_OK
