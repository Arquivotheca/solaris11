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

"""DNS classes used by nscfg."""

from __future__ import print_function
from .nssbase import Nssbase
import os

class DnsClient(Nssbase):
    """Domain Name Service (DNS) class. Represents the configuration
       of DNS.  See resolv.conf(4) for details."""

    SERVICE = 'svc:/network/dns/client'
    LEGACY = 'resolv.conf'
    LEGACYDIR = '/etc'
    DEFPG = 'config'
    DEFPROP = ''			# No default property
    DOC = "# See resolv.conf(4) for details.\n"

    # Supported Property groups
    ALLPGS = [  'config', ]
    # propname: [ type, multivalued]
    ALLPROPS = { 'nameserver': [ 'net_address', True],
	'domain': [ 'astring', False],
	'search': [ 'astring', True],
	'sortlist': [ 'net_address', True],
	'options': [ 'astring', False],	# XXX really multivalued
	}
    ALLPROPLIST = ( 'domain', 'search', 'options', 'sortlist', 'nameserver', )
    BACKEND = 'nss_dns'
    VALUE_AUTH = 'solaris.smf.value.name-service.dns.client'

    NMLIST = ( '0.0.0.0',
	    '128.0.0.0', '192.0.0.0', '224.0.0.0', '240.0.0.0',
	    '248.0.0.0', '252.0.0.0', '254.0.0.0', '255.0.0.0',
	    '255.128.0.0', '255.192.0.0', '255.224.0.0', '255.240.0.0',
	    '255.248.0.0', '255.252.0.0', '255.254.0.0', '255.255.0.0',
	    '255.255.128.0', '255.255.192.0', '255.255.224.0', '255.255.240.0',
	    '255.255.248.0', '255.255.252.0', '255.255.254.0', '255.255.255.0',
	    '255.255.255.128', '255.255.255.192', '255.255.255.224',
	    '255.255.255.240', '255.255.255.248', '255.255.255.252',
	    '255.255.255.254', '255.255.255.255', )

    def __init__(self):
	Nssbase.__init__(self)

    def na_to_sl(self, addr):
        """convert net_address to sort list format (expand netmask)."""
	self.print_msg('net_address -> sortlist got: ', addr)
        if addr.find('/') == -1:        # no netmask to change
            return addr
        ip, mask = addr.split('/')
	try:
	    im = int(mask)
	except:
	    return None
	if im >=0 and im <= 32:
	    nm = self.NMLIST[im]
	else:
	    return None
        ret = "%s/%s" % (ip, nm)
	self.print_msg('net_address -> sortlist returns: ', ret)
	return ret

    def sl_to_na(self, addr):
        """convert sort list to net_address format (compact netmask)."""
	self.print_msg('sortlist -> net_address got: ', addr)
        if addr.find('/') == -1:        # no netmask to change
            return addr
        ip, mask = addr.split('/')
	idx = 0
	while idx <= 32:
	    if self.NMLIST[idx] == mask:
		break
	    else:
		idx += 1
	if idx > 32:
	    return None
        ret = "%s/%d" % (ip, idx)
	self.print_msg('sortlist -> net_address returns: ', ret)
        return ret

    def export_from_smf(self):
	"""Export from SMF.  Re-generate resolv.conf file"""
	self.print_msg('exporting DNS legacy...')
	# Map SMF properties to DB
	db = {}
	for p in self.ALLPROPS.keys():
	    self.traceit('p= ', p)
	    db[p] = None				# Setup properties
	    if self.ALLPROPS[p][1]:
		pval = self.get_prop_val_list(prop=p)	# Get Tuple
	    else:
		pval = self.get_prop_val(prop=p)	# Get single value
	    self.traceit('  pval[p]= ', pval)
	    if pval != None and pval != '':
		db[p] = pval			# Set Specific
	self.traceit('db= ', db)
	data = ''
	# Generate config props
	for p in self.ALLPROPLIST:		# Generate config props
	    try:
		val = db[p]
		if type(val) == type(()):
		    if p == 'nameserver':	# one nameserver per line
			for v in val:
			    data += "%s\t%s\n" % (p, v)
		    else:			# otherwise combine all values
			vv = ''
			for v in val:
			    if p == 'sortlist': # convert net_addr -> sortlist
				v = self.na_to_sl(v)
				if v == None:
				    continue
			    if vv == '':
				vv = v
			    else:
				vv = vv + ' ' + v
			data += "%s\t%s\n" % (p, vv)
		elif type(val) == type(''):	# One value, one line
		    data += "%s\t%s\n" % (p, val)
	    except:
		pass
	if data == '':				# No resolv.conf
	    try:
		os.unlink(self.legacy_path())
	    except:
		return self.NOCHANGE		# Already deleted.  No change
	    return self.SUCCESS			# Is a valid configuration
	# Save to temp file, no CDDL header
        if not self.save_to_tmp(True, data):
	    return self.FAIL
	ret = self.tmp_to_legacy()
	if ret == self.FAIL:
	    self.print_msg('fail move to legacy...')
	    self.unlink_tmp()
	    return self.FAIL
	self.print_msg('successful export.')
	return ret

    def import_to_smf(self):
	"""Import to SMF.  Configure SMF from resolv.conf file."""
	self.print_msg('importing DNS legacy...')
	have_ns = False
	err = False
	db = []
	lines = self.load_legacy()
	if lines == None:
	    return self.NOCONFIG
	self.traceit('    loaded legacy: ', lines)
	for l in lines:
	    if l == '':				# Skip empty lines
		continue
	    if l[0] == ';' or l[0] == '#':	# Skip comment lines
		continue
	    if l.startswith('nameserver'):
		dc = l.find('#')		# nameserver allows # comments
		if dc >= 0:
		    l = l[:dc]
	    sc = l.find(';')			# Skip end of line ; comments
	    if sc >= 0:
		l = l[:sc]
	    l = l.rstrip()
	    vals = l.split()
	    if len(vals) < 2:
		continue
	    try:
		self.traceit('    trying: ', vals)
		key = vals[0]			# verify key and values
		tarray = self.ALLPROPS[key]
		if key == 'options':		# convert to single valued
		    v = ' '.join(vals[1:])
		    vals = [ key, v ]
		if not tarray[1] and len(vals) > 2:
		    vals = vals[0:2]
		vals = vals[1:]
		for v in vals:
		    if key == 'sortlist': 	# convert sortlist -> net_addr
			self.traceit('    sl2na >: ', v)
			v = self.sl_to_na(v)
			self.traceit('    sl2na <: ', v)
			if v == None:
			    continue
		    if not self.typecheck(tarray[0], v):
			emsg = 'Illegal value (%s): %s' % \
			    (tarray[0], v)
			self.err_msg(emsg)
			err = True
		    db.append((key, tarray[0], v))
	    except:
		continue
	if err:
	    return self.FAIL
	self.traceit('    processed db: ', db)
	# db now holds all key/value pars to send to smf
	# resolv.conf can be empty so NOCONFIG is not an error here
	# Therefore don't: if len(db) == 0: return self.NOCONFIG
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
	# Create the multivalued prop list
	mval = {}
	mvaltype = {}
	self.traceit('    loading mval, mvaltype...', mval, mvaltype)
	for prop in self.ALLPROPS.keys():
	    self.traceit('    loading mval, mvaltype...', mval, mvaltype)
	    if self.ALLPROPS[prop][1]:
		mval[prop] = []
		mvaltype[prop] = self.ALLPROPS[prop][0]
	self.traceit('    loading mval, mvaltype...', mval, mvaltype)
	# Populate the multivalued prop list
	for (prop, ptype, val) in db:
	    if self.ALLPROPS[prop][1] and mvaltype[prop] == ptype:
		mval[prop].append(val)
	        self.traceit('     mval adding: ', prop, ptype, val)
	self.traceit('    populated mval, mvaltype...', mval, mvaltype)
	# load the single valued props
	for (prop, ptype, val) in db:
	    self.traceit('      adding: ', prop, ptype, val)
	    if self.ALLPROPS[prop][1]:
		continue
	    if not self.add_prop_val(self.DEFPG, prop, ptype, val):
		    self.print_msg('      ERR adding: ', prop, ptype, v)
		    return self.FAIL
	for prop in mval.keys():
	    if len(mval[prop]) > 0:
	        self.traceit('      mval adding: ', prop, ptype, mval[prop])
		ptype = mvaltype[prop]
		if not self.add_prop_val(self.DEFPG, prop, ptype, mval[prop]):
		    self.print_msg('      ERR adding: ', prop, ptype, mval[prop])
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
	self.print_msg('unconfiguring DNS...')
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
		return True			# All properties are optional
	return False

