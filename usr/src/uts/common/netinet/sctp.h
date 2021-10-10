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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_NETINET_SCTP_H
#define	_NETINET_SCTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * This file contains the structure defintions and function prototypes
 * described in the IETF SCTP socket API document.
 */

/* SCTP association ID type. */
typedef int	sctp_assoc_t;
typedef int32_t	sctp_assoc32_t;

/*
 * Special association IDs.
 *
 * SCTP_ALL_ASSOC: SCTP_CURRENT_ASSOC and SCTP_FUTURE_ASSOC associations.
 * SCTP_CURRENT_ASSOC: all current established associations.
 * SCTP_FUTURE_ASSOC: future to be established associations.
 */
enum {
	SCTP_ALL_ASSOC = -1,
	SCTP_FUTURE_ASSOC = -2,
	SCTP_CURRENT_ASSOC = -3
};

/*
 * SCTP socket options
 */
#define	SCTP_RTOINFO			1
#define	SCTP_ASSOCINFO			2
#define	SCTP_INITMSG			3
#define	SCTP_NODELAY			4
#define	SCTP_AUTOCLOSE			5
#define	SCTP_SET_PEER_PRIMARY_ADDR	6
#define	SCTP_PRIMARY_ADDR		7
#define	SCTP_ADAPTATION_LAYER		8
#define	SCTP_DISABLE_FRAGMENTS		9
#define	SCTP_PEER_ADDR_PARAMS		10
/* SCTP_DEFAULT_SEND_PARAM is deprecated, use SCTP_DEFAULT_SNDINFO. */
#define	SCTP_DEFAULT_SEND_PARAM		11
/* SCTP_EVENTS option is deprecated, use SCTP_EVENT option. */
#define	SCTP_EVENTS			12
#define	SCTP_I_WANT_MAPPED_V4_ADDR	13
#define	SCTP_MAXSEG			14
#define	SCTP_STATUS			15
#define	SCTP_GET_PEER_ADDR_INFO		16

/*
 * Additional SCTP socket options. This socket option is used to enable or
 * disable PR-SCTP support prior to establishing an association. By default,
 * PR-SCTP support is disabled.
 */
#define	SCTP_PRSCTP			23

/*
 * SCTP socket option used to read per endpoint association statistics.
 */
#define	SCTP_GET_ASSOC_STATS		24

/* get/set congestion control */
#define	SCTP_CONGESTION			25

#define	SCTP_EVENT			26
#define	SCTP_HMAC_IDENT			27
#define	SCTP_DELAYED_SACK		28
#define	SCTP_FRAGMENT_INTERLEAVE	29
#define	SCTP_PARTIAL_DELIVERY_POINT	30
#define	SCTP_MAX_BURST			31
#define	SCTP_CONTEXT			32
#define	SCTP_EXPLICIT_EOR		33
#define	SCTP_REUSE_PORT			34
#define	SCTP_RECVRCVINFO		35
#define	SCTP_RECVNXTINFO		36
#define	SCTP_DEFAULT_SNDINFO		37
#define	SCTP_GET_ASSOC_NUMBER		38

/*
 * The option SCTP_GET_ASSOC_ID_LIST uses struct sctp_assoc_ids which
 * makes use of flexible array.  It requires C99 feature.  If C99
 * feature is disabled (via the -xC99=%none flag), the option cannot be
 * used.
 */
#if !defined(__SUNPRO_C) || defined(__C99FEATURES__)
#define	SCTP_GET_ASSOC_ID_LIST		39
#endif

/*
 * Private socket options.
 */
#define	SCTP_GET_NLADDRS		17
#define	SCTP_GET_LADDRS			18
#define	SCTP_GET_NPADDRS		19
#define	SCTP_GET_PADDRS			20
#define	SCTP_ADD_ADDR			21
#define	SCTP_REM_ADDR			22

/* Struct used to set SCTP integer opton value. */
struct sctp_assoc_value {
	sctp_assoc_t	assoc_id;
	uint32_t	assoc_value;
};

/* Struct used by option SCTP_DELAYED_SACK to set SACK timer related values. */
struct sctp_sack_info {
	sctp_assoc_t	sack_assoc_id;
	uint32_t	sack_delay;
	uint32_t	sack_freq;
};

#if !defined(__SUNPRO_C) || defined(__C99FEATURES__)
/*
 * Work around for old version of Studio C++ compiler which does not support
 * flexible array.
 */
#ifdef __SUNPRO_CC
#define	_SCTP_FLEX_ARRAY	0
#else
#define	_SCTP_FLEX_ARRAY
#endif

/*
 * Struct used by option SCTP_GET_ASSOC_ID_LIST to get the list of
 * association IDs currently in use.
 */
struct sctp_assoc_ids {
	uint32_t	gaids_number_of_ids;
	sctp_assoc_t	gaids_assoc_id[_SCTP_FLEX_ARRAY];
};
#endif

/*
 * Struct used by option SCTP_EVENT to enable/disable delivery of different
 * notifications.
 */
struct sctp_event {
	sctp_assoc_t	se_assoc_id;
	uint16_t	se_type;
	uint8_t		se_on;
};

/*
 * SCTP_FRAGMENT_INTERLEAVE optons values.
 *
 * SCTP_NO_INTERLEAVE: Prevents the interleaving of any messages.
 * SCTP_ASSOC_INTERLEAVE: (1-N style socket only) Allows interleaving of
 *                        messages that are from different associations of a
 *                        1-N style socket.
 * SCTP_COMPLETE_INTERLEAVE: Allows complete interleaving of messages.  This
 *                           requires the socket option SCTP_RECVRCVINFO to be
 *                           enabled.
 */
