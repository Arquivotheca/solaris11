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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Description:
 *
 *	Contains base code for netbios datagram service.
 *
 * Relavent sections from RFC1002:
 *
 *  5.3.  NetBIOS DATAGRAM SERVICE PROTOCOLS
 *
 *   The following are GLOBAL variables and should be NetBIOS user
 *   configurable:
 *
 *   - SCOPE_ID: the non-leaf section of the domain name preceded by a
 *     '.'  which represents the domain of the NetBIOS scope for the
 *     NetBIOS name.  The following protocol description only supports
 *     single scope operation.
 *
 *   - MAX_DATAGRAM_LENGTH: the maximum length of an IP datagram.  The
 *     minimal maximum length defined in for IP is 576 bytes.  This
 *     value is used when determining whether to fragment a NetBIOS
 *     datagram.  Implementations are expected to be capable of
 *     receiving unfragmented NetBIOS datagrams up to their maximum
 *     size.
 *
 *   - BROADCAST_ADDRESS: the IP address B-nodes use to send datagrams
 *     with group name destinations and broadcast datagrams.  The
 *     default is the IP broadcast address for a single IP network.
 *
 *
 *   The following are Defined Constants for the NetBIOS Datagram
 *   Service:
 *
 *   - IPPORT_NETBIOS_DGM: the globally well-known UDP port allocated
 *     where the NetBIOS Datagram Service receives UDP packets.  See
 *     section 6, "Defined Constants", for its value.
 */

/*
 *
 *  6.  DEFINED CONSTANTS AND VARIABLES
 *
 *   GENERAL:
 *
 *      SCOPE_ID                   The name of the NetBIOS scope.
 *
 *                                 This is expressed as a character
 *                                 string meeting the requirements of
 *                                 the domain name system and without
 *                                 a leading or trailing "dot".
 *
 *                                 An implementation may elect to make
 *                                 this a single global value for the
 *                                 node or allow it to be specified
 *                                 with each separate NetBIOS name
 *                                 (thus permitting cross-scope
 *                                 references.)
 *
 *      BROADCAST_ADDRESS          An IP address composed of the
 *                                 node network and subnetwork
 *                                 numbers with all remaining bits set
 *                                 to one.
 *
 *                                 I.e. "Specific subnet" broadcast
 *                                 addressing according to section 2.3
 *                                 of RFC 950.
 *
 *      BCAST_REQ_RETRY_TIMEOUT    250 milliseconds.
 *                                 An adaptive timer may be used.
 *
 *      BCAST_REQ_RETRY_COUNT      3
 *
 *      UCAST_REQ_RETRY_TIMEOUT    5 seconds
 *                                 An adaptive timer may be used.
 *
 *      UCAST_REQ_RETRY_COUNT      3
 *
 *      MAX_DATAGRAM_LENGTH        576 bytes (default)
 *
 *   DATAGRAM SERVICE:
 *
 *      IPPORT_NETBIOS_DGM          138 (decimal)
 *
 *      FRAGMENT_TO                2 seconds (default)
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <synch.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <smbns_netbios.h>

#include <smbsrv/libsmbns.h>

static int datagram_sock = -1;
static short datagram_id = 1;
static struct datagram_queue smb_datagram_queue;
static mutex_t smb_dgq_mtx;

static void smb_netbios_datagram_error(unsigned char *buf);

/*
 * Function:  smb_netbios_datagram_tick(void)
 *
 * Description:
 *
 *	Called once a second to handle time to live timeouts in
 *	datagram assembly queue.
 *
 * Inputs:
 *
 * Returns:
 *	void	-> Nothing at all...
 */

void
smb_netbios_datagram_tick(void)
{
	struct datagram *entry;
	struct datagram *next;

	(void) mutex_lock(&smb_dgq_mtx);

	for (entry = smb_datagram_queue.forw;
	    entry != (struct datagram *)((uintptr_t)&smb_datagram_queue);
	    entry = next) {
		next = entry->forw;
		if (--entry->discard_timer == 0) {
			/* Toss it */
			QUEUE_CLIP(entry);
			free(entry);
		}
	}
	(void) mutex_unlock(&smb_dgq_mtx);
}

