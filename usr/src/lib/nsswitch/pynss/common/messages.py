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
nscfg - Name Service Switch configuration tool.

A module containing all of the messages output by nscfg.
"""

import sys
from nss import _

class Msgs:
    """Indices corresponding to message numbers for nscfg."""

    (NSCFG_ERR_CREATE,
    NSCFG_ERR_MSG_SUB,
    NSCFG_ERR_ILL_SUBCOMMAND,
    NSCFG_ERR_ILL_FMRI,
    NSCFG_ERR_EXPORT,
    NSCFG_ERR_IMPORT,
    NSCFG_ERR_NO_MSG,
    NSCFG_ERR_OPT_ARGS,
    NSCFG_ERR_OS,
    NSCFG_ERR_UNCONFIG,
    NSCFG_ERR_VALIDATE,
    NSCFG_ERR_NO_CHANGE,
    NSCFG_ERR_NO_CONFIG,
    NSCFG_OUT_NO_CONFIG,
    NSCFG_MSG_FREE_FORMAT,
    ) = range(15)

    # Error message dictionaries.
    mNscfgErr = {}
    mNscfgOut = {}
    mNscfgLog = {}

    # Errors from beadm (to stderr).
    mNscfgErr[NSCFG_ERR_CREATE] = _("Unable to create %(0)s.\n%(1)s")
    mNscfgErr[NSCFG_ERR_MSG_SUB] = _("Fatal error. No message associated with index %d")
    mNscfgErr[NSCFG_ERR_ILL_SUBCOMMAND] = _("Illegal subcommand %s")
    mNscfgErr[NSCFG_ERR_ILL_FMRI] = _("Illegal or Unrecognized FMRI: %s")
    mNscfgErr[NSCFG_ERR_EXPORT] = _("Unable to export FMRI: %s")
    mNscfgErr[NSCFG_ERR_IMPORT] = _("Unable to import FMRI: %s")
    mNscfgErr[NSCFG_ERR_NO_MSG] = _("Unable to find message for error code: %d")
    mNscfgErr[NSCFG_ERR_OPT_ARGS] = _("Invalid options and arguments:")
    mNscfgErr[NSCFG_ERR_OS] = _("System error: %s")
    mNscfgErr[NSCFG_ERR_UNCONFIG] = _("Unable to unconfigure FMRI: %s")
    mNscfgErr[NSCFG_ERR_VALIDATE] = _("Invalid configuration FMRI: %s")
    mNscfgErr[NSCFG_ERR_NO_CHANGE] = _("No change to FMRI: %s")
    mNscfgErr[NSCFG_ERR_NO_CONFIG] = _("No configuration for FMRI: %s")
    mNscfgOut[NSCFG_OUT_NO_CONFIG] = _("No configuration for FMRI: %s")

    # Catchall
    mNscfgErr[NSCFG_MSG_FREE_FORMAT] = "%s"

    # Quiet flag
    mQuiet = False

msgLog, msgOut, msgErr = range(3)

def quiet(quiet = True):
    """Set Quiet mode."""
    Msgs.mQuiet = quiet

def printLog(string, log_fd):
    """Print log."""

    sendMsg(string, msgLog, log_fd)

def printStdout(string, log_fd):
    """Print standard output."""

    sendMsg(string, msgOut, log_fd)

def printStderr(string, log_fd):
    """Print standard error."""

    sendMsg(string, msgErr, log_fd)

def composeMsg(string, txt=None):
    """
    Compose the message to be dispayed.
    txt can be either a list or string object.
    Return the newly composed string.
    """

    try:
        msg = string % txt
    except TypeError:
        msg = string

    return (msg)

def sendMsg(string, mode, log_fd=-1):
    """Send message."""

    if not Msgs.mQuiet and mode == msgOut: 
        print >> sys.stdout, string,
    if not Msgs.mQuiet and mode == msgErr: 
        print >> sys.stderr, string
    if log_fd != -1 or mode == msgLog: 
        log_fd.write(string + "\n")

def printMsg(msg_idx=-1, txt="", log_fd=-1):
    """Print the message based on the message index."""

    if msg_idx in Msgs.mNscfgErr:
        printStderr(composeMsg(Msgs.mNscfgErr[msg_idx], txt),
            log_fd)
    elif msg_idx in Msgs.mNscfgOut:
        printStdout(composeMsg(Msgs.mNscfgOut[msg_idx], txt),
            log_fd)
    elif msg_idx in Msgs.mNscfgLog:
        printLog(composeMsg(Msgs.mNscfgLog[msg_idx], txt), log_fd)
    else:
        printStderr(composeMsg(Msgs.mLibbe[NSCFG_ERR_MSG_SUB],
            msg_idx), -1)
        sys.exit(1)

def getMsg(msg_idx=-1, txt=""):
    """Print the message based on the message index."""

    if msg_idx in  Msgs.mNscfgErr:
        return(composeMsg(Msgs.mNscfgErr[msg_idx], txt))
    elif msg_idx in Msgs.mNscfgOut:
        return(composeMsg(Msgs.mNscfgOut[msg_idx], txt))
    elif msg_idx in Msgs.mNscfgLog:
        return(composeMsg(Msgs.mNscfgLog[msg_idx], txt))
    else:
        return(composeMsg(Msgs.mLibbe[NSCFG_ERR_MSG_SUB]))
        sys.exit(1)