enum {
	SCTP_NO_INTERLEAVE = 0,
	SCTP_ASSOC_INTERLEAVE = 1,
	SCTP_COMPLETE_INTERLEAVE = 2
};

/*
 * SCTP Ancillary Data Definitions
 */

/*
 * Ancillary data identifiers
 */
#define	SCTP_SNDRCV		0x100
#define	SCTP_INIT		0x101
#define	SCTP_SNDINFO		0x102
#define	SCTP_RCVINFO		0x103
#define	SCTP_NXTINFO		0x104
#define	SCTP_PRINFO		0x105
#define	SCTP_AUTHINFO		0x106
#define	SCTP_DSTADDRV4		0x107
#define	SCTP_DSTADDRV6		0x108

/* Private ancillary data identifiers */
#define	SCTP_RNINFO		0x201
#define	SCTP_SPAINFO		0x202

/*
 * sctp_initmsg structure provides information for initializing new SCTP
 * associations with sendmsg().  The SCTP_INITMSG socket option uses
 * this same data structure.
 */
struct sctp_initmsg {
	uint16_t	sinit_num_ostreams;
	uint16_t	sinit_max_instreams;
	uint16_t	sinit_max_attempts;
	uint16_t	sinit_max_init_timeo;
};

/*
 * sctp_sndrcvinfo structure specifies SCTP options for sendmsg() and
 * describes SCTP header information about a received message through
 * recvmsg().
 *
 * This is deprecated.  Use sctp_sndinfo and sctp_rcvinfo instead.
 */
struct sctp_sndrcvinfo {
	uint16_t	sinfo_stream;
	uint16_t	sinfo_ssn;
	uint16_t	sinfo_flags;
	uint32_t	sinfo_ppid;
	uint32_t	sinfo_context;
	uint32_t	sinfo_timetolive;
	uint32_t	sinfo_tsn;
	uint32_t	sinfo_cumtsn;
	sctp_assoc_t	sinfo_assoc_id;
};

/*
 * sinfo_flags
 *
 * Flag names started with MSG_ are deprecated.  They are kept for
 * backward compatibility.
 */
#define	MSG_UNORDERED	0x01		/* Unordered data */
#define	MSG_ABORT	0x02		/* Abort the connection */
#define	MSG_EOF		0x04		/* Shutdown the connection */

/*
 * Use destination addr passed as parameter, not the association primary one.
 */
#define	MSG_ADDR_OVER	0x08

/*
 * This flag when set in sinfo_flags is used along with sinfo_timetolive to
 * implement the "timed reliability" service discussed in RFC 3758.
 */
#define	MSG_PR_SCTP	0x10

/* Flags used in sctp_sndinfo/sctp_rcvinfo/sctp_nxtinfo. */
#define	SCTP_UNORDERED	MSG_UNORDERED
#define	SCTP_ABORT	MSG_ABORT
#define	SCTP_EOF	MSG_EOF
#define	SCTP_ADDR_OVER	MSG_ADDR_OVER
#define	SCTP_SENDALL	0x20		/* Send to all assoc in 1-N socket */
#define	SCTP_EOR	0x40
#define	SCTP_COMPLETE	0x80
#define	SCTP_NOTIFICATION	MSG_NOTIFICATION

/*
 * SCTP Send Information Structure, cmsg_type is SCTP_SNDINFO.
 */
struct sctp_sndinfo {
	uint16_t	snd_sid;	/* stream ID */
	uint16_t	snd_flags;	/* send flags, see sinfo_flags above */
	uint32_t	snd_ppid;	/* payload ID */
	uint32_t	snd_context;	/* user given context */
	sctp_assoc_t	snd_assoc_id;	/* assoc ID */
};

/*
 * SCTP Receive Information Structure, cmsg_type is SCTP_RCVINFO.
 */
struct sctp_rcvinfo {
	uint16_t	rcv_sid;	/* stream ID */
	uint16_t	rcv_ssn;	/* stream sequence number */
	uint16_t	rcv_flags;	/* receive flags */
	uint32_t	rcv_ppid;	/* payload ID */
	uint32_t	rcv_tsn;	/* SCTP transmission sequence number */
	uint32_t	rcv_cumtsn;	/* SCTP cumulative TSN */
	uint32_t	rcv_context;	/* user given context */
	sctp_assoc_t	rcv_assoc_id;	/* assoc ID */
};

/*
 * SCTP Next Receive Information Structure, cmsg_type is SCTP_NXTINFO.
 */
struct sctp_nxtinfo {
	uint16_t	nxt_sid;	/* streamd ID */
	uint16_t	nxt_flags;	/* receive flags */
	uint32_t	nxt_ppid;	/* payload ID */
	uint32_t	nxt_length;	/* message length */
	sctp_assoc_t	nxt_assoc_id;	/* assoc ID */
};

/*
 * SCTP Partial Reliability Information Structure, cmsg_type is SCTP_PRINFO.
 */
struct sctp_prinfo {
	uint16_t	pr_policy;	/* PR-SCTP policy */
	uint32_t	pr_value;	/* PR-SCTP policy specified value */
};

/* Partial Reliability Policy */
#define	SCTP_PR_SCTP_NONE	0
#define	SCTP_PR_SCTP_TTL	1

/*
 * SCTP AUTH Information Structure, cmsg_type is SCTP_AUTHINFO.
 * This is CURRENTLY NOT SUPPORTED.  It is defined here as a place
 * holder for struct sctp_sendv_spa.
 */