void
smb_netbios_datagram_fini()
{
	struct datagram *entry;

	(void) mutex_lock(&smb_dgq_mtx);
	while ((entry = smb_datagram_queue.forw) !=
	    (struct datagram *)((uintptr_t)&smb_datagram_queue)) {
		QUEUE_CLIP(entry);
		free(entry);
	}
	(void) mutex_unlock(&smb_dgq_mtx);
}

/*
 * Function: int smb_netbios_send_Bnode_datagram(unsigned char *data,
 *		struct name_entry *source, struct name_entry *destination,
 *		uint32_t broadcast)
 *
 * Description from rfc1002:
 *
 *  5.3.1.  B NODE TRANSMISSION OF NetBIOS DATAGRAMS
 *
 *   PROCEDURE send_datagram(data, source, destination, broadcast)
 *
 *   (*
 *    * user initiated processing on B node
 *    *)
 *
 *   BEGIN
 *        group = FALSE;
 *
 *        do name discovery on destination name, returns name type and
 *             IP address;
 *
 *        IF name type is group name THEN
 *        BEGIN
 *             group = TRUE;
 *        END
 *
 *        (*
 *         * build datagram service UDP packet;
 *         *)
 *        convert source and destination NetBIOS names into
 *             half-ASCII, biased encoded name;
 *        SOURCE_NAME = cat(source, SCOPE_ID);
 *        SOURCE_IP = this nodes IP address;
 *        SOURCE_PORT =  IPPORT_NETBIOS_DGM;
 *
 *        IF NetBIOS broadcast THEN
 *        BEGIN
 *             DESTINATION_NAME = cat("*", SCOPE_ID)
 *        END
 *        ELSE
 *        BEGIN
 *             DESTINATION_NAME = cat(destination, SCOPE_ID)
 *        END
 *
 *        MSG_TYPE = select_one_from_set
 *             {BROADCAST, DIRECT_UNIQUE, DIRECT_GROUP}
 *        DGM_ID = next transaction id for Datagrams;
 *        DGM_LENGTH = length of data + length of second level encoded
 *             source and destination names;
 *
 *        IF (length of the NetBIOS Datagram, including UDP and
 *            IP headers, > MAX_DATAGRAM_LENGTH) THEN
 *        BEGIN
 *             (*
 *              * fragment NetBIOS datagram into 2 UDP packets
 *              *)
 *             Put names into 1st UDP packet and any data that fits
 *                  after names;
 *             Set MORE and FIRST bits in 1st UDP packets FLAGS;
 *             OFFSET in 1st UDP = 0;
 *
 *             Replicate NetBIOS Datagram header from 1st UDP packet
 *                  into 2nd UDP packet;
 *             Put rest of data in 2nd UDP packet;
 *             Clear MORE and FIRST bits in 2nd UDP packets FLAGS;
 *             OFFSET in 2nd UDP = DGM_LENGTH - number of name and
 *                  data bytes in 1st UDP;
 *        END
 *        BEGIN
 *             (*
 *              * Only need one UDP packet
 *              *)
 *             USER_DATA = data;
 *             Clear MORE bit and set FIRST bit in FLAGS;
 *             OFFSET = 0;
 *        END
 *
 *        IF (group == TRUE) OR (NetBIOS broadcast) THEN
 *        BEGIN
 *             send UDP packet(s) to BROADCAST_ADDRESS;
 *        END
 *        ELSE
 *        BEGIN
 *             send UDP packet(s) to IP address returned by name
 *                discovery;
 *        END
 *   END (* procedure *)
 *                        1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |   MSG_TYPE    |     FLAGS     |           DGM_ID              |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                           SOURCE_IP                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |          SOURCE_PORT          |          DGM_LENGTH           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |         PACKET_OFFSET         |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *   MSG_TYPE values (in hexidecimal):
 *
 *           10 -  DIRECT_UNIQUE DATAGRAM
 *           11 -  DIRECT_GROUP DATAGRAM
 *           12 -  BROADCAST DATAGRAM
 *           13 -  DATAGRAM ERROR
 *           14 -  DATAGRAM QUERY REQUEST
 *           15 -  DATAGRAM POSITIVE QUERY RESPONSE
 *           16 -  DATAGRAM NEGATIVE QUERY RESPONSE
 *
 *   Bit definitions of the FLAGS field:
 *
 *     0   1   2   3   4   5   6   7
 *   +---+---+---+---+---+---+---+---+
 *   | 0 | 0 | 0 | 0 |  SNT  | F | M |
 *   +---+---+---+---+---+---+---+---+
 *
 *   Symbol     Bit(s)   Description
 *
 *   M               7   MORE flag, If set then more NetBIOS datagram
 *                       fragments follow.
 *
 *   F               6   FIRST packet flag,  If set then this is first
 *                       (and possibly only) fragment of NetBIOS
 *                       datagram
 *
 *   SNT           4,5   Source End-Node type:
 *                          00 = B node
 *                          01 = P node
 *                          10 = M node
 *                          11 = NBDD
 *   RESERVED      0-3   Reserved, must be zero (0)
 *      (But MS sets bit 3 in this field)
 *
 */

