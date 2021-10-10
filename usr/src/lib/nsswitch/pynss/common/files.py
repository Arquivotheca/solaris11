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

"""Files classes used by nscfg."""

from __future__ import print_function
from .nssbase import Nssbase

class Files(Nssbase):
    """Local files name service class. Represents the configuration
       of local files.  See nsswitch.conf(4) for details."""

    # Currently no service exists for files.
    # If a service were to exist, it would be this one.
    SERVICE = 'svc:/system/name-service/switch'
    LEGACY = None
    LEGACYDIR = None
    DEFPG = 'file_paths'
    DEFPROP = None
    # Supported Property groups
    BACKEND = 'nss_files'

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Export from SMF.  Do nothing.  no legacy configuration files."""
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Import to SMF.  Do nothing.  no legacy configuration files."""
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Do nothing.  no legacy configuration files."""
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  Yes. (no SMF config)"""
	return True
