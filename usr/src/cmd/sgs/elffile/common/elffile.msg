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
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
#

@ _START_

# Message file for cmd/sgs/elffile.

@ MSG_ID_ELFFILE

# Usage Messages

@ MSG_USAGE_BRIEF	"usage: %s [-s basic | detail | summary] file...\n"
@ MSG_USAGE_DETAIL_L_S	"\t[-s]\t\tbasic | detail | summary\n"

@ MSG_FILE_NONELF	"non-ELF\n"

@ MSG_ERR_OPEN		"%s: open: %s\n"
@ MSG_ERR_BAD_STYLE	"%s: unrecognized -f value: expected one of \
			 'basic', 'detail', or 'summary': %s\n"

@ _END_

# The following strings represent reserved words, files, pathnames and symbols.
# Reference to this strings is via the MSG_ORIG() macro, and thus no message
# translation is required.

@ MSG_STR_OPTIONS	"s:"
@ MSG_STR_BASIC		"basic"
@ MSG_STR_DETAIL	"detail"
@ MSG_STR_SUMMARY	"summary"
@ MSG_STR_EMPTY		""
@ MSG_FMT_FILE		"%s: "
@ MSG_SUNW_OST_SGS	"SUNW_OST_SGS"