int
smb_netbios_datagram_send(struct name_entry *src, struct name_entry *dest,
    unsigned char *data, int length)
{
	smb_inaddr_t ipaddr;
	size_t count, srclen, destlen, sinlen;
	addr_entry_t *addr;
	struct sockaddr_in sin;
	char *buffer;
	char ha_source[NETBIOS_DOMAIN_NAME_MAX];
	char ha_dest[NETBIOS_DOMAIN_NAME_MAX];

	(void) smb_first_level_name_encode(src, (unsigned char *)ha_source,
	    sizeof (ha_source));
	srclen = strlen(ha_source) + 1;

	(void) smb_first_level_name_encode(dest, (unsigned char *)ha_dest,
	    sizeof (ha_dest));
	destlen = strlen(ha_dest) + 1;

	/* give some extra room */
	if ((buffer = calloc(1, MAX_DATAGRAM_LENGTH * 4)) == NULL) {
		syslog(LOG_ERR, "nbt datagram: send: %m");
		return (-1);
	}

	buffer[0] = DATAGRAM_TYPE_DIRECT_UNIQUE;
	switch (smb_node_type) {
	case 'B':
		buffer[1] = DATAGRAM_FLAGS_B_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	case 'P':
		buffer[1] = DATAGRAM_FLAGS_P_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	case 'M':
		buffer[1] = DATAGRAM_FLAGS_M_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	case 'H':
	default:
		buffer[1] = DATAGRAM_FLAGS_H_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	}

	datagram_id++;
	BE_OUT16(&buffer[2], datagram_id);
	(void) memcpy(&buffer[4], &src->addr_list.sin.sin_addr.s_addr,
	    sizeof (uint32_t));
	(void) memcpy(&buffer[8], &src->addr_list.sin.sin_port,
	    sizeof (uint16_t));
	BE_OUT16(&buffer[10], length + srclen + destlen);
	BE_OUT16(&buffer[12], 0);

	bcopy(ha_source, &buffer[14], srclen);
	bcopy(ha_dest, &buffer[14 + srclen], destlen);
	bcopy(data, &buffer[14 + srclen + destlen], length);
	count = &buffer[14 + srclen + destlen + length] - buffer;

	bzero(&sin, sizeof (sin));
	sin.sin_family = AF_INET;
	sinlen = sizeof (sin);
	addr = &dest->addr_list;
	do {
		ipaddr.a_ipv4 = addr->sin.sin_addr.s_addr;
		ipaddr.a_family = AF_INET;
		/* Don't send anything to myself... */
		if (smb_nic_is_local(&ipaddr))
			goto next;

		sin.sin_addr.s_addr = ipaddr.a_ipv4;
		sin.sin_port = addr->sin.sin_port;
		(void) sendto(datagram_sock, buffer, count, 0,
		    (struct sockaddr *)&sin, sinlen);

next:		addr = addr->forw;
	} while (addr != &dest->addr_list);
	free(buffer);
	return (0);
}


