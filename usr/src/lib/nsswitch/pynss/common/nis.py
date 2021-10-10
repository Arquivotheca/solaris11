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

"""NIS classes used by nscfg."""

from __future__ import print_function
from .nssbase import Nssbase
import os, pwd, grp

#
# NIS Domain class shared by all NIS components.
#

class NisDomain(Nssbase):
    """Network Information Service (NIS) class. Represents the configuration
       of a NIS Domain.  See defaultdomain(4) for details."""

    SERVICE = 'svc:/network/nis/domain'
    LEGACY = ''
    LEGACYDIR = ''
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    DOC = "# See ypfiles(4) and defaultdomain(4) for details.\n"

    # Supported Property groups
    ALLPGS = [  'config', ]
    ALLPROPS = { 'domainname': True, 'ypservers': False, 'securenets': False }
    BACKEND = ''			# Set to backend name if nss_backend

    # domainname specifics
    LEGACY_ETCDIR = '/etc'
    LEGACY_YPDIR = '/var/yp'
    LEGACY_YPBINDDIR = '/var/yp/binding'
    LEGACYDEFDOM = 'defaultdomain'
    LEGACYYPSERVERS = 'ypservers'
    LEGACYSECURENETS = 'securenets'
    DOMNAME = 'domainname'
    DOMTYPE = 'hostname'
    YPSERVERS = 'ypservers'
    YPHOSTTYPE = 'host'
    SECURENETS = 'securenets'
    YPASTRING = 'astring'

    UNCPROP = ( 'domainname', 'hostname' , '' )
    VALUE_AUTH = 'solaris.smf.value.name-service.nis.domain'

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Export from SMF.  Re-generate the following configuration file:
	       domainname, ypservers, securenets
	   Non-existent or empty domain name means no domain."""
	self.print_msg('exporting NIS DOMAIN legacy...')
	# Get domainname property
	domain = self.get_prop_val(prop=self.DOMNAME)
	if domain == None or domain == '':
	    self.legacy_dir(self.LEGACY_ETCDIR)
	    self.legacy_file(self.LEGACYDEFDOM)
	    try:
		os.unlink(self.legacy_path())	# delete old and exit
	    except:
		return self.NOCHANGE		# Already deleted
	    return self.SUCCESS
	# Get ypservers data
	ypservers = None
	yps = self.get_prop_val_list(prop=self.YPSERVERS)
	if type(yps) == type(()) and len(yps) > 0:
	    ypservers = '\n'.join(yps)
	    ypservers += '\n'
	# securenets single line per smf propval
	securenets = None
	snet = self.get_prop_val_list(prop=self.SECURENETS)
	if type(snet) == type(()) and len(snet) > 0:
	    securenets = ''
	    for sarr in snet:
		if type(sarr) != type('') or len(sarr.split()) != 2:
		    continue			# wrong number of args
		securenets += sarr		# sarr is a 'netmask network'
		securenets += '\n'		# per line
	    if securenets == '':
		securenets = None

	# Create domain directory
	if not os.path.isdir(self.LEGACY_YPDIR):
	    return self.FAIL		# No top level yp dir
	if not os.path.isdir(self.LEGACY_YPBINDDIR):
	    return self.FAIL		# No Binding dir
	domdir = os.path.join(self.LEGACY_YPBINDDIR, domain)
	if not os.path.isdir(domdir):	# No domain dir
	    try:
		uid = pwd.getpwnam('root').pw_uid
		gid = grp.getgrnam('root').gr_gid
		os.mkdir(domdir, 0755)
		os.chown(domdir, uid, gid)
	    except:
		return self.FAIL

	ddret = self.NOCHANGE
	ysret = self.NOCHANGE
	snret = self.NOCHANGE

	# Write defaultdomain
	self.print_msg('exporting defaultdomain.')
	self.legacy_dir(self.LEGACY_ETCDIR)
	self.legacy_file(self.LEGACYDEFDOM)
	data = domain + '\n'
	# Save to temp file, no CDDL header
        if not self.save_to_tmp(False, data):
	    return self.FAIL
	ddret = self.tmp_to_legacy()
	if ddret == self.FAIL:
	    self.print_msg('fail move to legacy...')
	    self.unlink_tmp()
	    return self.FAIL

	# Write ypservers if it exists
	if ypservers != None:
	    self.print_msg('exporting ypservers.')
	    self.legacy_dir(domdir)
	    self.legacy_file(self.LEGACYYPSERVERS)
	    # Save to temp file, CDDL header is legal
            if not self.save_to_tmp(True, ypservers):
		return self.FAIL
	    ysret = self.tmp_to_legacy()
	    if ysret == self.FAIL:
		self.print_msg('fail move to legacy...')
		self.unlink_tmp()
		return self.FAIL

	# write securenets if it exists
	if securenets != None:
	    self.print_msg('exporting securenets.')
	    self.legacy_dir(self.LEGACY_YPDIR)
	    self.legacy_file(self.LEGACYSECURENETS)
	    # Save to temp file, CDDL header is legal
            if not self.save_to_tmp(True, securenets):
		return self.FAIL
	    snret = self.tmp_to_legacy()
	    if snret == self.FAIL:
		self.print_msg('fail move to legacy...')
		self.unlink_tmp()
		return self.FAIL
	if ddret == self.NOCHANGE and ysret == self.NOCHANGE and \
		snret == self.NOCHANGE:
	    return self.NOCHANGE
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Import to SMF.  Configure SMF using /var/yp configuration.
	   If the domainname files does not exists, there is no domain."""
	self.print_msg('importing legacy defaultdomain...')
	# Check for domainname first
	self.legacy_dir(self.LEGACY_ETCDIR)
	self.legacy_file(self.LEGACYDEFDOM)
	legacy = self.legacy_path()
	err = False

	# First check for nis/domain un-configuration
	if not os.path.exists(legacy):		# No domainname , no import
	    return self.NOCONFIG

	# Import default domain
	lines = self.load_legacy()
	if lines == None or len(lines) != 1:
	    return self.NOCONFIG
	domain = lines[0]
	self.print_msg('    processed domain: ', domain)

	yserv = []
	snet = []
	# LOAD ypservers if it exists
	    # Check for top level yp dir
	    # Binding dir and domaindir
	if os.path.isdir(self.LEGACY_YPDIR) and \
	   os.path.isdir(self.LEGACY_YPBINDDIR):
	    domdir = os.path.join(self.LEGACY_YPBINDDIR, domain)
	    if os.path.isdir(domdir):	# domain dir
		self.legacy_dir(domdir)
		self.legacy_file(self.LEGACYYPSERVERS)
		self.print_msg('importing legacy ypservers...')
		lines = self.load_legacy()
		if lines == None:
		    lines = []
		for s in lines:
		    if len(s) > 0:
			if s.startswith('#'):	# Skip comments
			    continue
			if not self.typecheck(self.YPHOSTTYPE, s):
			    emsg = 'Illegal value (%s): %s' % \
				 (self.YPHOSTTYPE, s)
			    self.err_msg(emsg)
			    err = True
			    continue
			yserv.append(s)
	if err:
	    return self.FAIL
	self.print_msg('    processed ypservers yserv: ', yserv)

	# LOAD securenets if it exists
	self.legacy_dir(self.LEGACY_YPDIR)
	self.legacy_file(self.LEGACYSECURENETS)
	self.print_msg('importing legacy securenets...')
	lines = self.load_legacy()
	if lines == None:
	    lines = []
	self.traceit('  legacy securenets...', lines)
	for s in lines:
	    if len(s) > 0:
		if s.startswith('#'):		# Skip comments
		    continue
		if len(s.split()) != 2:		# must have 2 fields
		    continue
		snet.append(s)
	self.print_msg('    processed securenets snet: ', snet)

	# Exit before write processing
	if self.no_write:
	    return self.SUCCESS

	# delete old property group
	self.print_msg('    delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	# load property group
	self.print_msg('    loading pg...')
	# Add domainname
	self.traceit('      adding: domainname ', domain)
	ret = self.add_prop_val(self.DEFPG, self.DOMNAME, self.DOMTYPE, domain)
	if not ret:
	    return self.FAIL
	if len(yserv) > 0:
	    self.traceit('      adding: ypservers ', yserv)
	    if not self.add_prop_val(self.DEFPG, \
		    self.YPSERVERS, self.YPHOSTTYPE, yserv):
		self.traceit('      ERR adding ypservers: ', yserv)
		return self.FAIL
	if len(snet) > 0:
	    self.traceit('      adding: securenets ', snet)
	    if not self.add_prop_val(self.DEFPG, \
		    self.SECURENETS, self.YPASTRING, snet):
		self.traceit('      ERR adding securenets: ', snet)
		return self.FAIL
	# Commit the property group
	self.print_msg('    committing pg...')
	if not self.commit(self.DEFPG):
	    return self.FAIL
	# validate
	self.print_msg('    validating pg...')
	if not self.validate():
	    return self.FAIL
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Reset DEFPG."""
	self.print_msg('unconfiguring NIS DOMAIN...')
	self.print_msg('Delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  True/False"""
	pgs = self.get_pgs()
	if pgs != None:
	    if self.DEFPG in pgs:		# default PG must exist
	        domain = self.get_prop_val(prop=self.DOMNAME)
	        if domain != None and domain != '':
		    return True			# and domain must be set
	return False

    #
    # This Function is a bit different for nis/domain, because
    # domainname is a single line with no comments.  So check to see
    # if there is a domainname and it matches the property before
    # determining if nis/domain is configured or not.
    #
    def is_configured(self):
	"""Is this SMF service configured from SMF data?"""
	if self.is_enabled():			# Service is already 'online'
	    return True

	pgs = self.get_pgs()
	if pgs == None:
		    return False
	if not self.DEFPG in pgs:		# default PG must exist
	    return False

	# Must have a domainname
	domain = self.get_prop_val(prop=self.DOMNAME)
	if domain == None:
	    return False

	dom_is_conf = False
	has_dom = False
	srv_is_conf = False
	sec_is_conf = False

	# First check domainname
	self.legacy_dir(self.LEGACY_ETCDIR)
	self.legacy_file(self.LEGACYDEFDOM)
	if domain == '':
	   if not self.legacy_exists():		# [1] no domain / no legacy file
		dom_is_conf = True
	else:
	   if not self.legacy_exists():		# [2] domain / no legacy file
		dom_is_conf = True
	   else:
		lines = self.load_legacy()	# domain / legacy file
		if type(lines) == type([]) and len(lines) > 0:
		    if lines[0] == domain:
			dom_is_conf = True	# [3] domain / legacy file match
			has_dom = True

	# Unless there is a configured domain, there cannot be anything else
	if not dom_is_conf:			# [4] unconfigured
	    return False

	# if property is configured but not generated that is ok.
	if not has_dom:				# [1] or [2]
	    return True

	# Check for other generated configuration files
	if not os.path.isdir(self.LEGACY_YPDIR):
	    return False

	if os.path.isdir(self.LEGACY_YPDIR) and \
	   os.path.isdir(self.LEGACY_YPBINDDIR):
	    domdir = os.path.join(self.LEGACY_YPBINDDIR, domain)
	    if not os.path.isdir(domdir):	# domain dir must also exist
		return False
	else:
	    return False			# Missing ypbind tree

	# Directory structure is ok.
	# Next check ypservers
	self.legacy_dir(domdir)
	self.legacy_file(self.LEGACYYPSERVERS)
	if self.legacy_exists()and not self.is_autogenerated():
	    return False

	# Finally check securenets
	self.legacy_dir(self.LEGACY_YPDIR)
	self.legacy_file(self.LEGACYSECURENETS)
	if self.legacy_exists()and not self.is_autogenerated():
	    return False

	# Domain name, dirs and optional files are configured
	return True

#
# NIS Client class - nsswitch client side component
#

class NisClient(Nssbase):
    """Network Information Service (NIS) class. Represents the configuration
       of the NIS client.  See ypinit(1m) for details."""

    SERVICE = 'svc:/network/nis/client'
    LEGACY = ''
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    # Supported Property groups
    ALLPGS = [  'config', ]
    ALLPROPS = {
	'use_broadcast': False, 'use_ypsetme': False,
	}
    BACKEND = 'nss_nis'			# Set to backend name if nss_backend
    VALUE_AUTH = 'solaris.smf.value.name-service.nis.client'

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Do nothing for nis/client.  No properties to auto export."""
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Do nothing for nis/client.  No properties to auto import."""
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Reset DEFPG."""
	self.print_msg('unconfiguring NIS client...')
	self.print_msg('Delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  True/False"""
	pgs = self.get_pgs()
	if pgs != None:
	    if self.DEFPG in pgs:		# default PG exists
		return True
	return False

    def is_configured(self):
	"""Is this SMF service configured from SMF data?"""
	if self.is_enabled():			# Service is already 'online'
	    return True
	if self.is_populated():			# Service must be populated
	    return True
	return False


#
# NIS Server class - For ypserv
#

class NisServer(Nssbase):
    """Network Information Service (NIS) class. Represents the configuration
       of the NIS server.  See ypserv(1m) for details."""

    SERVICE = 'svc:/network/nis/server'
    LEGACY = ''
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    # Supported Property groups
    ALLPGS = [  'config', ]
    ALLPROPS = {
	'service_dns': False,
	}
    BACKEND = 'nss_dns'			# Set to backend name if nss_backend
    VALUE_AUTH = 'solaris.smf.value.name-service.nis.server'

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Do nothing for nis/server.  No properties to auto export."""
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Do nothing for nis/server.  No properties to auto import."""
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Reset DEFPG."""
	self.print_msg('unconfiguring NIS SERVER...')
	self.print_msg('Delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  True/False"""
	pgs = self.get_pgs()
	if pgs != None:
	    if self.DEFPG in pgs:		# default PG exists
		return True
	return False

    def is_configured(self):
	"""Is this SMF service configured from SMF data?"""
	if self.is_enabled():			# Service is already 'online'
	    return True
	if self.is_populated():			# Service must be populated
	    return True
	return False


#
# NIS Xfr class - For ypxfrd
#

class NisXfr(Nssbase):
    """Network Information Service (NIS) class. Represents the configuration
       of the NIS transfer daemon (ypxfrd).  See ypxfrd(1m) for details."""

    SERVICE = 'svc:/network/nis/xfr'
    LEGACY = ''
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    # Supported Property groups
    ALLPGS = [  'config', ]
    ALLPROPS = {
	}
    BACKEND = ''			# Set to backend name if nss_backend

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Do nothing for nis/xfr.  No properties to auto export."""
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Do nothing for nis/xfr.  No properties to auto import."""
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Do nothing.  No configuration files."""
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  Yes. (no SMF config)"""
	return True

    def is_configured(self):
	"""Is this SMF service configured from SMF data?"""
	return True

#
# NIS Passwd class - for rpc.yppasswdd
#

class NisPasswd(Nssbase):
    """Network Information Service (NIS) class. Represents the configuration
       of the NIS Passwd daemon.  See rpc.yppasswdd(1m) for details."""

    SERVICE = 'svc:/network/nis/passwd'
    LEGACY = ''
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    # Supported Property groups
    ALLPGS = [  'config', ]
    ALLPROPS = {
	}
    BACKEND = ''			# Set to backend name if nss_backend

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Do nothing for nis/passwd.  No properties to auto export."""
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Do nothing for nis/passwd.  No properties to auto import."""
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Do nothing.  No configuration files."""
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  Yes. (no SMF config)"""
	return True

    def is_configured(self):
	"""Is this SMF service configured from SMF data?"""
	return True

#
# NIS Update class - for rpc.ypupdated
#

class NisUpdate(Nssbase):
    """Network Information Service (NIS) class. Represents the configuration
       of the NIS Update daemon.  See rpc.ypupdated(1m) for details."""

    SERVICE = 'svc:/network/nis/update'
    LEGACY = ''
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    # Supported Property groups
    ALLPGS = [  'config', ]
    ALLPROPS = {
	}
    BACKEND = ''			# Set to backend name if nss_backend

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Do nothing for nis/update.  No properties to auto export."""
	self.print_msg('successful export.')
	return self.SUCCESS

    def import_to_smf(self):
	"""Do nothing for nis/update.  No properties to auto import."""
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Do nothing.  No configuration files."""
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    def is_populated(self):
	"""Is SMF populated with this service?  Yes. (no SMF config)"""
	return True

    def is_configured(self):
	"""Is this SMF service configured from SMF data?"""
	return True
