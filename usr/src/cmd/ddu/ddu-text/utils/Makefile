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
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#

include ../../Makefile.com

PYSRCS = \
	__init__.py \
	action.py \
	base_screen.py \
	color_theme.py \
	ddu-text.py \
	device_scan.py \
	error_window.py \
	inner_window.py \
	input_window.py \
	main_window.py \
	media_scan.py \
	media_window.py \
	screen_list.py \
	toplist_window.py \
	window_area.py
PYOBJS =	$(PYSRCS:%.py=%.pyc)

PYFILES = $(PYSRCS) $(PYOBJS)
ROOTUSRBINDDUTEXT = $(ROOTUSRBIN)/ddu-text
CLEANFILES= $(PYOBJS)

all: $(PYOBJS)

install: all .WAIT $(ROOTUSRBINDDUTEXT)

$(ROOTUSRBINDDUTEXT): $(ROOTDDUTEXTUTILSPYFILES)
	$(RM) $@; $(SYMLINK) ../ddu/ddu-text/utils/ddu-text.py $@

clean:
	$(RM) $(CLEANFILES)

include ../../Makefile.targ
