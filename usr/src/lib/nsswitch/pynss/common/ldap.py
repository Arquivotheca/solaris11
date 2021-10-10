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

"""LDAP classes used by nscfg."""

from __future__ import print_function
from .nssbase import Nssbase

class LdapClient(Nssbase):
    """LDAP Name Service class. Represents the configuration
       of LDAP.  See ldapclient(1m) for details."""

    SERVICE = 'svc:/network/ldap/client'
    LEGACY = ''
    LEGACY_CLIENT = 'ldap_client_file'
    LEGACY_CRED = 'ldap_client_cred'
    LEGACYDIR = '/var/ldap'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    DOC = "# See ldapclient(1m) for details.\n"

    # Supported Property groups
    ALLPGS = [  'config', 'cred', ]
		# Type	    LEGACYNAME     Multi-valued(T/F)    MV type
		#  MV Types:   ',' ' ' ';' NL (1 per line)
    ALLPROPS = {
	'version':
		['astring', 'NS_LDAP_FILE_VERSION', False, None ],
	'bind_dn':
		['astring', 'NS_LDAP_BINDDN', False, None ],
	'bind_passwd':
		['astring', 'NS_LDAP_BINDPASSWD', False, None ],
	'enable_shadow_update':
		['boolean', 'NS_LDAP_ENABLE_SHADOW_UPDATE', False, None ],
	'admin_bind_dn':
		['astring', 'NS_LDAP_ADMIN_BINDDN', False, None ],
	'admin_bind_passwd':
		['astring', 'NS_LDAP_ADMIN_BINDPASSWD', False, None ],
	'host_certpath':
		['astring', 'NS_LDAP_HOST_CERTPATH', False, None ],
	'profile':
		['astring', 'NS_LDAP_PROFILE', False, None ],
	'preferred_server_list':
		['host', 'NS_LDAP_SERVER_PREF', True, ',' ],
	'server_list':
		['host', 'NS_LDAP_SERVERS', True, ',' ],
	'search_base':
		['astring', 'NS_LDAP_SEARCH_BASEDN', False, None ],
	'search_scope':
		['astring', 'NS_LDAP_SEARCH_SCOPE', False, None ],
	'authentication_method':
		['astring', 'NS_LDAP_AUTH', True, ';' ],
	'credential_level':
		['astring', 'NS_LDAP_CREDENTIAL_LEVEL', True, ' ' ],
	'service_search_descriptor':
		['astring', 'NS_LDAP_SERVICE_SEARCH_DESC', True, 'NL' ],
	'search_time_limit':
		['integer', 'NS_LDAP_SEARCH_TIME', False, None ],
	'bind_time_limit':
		['integer', 'NS_LDAP_BIND_TIME', False, None ],
	'follow_referrals':
		['boolean', 'NS_LDAP_SEARCH_REF', False, None ],
	'profile_ttl':
		['astring', 'NS_LDAP_CACHETTL', False, None ],
	'attribute_map':
		['astring', 'NS_LDAP_ATTRIBUTEMAP', True, 'NL' ],
	'objectclass_map':
		['astring', 'NS_LDAP_OBJECTCLASSMAP', True, 'NL' ],
	'service_credential_level':
		['astring', 'NS_LDAP_SERVICE_CRED_LEVEL', True, 'NL' ],
	'service_authentication_method':
		['astring', 'NS_LDAP_SERVICE_AUTH_METHOD', True, 'NL' ],
	}
    BACKEND = 'nss_ldap'

    CREDPG = 'cred'
    CREDCONFIG = {
	'bind_dn': True,
	'bind_passwd': True,
	'enable_shadow_update': True,
	'admin_bind_dn': True,
	'admin_bind_passwd': True,
	'host_certpath': True,
	}

    PERM = 0400

    READ_AUTH = 'read_authorization'
    READ_AUTH_TYPE = 'astring'
    READ_AUTH_VALUE = 'solaris.smf.value.name-service.ldap.client'
    VALUE_AUTH = 'solaris.smf.value.name-service.ldap.client'

    UNCPROP = (
	    ( 'version', 'astring', '2.0' ),
	    ( 'search_base', 'astring', '' ),
	    ( 'server_list', 'net_address', '0.0.0.0' ),
	)

    VALID_AUTH = ( 'none', 'simple', 'sasl/CRAM-MD5', 'sasl/DIGEST-MD5',
	'sasl/DIGEST-MD5:auth-int', 'sasl/DIGEST-MD5:auth-conf',
        'sasl/EXTERNAL', 'sasl/GSSAPI', 'tls:none', 'tls:simple',
        'tls:sasl/CRAM-MD5', 'tls:sasl/DIGEST-MD5',
	'tls:sasl/DIGEST-MD5:auth-int', 'tls:sasl/DIGEST-MD5:auth-conf' )
    VALID_CRED = ( 'anonymous', 'proxy', 'self' )
    VALID_SCOPE = ( 'base', 'one', 'sub' )

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Export from SMF.  Re-generate /var/ldap/* files."""
	self.print_msg('exporting LDAP legacy...')
	# Map SMF properties to DB
	db = {}
	for p in self.ALLPROPS.keys():
	    self.traceit(' export trying p= ', p)
	    db[p] = None			# Setup properties
	    if p in self.CREDCONFIG:		# Get available properties
	        self.traceit(' export trying CRED p= ', p)
	        pval = self.get_prop_val(pg=self.CREDPG, prop=p)
		if pval == None:
		    continue
		if self.ALLPROPS[p][0] == 'boolean':
		    pval = pval.upper()
		self.traceit('  cred pval= ???')
	    else:
	        self.traceit(' export trying CONFIG p= ', p)
		if self.ALLPROPS[p][2]:		# Multi-valued tuple
	            pval = self.get_prop_val_list(pg=self.DEFPG, prop=p)
		    if pval == None or len(pval) == 0:
			continue
		else:
	            pval = self.get_prop_val(pg=self.DEFPG, prop=p)
		    if pval == None:
			continue
		    if self.ALLPROPS[p][0] == 'boolean':
			pval = pval.upper()
		self.traceit('  client pval= ', pval)
	    if pval != None and pval != '':
	        db[p] = pval		# Single valued

	## Process ldap_client_file
	data = ''
	try:					# start with the version
		val = db['version']
	except:
	    return self.FAIL
	# Only support newer (V2) configurations
	op = self.ALLPROPS['version'][1]
	data += "%s= %s\n" % (op, val)
	# Note: the client file has both multivalued params on a single line
	# and multivalued params 1 per line, and single valued params
	for p in self.ALLPROPS.keys():		# Generate config props
	    if p == 'version':
		continue
	    if p in self.CREDCONFIG:
		continue
	    try:				# Get a valid property
		val = db[p]
	    except:
		val = None
	    if val == None or val == '' or val == '""':
		continue
	    op = self.ALLPROPS[p][1]
	    opmv = self.ALLPROPS[p][2]
	    opsep = self.ALLPROPS[p][3]
	    if opmv:
		if type(val) != type(()):
		    continue			# should not happen (see above)
		if opsep == 'NL':		# 1 entry per line
		    for v in val:
			if v == '0.0.0.0':
			    continue
			data += "%s= %s\n" % (op, v)
		else:
		    jv = opsep.join(val)	# combine into 1 entry
		    data += "%s= %s\n" % (op, jv)
	    else:
		data += "%s= %s\n" % (op, val)	# Single valued entry
	self.legacy_file(self.LEGACY_CLIENT)
	# Save to temp file, with CDDL header
        if not self.save_to_tmp(True, data):
	    self.print_msg('could not save temp client file...')
	    return self.FAIL
	# Save away last_tmp for later comparison
	client_last_tmp = self.last_tmp

	## Process ldap_client_cred
	data = ''
	for p in self.CREDCONFIG.keys():	# Generate only cred props
	    try:				# Get a valid property
		val = db[p]
	    except:
		val = None
	    if val == None or val == '' or val == '""':
		continue
	    op = self.ALLPROPS[p][1]
	    data += "%s= %s\n" % (op, val)
	self.legacy_file(self.LEGACY_CRED)
	# Save to temp file, with CDDL header
        if not self.save_to_tmp(True, data):
	    self.print_msg('could not save temp cred file...')
	    return self.FAIL
	# Save away last_tmp for later comparison
	cred_last_tmp = self.last_tmp

	## Move temp files
	self.legacy_file(self.LEGACY_CLIENT)
	# Restore last_tmp
	self.last_tmp = client_last_tmp
	clret = self.tmp_to_legacy()
	if clret == self.FAIL:
	    self.print_msg('fail client move to legacy...')
	    self.unlink_tmp()
	    return self.FAIL
	self.legacy_file(self.LEGACY_CRED)
	# Restore last_tmp
	self.last_tmp = cred_last_tmp
	crret = self.tmp_to_legacy()
	if crret == self.FAIL:
	    self.print_msg('fail cred move to legacy...')
	    self.unlink_tmp()
	    return self.FAIL
	if clret == self.NOCHANGE and crret == self.NOCHANGE:
	    return self.NOCHANGE
	self.print_msg('successful export.')
	return self.SUCCESS

    def read_file(self):
	"""Read a /var/ldap/* file into a db array."""
	self.print_msg('importing legacy...', self.LEGACY)
	err = False
	db = []
	lines = self.load_legacy()
	if lines == None:			# defaults always exist
	    lines = []
	self.traceit('    loaded legacy: ', lines)
	for l in lines:
	    if l.startswith('#'):		# Skip comment lines
		continue
	    idx = l.find('#')
	    if idx > 0:
		l = l[:idx]
	    while l.endswith('\t') or l.endswith(' '):
		l = l[:-1]
	    if l == '':				# Skip empty lines
		continue
	    self.traceit('    PROCESS LINE: ', l)
	    vals = l.split(None, 1)
	    if len(vals) != 2:
		continue
	    try:
		self.traceit('    Trying: ', vals)
		key = vals[0]			# verify key and values
		if not key.endswith('='):
		    continue			# Not a valid DB key
		key = key[:-1]
		self.traceit('    KEY: ', key)
		map = None
		mtype = None
		ismulti = False
		msep = None
		for m in self.ALLPROPS.keys():
		    self.traceit('   >> ', self.ALLPROPS[m][1], key)
		    if self.ALLPROPS[m][1] == key:
			map = m
			mtype = self.ALLPROPS[m][0]
			ismulti = self.ALLPROPS[m][2]
			msep = self.ALLPROPS[m][3]
			break
		if map == None:
		    continue
		self.traceit('    MAP: ', map, ismulti, msep)
		if not ismulti or (ismulti and msep == 'NL'):
		    if mtype == 'boolean':
			vals[1] = vals[1].lower()
		    if not self.typecheck(mtype, vals[1]):
			emsg = 'Illegal value (%s): %s' \
				% (mtype, vals[1])
			self.err_msg(emsg)
			err = True
			continue
		    if map == 'search_scope' and \
			    not vals[1] in self.VALID_SCOPE:
			emsg = 'Illegal scope value: %s' % vals[1]
			self.err_msg(emsg)
			err = True
			continue
		    db.append((map, mtype, vals[1]))
		    self.traceit('    ADD: ', (map, mtype, vals[1]))
		else:
		    mvals = vals[1].split(msep)
		    if len(mvals) == 0:
			emsg = 'Illegal value (%s): %s' \
				% (mtype, vals[1])
			self.err_msg(emsg)
			err = True
			continue
		    for mv in mvals:
			if map == 'authentication_method' and \
				not mv in self.VALID_AUTH:
			    emsg = 'Illegal auth value: %s' % mv
			    self.err_msg(emsg)
			    err = True
			    continue
			if map == 'credential_level' and \
				not mv in self.VALID_CRED:
			    emsg = 'Illegal cred value: %s' % mv
			    self.err_msg(emsg)
			    err = True
			    continue
			db.append((map, mtype, mv))
			self.traceit('    ADD: ', (map, mtype, mv))
	    except:
		continue
	return (err, db)

    def import_to_smf(self):
	"""Import to SMF.  Configure SMF from /var/ldap/* files."""
	self.print_msg('Reading legacy... ldap client')
	self.legacy_file(self.LEGACY_CLIENT)
	(err, cldb) = self.read_file()
	if err:
	    return self.FAIL
	self.print_msg('Reading legacy... ldap cred')
	self.legacy_file(self.LEGACY_CRED)
	(err, crdb) = self.read_file()
	if err:
	    return self.FAIL
	self.print_msg('Processing legacy data...')
	self.traceit('    check client db: ', cldb)
	# db now holds all key/value pairs to send to smf
	if len(cldb) == 0:
	    return self.NOCONFIG		# Nothing to load

	# Check for required client elements
	#     must have version, search_base and at least one server
	hasver = False
	hasbase = False
	hasserv = False
	for (prop, ptype, val) in cldb:
	    self.traceit('    prop check >> ', prop, ptype, val)
	    if prop == 'version':
		if val == '2.0':
		    hasver = True
		else:
		    emsg = 'Unsupported version: %s detected' % val
		    self.err_msg(emsg)
		    return self.FAIL
	    elif prop == 'search_base':
		hasbase = True
	    elif prop == 'preferred_server_list' or prop == 'server_list':
		hasserv = True
	    elif prop in self.CREDCONFIG.keys():
		self.err_msg('Illegal property found in client file')
		return self.FAIL
	    elif not prop in self.ALLPROPS.keys():
		self.err_msg('Unknown property found in client file')
		return self.FAIL
	if not hasver or not hasbase or not hasserv:
	    emsg = 'Missing configuration: version, search base or server list'
	    self.err_msg(emsg)
	    return self.FAIL

	# Check for cred elements
	#     Only legal values are in CREDCONFIG
	for (prop, ptype, val) in crdb:
	    try:
		if self.CREDCONFIG[prop]:
		    pass
	    except:
		self.err_msg('Illegal property found in cred file')
		return self.FAIL

	# Exit before write processing
	if self.no_write:
	    return self.SUCCESS

	# Update config property group
	self.print_msg('    delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	if not self.delcust_pg(self.CREDPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL

	# load config property group
	self.print_msg('    loading config pg...')
	err = False
	# Create the multivalued prop list
	mval = {}
	mvaltype = {}
	for prop in self.ALLPROPS.keys():
	    if self.ALLPROPS[prop][2]:
		mval[prop] = []
		mvaltype[prop] = self.ALLPROPS[prop][0]
	# Populate the multivalued prop list
	for (prop, ptype, val) in cldb:
	    if self.ALLPROPS[prop][2] and mvaltype[prop] == ptype:
		mval[prop].append(val)
	# Load the single valued props
	for (prop, ptype, val) in cldb:
	    self.traceit('      adding: ', prop, ptype, val)
	    if ptype == 'boolean':
		val = val.lower()
	    if self.ALLPROPS[prop][2]: 	# Skip over multivalued props
		continue
	    if not self.add_prop_val(self.DEFPG, prop, ptype, val):
		self.traceit('      ERR adding: ', prop, ptype, val)
		return self.FAIL
	# Now add the multivalued properties
	for prop in mval.keys():
	    if len(mval[prop]) > 0:
		ptype = mvaltype[prop]
		if not self.add_prop_val(self.DEFPG, prop, ptype, mval[prop]):
		    self.traceit('      ERR adding: ', prop, ptype, mval[prop])
		    return self.FAIL

	# load cred property group
	self.print_msg('    loading cred pg...')
	err = False
	for (prop, ptype, val) in crdb:
	    self.traceit('      adding: ', prop, ptype, val)
	    if ptype == 'boolean':
		val = val.lower()
	    if not self.add_prop_val(self.CREDPG, prop, ptype, val):
		self.traceit('      ERR adding: ', prop, ptype, val)
		return self.FAIL

	# Commit both property groups
	self.print_msg('    committing pg...')
	if not self.commit(self.CREDPG):
	    return self.FAIL
	# validate
	self.print_msg('    validating pg...')
	if not self.validate():
	    return self.FAIL
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Reset DEFPG."""
	self.print_msg('unconfiguring LDAP...')
	self.print_msg('Delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	if not self.delcust_pg(self.CREDPG):
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
		props = self.get_props(self.DEFPG)
		if props != None:
		    if 'config/version' in props and \
			'config/search_base' in props and \
		        ( 'config/preferred_server_list' in props or \
			  'config/server_list' in props ):
			    pv = self.get_prop_val(self.DEFPG, 'search_base')
			    if pv == None or pv == '':
				return False
			    return True
	return False

    def is_autogenerated(self):
	"""Was the legacy file generated from SMF data?"""
	self.legacy_file(self.LEGACY_CLIENT)
	if self.legacy_exists():		# client file must exist
	    lines = self.load_legacy()
	    if type(lines) == type([]) and len(lines) > 0:
		if self.AUTO_GEN in lines:
		    self.legacy_file(self.LEGACY_CRED)
		    if self.legacy_exists():	# cred file may exist
			lines = self.load_legacy()
			if type(lines) == type([]) and len(lines) > 0:
			    if self.AUTO_GEN in lines:
			        return True	# client and cred file are good
			return False		# cred file not good
		    return True			# client file good, no cred
	return False
