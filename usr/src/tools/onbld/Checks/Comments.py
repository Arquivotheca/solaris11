#! /usr/bin/python
#
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Check delta comments:
# 	- Have the correct form.
# 	- Have a synopsis matching that of the CR or ARC case.
# 	- Appear only once.
#

import re, sys
from onbld.Checks.DbLookups import BugDB, ARC

arcre = re.compile(r'^([A-Z][A-Z]*ARC[/ \t][12]\d{3}/\d{3}) (.*)$')
bugre = re.compile(r'^(\d{7}) (.*)$')

def isARC(comment):
	return arcre.match(comment)

def isBug(comment):
	return bugre.match(comment)

#
# Translate any acceptable case number format into "<ARC> <YEAR>/<NUM>"
# format.
#
def normalize_arc(caseid):
	return re.sub(r'^([A-Z][A-Z]*ARC)[/ \t]', '\\1 ', caseid)

def comchk(comments, check_db=True, output=sys.stderr, arcPath=None):
	'''Validate checkin comments against ON standards.

	Comments must be a list of one-line comments, with no trailing
	newline.
	
	If check_db is True (the default), validate CR and ARC
	synopses against the databases.

	Error messages intended for the user are written to output,
	which defaults to stderr
	'''
	bugnospcre = re.compile(r'^(\d{7})([^ ].*)')
	ignorere = re.compile(r'^(Portions contributed by |Contributed by |back[ -]?out )')

	errors = { 'bugnospc': [],
		   'mutant': [],
		   'dup': [],
		   'nomatch': [],
		   'nonexistent': [] }
	bugs = {}
	arcs = {}
	ret = 0
	blanks = False

	for com in comments:
		# Our input must be newline-free, comments are line-wise.
		if com.find('\n') != -1:
			raise ValueError("newline in comment '%s'" % com)

		# Ignore valid comments we can't check
		if ignorere.search(com):
			continue

		if not com or com.isspace():
			blanks = True
			continue

		match = bugre.search(com)
		if match:
			if match.group(1) not in bugs:
				bugs[match.group(1)] = []
			bugs[match.group(1)].append(match.group(2))
			continue

		#
		# Bugs missing a space after the ID are still bugs
		# for the purposes of the duplicate ID and synopsis
		# checks.
		#
		match = bugnospcre.search(com)
		if match:
			if match.group(1) not in bugs:
				bugs[match.group(1)] = []
			bugs[match.group(1)].append(match.group(2))
			errors['bugnospc'].append(com)
			continue

		# ARC case
		match = arcre.search(com)
		if match:
			arc, case = re.split('[/ \t]', match.group(1), 1)
			arcs.setdefault((arc, case), []).append(match.group(2))
			continue

		# Anything else is bogus
		errors['mutant'].append(com)

	if len(bugs) > 0 and check_db:
		bugdb = BugDB()
		results = bugdb.lookup(bugs.keys())

	for crid, insts in bugs.iteritems():
		if len(insts) > 1:
			errors['dup'].append(crid)

		if not check_db:
			continue

		if crid not in results:
			errors['nonexistent'].append(crid)
			continue

		#
		# For each synopsis, compare the real synopsis with
		# that in the comments, allowing for possible '(fix
		# stuff)'-like trailing text
		#
		for entered in insts:
			synopsis = results[crid]["synopsis"]
			if not re.search(r'^' + re.escape(synopsis) +
					r'( \([^)]+\))?$', entered):
				errors['nomatch'].append([crid, synopsis,
							entered])

	if check_db:
		valid = ARC(arcs.keys(), arcPath)

	for case, insts in arcs.iteritems():
		if len(insts) > 1:
			errors['dup'].append(' '.join(case))

 		if not check_db:
			continue
                
		if not case in valid:
			errors['nonexistent'].append(' '.join(case))
			continue

		#
		# We first try a direct match between the actual case name
		# and the entered comment.  If that fails we remove a possible
		# trailing (fix nit)-type comment, and re-try.
		#
		for entered in insts:
			if entered == valid[case]:
				break
			else:
				# Try again with trailing (fix ...) removed.
				dbcom = re.sub(r' \([^)]+\)$', '', entered)
				if dbcom != valid[case]:
					errors['nomatch'].append(
						[' '.join(case), valid[case],
						 entered])

	if blanks:
		output.write("WARNING: Blank line(s) in comments\n")
		ret = 1

	if errors['dup']:
		ret = 1
		output.write("These IDs appear more than once in your "
			     "comments:\n")
		for err in errors['dup']:
			output.write("  %s\n" % err)

	if errors['bugnospc']:
		ret = 1
		output.write("These bugs are missing a single space following "
			     "the ID:\n")
		for com in errors['bugnospc']:
			output.write("  %s\n" % com)

	if errors['mutant']:
		ret = 1
		output.write("These comments are neither bug nor ARC case:\n")
		for com in errors['mutant']:
			output.write("  %s\n" % com)

	if errors['nonexistent']:
		ret = 1
		output.write("These bugs/ARC cases were not found in the "
			     "databases:\n")
		for id in errors['nonexistent']:
			output.write("  %s\n" % id)

	if errors['nomatch']:
		ret = 1
		output.write("These bugs/ARC case synopsis/names don't match "
			     "the database entries:\n")
		for err in errors['nomatch']:
			output.write("Synopsis/name of %s is wrong:\n" % err[0])
			output.write("  should be: '%s'\n" % err[1])
			output.write("         is: '%s'\n" % err[2])

	return ret
