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

"""Name Service Switch classes used by nscfg."""

from __future__ import print_function
from .nssbase import Nssbase

class Nsswitch(Nssbase):
    """Name Service Switch class. Represents the configuration
       of the name service switch.  See nsswitch.conf(4) for details."""

    SERVICE = 'svc:/system/name-service/switch'
    LEGACY = 'nsswitch.conf'
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = 'default'
    COPY_YR = '1991'			# Initial Copyright year
    DOC = "# See nsswitch.conf(4) for details.\n"

	# This class only supports the nsswitch_config property group
	# The Nssfiles class supports the files_config property group
    ALLPGS = [ 'config' ]
    ALLPROPS = {'default': 'astring',
	'host': 'astring',
	'password': 'astring',
	'group': 'astring',
	'network': 'astring',
	'protocol': 'astring',
	'rpc': 'astring',
	'ether': 'astring',
	'netmask': 'astring',
	'bootparam': 'astring',
	'publickey': 'astring',
	'netgroup': 'astring',
	'automount': 'astring',
	'alias': 'astring',
	'service': 'astring',
	'printer': 'astring',
	'project': 'astring',
	'auth_attr': 'astring',
	'prof_attr': 'astring',
	'tnrhtp': 'astring',
	'tnrhdb': 'astring',
	'enable_passwd_compat': 'boolean',
	'enable_group_compat': 'boolean', }

    # nsswitch.conf order is not important, but administrators
    # historically tend to expect it in this specific order.

    DBORDER = ('passwd', 'group', 'hosts', 'ipnodes', 'networks',
	     'protocols', 'rpc', 'ethers', 'netmasks', 'bootparams',
	     'publickey', 'netgroup', 'automount', 'aliases',
	     'services', 'printers', 'project', 'auth_attr',
	     'prof_attr', 'tnrhtp', 'tnrhdb',)

    DEFPVAL = 'files'
    DEFPTYPE = 'astring'

    PWD_COMPAT = 'passwd_compat'
    EN_PWD_COMPAT = 'enable_passwd_compat'
    GRP_COMPAT = 'group_compat'
    EN_GRP_COMPAT = 'enable_group_compat'

    UNCPROP = (
	    ( 'default', 'astring', 'files' ),
	    ( 'printer', 'astring', 'user files' ),
	)

    VALUE_AUTH = 'solaris.smf.value.name-service.switch'

    def __init__(self):
	Nssbase.__init__(self)

    def export_from_smf(self):
	"""Export from SMF.  Re-generate nsswitch.conf file"""
	self.print_msg('exporting nsswitch.conf legacy...')
	# Map SMF properties to DB
	db = {}
	pwd_config = False
	pval = self.get_prop_val(prop='enable_passwd_compat')
	if type(pval) == type('') and pval == 'true':
	    pwd_config = True
	grp_config = False
	pval = self.get_prop_val(prop='enable_group_compat')
	if type(pval) == type('') and pval == 'true':
	    grp_config = True
	dval = self.get_prop_val()		# Get default pg/prop
	if dval == None or dval == '':
	    dval = 'files'			# fallback to files only
	self.traceit('dval ', dval)
	for k in self.DBMAP.keys():
	    self.traceit('k= ', k)
	    db[k] = dval			# Force default
	    pval = self.get_prop_val(prop=self.DBMAP[k])
	    self.traceit('  pval[k]= ', pval)
	    if pval != None and pval != '':
		db[k] = pval			# Set Specific
	    self.traceit('db[k]= ', db[k])
	data = ''
	for k in self.DBORDER:			# Could use db.keys()
	    if k == 'passwd' and pwd_config:
		data += "%s:\t%s\n" % (k, 'compat')
		data += "%s:\t%s\n" % (self.PWD_COMPAT, db[k])
	    elif k == 'group' and grp_config:
		data += "%s:\t%s\n" % (k, 'compat')
		data += "%s:\t%s\n" % (self.GRP_COMPAT, db[k])
	    else:
		data += "%s:\t%s\n" % (k, db[k])
	self.traceit('data= ', data)
	# Save to temp file, with CDDL header
        if not self.save_to_tmp(True, data):
	    self.err_msg('FAIL save_to_tmp')
	    return self.FAIL
	ret = self.tmp_to_legacy()
	if ret == self.FAIL:
	    self.print_msg('fail move to legacy...')
	    self.err_msg('FAIL tmp_to_legacy')
	    self.unlink_tmp()
	    return self.FAIL
	self.print_msg('successful export.')
	return ret

    def import_to_smf(self):
	"""Import to SMF.  Configure SMF from nsswitch.conf file.
	   The default props are always reset."""
	self.print_msg('importing legacy nsswitch.conf...')
	db = []
	pwd_config = False
	pwd_val = None
	grp_config = False
	grp_val = None
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
	    if l == '':				# Skip enpty lines
		continue
	    self.traceit('    PROCESS LINE: ', l)
	    vals = l.split(None, 1)
	    if len(vals) != 2:
		continue
	    try:
		self.traceit('    trying: ', vals)
		key = vals[0]			# verify key and values
		if not key.endswith(':'):
		    continue			# Not a valid DB key
		key = key[:-1]
		if key == 'ipnodes':
		    continue			# use 'hosts' not 'ipnodes'
		if key == 'passwd' and vals[1] == 'compat':
		    pwd_config = True
		    db.append((self.EN_PWD_COMPAT, \
			      self.ALLPROPS[self.EN_PWD_COMPAT], 'true'))
		    continue
		if key == 'group' and vals[1] == 'compat':
		    grp_config = True
		    db.append((self.EN_GRP_COMPAT, \
			      self.ALLPROPS[self.EN_GRP_COMPAT], 'true'))
		    continue
		if key == self.PWD_COMPAT:
		    if pwd_config == False:
			continue		# ignore (actually error)
		    map = self.DBMAP['passwd']
		elif key == self.GRP_COMPAT:
		    if grp_config == False:
			continue		# ignore (actually error)
		    map = self.DBMAP['group']
		else:
		    map = self.DBMAP[key]
		if map == 'password':
		    pwd_val = vals[1]
		if map == 'group':
		    grp_val = vals[1]
		db.append((map, self.ALLPROPS[map], vals[1]))
	    except:
		continue
	self.traceit('    processed db: ', db)
	# Backwards compatibility checks
	# Check and adjust for passwd: compat w/ no passwd_compat line.
	if pwd_config and pwd_val == None:
	    map = 'password'
	    db.append((map, self.ALLPROPS[map], "nis"))
	if grp_config and grp_val == None:
	    map = 'group'
	    db.append((map, self.ALLPROPS[map], "nis"))
	# db now holds all key/value pairs to send to smf
	# Exit before write processing
	if self.no_write:
	    return self.SUCCESS
	self.print_msg('    delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	self.print_msg('    loading pg...')
	err = False
	for (prop, ptype, val) in db:
	    self.traceit('      adding: ', prop, ptype, val)
	    if val == self.DEFPVAL:
		continue			# Skip over default 'files'
	    if not self.add_prop_val(self.DEFPG, prop, ptype, val):
		self.traceit('      ERR adding: ', prop, ptype, val)
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
	self.print_msg('unconfiguring nsswitch...')
	self.print_msg('Delete customizations.')
	if not self.delcust_pg(self.DEFPG):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

