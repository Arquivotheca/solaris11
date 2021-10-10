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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

#
# Shell script fragment to set standard build environment variables,
# for use by bldenv(1) and nightly(1).  Can be overridden by the
# user's environment file.  Because bldenv and nightly are both ksh
# scripts, we can use ksh syntax here.
#

#
# the source product has no SCCS history, and is modified to remove source
# that cannot be shipped. EXPORT_SRC is where the clear files are copied, then
# modified with 'make EXPORT_SRC'.
#
[ -n "$EXPORT_SRC" ] || export EXPORT_SRC="$CODEMGR_WS/export_src"

#
# CRYPT_SRC is similar to EXPORT_SRC, but after 'make CRYPT_SRC' the files in
# xmod/cry_files are saved. They are dropped on the exportable source to create
# the domestic build.
#
[ -n "$CRYPT_SRC" ] || export CRYPT_SRC="$CODEMGR_WS/crypt_src"

#
# OPEN_SRCDIR is where we copy the open tree to so that we can be sure
# we don't have a hidden dependency on closed code.  The name ends in
# "DIR" to avoid confusion with the flags related to open source
# builds.
#
[ -n "$OPEN_SRCDIR" ] || export OPEN_SRCDIR="$CODEMGR_WS/open_src"

#
# Flag to enable creation of per-build-type proto areas.  If "yes",
# more proto areas are created, which speeds up incremental builds but
# increases storage consumption.  Will be forced to "yes" for
# OpenSolaris deliveries.
#
[ -n "$MULTI_PROTO" ] || export MULTI_PROTO=no