struct sctp_authinfo {
	uint16_t	auth_keyid;	/* shared key ID */
};

/*
 * Struct used by sctp_recvv().
 */
struct sctp_recvv_rn {
	struct sctp_rcvinfo	recvv_rcvinfo;	/* current message info */
	struct sctp_nxtinfo	recvv_nxtinfo;	/* next message info */
};

/*
 * Flags for infotype parameter of sctp_recvv() indicating the type of *info.
 *
 * SCTP_RECVV_NOINFO: no info is returned.
 * SCTP_RECVV_RCVINFO: struct sctp_rcvinfo is returned.
 * SCTP_RECVV_NXTINFO: struct sctp_nxtinfo is returned.
 * SCTP_RECVV_RN: struct sctp_rrecvv_rn is returned.
 */
#define	SCTP_RECVV_NOINFO	0
#define	SCTP_RECVV_RCVINFO	SCTP_RCVINFO
#define	SCTP_RECVV_NXTINFO	SCTP_NXTINFO
#define	SCTP_RECVV_RN		SCTP_RNINFO

/*
 * Struct used by sctp_sendv().
 * Note that use of sendv_authinfo is currently not supported.
 */
struct sctp_sendv_spa {
	uint32_t		sendv_flags;	/* which info is present */
	struct sctp_sndinfo	sendv_sndinfo;	/* send info */
	struct sctp_prinfo	sendv_prinfo;	/* partial reliability info */
	struct sctp_authinfo	sendv_authinfo;	/* authentication info */
};

/*
 * sendv_flags indicating which structs are present.
 */
#define	SCTP_SEND_SNDINFO_VALID		0x1
#define	SCTP_SEND_PRINFO_VALID		0x2
#define	SCTP_SEND_AUTHINFO_VALID	0x4

/*
 * Flags for infotype parameter of sctp_sendv() indicating the type of *info.
 *
 * SCTP_SENDV_SNDINFO: struct sctp_sndinfo is provided.
 * SCTP_SENDV_PRINFO: struct sctp_prinfo is provided.
 * SCTP_SENDV_AUTHINFO: struct sctp_autinfo is provided.
 * SCTP_SENDV_SPA: struct sctp_sendv_spa is provided.
 */
#define	SCTP_SENDV_SNDINFO	SCTP_SNDINFO
#define	SCTP_SENDV_PRINFO	SCTP_PRINFO
#define	SCTP_SENDV_AUTHINFO	SCTP_AUTHINFO
#define	SCTP_SENDV_SPA		SCTP_SPAINFO

/*
 * SCTP notification definitions
 */

/*
 * Notification types
 */
#define	SCTP_ASSOC_CHANGE		1
#define	SCTP_PEER_ADDR_CHANGE		2
#define	SCTP_REMOTE_ERROR		3
/* SCTP_SEND_FAILED is deprecated, use SCTP_SEND_FAILED_EVENT. */
#define	SCTP_SEND_FAILED		4
#define	SCTP_SHUTDOWN_EVENT		5
#define	SCTP_ADAPTATION_INDICATION	6
#define	SCTP_PARTIAL_DELIVERY_EVENT	7
#define	SCTP_AUTHENTICATION_EVENT	8
#define	SCTP_SENDER_DRY_EVENT		9
#define	SCTP_NOTIFICATIONS_STOPPED_EVENT 10

/*
 * The notification SCTP_SEND_FAILED_EVENT uses struct sctp_send_failed_event
 * which makes use of flexible array.  It requires C99 feature.  If C99
 * feature is disabled (via the -xC99=%none flag), the notification cannot be
 * used.
 */
#if !defined(__SUNPRO_C) || defined(__C99FEATURES__)
#define	SCTP_SEND_FAILED_EVENT		11
#endif

/*
 * To receive any ancillary data or notifications, the application can
 * register it's interest by calling the SCTP_EVENTS setsockopt() with
 * the sctp_event_subscribe structure.
 *
 * Deprecated, use SCTP_EVENT option.
 */
struct sctp_event_subscribe {
	uint8_t	sctp_data_io_event;
	uint8_t sctp_association_event;
	uint8_t sctp_address_event;
	uint8_t sctp_send_failure_event;
	uint8_t sctp_peer_error_event;
	uint8_t sctp_shutdown_event;
	uint8_t sctp_partial_delivery_event;
	uint8_t sctp_adaptation_layer_event;
};

/* Association events used in sctp_assoc_change structure */
#define	SCTP_COMM_UP		0
#define	SCTP_COMM_LOST		1
#define	SCTP_RESTART		2
#define	SCTP_SHUTDOWN_COMP	3
#define	SCTP_CANT_STR_ASSOC	4

/*
 * Association flags. This flags is filled in the sac_flags for a SCTP_COMM_UP
 * event if the association supports PR-SCTP.
 */
#define	SCTP_PRSCTP_CAPABLE	0x01

/*
 * sctp_assoc_change notification informs the socket that an SCTP association
 * has either begun or ended.  The identifier for a new association is
 * provided by this notification.
 */
struct sctp_assoc_change {
	uint16_t	sac_type;
	uint16_t	sac_flags;
	uint32_t	sac_length;
	uint16_t	sac_state;
	uint16_t	sac_error;
	uint16_t	sac_outbound_streams;
	uint16_t	sac_inbound_streams;
	sctp_assoc_t	sac_assoc_id;
	/*
	 * The assoc ID can be followed by the ABORT chunk if available.
	 */
};