int
smb_netbios_datagram_send_to_net(struct name_entry *src,
    struct name_entry *dest, char *data, int length)
{
	smb_inaddr_t ipaddr;
	size_t count, srclen, destlen, sinlen;
	addr_entry_t *addr;
	struct sockaddr_in sin;
	char *buffer;
	char ha_source[NETBIOS_DOMAIN_NAME_MAX];
	char ha_dest[NETBIOS_DOMAIN_NAME_MAX];

	(void) smb_first_level_name_encode(src, (unsigned char *)ha_source,
	    sizeof (ha_source));
	srclen = strlen(ha_source) + 1;

	(void) smb_first_level_name_encode(dest, (unsigned char *)ha_dest,
	    sizeof (ha_dest));
	destlen = strlen(ha_dest) + 1;

	/* give some extra room */
	if ((buffer = calloc(1, MAX_DATAGRAM_LENGTH * 4)) == NULL) {
		syslog(LOG_ERR, "nbt datagram: send_to_net: %m");
		return (-1);
	}

	buffer[0] = DATAGRAM_TYPE_DIRECT_UNIQUE;
	switch (smb_node_type) {
	case 'B':
		buffer[1] = DATAGRAM_FLAGS_B_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	case 'P':
		buffer[1] = DATAGRAM_FLAGS_P_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	case 'M':
		buffer[1] = DATAGRAM_FLAGS_M_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	case 'H':
	default:
		buffer[1] = DATAGRAM_FLAGS_H_NODE | DATAGRAM_FLAGS_FIRST;
		break;
	}

	datagram_id++;
	BE_OUT16(&buffer[2], datagram_id);
	(void) memcpy(&buffer[4], &src->addr_list.sin.sin_addr.s_addr,
	    sizeof (uint32_t));
	(void) memcpy(&buffer[8], &src->addr_list.sin.sin_port,
	    sizeof (uint16_t));
	BE_OUT16(&buffer[10], length + srclen + destlen);
	BE_OUT16(&buffer[12], 0);

	bcopy(ha_source, &buffer[14], srclen);
	bcopy(ha_dest, &buffer[14 + srclen], destlen);
	bcopy(data, &buffer[14 + srclen + destlen], length);
	count = &buffer[14 + srclen + destlen + length] - buffer;

	bzero(&sin, sizeof (sin));
	sin.sin_family = AF_INET;
	sinlen = sizeof (sin);
	addr = &dest->addr_list;
	do {
		ipaddr.a_ipv4 = addr->sin.sin_addr.s_addr;
		ipaddr.a_family = AF_INET;
		if (smb_nic_is_local(&ipaddr))
			goto next;

		sin.sin_addr.s_addr = ipaddr.a_ipv4;
		sin.sin_port = addr->sin.sin_port;
		(void) sendto(datagram_sock, buffer, count, 0,
		    (struct sockaddr *)&sin, sinlen);

next:		addr = addr->forw;
	} while (addr != &dest->addr_list);
	free(buffer);
	return (0);
}


