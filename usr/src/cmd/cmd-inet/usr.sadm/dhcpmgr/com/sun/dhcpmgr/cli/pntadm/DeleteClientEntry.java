/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * ident	"%Z%%M%	%I%	%E% SMI"
 *
 * Copyright (c) 2001 by Sun Microsystems, Inc.
 * All rights reserved.
 */
package com.sun.dhcpmgr.cli.pntadm;

import com.sun.dhcpmgr.data.DhcpClientRecord;
import com.sun.dhcpmgr.data.Network;
import com.sun.dhcpmgr.bridge.BridgeException;
import com.sun.dhcpmgr.bridge.NoEntryException;

import java.lang.IllegalArgumentException;

/**
 * The main class for the "delete client" functionality of pntadm.
 */
public class DeleteClientEntry extends PntAdmFunction {

    /**
     * The valid options associated with deleting a client entry.
     */
    static final int supportedOptions[] = {
	PntAdm.DELETE_HOST,
	PntAdm.RESOURCE,
	PntAdm.RESOURCE_CONFIG,
	PntAdm.PATH
    };

    /**
     * The client entry to delete.
     */
    String clientIP;

    /**
     * Constructs a DeleteClientEntry object for the client, clientIP.
     * @param clientIP the client name or IP address.
     */
    public DeleteClientEntry(String clientIP) {

	this.clientIP = clientIP;
	validOptions = supportedOptions;

    } // constructor

    /**
     * Returns the option flag for this function.
     * @returns the option flag for this function.
     */
    public int getFunctionFlag() {
	return (PntAdm.DELETE_CLIENT_ENTRY);
    }

    /**
     * Executes the "delete client" functionality.
     * @return PntAdm.SUCCESS, PntAdm.ENOENT, PntAdm.WARNING, or
     * PntAdm.CRITICAL
     */
    public int execute()
	throws IllegalArgumentException {

	int returnCode = PntAdm.SUCCESS;

	// Check to see if the user wants the host deleted.
	//
	boolean deleteHost = options.isSet(PntAdm.DELETE_HOST);
	if (deleteHost && !isHostsManaged()) {
		deleteHost = false;
		returnCode = PntAdm.WARNING;
	}

	// Build up a DhcpClientRecord from the user specified options.
	//
	try {
	    DhcpClientRecord dhcpClientRecord = new DhcpClientRecord(clientIP);

	    // Create a Network object.
	    //
	    Network network = getNetMgr().getNetwork(networkName);
	    if (networkName == null) {
		printErrMessage(getString("network_name_error"));
		return (PntAdm.WARNING);
	    }

	    // Delete the client and remove host from the hosts table
	    // if requested.
	    //
	    getNetMgr().deleteClient(dhcpClientRecord, network.toString(),
		deleteHost, getDhcpDatastore());

	} catch (NoEntryException e) {
	    printErrMessage(getMessage(e));
	    returnCode = PntAdm.ENOENT;
	} catch (Throwable e) {
	    printErrMessage(getMessage(e));
	    returnCode = PntAdm.WARNING;
	}

	return (returnCode);

    } // execute

} // DeleteClientEntry
