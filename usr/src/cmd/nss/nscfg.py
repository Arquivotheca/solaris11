#!/usr/bin/python2.6 -S
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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

"""
nscfg - The Name Service Switch Administration tool. Use this CLI to
manage name service switch configuration.
"""

import os
import sys
#
# don't import site.py (-S), but do include the desired paths
# Need to avoid unnecessary getpwuid calls in site.py
#
try:
    sys.path.append('/usr/lib/python2.6/site-packages')
    sys.path.append('/usr/lib/python2.6/vendor-packages')
except:
    pass
import getopt
import gettext
import shutil
import traceback
import time
import subprocess
import optparse

from nss import *
import nss.messages as msg

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def nscfg_usage():
    '''Defines parameters and options of the command nscfg.'''
    print >> sys.stderr, _("""
Usage:
    nscfg subcommand cmd_options

    subcommands:

    nscfg import [-fnvq] FMRI
    nscfg export [-nvq] FMRI
    nscfg unconfig [-vq] FMRI
    nscfg validate [-vq] FMRI
    """)
    sys.exit(1)

class NscfgOptionParser(optparse.OptionParser):
    def exit(self, status=0, msg=None):
	if msg:
	    sys.stderr.write(msg)
	sys.exit(2)

    def print_usage(self, file=None):
	nscfg_usage()

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Public Command Line functions described in nscfg(1m)
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def import_cmd(opts):
    """ 
    Function:    import

            Description: Populate SMF FMRI properties from legacy Name
                         Service Switch configuration files.

            Parameters:
                FMRI - A SMF FMRI representing a name service component

            Returns:
                0 - Success
                1 - Failure
                3 - No Configuration
    """

    op = NscfgOptionParser()
    op.add_option('-f', action='store_true', dest='force', \
	default=False, help=optparse.SUPPRESS_HELP)
    op.add_option('-n', action='store_true', dest='nowrite', \
	default=False, help=optparse.SUPPRESS_HELP)
    op.add_option('-v', action='count', dest='verbose', \
	default=0, help=optparse.SUPPRESS_HELP)
    op.add_option('-q', action='store_true', dest='quiet', \
	default=False, help=optparse.SUPPRESS_HELP)
    (opt, args) = op.parse_args(opts)

    if opt.quiet:
	msg.quiet()

    if len(args) != 1:
        msg.printMsg(msg.Msgs.NSCFG_ERR_OPT_ARGS, None, -1)
        nscfg_usage()
    fmri = args[0]
    obj = create_nss_object(fmri)
    if obj == None:
        msg.printMsg(msg.Msgs.NSCFG_ERR_ILL_FMRI, fmri, -1)
	return 1
    if opt.verbose:
	obj.verbose(opt.verbose)
    if opt.nowrite:
	obj.nowrite()
    if obj.is_configured() and not opt.force:
	return 0
    try:
        ret = obj.import_to_smf()
	if ret == 0:
	    return 0
	if ret > 0:
	    if opt.verbose:
		msg.printMsg(msg.Msgs.NSCFG_ERR_NO_CONFIG, fmri, -1)
	    return 3
    except:
	pass
    msg.printMsg(msg.Msgs.NSCFG_ERR_IMPORT, fmri, -1)

    return 1