int
smb_datagram_decode(struct datagram *datagram, int bytes)
{
	unsigned char *ha_src;
	unsigned char *ha_dest;
	unsigned char *data;

	if (bytes == DATAGRAM_ERR_HEADER_LENGTH) {
		if (datagram->rawbuf[0] == DATAGRAM_TYPE_ERROR_DATAGRAM)
			smb_netbios_datagram_error(datagram->rawbuf);
		return (-1);

	}

	if (bytes >= DATAGRAM_HEADER_LENGTH) {
		ha_src = &datagram->rawbuf[DATAGRAM_HEADER_LENGTH];
		ha_dest = ha_src + strlen((char *)ha_src) + 1;
		data = ha_dest + strlen((char *)ha_dest) + 1;

		bzero(&datagram->src, sizeof (struct name_entry));
		bzero(&datagram->dest, sizeof (struct name_entry));

		datagram->rawbytes = bytes;
		datagram->packet_type = datagram->rawbuf[0];
		datagram->flags = datagram->rawbuf[1];
		datagram->datagram_id = BE_IN16(&datagram->rawbuf[2]);

		datagram->src.addr_list.sinlen = sizeof (struct sockaddr_in);
		(void) memcpy(&datagram->src.addr_list.sin.sin_addr.s_addr,
		    &datagram->rawbuf[4], sizeof (uint32_t));
		(void) memcpy(&datagram->src.addr_list.sin.sin_port,
		    &datagram->rawbuf[8], sizeof (uint16_t));
		datagram->src.addr_list.forw = datagram->src.addr_list.back =
		    &datagram->src.addr_list;

		datagram->data = data;
		datagram->data_length = BE_IN16(&datagram->rawbuf[10]);
		datagram->offset = BE_IN16(&datagram->rawbuf[12]);

		if (smb_first_level_name_decode(ha_src, &datagram->src) < 0) {
			smb_tracef("NbtDatagram[%s]: invalid calling name",
			    inet_ntoa(datagram->src.addr_list.sin.sin_addr));
			smb_tracef("Calling name: <%02X>%32.32s",
			    ha_src[0], &ha_src[1]);
		}

		datagram->dest.addr_list.forw = datagram->dest.addr_list.back =
		    &datagram->dest.addr_list;

		if (smb_first_level_name_decode(ha_dest, &datagram->dest) < 0) {
			smb_tracef("NbtDatagram[%s]: invalid called name",
			    inet_ntoa(datagram->src.addr_list.sin.sin_addr));
			smb_tracef("Called name: <%02X>%32.32s", ha_dest[0],
			    &ha_dest[1]);
		}

		return (0);
	}

	/* ignore other malformed datagram packets */
	return (-1);
}

/*
 * 4.4.3. Datagram Error Packet
 */
static void
smb_netbios_datagram_error(unsigned char *buf)
{
	int error;
	int datagram_id;

	if (buf[0] != DATAGRAM_TYPE_ERROR_DATAGRAM)
		return;

	datagram_id = BE_IN16(&buf[2]);
	error = buf[DATAGRAM_ERR_HEADER_LENGTH - 1];
	switch (error) {
	case DATAGRAM_INVALID_SOURCE_NAME_FORMAT:
		smb_tracef("NbtDatagramError[%d]: invalid source name format",
		    datagram_id);
		break;

	case DATAGRAM_INVALID_DESTINATION_NAME_FORMAT:
		smb_tracef("NbtDatagramError[%d]: invalid destination name "
		    "format", datagram_id);
		break;

	case DATAGRAM_DESTINATION_NAME_NOT_PRESENT:
	default:
		break;
	}
}


