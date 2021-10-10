#!/usr/bin/sh
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
. /lib/svc/share/smf_include.sh

#
# Disable sac if we can't write to its directory.
#
WDIR=/etc/saf
if [[ ! -w $WDIR ]]
then
	/usr/sbin/svcadm disable -t "$SMF_FMRI"
	echo "$SMF_FMRI is disabled because $WDIR is not writable."
	sleep 5
	exit $SMF_EXIT_OK
fi

exec /usr/lib/saf/sac "$@" 
