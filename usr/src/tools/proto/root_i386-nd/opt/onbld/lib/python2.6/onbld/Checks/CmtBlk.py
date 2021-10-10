#! /usr/bin/python

#
# 
#

#
# Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
#

#
# Check that source files contain a valid comment block
#

import re, sys

CmntChrs = r'#*!/\\";. '

class CmtBlkError(Exception):
	def __init__(self, lineno, seen, shouldbe):
		Exception.__init__(self)
		self.lineno = lineno
		self.seen = seen
		self.shouldbe = shouldbe

def checkblock(block, blk_text):
	line = block['start']
	lictxt = block['block']

	for actual, valid in map(lambda x, y: (x and x.lstrip(CmntChrs), y),
			       lictxt, blk_text):
		if actual != valid:
			raise CmtBlkError(line, actual, valid)
		line += 1

def cmtblkchk(fh, blk_name, blk_text, filename=None,
	      lenient=False, verbose=False, output=sys.stderr):

	ret = 0
	blocks = []
	lic = []
	in_cmt = False
	start = 0
	lineno = 0

	StartText = '%s HEADER START' % blk_name
	EndText = '%s HEADER END' % blk_name
	full_text = [StartText, ''] + blk_text + ['', EndText]

	StartRE = re.compile(r'^[%s ]*%s' % (CmntChrs, StartText))
	EndRE = re.compile(r'^[%s ]*%s' % (CmntChrs, EndText))

	if not filename:
		filename = fh.name

	for line in fh:
		line = line.rstrip('\r\n')
		lineno += 1
		
		if StartRE.search(line):
			in_cmt = True
			lic.append(line)
			start = lineno
		elif in_cmt and EndRE.search(line):
			in_cmt = False
			lic.append(line)
			blocks.append({'start':start, 'block':lic})
			start = 0
			lic = []
		elif in_cmt:
			lic.append(line)

	if in_cmt:
		output.write('%s: %s: Error: Incomplete %s block\n''' %
		    (filename, start, blk_name))

	# Check for no comment block, warn if we're not being lenient
	if not len(blocks) and not lenient:
		if not ret:
			ret = 2
		output.write("%s: Warning: No %s block\n" %
			     (filename, blk_name))

	# Check for multiple comment blocks
	if len(blocks) > 1:
		ret = 1
		output.write('%s: Error: Multiple %s blocks\n'
			     '    at lines %s\n''' %
			     (filename, blk_name,
			      ', '.join([str(x['start']) for x in blocks])))

	# Validate each comment block
	for b in blocks:
		try:
			checkblock(b, full_text)
		except CmtBlkError, e:
			ret = 1
			output.write(
				"%s: %d: Error: Invalid line in %s block:\n"
				"    should be\n"
				"    '%s'\n"
				"    is\n"
				"    '%s'\n" % (filename, e.lineno, blk_name,
						e.shouldbe, e.seen))
			break
		
	if verbose and not ret:
		output.write("%s: Valid %s block\n" %
			     (filename, blk_name))

	return ret