/*
 * Function: int smb_netbios_process_BPM_datagram(unsigned char *packet,
 *		addr_entry_t *addr)
 *
 * Description from rfc1002:
 *
 *  5.3.3.  RECEPTION OF NetBIOS DATAGRAMS BY ALL NODES
 *
 *   The following algorithm discards out of order NetBIOS Datagram
 *   fragments.  An implementation which reassembles out of order
 *   NetBIOS Datagram fragments conforms to this specification.  The
 *   fragment discard timer is initialized to the value FRAGMENT_TIMEOUT.
 *   This value should be user configurable.  The default value is
 *   given in Section 6, "Defined Constants and Variables".
 *
 *   PROCEDURE datagram_packet(packet)
 *
 *   (*
 *    * processing initiated by datagram packet reception
 *    * on B, P and M nodes
 *    *)
 *   BEGIN
 *        (*
 *         * if this node is a P node, ignore
 *         * broadcast packets.
 *         *)
 *
 *        IF this is a P node AND incoming packet is
 *             a broadcast packet THEN
 *        BEGIN
 *             discard packet;
 *        END
 *
 *        CASE packet type OF
 *
 *           DATAGRAM SERVICE:
 *           BEGIN
 *             IF FIRST bit in FLAGS is set THEN
 *             BEGIN
 *                  IF MORE bit in FLAGS is set THEN
 *                  BEGIN
 *                       Save 1st UDP packet of the Datagram;
 *                       Set this Datagrams fragment discard
 *                         timer to FRAGMENT_TIMEOUT;
 *                       return;
 *                  END
 *                  ELSE
 *                       Datagram is composed of a single
 *                         UDP packet;
 *             END
 *             ELSE
 *             BEGIN
 *                  (* Have the second fragment of a Datagram *)
 *
 *                  Search for 1st fragment by source IP address
 *                     and DGM_ID;
 *                  IF found 1st fragment THEN
 *                       Process both UDP packets;
 *                  ELSE
 *                  BEGIN
 *                       discard 2nd fragment UDP packet;
 *                       return;
 *                  END
 *             END
 *
 *             IF DESTINATION_NAME is '*' THEN
 *             BEGIN
 *                  (* NetBIOS broadcast *)
 *
 *                  deliver USER_DATA from UDP packet(s) to all
 *                       outstanding receive broadcast
 *                       datagram requests;
 *                  return;
 *             END
 *             ELSE
 *             BEGIN (* non-broadcast *)
 *                  (* Datagram for Unique or Group Name *)
 *
 *                  IF DESTINATION_NAME is not present in the
 *                     local name table THEN
 *                  BEGIN
 *                       (* destination not present *)
 *                       build DATAGRAM ERROR packet, clear
 *                            FIRST and MORE bit, put in
 *                            this nodes IP and PORT, set
 *                            ERROR_CODE;
 *                       send DATAGRAM ERROR packet to
 *                            source IP address and port
 *                            of UDP;
 *                       discard UDP packet(s);
 *                       return;
 *                  END
 *                  ELSE
 *                  BEGIN (* good *)
 *                       (*
 *                        * Replicate received NetBIOS datagram for
 *                        * each recipient
 *                        *)
 *                       FOR EACH pending NetBIOS users receive
 *                            datagram operation
 *                       BEGIN
 *                            IF source name of operation
 *                               matches destination name
 *                               of packet THEN
 *                            BEGIN
 *                               deliver USER_DATA from UDP
 *                                 packet(s);
 *                            END
 *                       END (* for each *)
 *                       return;
 *                  END (* good *)
 *             END (* non-broadcast *)
 *            END (* datagram service *)
 *
 *           DATAGRAM ERROR:
 *           BEGIN
 *                (*
 *                 * name service returned incorrect information
 *                 *)
 *
 *                inform local name service that incorrect
 *                  information was provided;
 *
 *                IF this is a P or M node THEN
 *                BEGIN
 *                     (*
 *                      * tell NetBIOS Name Server that it may
 *                      * have given incorrect information
 *                      *)
 *
 *                     send NAME RELEASE REQUEST with name
 *                       and incorrect IP address to NetBIOS
 *                       Name Server;
 *                END
 *           END (* datagram error *)
 *
 *        END (* case *)
 *   END
 */