/*
 * A remote peer may send an Operational Error message to its peer. This
 * message indicates a variety of error conditions on an association.
 * The entire ERROR chunk as it appears on the wire is included in a
 * SCTP_REMOTE_ERROR event.  Refer to the SCTP specification RFC2960
 * and any extensions for a list of possible error formats.
 */
struct sctp_remote_error {
	uint16_t	sre_type;
	uint16_t	sre_flags;
	uint32_t	sre_length;
	uint16_t	sre_error;
	sctp_assoc_t	sre_assoc_id;
	/*
	 * The assoc ID is followed by the actual error chunk.
	 */
};

/*
 * Note:
 *
 * In order to keep the offsets and size of the structure having a
 * struct sockaddr_storage field the same between a 32-bit application
 * and a 64-bit amd64 kernel, we use a #pragma pack(4) for those
 * structures.
 */
#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

/* Address change event state */
#define	SCTP_ADDR_AVAILABLE	0
#define	SCTP_ADDR_UNREACHABLE	1
#define	SCTP_ADDR_REMOVED	2
#define	SCTP_ADDR_ADDED		3
#define	SCTP_ADDR_MADE_PRIM	4

/*
 * When a destination address on a multi-homed peer encounters a change,
 * an interface details event, sctp_paddr_change, is sent to the socket.
 */
struct sctp_paddr_change {
	uint16_t	spc_type;
	uint16_t	spc_flags;
	uint32_t	spc_length;
	struct sockaddr_storage spc_aaddr;
	int		spc_state;
	int		spc_error;
	sctp_assoc_t	spc_assoc_id;
};

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

/* flags used in sctp_send_failed notification. */
#define	SCTP_DATA_UNSENT	1
#define	SCTP_DATA_SENT		2

/*
 * If SCTP cannot deliver a message it may return the message as a
 * notification using the following structure.
 */
struct sctp_send_failed {
	uint16_t	ssf_type;
	uint16_t	ssf_flags;
	uint32_t	ssf_length;
	uint32_t	ssf_error;
	struct sctp_sndrcvinfo ssf_info;
	sctp_assoc_t	ssf_assoc_id;
	/*
	 * The assoc ID is followed by the failed message.
	 */
};

/*
 * When a peer sends a SHUTDOWN, SCTP delivers the sctp_shutdown_event
 * notification to inform the socket user that it should cease sending data.
 */
struct sctp_shutdown_event {
	uint16_t	sse_type;
	uint16_t	sse_flags;
	uint16_t	sse_length;
	sctp_assoc_t	sse_assoc_id;
};

/*
 * When a peer sends an Adaptation Layer Indication parameter, SCTP
 * delivers the sctp_adaptation_event notification to inform the socket
 * user the peer's requested adaptation layer.
 */
struct sctp_adaptation_event {
	uint16_t	sai_type;
	uint16_t	sai_flags;
	uint32_t	sai_length;
	uint32_t	sai_adaptation_ind;
	sctp_assoc_t	sai_assoc_id;
};

/* Possible values in pdapi_indication for sctp_pdapi_event notification. */
#define	SCTP_PARTIAL_DELIVERY_ABORTED	1

/*
 * When a receiver is engaged in a partial delivery of a message the
 * sctp_pdapi_event notification is used to indicate various events.
 */
struct sctp_pdapi_event {
	uint16_t	pdapi_type;
	uint16_t	pdapi_flags;
	uint32_t	pdapi_length;
	uint32_t	pdapi_indication;
	sctp_assoc_t	pdapi_assoc_id;
	uint32_t	pdapi_stream;
	uint32_t	pdapi_seq;
};

/*
 * When the SCTP stack has no more user data to send or retransmit, this
 * notification is given to the user.  Also, at the time when a user app
 * subscribes to this event, if there is no data to be sent or retransmit,
 * the stack will immediately send up this notification.
 */
struct sctp_sender_dry_event {
	uint16_t	sender_dry_type;
	uint16_t	sender_dry_flags;
	uint32_t	sender_dry_length;
	sctp_assoc_t	sender_dry_assoc_id;
};

#if !defined(__SUNPRO_C) || defined(__C99FEATURES__)
/*
 * If SCTP cannot deliver a message, it can return back the message as a
 * notification if the SCTP_SEND_FAILED_EVENT event is enabled.
 */
struct sctp_send_failed_event {
	uint16_t		ssfe_type;
	uint16_t		ssfe_flags;
	uint32_t		ssfe_length;
	uint32_t		ssfe_error;
	struct sctp_sndinfo	ssfe_info;
	sctp_assoc_t		ssfe_assoc_id;
	uint8_t			ssfe_data[_SCTP_FLEX_ARRAY];
};
#endif

/*
 * The sctp_notification structure is defined as the union of all
 * notification types defined above.
 */
union sctp_notification {
	struct sctp_tlv {
		uint16_t		sn_type; /* Notification type. */
		uint16_t		sn_flags;
		uint32_t		sn_length;
	} sn_header;
	struct sctp_assoc_change	sn_assoc_change;
	struct sctp_paddr_change	sn_paddr_change;
	struct sctp_remote_error	sn_remote_error;
	struct sctp_send_failed		sn_send_failed;
	struct sctp_shutdown_event	sn_shutdown_event;
	struct sctp_adaptation_event	sn_adaptation_event;
	struct sctp_pdapi_event		sn_pdapi_event;
	struct sctp_sender_dry_event	sn_sender_dry_event;
#if !defined(__SUNPRO_C) || defined(__C99FEATURES__)
	struct sctp_send_failed_event	sn_send_failed_event;
#endif
};

/*
 * sctp_opt_info() option definitions
 */

