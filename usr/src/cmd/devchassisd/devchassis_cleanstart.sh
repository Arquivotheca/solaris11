#!/sbin/sh
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

#
# Move an active, non-empty, DI_CRO_DB_FILE file aside to
# DI_CRO_DB_FILE_OLD.
#
if [[ -s /etc/dev/cro_db ]]; then
	rm -f /etc/dev/cro_db.old		> /dev/null 2>&1
	mv /etc/dev/cro_db /etc/dev/cro_db.old	> /dev/null 2>&1
fi

#
# cleanup devchassis: remove all /dev/chassis namespace and
#
rm -rf /dev/chassis/*				> /dev/null 2>&1