static struct datagram *
smb_netbios_datagram_getq(struct datagram *datagram)
{
	struct datagram *prev = 0;

	(void) mutex_lock(&smb_dgq_mtx);
	for (prev = smb_datagram_queue.forw;
	    prev != (struct datagram *)((uintptr_t)&smb_datagram_queue);
	    prev = prev->forw) {
		if (prev->src.addr_list.sin.sin_addr.s_addr ==
		    datagram->src.addr_list.sin.sin_addr.s_addr) {
			/* Something waiting */
			QUEUE_CLIP(prev);
			(void) mutex_unlock(&smb_dgq_mtx);
			bcopy(datagram->data, &prev->data[prev->data_length],
			    datagram->data_length);
			prev->data_length += datagram->data_length;
			free(datagram);
			return (prev);
		}
	}
	(void) mutex_unlock(&smb_dgq_mtx);

	return (0);
}

static void
smb_netbios_BPM_datagram(struct datagram *datagram)
{
	struct name_entry *entry = 0;
	struct datagram *qpacket = 0;
	pthread_t browser_dispatch;

	switch (datagram->packet_type) {
	case DATAGRAM_TYPE_BROADCAST :
		if (smb_node_type == 'P') {
			/*
			 * if this node is a P node, ignore
			 * broadcast packets.
			 */
			break;
		}
		/* FALLTHROUGH */

	case DATAGRAM_TYPE_DIRECT_UNIQUE :
	case DATAGRAM_TYPE_DIRECT_GROUP :
		if ((datagram->flags & DATAGRAM_FLAGS_FIRST) != 0) {
			if (datagram->flags & DATAGRAM_FLAGS_MORE) {
				/* Save 1st UDP packet of the Datagram */
				datagram->discard_timer = FRAGMENT_TIMEOUT;
				(void) mutex_lock(&smb_dgq_mtx);
				QUEUE_INSERT_TAIL(&smb_datagram_queue, datagram)
				(void) mutex_unlock(&smb_dgq_mtx);
				return;
			}
			/* process datagram */
		} else {
			qpacket = smb_netbios_datagram_getq(datagram);
			if (qpacket) {
				datagram = qpacket;
				goto process_datagram;
			}
			break;
		}

process_datagram:
		entry = 0;
		if ((strcmp((char *)datagram->dest.name, "*") == 0) ||
		    ((entry =
		    smb_netbios_cache_lookup(&datagram->dest)) != 0)) {
			if (entry) {
				int is_local = IS_LOCAL(entry->attributes);
				smb_netbios_cache_unlock_entry(entry);

				if (is_local) {
					(void) pthread_create(&browser_dispatch,
					    0, smb_browser_dispatch,
					    (void *)datagram);
					(void) pthread_detach(browser_dispatch);
					return;
				}
			}

			datagram->rawbuf[0] = DATAGRAM_TYPE_ERROR_DATAGRAM;
			datagram->rawbuf[1] &= DATAGRAM_FLAGS_SRC_TYPE;

			(void) memcpy(&datagram->rawbuf[4],
			    &datagram->src.addr_list.sin.sin_addr.s_addr,
			    sizeof (uint32_t));
			BE_OUT16(&datagram->rawbuf[8], IPPORT_NETBIOS_DGM);

			(void) sendto(datagram_sock, datagram->rawbuf,
			    datagram->rawbytes, 0,
			    (struct sockaddr *)&datagram->src.addr_list.sin,
			    datagram->src.addr_list.sinlen);
		}
		break;

	case DATAGRAM_TYPE_ERROR_DATAGRAM :
		break;
	}
	free(datagram);
}

/*
 * NetBIOS Datagram Service (port 138)
 */