/*
 * The protocol parameters used to initialize and bound retransmission
 * timeout (RTO) are tunable.  See RFC2960 for more information on
 * how these parameters are used in RTO calculation.
 *
 * The sctp_rtoinfo structure is used to access and modify these
 * parameters.
 */
struct sctp_rtoinfo {
	sctp_assoc_t	srto_assoc_id;
	uint32_t	srto_initial;
	uint32_t	srto_max;
	uint32_t	srto_min;
};

/*
 * The sctp_assocparams option is used to both examine and set various
 * association and endpoint parameters.  See RFC2960 for more information
 * on how this parameter is used.  The peer address parameter is ignored
 * for one-to-one style socket.
 */
struct sctp_assocparams {
	sctp_assoc_t	sasoc_assoc_id;
	uint16_t	sasoc_asocmaxrxt;
	uint16_t	sasoc_number_peer_destinations;
	uint32_t	sasoc_peer_rwnd;
	uint32_t	sasoc_local_rwnd;
	uint32_t	sasoc_cookie_life;
};

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

/*
 * sctp_paddrinfo reachability state.
 *
 * SCTP_INACTIVE: The peer address is not reachable so SCTP does not use it.
 * SCTP_ACTIVE: The peer address is reachable and SCTP can use it.
 * SCTP_UNCONFIRMED: The peer address is never confirmed to be working, SCTP
 *                   does not use it.
 */
#define	SCTP_INACTIVE		1
#define	SCTP_ACTIVE		2
#define	SCTP_UNCONFIRMED	3

/*
 * Applications can retrieve information about a specific peer address
 * of an association, including its reachability state, congestion
 * window, and retransmission timer values.  This information is
 * read-only. The sctp_paddrinfo structure is used to access this
 * information:
 */
struct sctp_paddrinfo {
	sctp_assoc_t	spinfo_assoc_id;
	struct sockaddr_storage spinfo_address;
	int32_t		spinfo_state;
	uint32_t	spinfo_cwnd;
	uint32_t	spinfo_srtt;
	uint32_t	spinfo_rto;
	uint32_t	spinfo_mtu;
};

/*
 * Applications can enable or disable heartbeats for any peer address of
 * an association, modify an address's heartbeat interval, force a
 * heartbeat to be sent immediately, and adjust the address's maximum
 * number of retransmissions sent before an address is considered
 * unreachable.  The sctp_paddrparams structure is used to access and modify
 * an address' parameters.
 */
struct sctp_paddrparams {
	sctp_assoc_t		spp_assoc_id;
	struct sockaddr_storage	spp_address;
	uint32_t		spp_hbinterval;
	uint16_t		spp_pathmaxrxt;
	uint32_t		spp_pathmtu;
	uint32_t		spp_flags;
	uint32_t		spp_ipv6_flowlabel;
	uint8_t			spp_ipv4_tos;
};

/* Flags used in spp_flags field. */
#define	SPP_HB_ENABLE		0x01
#define	SPP_HB_DISABLE		0x02
#define	SPP_HB_DEMAND		0x04
#define	SPP_HB_TIME_IS_ZERO	0x08
#define	SPP_PMTUD_ENABLE	0x10
#define	SPP_PMTUD_DISABLE	0x20
#define	SPP_IPV6_FLOWLABEL	0x40
#define	SPP_IPV4_TOS		0x80

/*
 * Old definition of sctp_paddrparams.  Kept in kernel for binary backward
 * compatibility.
 */
#ifdef _KERNEL
struct _sctp_paddrparams {
	sctp_assoc_t		spp_assoc_id;
	struct sockaddr_storage	spp_address;
	uint32_t		spp_hbinterval;
	uint16_t		spp_pathmaxrxt;
};
#endif

/*
 * A socket user can request that the peer mark the enclosed address as the
 * association's primary.  The enclosed address must be one of the
 * association's locally bound addresses. The sctp_setpeerprim structure is
 * used to make such request.
 */
struct sctp_setpeerprim {
	sctp_assoc_t		sspp_assoc_id;
	struct sockaddr_storage	sspp_addr;
};

/*
 * A socket user can request that the local SCTP stack use the enclosed peer
 * address as the association primary.  The enclosed address must be one of
 * the association peer's addresses.  The sctp_setprim structure is used to
 * make such request.
 */
struct sctp_setprim {
	sctp_assoc_t		ssp_assoc_id;
	struct sockaddr_storage	ssp_addr;
};

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

/* SCTP association states */
typedef enum {
	SCTPS_IDLE		= -5,	/* idle (opened, but not bound) */
	SCTPS_BOUND		= -4,	/* bound, ready to connect or accept */
	SCTPS_LISTEN		= -3,	/* listening for connection */
	SCTPS_COOKIE_WAIT	= -2,
	SCTPS_COOKIE_ECHOED	= -1,
/* states < SCTPS_ESTABLISHED are those where connections not established */
	SCTPS_ESTABLISHED	= 0,	/* established */
	SCTPS_SHUTDOWN_PENDING	= 1,
	SCTPS_SHUTDOWN_SENT	= 2,
	SCTPS_SHUTDOWN_RECEIVED	= 3,
	SCTPS_SHUTDOWN_ACK_SENT	= 4
} sctp_state_t;

/*
 * Applications can retrieve current status information about an
 * association, including association state, peer receiver window size,
 * number of unacked data chunks, and number of data chunks pending
 * receipt.  This information is read-only.  The sctp_status structure is
 * used to access this information:
 */
