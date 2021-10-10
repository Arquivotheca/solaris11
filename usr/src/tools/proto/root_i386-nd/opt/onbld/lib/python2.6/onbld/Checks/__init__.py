#! /usr/bin/python
#
# 
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# The 'checks' package contains various checks that may be run
#

__all__ = [
	'Cddl',
	'Comments',
	'Copyright',
	'CStyle',
	'HdrChk',
	'JStyle',
	'Keywords',
	'Mapfile',
	'Rti',
	'onSWAN']


import socket

# 
# Generic check to test if a host is on SWAN
# 
def onSWAN():
	try:
		if socket.gethostbyname("sunweb.central.sun.com."):
			return True
		else:
			return False
	except:
		return False
