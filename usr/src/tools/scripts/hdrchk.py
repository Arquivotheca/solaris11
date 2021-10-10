#!/usr/bin/python2.6
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

#
# Check header files conform to ON standards.
#

import sys, os, getopt

sys.path.insert(1, os.path.join(os.path.dirname(__file__), "..", "lib",
                                "python%d.%d" % sys.version_info[:2]))

# Allow running from the source tree, using the modules in the source tree
sys.path.insert(2, os.path.join(os.path.dirname(__file__), '..'))

from onbld.Checks.HdrChk import hdrchk

def usage():
	progname = os.path.split(sys.argv[0])[1]
	msg =  ['Usage: %s [-a] file [file...]\n' % progname,
		'  -a\tApply (more lenient) application header rules\n']
	sys.stderr.writelines(msg)


try:
	opts, args = getopt.getopt(sys.argv[1:], 'a')
except getopt.GetoptError:
	usage()
	sys.exit(2)

lenient = False
for opt, arg in opts:
	if opt == '-a':
		lenient = True

ret = 0
for filename in args:
	try:
		fh = open(filename, 'r')
	except IOError, e:
		sys.stderr.write("failed to open '%s': %s\n" %
				 (e.filename, e.strerror))
	else:
		ret |= hdrchk(fh, lenient=lenient, output=sys.stderr)
		fh.close()
sys.exit(ret)