struct sctp_status {
	sctp_assoc_t		sstat_assoc_id;
	int32_t			sstat_state;
	uint32_t		sstat_rwnd;
	uint16_t		sstat_unackdata;
	uint16_t		sstat_penddata;
	uint16_t		sstat_instrms;
	uint16_t		sstat_outstrms;
	uint32_t		sstat_fragmentation_point;
	struct sctp_paddrinfo	sstat_primary;
};

/* Possible values for sstat_state */
#define	SCTP_CLOSED		SCTPS_IDLE
#define	SCTP_BOUND		SCTPS_BOUND
#define	SCTP_LISTEN		SCTPS_LISTEN
#define	SCTP_COOKIE_WAIT	SCTPS_COOKIE_WAIT
#define	SCTP_COOKIE_ECHOED	SCTPS_COOKIE_ECHOED
#define	SCTP_ESTABLISHED	SCTPS_ESTABLISHED
#define	SCTP_SHUTDOWN_PENDING	SCTPS_SHUTDOWN_PENDING
#define	SCTP_SHUTDOWN_SENT	SCTPS_SHUTDOWN_SENT
#define	SCTP_SHUTDOWN_RECEIVED	SCTPS_SHUTDOWN_RECEIVED
#define	SCTP_SHUTDOWN_ACK_SENT	SCTPS_SHUTDOWN_ACK_SENT

/*
 * A socket user can request that the local endpoint set the specified
 * Adaptation Layer Indication parameter for all future INIT and INIT-ACK
 * exchanges.  The sctp_setadaptation structure is used to make such request.
 */
struct sctp_setadaptation {
	uint32_t   ssb_adaptation_ind;
};

/*
 * A socket user request reads local per endpoint association stats.
 * All stats are counts except sas_maxrto, which is the max value
 * since the last user request for stats on this endpoint.
 */
typedef struct sctp_assoc_stats {
	uint64_t	sas_rtxchunks; /* Retransmitted Chunks */
	uint64_t	sas_gapcnt; /* Gap Acknowledgements Received */
	uint64_t	sas_maxrto; /* Maximum Observed RTO this period */
	uint64_t	sas_outseqtsns; /* TSN received > next expected */
	uint64_t	sas_osacks; /* SACKs sent */
	uint64_t	sas_isacks; /* SACKs received */
	uint64_t	sas_octrlchunks; /* Control chunks sent - no dups */
	uint64_t	sas_ictrlchunks; /* Control chunks received - no dups */
	uint64_t	sas_oodchunks; /* Ordered data chunks sent */
	uint64_t	sas_iodchunks; /* Ordered data chunks received */
	uint64_t	sas_ouodchunks; /* Unordered data chunks sent */
	uint64_t	sas_iuodchunks; /* Unordered data chunks received */
	uint64_t	sas_idupchunks; /* Dups received (ordered+unordered) */
} sctp_assoc_stats_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

/* SCTP_CONGESTION variable length structure */
typedef struct sctp_congestion {
	sctp_assoc_t		sc_assoc_id;	/* association ID or 0 */
	struct sockaddr_storage	sc_address;	/* peer address (optional) */
	char			sc_name[1];
} sctp_congestion_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

/*
 * Private ioctl option structure
 */
struct sctpopt {
	sctp_assoc_t	sopt_aid;
	int		sopt_name;
	uint_t		sopt_len;
	caddr_t		sopt_val;
};

struct sctp_ctx {
	sctp_assoc_t	sctx_aid;
	int		sctx_addr_len;
	int		sctx_addr_cnt;
	caddr_t		sctx_addrs;
};

struct sctp_sendv {
	sctp_assoc_t		ssndv_aid;
	const struct iovec	*ssndv_iov;
	int			ssndv_iovcnt;
	caddr_t			ssndv_addr;
	int			ssndv_addr_len;
	caddr_t			ssndv_info;
	int			ssndv_info_len;
	uint_t			ssndv_info_type;
	int			ssndv_flags;
};

#if defined(_SYSCALL32)
struct sctpopt32 {
	sctp_assoc32_t	sopt_aid;
	int32_t		sopt_name;
	uint32_t	sopt_len;
	caddr32_t	sopt_val;
};

struct sctp_ctx32 {
	sctp_assoc_t	sctx_aid;
	int32_t		sctx_addr_len;
	int32_t		sctx_addr_cnt;
	caddr32_t	sctx_addrs;
};

struct sctp_sendv32 {
	sctp_assoc32_t		ssndv_aid;
	caddr32_t		ssndv_iov;
	int32_t			ssndv_iovcnt;
	caddr32_t		ssndv_addr;
	int32_t			ssndv_addr_len;
	caddr32_t		ssndv_info;
	int32_t			ssndv_info_len;
	uint32_t		ssndv_info_type;
	int32_t			ssndv_flags;
};
#endif	/* _SYSCALL32 */

/* Forward Cumulative TSN chunk entry. */
typedef struct ftsn_entry_s {
	uint16_t	ftsn_sid;
	uint16_t	ftsn_ssn;
} ftsn_entry_t;

/*
 * New socket functions for SCTP
 */

/* sctp_bindx() operations. */
#define	SCTP_BINDX_ADD_ADDR	1
#define	SCTP_BINDX_REM_ADDR	2

#if !defined(_KERNEL) || defined(_BOOT)
#ifdef	__STDC__
extern int sctp_bindx(int, void *, int, int);
extern int sctp_connectx(int, struct sockaddr *, int, sctp_assoc_t *);
extern void sctp_freeladdrs(void *);
extern void sctp_freepaddrs(void *);
extern int sctp_getladdrs(int, sctp_assoc_t, void **);
extern int sctp_getpaddrs(int, sctp_assoc_t, void **);
extern int sctp_opt_info(int, sctp_assoc_t, int, void *, socklen_t *);
extern int sctp_peeloff(int, sctp_assoc_t);
extern ssize_t sctp_recvmsg(int, void *, size_t, struct sockaddr *,
    socklen_t *, struct sctp_sndrcvinfo *, int *msg_flags);