#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def export_cmd(opts):
    """ 
    Function:    export

            Description: Export SMF FMRI properties into legacy Name
                         Service Switch files given an SMF FMRI.

            Parameters:
                FMRI - A SMF FMRI representing a name service component

            Returns:
                0 - Success
                1 - Failure
                2 - No change necessary
    """

    op = NscfgOptionParser()
    op.add_option('-n', action='store_true', dest='nowrite', \
	default=False, help=optparse.SUPPRESS_HELP)
    op.add_option('-v', action='count', dest='verbose', \
	default=0, help=optparse.SUPPRESS_HELP)
    op.add_option('-q', action='store_true', dest='quiet', \
	default=False, help=optparse.SUPPRESS_HELP)
    (opt, args) = op.parse_args(opts)

    if opt.quiet:
	msg.quiet()

    if len(args) != 1:
        msg.printMsg(msg.Msgs.NSCFG_ERR_OPT_ARGS, None, -1)
        nscfg_usage()
    fmri = args[0]
    obj = create_nss_object(fmri)
    if obj == None:
        msg.printMsg(msg.Msgs.NSCFG_ERR_ILL_FMRI, fmri, -1)
	return 1
    if opt.verbose:
	obj.verbose(opt.verbose)
    if opt.nowrite:
	return 0
    try:
        if obj.is_populated():
	    ret = obj.export_from_smf()
	    if ret == 0:
		return 0
	    if ret > 0:
		if opt.verbose:
		    msg.printMsg(msg.Msgs.NSCFG_ERR_NO_CHANGE, fmri, -1)
		return 2
	else:
	    if opt.verbose:
		msg.printMsg(msg.Msgs.NSCFG_OUT_NO_CONFIG, fmri, -1)
    except:
	pass
    msg.printMsg(msg.Msgs.NSCFG_ERR_EXPORT, fmri, -1)
    return 2

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def unconfig_cmd(opts):
    """ 
    Function:    unconfig

            Description: Unconfigure all SMF FMRI properties for the
                         given SMF FMRI.

            Parameters:
                FMRI - A SMF FMRI representing a name service component

            Returns:
                0 - Success
                1 - Failure
    """

    op = NscfgOptionParser()
    op.add_option('-n', action='store_true', dest='nowrite', \
	default=False, help=optparse.SUPPRESS_HELP)
    op.add_option('-v', action='count', dest='verbose', \
	default=0, help=optparse.SUPPRESS_HELP)
    op.add_option('-q', action='store_true', dest='quiet', \
	default=False, help=optparse.SUPPRESS_HELP)
    (opt, args) = op.parse_args(opts)

    if opt.quiet:
	msg.quiet()

    if len(args) != 1:
        msg.printMsg(msg.Msgs.NSCFG_ERR_OPT_ARGS, None, -1)
        nscfg_usage()
    fmri = args[0]
    obj = create_nss_object(fmri)
    if obj == None:
        msg.printMsg(msg.Msgs.NSCFG_ERR_ILL_FMRI, fmri, -1)
	return 1
    if opt.verbose:
	obj.verbose(opt.verbose)
    if opt.nowrite:
	return 0
    try:
        ret = obj.unconfig_smf()
	if ret == 0:
	    return 0
    except:
	pass
    msg.printMsg(msg.Msgs.NSCFG_ERR_UNCONFIG, fmri, -1)
    return 1

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def validate_cmd(opts):
    """ 
    Function:    validate

            Description: Validate the configuration of a given SMF FMRI.

            Parameters:
                FMRI - A SMF FMRI representing a name service component

            Returns:
                0 - Success
                1 - Failure
    """

    op = NscfgOptionParser()
    op.add_option('-v', action='count', dest='verbose', \
	default=0, help=optparse.SUPPRESS_HELP)
    op.add_option('-q', action='store_true', dest='quiet', \
	default=False, help=optparse.SUPPRESS_HELP)
    (opt, args) = op.parse_args(opts)

    if opt.quiet:
	msg.quiet()

    if len(args) != 1:
        msg.printMsg(msg.Msgs.NSCFG_ERR_OPT_ARGS, None, -1)
        nscfg_usage()
    fmri = args[0]
    obj = create_nss_object(fmri)
    if obj == None:
        msg.printMsg(msg.Msgs.NSCFG_ERR_ILL_FMRI, fmri, -1)
	return 1
    if opt.verbose:
	obj.verbose(opt.verbose)
    try:
        ret = obj.validate(num_tries=1)
	if ret == True:
	    return 0
    except:
	pass
    msg.printMsg(msg.Msgs.NSCFG_ERR_VALIDATE, fmri, -1)
    return 1

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# End of CLI public functions
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def parseCLI(cli_opts_args):
    """Parse command line interface arguments."""

    gettext.install("nscfg", "/usr/lib/locale")

    if len(cli_opts_args) == 0:
        nscfg_usage()

    subcommand = cli_opts_args[0]
    opts_args = cli_opts_args[1:]

    if subcommand == "import":
        rc = import_cmd(opts_args)
    elif subcommand == "export":
        rc = export_cmd(opts_args)
    elif subcommand == "unconfig":
        rc = unconfig_cmd(opts_args)
    elif subcommand == "validate":
        rc = validate_cmd(opts_args)
    else:
        msg.printMsg(msg.Msgs.NSCFG_ERR_ILL_SUBCOMMAND,
            subcommand, -1)
        nscfg_usage()

    return(rc)

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def main():
    """main function."""

    gettext.install("nscfg", "/usr/lib/locale")

    return(parseCLI(sys.argv[1:]))

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
if __name__ == "__main__":
    try:
        RC = main()
    except SystemExit, e:
        raise e
    except:
        traceback.print_exc()
        sys.exit(1)
    sys.exit(RC)
