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
# Wrap a command-line check tool in a pythonic API
#

import subprocess
import tempfile

def processcheck(command, args, inpt, output):
	'''Run a checking command, command, with arguments as args.
	Input is provided by inpt (an iterable), error output is
	written to output (a stream-like entity).

	Return a tuple (error, handle), where handle is a file handle
	(you must close it), containing output from the command.'''

	#
	# We use a tempfile for output, rather than a pipe, so we
	# don't deadlock with the child if both pipes fill.
	#
	try:
		tmpfile = tempfile.TemporaryFile(prefix=command)
	except EnvironmentError, e:
		output.write("Could not create temporary file: %s\n" % e)
		return (3, None)

	try:
		p = subprocess.Popen([command] + args,
				     stdin=subprocess.PIPE, stdout=tmpfile,
				     stderr=subprocess.STDOUT, close_fds=False)
	except OSError, e:
		output.write("Could not execute %s: %s\n" % (command, e))
		return (3, None)

	for line in inpt:
		p.stdin.write(line)

	p.stdin.close()

	ret = p.wait()
	tmpfile.seek(0)

	return (ret < 0 and 1 or ret, tmpfile)