extern	ssize_t sctp_recvv(int, const struct iovec *, int, struct sockaddr *,
    socklen_t *, void *, socklen_t *, unsigned int *, int *);
extern ssize_t sctp_send(int, const void *, size_t,
    const struct sctp_sndrcvinfo *, int);
extern ssize_t sctp_sendmsg(int, const void *, size_t, const struct sockaddr *,
    socklen_t, uint32_t, uint32_t, uint16_t, uint32_t, uint32_t);
extern ssize_t sctp_sendv(int, const struct iovec *, int, struct sockaddr *,
    int, void *, socklen_t, unsigned int, int flags);
#else	/* __STDC__ */
extern int sctp_bindx();
extern int sctp_connectx();
extern void sctp_freeladdrs();
extern void sctp_freepaddrs();
extern int sctp_getladdrs();
extern int sctp_getpaddrs();
extern int sctp_opt_info();
extern int sctp_peeloff();
extern ssize_t sctp_recvmsg();
extern ssize_t sctp_recvv();
extern ssize_t sctp_send();
extern ssize_t sctp_sendmsg();
extern ssize_t sctp_sendv();
#endif	/* __STDC__ */
#endif	/* !defined(_KERNEL) || defined(_BOOT) */


/*
 * SCTP protocol related elements.
 */

/* All SCTP chunks and parameters are 32-bit aligned */
#define	SCTP_ALIGN	4

/* Given a length l, return the number of padding bytes needed. */
#define	SCTP_PAD_LEN(l)	((((l) ^ (SCTP_ALIGN - 1)) + 1) & (SCTP_ALIGN - 1))

/*
 * SCTP association optional parameter handling. The top two bits
 * of the parameter type define how this and further parameters in
 * the received chunk should be handled.
 */
#define	SCTP_UNREC_PARAM_MASK	0xc000
/* Continue processing parameters after an unrecognized optional param? */
#define	SCTP_CONT_PROC_PARAMS	0x8000
/* Report this unreconized optional parameter or silently ignore it? */
#define	SCTP_REPORT_THIS_PARAM	0x4000

/*
 * Data chunk bit manipulations
 */
#define	SCTP_DATA_EBIT	0x01
#define	SCTP_TBIT	0x01
#define	SCTP_DATA_BBIT	0x02
#define	SCTP_DATA_UBIT	0x04

#define	SCTP_DATA_GET_BBIT(sdc)	((sdc)->sdh_flags & SCTP_DATA_BBIT)
#define	SCTP_GET_TBIT(cp)	((cp)->sch_flags & SCTP_TBIT)
#define	SCTP_DATA_GET_EBIT(sdc)	((sdc)->sdh_flags & SCTP_DATA_EBIT)
#define	SCTP_DATA_GET_UBIT(sdc)	((sdc)->sdh_flags & SCTP_DATA_UBIT)

#define	SCTP_DATA_SET_BBIT(sdc)	((sdc)->sdh_flags |= SCTP_DATA_BBIT)
#define	SCTP_SET_TBIT(cp)	((cp)->sch_flags |= SCTP_TBIT)
#define	SCTP_DATA_SET_EBIT(sdc)	((sdc)->sdh_flags |= SCTP_DATA_EBIT)
#define	SCTP_DATA_SET_UBIT(sdc)	((sdc)->sdh_flags |=  SCTP_DATA_UBIT)

/* SCTP common header */
typedef struct sctp_hdr {
	uint16_t	sh_sport;
	uint16_t	sh_dport;
	uint32_t	sh_verf;
	uint32_t	sh_chksum;
} sctp_hdr_t;

/* Chunk IDs */
typedef enum {
	CHUNK_DATA,
	CHUNK_INIT,
	CHUNK_INIT_ACK,
	CHUNK_SACK,
	CHUNK_HEARTBEAT,
	CHUNK_HEARTBEAT_ACK,
	CHUNK_ABORT,
	CHUNK_SHUTDOWN,
	CHUNK_SHUTDOWN_ACK,
	CHUNK_ERROR,
	CHUNK_COOKIE,
	CHUNK_COOKIE_ACK,
	CHUNK_ECNE,
	CHUNK_CWR,
	CHUNK_SHUTDOWN_COMPLETE,
	CHUNK_ASCONF_ACK = 128,
	CHUNK_FORWARD_TSN = 192,
	CHUNK_ASCONF = 193
} sctp_chunk_id_t;

/* Common chunk header */
typedef struct sctp_chunk_hdr {
	uint8_t		sch_id;
	uint8_t		sch_flags;
	uint16_t	sch_len;
} sctp_chunk_hdr_t;

/* INIT chunk data definition */
typedef struct sctp_init_chunk {
	uint32_t	sic_inittag;
	uint32_t	sic_a_rwnd;
	uint16_t	sic_outstr;
	uint16_t	sic_instr;
	uint32_t	sic_inittsn;
} sctp_init_chunk_t;

/* SCTP DATA chunk */
typedef struct sctp_data_chunk {
	uint32_t	sdc_tsn;
	uint16_t	sdc_sid;
	uint16_t	sdc_ssn;
	uint32_t	sdc_payload_id;
} sctp_data_chunk_t;

