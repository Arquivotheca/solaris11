#! /usr/bin/python
#
# 
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

#
# Mercurial (lack of) keyword checks
#

import re, sys

# A general 'ident'-style decleration, to allow for leniency.
ident = re.compile(r'((\%Z\%(\%M\%)\s+\%I\%|\%W\%)\s+\%E\% SMI)')

#
# Absolutely anything that appears to be an SCCS keyword.
# It's impossible to programatically differentiate between these
# and other, legitimate, uses of matching strings. 
#
anykword = re.compile(r'%[A-ILMP-UWYZ]%')

def keywords(fh, filename=None, lenient=False, verbose=False,
             output=sys.stderr):
    '''Search FH for SCCS keywords, which should not be present when
    Mercurial is in use.

    If LENIENT, accept #ident-style declarations, for the purposes of
    migration'''

    if not filename:
        filename = fh.name

    ret = 0
    lineno = 0
    
    for line in fh:
        line = line.rstrip('\r\n')
        lineno += 1
        
        if lenient and ident.search(line):
            continue
        
        match = anykword.findall(line)
        if match:
            ret = 1
            output.write('%s: %d: contains SCCS keywords "%s"\n' %
                         (filename, lineno, ', '.join(match)))
            if verbose:
                output.write("   %s\n" % line)

    return ret
