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
# Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.

for svc in client server xfr passwd update; do
	/usr/sbin/svcadm disable -t svc:/network/nis/$svc:default
	[ $? = 0 ] || \
	    echo "ypstop: could not disable network/nis/$svc:default"
done

/usr/sbin/svcadm disable -t svc:/network/nis/domain:default
[ $? = 0 ] || \
    echo "ypstop: could not disable network/nis/domain:default"

# As this operation is likely configuration changing, restart the
# name-services milestone (such that configuration-sensitive services
# are in turn restarted).
/usr/sbin/svcadm restart milestone/name-services

exit 0