/* sctp_data_hdr includes the SCTP chunk hdr and the DATA chunk */
typedef struct sctp_data_hdr {
	sctp_chunk_hdr_t	sdh_chdr;
	sctp_data_chunk_t	sdh_data;
#define	sdh_id		sdh_chdr.sch_id
#define	sdh_flags	sdh_chdr.sch_flags
#define	sdh_len		sdh_chdr.sch_len
#define	sdh_tsn		sdh_data.sdc_tsn
#define	sdh_sid		sdh_data.sdc_sid
#define	sdh_ssn		sdh_data.sdc_ssn
#define	sdh_payload_id	sdh_data.sdc_payload_id
} sctp_data_hdr_t;

typedef struct sctp_sack_chunk {
	uint32_t	ssc_cumtsn;
	uint32_t	ssc_a_rwnd;
	uint16_t	ssc_numfrags;
	uint16_t	ssc_numdups;
} sctp_sack_chunk_t;

typedef struct sctp_sack_frag {
	uint16_t	ssf_start;
	uint16_t	ssf_end;
} sctp_sack_frag_t;

/* Parameter types */
#define	PARM_UNKNOWN		0
#define	PARM_HBINFO		1
#define	PARM_ADDR4		5
#define	PARM_ADDR6		6
#define	PARM_COOKIE		7
#define	PARM_UNRECOGNIZED	8
#define	PARM_COOKIE_PRESERVE	9
#define	PARM_ADDR_HOST_NAME	11
#define	PARM_SUPP_ADDRS		12
#define	PARM_ECN		0x8000
#define	PARM_ECN_CAPABLE	PARM_ECN
#define	PARM_FORWARD_TSN	0xc000
#define	PARM_ADD_IP		0xc001
#define	PARM_DEL_IP		0xc002
#define	PARM_ERROR_IND		0xc003
#define	PARM_ASCONF_ERROR	PARM_ERROR_IND
#define	PARM_SET_PRIMARY	0xc004
#define	PARM_PRIMARY_ADDR	PARM_SET_PRIMARY
#define	PARM_SUCCESS		0xc005
#define	PARM_ASCONF_SUCCESS	PARM_SUCCESS
#define	PARM_ADAPT_LAYER_IND	0xc006


/* Lengths from SCTP spec */
#define	PARM_ADDR4_LEN		8
#define	PARM_ADDR6_LEN		20

/* Parameter header */
typedef struct sctp_parm_hdr {
	uint16_t	sph_type;
	uint16_t	sph_len;
} sctp_parm_hdr_t;

/*
 * The following extend sctp_parm_hdr_t
 * with cause-specfic content used to fill
 * CAUSE blocks in ABORT or ERROR chunks.
 * The overall size of the CAUSE block will
 * be sizeof (sctp_parm_hdr_t) plus the size
 * of the extended cause structure,
 */

/*
 * Invalid stream-identifier extended cause.
 * SCTP_ERR_BAD_SID
 */
typedef struct sctp_bsc {
	uint16_t	bsc_sid;
	uint16_t	bsc_pad; /* RESV = 0 */
} sctp_bsc_t;

/*
 * Missing parameter extended cause, currently
 * only one missing parameter is supported.
 * SCTP_ERR_MISSING_PARM
 */
typedef struct sctp_mpc {
	uint32_t	mpc_num;
	uint16_t	mpc_param;
	uint16_t	mpc_pad;
} sctp_mpc_t;

/* Error causes */
#define	SCTP_ERR_UNKNOWN		0
#define	SCTP_ERR_BAD_SID		1
#define	SCTP_ERR_MISSING_PARM		2
#define	SCTP_ERR_STALE_COOKIE		3
#define	SCTP_ERR_NO_RESOURCES		4
#define	SCTP_ERR_BAD_ADDR		5
#define	SCTP_ERR_UNREC_CHUNK		6
#define	SCTP_ERR_BAD_MANDPARM		7
#define	SCTP_ERR_UNREC_PARM		8
#define	SCTP_ERR_NO_USR_DATA		9
#define	SCTP_ERR_COOKIE_SHUT		10
#define	SCTP_ERR_RESTART_NEW_ADDRS	11
#define	SCTP_ERR_USER_ABORT		12
#define	SCTP_ERR_DELETE_LASTADDR	256
#define	SCTP_ERR_RESOURCE_SHORTAGE	257
#define	SCTP_ERR_DELETE_SRCADDR		258
#define	SCTP_ERR_AUTH_ERR		260

/*
 * Extensions
 */

/* Extended Chunk Types */
#define	CHUNK_ASCONF		0xc1
#define	CHUNK_ASCONF_ACK	0x80

/* Extension Error Causes */
#define	SCTP_ERR_DEL_LAST_ADDR	0x0100
#define	SCTP_ERR_RES_SHORTAGE	0x0101
#define	SCTP_ERR_DEL_SRC_ADDR	0x0102
#define	SCTP_ERR_ILLEGAL_ACK	0x0103
#define	SCTP_ERR_UNAUTHORIZED	0x0104

typedef struct sctp_addip4 {
	sctp_parm_hdr_t		sad4_addip_ph;
	uint32_t		asconf_req_cid;
	sctp_parm_hdr_t		sad4_addr4_ph;
	ipaddr_t		sad4_addr;
} sctp_addip4_t;

typedef struct sctp_addip6 {
	sctp_parm_hdr_t		sad6_addip_ph;
	uint32_t		asconf_req_cid;
	sctp_parm_hdr_t		sad6_addr6_ph;
	in6_addr_t		sad6_addr;
} sctp_addip6_t;

#ifdef __cplusplus
}
#endif

#endif	/* _NETINET_SCTP_H */
