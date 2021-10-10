#
# 
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

#
# CStyle, wrap the cstyle tool in a pythonic API
#

import sys
from onbld.Checks.ProcessCheck import processcheck

def cstyle(fh, filename=None, output=sys.stderr, **opts):
	opttrans = {'check_continuation': '-c',
		    'heuristic': '-h',
		    'picky': '-p',
		    'verbose': '-v',
		    'ignore_hdr_comment': '-C',
		    'check_posix_types': '-P',
		    'doxygen_comments': '-o doxygen',
		    'splint_comments': '-o splint'}

	for x in filter(lambda x: x not in opttrans, opts):
		raise TypeError('cstyle() got an unexpected keyword '
				'argument %s' % x)

	options = [opttrans[x] for x in opts if opts[x]]

	if not filename:
		filename = fh.name

	ret, tmpfile = processcheck('cstyle', options, fh, output)

	if tmpfile:
		for line in tmpfile:
			line = line.replace('<stdin>', filename)
			output.write(line)

		tmpfile.close()
	return ret