/*ARGSUSED*/
void *
smb_netbios_datagram_service(void *arg)
{
	struct sockaddr_in 	sin;
	struct datagram 	*datagram;
	int			bytes, flag = 1;
	smb_inaddr_t 		ipaddr;

	(void) mutex_lock(&smb_dgq_mtx);
	bzero(&smb_datagram_queue, sizeof (smb_datagram_queue));
	smb_datagram_queue.forw = smb_datagram_queue.back =
	    (struct datagram *)((uintptr_t)&smb_datagram_queue);
	(void) mutex_unlock(&smb_dgq_mtx);

	if ((datagram_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "nbt datagram: socket failed: %m");
		smb_netbios_event(NETBIOS_EVENT_ERROR);
		return (NULL);
	}

	flag = 1;
	(void) setsockopt(datagram_sock, SOL_SOCKET, SO_REUSEADDR, &flag,
	    sizeof (flag));

	bzero(&sin, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(IPPORT_NETBIOS_DGM);
	if (bind(datagram_sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
		syslog(LOG_ERR, "nbt datagram: bind(%d) failed: %m",
		    IPPORT_NETBIOS_DGM);
		(void) close(datagram_sock);
		smb_netbios_event(NETBIOS_EVENT_ERROR);
		return (NULL);
	}

	flag = 1;
	(void) setsockopt(datagram_sock, SOL_SOCKET, SO_BROADCAST, &flag,
	    sizeof (flag));

	smb_netbios_event(NETBIOS_EVENT_DGM_START);

	while (smb_netbios_running()) {
		if ((datagram = calloc(1, sizeof (struct datagram))) == NULL) {
			/* Sleep for 10 seconds and try again */
			smb_netbios_sleep(10);
			continue;
		}

ignore:		bzero(&datagram->inaddr, sizeof (addr_entry_t));
		datagram->inaddr.sinlen = sizeof (datagram->inaddr.sin);
		datagram->inaddr.forw = datagram->inaddr.back =
		    &datagram->inaddr;

		if ((bytes = recvfrom(datagram_sock, datagram->rawbuf,
		    MAX_DATAGRAM_LENGTH, 0,
		    (struct sockaddr *)&datagram->inaddr.sin,
		    &datagram->inaddr.sinlen)) < 0) {
			syslog(LOG_ERR, "nbt datagram: recvfrom failed: %m");
			smb_netbios_event(NETBIOS_EVENT_ERROR);
			break;
		}

		/* Ignore any incoming packets from myself... */
		ipaddr.a_ipv4 = datagram->inaddr.sin.sin_addr.s_addr;
		ipaddr.a_family = AF_INET;
		if (smb_nic_is_local(&ipaddr)) {
			goto ignore;
		}

		if (smb_datagram_decode(datagram, bytes) < 0)
			goto ignore;

	/*
	 * This code was doing the wrong thing with responses from a
	 * Windows2000 PDC because both DATAGRAM_FLAGS_H_NODE and
	 * DATAGRAM_FLAGS_NBDD are defined to be the same value (see
	 * netbios.h). Since the Windows2000 PDC wants to be an H-Node,
	 * we need to handle all messages via smb_netbios_BPM_datagram.
	 *
	 *	if ((datagram->flags & DATAGRAM_FLAGS_SRC_TYPE) ==
	 *	    DATAGRAM_FLAGS_NBDD)
	 *		smb_netbios_NBDD_datagram(datagram);
	 *	else
	 *		smb_netbios_BPM_datagram(datagram);
	 */

		smb_netbios_BPM_datagram(datagram);
	}

	smb_netbios_event(NETBIOS_EVENT_DGM_STOP);
	(void) smb_netbios_wait(NETBIOS_EVENT_BROWSER_STOP);

	(void) close(datagram_sock);
	smb_netbios_datagram_fini();
	return (NULL);
}

static char
/* LINTED - E_STATIC_UNUSED */
nb_fmt_flags(unsigned char flags)
{
	switch (flags & DATAGRAM_FLAGS_SRC_TYPE) {
	case DATAGRAM_FLAGS_B_NODE:	return ('B');
	case DATAGRAM_FLAGS_P_NODE:	return ('P');
	case DATAGRAM_FLAGS_M_NODE:	return ('M');
	case DATAGRAM_FLAGS_H_NODE:	return ('H');
	default:	return ('?');
	}
}
