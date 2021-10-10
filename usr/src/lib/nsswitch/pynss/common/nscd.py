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

"""Name Service Cache Daemon (nscd) classes used by nscfg."""

from __future__ import print_function
from .nssbase import Nssbase

class Nscd(Nssbase):
    """Name Service Cache Daemon class. Represents the configuration
       of the name service cache.  See nscd.conf(4) for details."""

    SERVICE = 'svc:/system/name-service/cache'
    LEGACY = 'nscd.conf'
    LEGACYDIR = '/etc'
    DEFPG = 'default'
    DEFPROP = ''			# No default property
    COPY_YR = '1994'			# Initial Copyright year
    DOC = "# See nscd(1m) and nscd.conf(4) for details.\n"

    # Supported Property groups
    ALLPGS = [  'config', 'default', 'host', 'password', 'group',
		'network', 'protocol', 'rpc', 'ether', 'netmask',
 		'bootparam', 'netgroup', 'service',
		'printer', 'project', 'auth_attr', 'exec_attr',
		'prof_attr', 'user_attr', 'tnrhtp', 'tnrhdb',
		]

    EXPORTS = [  'config', 'hosts', 'passwd', 'group',
		'networks', 'protocols', 'rpc', 'ethers', 'netmasks',
 		'bootparams', 'netgroup', 'services',
		'printers', 'project', 'auth_attr', 'exec_attr',
		'prof_attr', 'user_attr', 'tnrhtp', 'tnrhdb',
		]

    ALLPROPS = { }			# For Nscd, populate this at init

    configPG = 'config'
    configPROPS = {
		# 'enable_per_user_lookup': [ 'boolean', 'true' ],
		# 'per_user_nscd_time_to_live': [ 'integer', '120' ],
		'logfile': [ 'astring', '' ],
		'debug_level': [ 'astring', '' ],
		'debug_components': [ 'astring', '' ],
		}

    cachePROPS = {
		'enable_cache': [ 'boolean', 'true' ],
		'positive_time_to_live': [ 'integer', '3600' ],
		'negative_time_to_live': [ 'integer', '5' ],
		'keep_hot_count': [ 'integer', '20' ],
		'check_files': [ 'boolean', 'true' ],
		'check_file_interval': [ 'integer', '0' ],
		'maximum_entries_allowed': [ 'integer', '' ],
		}

    # debug component/level string to integer conversions
    debug_comp = {
		'cache': 0x0001,
		'switch': 0x0002,
		'frontend': 0x0004,
		'self_cred': 0x0008,
		'admin': 0x0010,
		'config': 0x0020,
		'smf_monitor': 0x0040,
		'nsw_state': 0x0080,
		'getent': 0x0100,
		'access': 0x0200,
		'int_addr': 0x0400,
		'all': 0x07ff,
		}

    debug_level = {
		'cant_find': 0x0001,
		'debug': 0x0100,
		'error': 0x0200,
		'warning': 0x0400,
		'info': 0x0800,
		'notice': 0x1000,
		'alert': 0x2000,
		'crit': 0x4000,
		'all': 0x7fff,
		}

    VALUE_AUTH = 'solaris.smf.value.name-service.cache'

    def __init__(self):
	Nssbase.__init__(self)
	if len(self.ALLPROPS) == 0:		# One time class init
	    for p in self.configPROPS:		# Generate config props
		prop = self.join(self.configPG, p)
		self.ALLPROPS[prop] = self.configPROPS[p]
	    for pg in self.ALLPGS:		# Generate cache DB props
		if pg == self.configPG:
		    continue
		for p in self.cachePROPS:	# Generate config props
		    prop = self.join(pg, p)
		    self.ALLPROPS[prop] = self.cachePROPS[p]

    def yesno(self, val):
	"""Convert True/False to yes/no expected in nscd.conf."""
	ret = 'no'
	if type(val) == type(True):
	    if val == True:
		ret = 'yes'
	elif type(val) == type(''):
	    r = val.lower()
	    if r == 'yes' or r == 'true':
		ret = 'yes'
	return ret

    def truefalse(self, val):
	"""Convert yes/no to true/false expected by the SMF boolean type."""
	ret = 'false'
	if type(val) == type(True):
	    if val == True:
		ret = 'true'
	elif type(val) == type(''):
	    r = val.lower()
	    if r == 'yes' or r == 'true':
		ret = 'true'
	return ret

    def import_component(self, strvalue):
	"""Converts a debug_component integer to a SMF value list."""
	try:
	    input = int(strvalue)
	except:
	    self.err_msg('Non integer component value found in legacy file')
	    return None
	if input == self.debug_comp['all']:
	    return 'all'
	list = []
	for i in self.debug_comp.keys():
	    if i == 'all':
		continue
	    if input & self.debug_comp[i]:
		list.append(i)
	if len(list) == 0:
	    self.err_msg('No component values found in legacy file')
	    return None
	return list

    def export_component(self):
	"""Converts a debug_component SMF value list to an integer."""
	list = self.get_prop_val_list(prop='config/debug_components')
	self.traceit('export debug_components= ', list)
	if list == None or len(list) == 0:
	    return None
	if len(list) == 1 and list[0] == 'all':
	    return str(self.debug_comp['all'])
	dbint=0
	for i in list:
	    try:
		value = self.debug_comp[i]
		dbint |= value
	    except:
		pass	# ignore bad values
	if dbint == 0:
	    return None
	return str(dbint)

    def import_level(self, strvalue):
	"""Converts a debug_level integer to a SMF value list."""
	try:
	    input = int(strvalue)
	except:
	    self.err_msg('Non integer value debug value found in legacy file')
	    return None
	if input == self.debug_level['all']:
	    return 'all'
	list = []
	for i in self.debug_level.keys():
	    if i == 'all':
		continue
	    if input & self.debug_level[i]:
		list.append(i)
	if len(list) == 0:
	    self.err_msg('Non debug values found in legacy file')
	    return None
	return list

    def export_level(self):
	"""Converts a debug_level SMF value list to an integer."""
	list = self.get_prop_val_list(prop='config/debug_level')
	self.traceit('export debug_level= ', list)
	if list == None or len(list) == 0:
	    return None
	if len(list) == 1:
	    val = list[0]
	    if val== 'all':
		return str(self.debug_level['all'])
	    try:
		i = int(val)	# except if an illegal numeric
		if i == 0:
		    return None
		if i > 0 and i <= 10:
		    return val
		return None
	    except:
		pass
	dbint=0
	for i in list:
	    try:
		value = self.debug_level[i]
		dbint |= value
	    except:
		pass	# ignore bad values
	if dbint == 0:
	    return None
	return str(dbint)

    def export_from_smf(self):
	"""Export from SMF.  Re-generate nsswitch.conf file"""
	self.print_msg('exporting nscd.conf legacy...')
	# Map SMF properties to DB
	db = {}
	for p in self.ALLPROPS.keys():
	    self.traceit('export prop: ', p)
	    db[p] = self.ALLPROPS[p][1]		# Force default
	    if p == 'config/debug_level':
		pval = self.export_level()
	    elif p == 'config/debug_components':
		pval = self.export_component()
	    else:
		pval = self.get_prop_val(prop=p)
	    self.traceit('  pval[p]= ', pval)
	    if pval != None and type(pval) == type('') and pval != '':
		db[p] = pval			# Set Specific
	self.traceit('db= ', db)
	data = ''
	# Generate config props
	for tag in self.configPROPS:		# Generate config props
	    prop = self.join(self.configPG, tag)
	    try:
		val = db[prop]
	    except:
		val = None
	    if val == None or val == '':
		continue
	    if self.configPROPS[tag][0] == 'boolean':
		val = self.yesno(val)
	    data += "%s\t%s\n" % (tag, val)
	data += '\n'
	# Generate cache props
	self.traceit('CACHE PROPS')
	for map in self.EXPORTS:		# Generate cache DB props
	    if map in self.DBMAP.keys():
		pg = self.DBMAP[map]
	    else:
		pg = map
	    self.traceit('    pg= ', pg)
	    for tag in self.cachePROPS:		# Generate config props
		if map == 'netgroup' and \
		   (tag == 'check_files' or tag == 'check_file_interval'):
		    continue
	        self.traceit('    tag= ', tag)
		prop = self.join(pg, tag)
	        self.traceit('    prop= ', prop)
		try:
		    val = db[prop]
		except:
		    val = None
		if val == None or val == '':
		    continue
		if self.cachePROPS[tag][0] == 'boolean':
		    val = self.yesno(val)
	        self.traceit('    val= ', val)
		data += "%s\t%s\t%s\n" % (tag, map, val)
	    data += '\n'
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
	"""Import to SMF.  Configure SMF from nscd.conf file.
	   This function falls back to hardwired default values."""
	self.print_msg('importing legacy nscd.conf...')
	nprop = []				# new props
	npg = {}				# new pgs
	err = False
	lines = self.load_legacy()
	if lines == None:			# defaults always exist
	    lines = []
	self.traceit('    loaded legacy: ', lines)
	for l in lines:
	    while l.startswith('\t') or l.startswith(' '):
		l = l[1:]			# Remove leading whitespace
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
	    vals = l.split(None)
	    vlen = len(vals)
	    if vlen != 2 and vlen != 3:		# only config or cache props
		continue
	    pname = vals[0].replace('-','_')	# If old style format
	    skipcheck = False
	    if vlen == 2:			# Config
		pval = vals[1]
		map = self.configPG		# Goes to the config PG
		self.traceit('       pg, prop: ', map, pval)
		try:
		    defval = self.configPROPS[pname]
		    if pname == 'debug_level':
			try:
			    i = int(pval)	# except if an illegal numeric
			    if i == 0:
				continue	# skip it
			    if i > 0 and i <= 10:
				pass
			    else:
				pval = self.import_level(pval)
				if pval == None:
				    err = True
				    continue
			    skipcheck = True
			except:
			    continue		# other side skip
		    elif pname == 'debug_components':
			pval = self.import_component(pval)
			if pval == None:
			    err = True
			    continue
			skipcheck = True
		except:
		    continue
	    else:				# Cache
		if vals[1] == 'ipnodes':
		    continue			# Ignore ipnodes (dup hosts)
		pval = vals[2]
		try:
		    if vals[1] in self.DBMAP.keys():
			map = self.DBMAP[vals[1]]
		    elif vals[1] in self.ALLPGS:
			map = vals[1]
		    else:
			self.traceit('       Match fail vals: ', vals)
			continue		# Not a valid DB/cache
		    self.traceit('       pg, prop: ', map, pval)
		    defval = self.cachePROPS[pname]
		    if defval[0] == 'boolean':
			pval = self.truefalse(pval)
		    if pval == defval[1]:
			continue		# Ignore default values
		except:
		    self.traceit('       Match fail vals: ', vals)
		    continue
	    if not skipcheck and not self.typecheck(defval[0], pval):
		emsg = 'Illegal value (%s): %s' % (defval[0], pval)
		self.err_msg(emsg)
		err = True
		continue
	    nprop.append((map, pname, defval, pval))
	if err:
	    return self.FAIL
	self.traceit('    processed nprop: ', nprop)

	# Exit before write processing
	if self.no_write:
	    return self.SUCCESS
	self.print_msg('    delete all service customizations.')
	# delete old property groups
	for pg in self.ALLPGS:
	    self.print_msg('    deleting customizations for pg:  ', pg)
	    self.delcust_pg(pg)
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	lastpg = self.DEFPG
	# load property groups
	self.print_msg('    loading modified cache properties...')
	for (pg, prop, defval, val) in nprop:
	    ptype = defval[0]
	    if val == defval[1] or val == '':
		continue			# Skip over default values
	    if ptype == 'boolean':
		val = self.truefalse(val)
	    self.traceit('          adding: ', pg, prop, ptype, val)
	    if pg != self.DEFPG and pg != self.configPG:
		if not pg in npg:
		    self.print_msg('    adding pg:', pg)
		    if not self.addpg(pg, self.DEFPGTYPE):	# add pg
			self.err_list(('Unable to add pg:', pg))
			return self.FAIL
		    npg[pg] = pg
	    if not self.add_prop_val(pg, prop, ptype, val):
		self.err_list(('Unable to add:', prop, ptype, val))
		return self.FAIL
	    self.traceit('          added: ', pg, prop, ptype, val)
	    lastpg = pg
	# Commit the property group
	self.print_msg('    committing pg...')
	if not self.commit(lastpg):
	    return self.FAIL
	# validate
	self.print_msg('    validating pg...')
	if not self.validate():
	    return self.FAIL
	self.print_msg('successful import.')
	return self.SUCCESS

    def unconfig_smf(self):
	"""Unconfigure SMF.  Reset DEFPG."""
	self.print_msg('unconfiguring nscd...')
	self.print_msg('Delete all service customizations.')
	if not self.delcust_pg(''):
	    return self.FAIL
	# Commit the property group
	if not self.commit():
	    return self.FAIL
	self.print_msg('successful unconfigure.')
	return self.SUCCESS

    # Check to see if both default and config PG exist
    # and the config PG contains the required properties.
    def is_populated(self):
	"""Is SMF populated with this service?  True/False"""
	pgs = self.get_pgs()
	if pgs != None:
	    if self.DEFPG in pgs and self.configPG in pgs:
		props = self.get_props(self.DEFPG)
		if props != None:
		    fndprop = 0
		    allprop = 0
		    for p in self.cachePROPS.keys():
			if self.cachePROPS[p][1] == '':
			    continue
			allprop += 1
			tprop = '/'.join((self.DEFPG, p))
			if tprop in props:
			    fndprop += 1
		    if fndprop == allprop:
			return True
	return False

