/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ethernet.h>
#include <sys/net802dot1.h>

#include <snoop.h>

struct conf_bpdu {
	uchar_t cb_protid[2];		/* Protocol Identifier */
	uchar_t cb_protvers;		/* Protocol Version Identifier */
	uchar_t cb_type;		/* BPDU Type */
	uchar_t cb_flags;		/* BPDU Flags */
	uchar_t cb_rootid[8];		/* Root Identifier */
	uchar_t cb_rootcost[4];		/* Root Path Cost */
	uchar_t cb_bridgeid[8];		/* Bridge Identifier */
	uchar_t cb_portid[2];		/* Port Identifier */
	uchar_t cb_messageage[2];	/* Message Age */
	uchar_t cb_maxage[2];		/* Max Age */
	uchar_t cb_hello[2];		/* Hello Time */
	uchar_t cb_fwddelay[2];		/* Forward Delay */
};

#define	BPDU_TYPE_CONF		0
#define	BPDU_TYPE_RCONF		2
#define	BPDU_TYPE_TCNOTIF	0x80

static const uchar_t gvrp_protid[] = GVRP_PROTID;

static char *
gvrp_type_string(uint8_t type)
{
	switch (type) {
	case GARP_LEAVE_ALL:
		return ("Leave All");
	case GARP_JOIN_EMPTY:
		return ("Join Empty");
	case GARP_JOIN_IN:
		return ("Join In");
	case GARP_LEAVE_EMPTY:
		return ("Leave Empty");
	case GARP_LEAVE_IN:
		return ("Leave In");
	case GARP_EMPTY:
		return ("Empty");
	default:
		return ("Unknown");
	}
	/* unreachable */
	return (NULL);
}

int
interpret_bpdu(int flags, char *data, int dlen, boolean_t gvrp)
{
	struct conf_bpdu *cb;
	const char *pdutype;

	if (dlen < 4) {
		(void) snprintf(get_sum_line(), MAXLINE,
		    "BPDU (short packet)");
		return (0);
	}

	cb = (struct conf_bpdu *)data;

	if (flags & F_SUM) {
		(void) snprintf(get_sum_line(), MAXLINE,
		    "Bridge PDU T:%d L:%d", cb->cb_type, dlen);
	}

	if (flags & F_DTAIL) {
		boolean_t gvrp_data = (memcmp(cb, gvrp_protid,
		    GVRP_PROTID_LEN) == 0);

		show_header("Bridge-PDU: ",
		    "Bridge PDU Frame", dlen);
		show_space();

		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "protid: %x%x version: %x", cb->cb_protid[0],
		    cb->cb_protid[1], cb->cb_protvers);

		if (gvrp_data) {
			uint16_t offset = GVRP_PROTID_LEN;

			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "GARP VLAN Registration Protocol");

			if (!gvrp) {
				(void) snprintf(get_line(0, 0),
				    get_line_remain(),
				    "GVRP PBDU type / address mismatch");
			}
			while (offset < dlen && data[offset] !=
			    GARP_ATTR_LIST_ENDMARK) {
				struct gvrp_attr *attr =
				    (struct gvrp_attr *)&data[offset];

				(void) snprintf(get_line(0, 0),
				    get_line_remain(),
				    "    type: %s (%x), vid: %d",
				    gvrp_type_string(attr->event), attr->event,
				    ntohs(attr->value));
				offset += attr->len;
			}

		} else {

			switch (cb->cb_type) {
			case BPDU_TYPE_CONF:
				pdutype = "Configuration";
				break;
			case BPDU_TYPE_RCONF:
				pdutype = "Rapid Configuration";
				break;
			case BPDU_TYPE_TCNOTIF:
				pdutype = "TC Notification";
				break;
			default:
				pdutype = "?";
				break;
			}
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "PDU type = %d (%s)", cb->cb_type, pdutype);
		}
		show_trailer();
	}
	return (0);
}
