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

import gettext
import solaris.misc

try:
    _ = gettext.translation('SUNW_OST_OSLIB', '/usr/lib/locale',
                        fallback=True).gettext
except:
    _ = solaris.misc.gettext

__version__ = '0.1'

__all__ = [ '_', 'Nssscf', 'messages', 'Nssbase',
	    'Nsswitch', 'Nscd', 'Files', 'DnsClient', 'LdapClient',
	    'NisDomain', 'NisClient', 'NisServer', 'NisXfr',
	    'NisPasswd', 'NisUpdate', 'create_nss_object',
	  ]

from .nssscf import Nssscf
from .nssbase import Nssbase
from .nsswitch import Nsswitch
from .nscd import Nscd
from .files import Files
from .dns import DnsClient
from .ldap import LdapClient
from .nis import NisDomain
from .nis import NisClient
from .nis import NisServer
from .nis import NisXfr
from .nis import NisPasswd
from .nis import NisUpdate

def create_nss_object(fmri):
    """Return a nss object based on fmri name."""
    if type(fmri) != type(''):
	return None
    if fmri.find('name-service/cache') != -1:
	return Nscd()
    elif fmri.find('name-service/switch') != -1:
	return Nsswitch()
    elif fmri.find('dns/client') != -1:
	return DnsClient()
    elif fmri.find('ldap/client') != -1:
	return LdapClient()
    elif fmri.find('nis/domain') != -1:
	return NisDomain()
    elif fmri.find('nis/client') != -1:
	return NisClient()
    elif fmri.find('nis/server') != -1:
	return NisServer()
    elif fmri.find('nis/passwd') != -1:
	return NisPasswd()
    elif fmri.find('nis/xfr') != -1:
	return NisXfr()
    elif fmri.find('nis/update') != -1:
	return NisUpdate()
    return None
