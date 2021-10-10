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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IKEDOOR_H
#define	_IKEDOOR_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <limits.h>
#include <sys/sysmacros.h>
#include <paths.h>
#include <net/pfkeyv2.h>
#include <door.h>

/*
 * This version number is intended to stop the calling process from
 * getting confused if a structure is changed and a mismatch occurs.
 * This should be incremented each time a structure is changed.
 */

/*
 * The IKE process may be a 64-bit process, but ikeadm or any other IKE
 * door consumer does not have to be.  We need to be strict ala. PF_KEY or
 * any on-the-wire-protocol with respect to structure fields offsets and
 * alignment.  Please make sure all structures are the same size on both
 * 64-bit and 32-bit execution environments (or even other ones), and that
 * apart from trivial 4-byte enums or base headers, that all structures are
 * multiples of 8-bytes (64-bits).
 */
#define	DOORVER 4
#define	DOORNM	_PATH_SYSVOL "/ike_door"


typedef enum {
	IKE_SVC_GET_DBG,
	IKE_SVC_SET_DBG,

	IKE_SVC_GET_PRIV,
	IKE_SVC_SET_PRIV,

	IKE_SVC_GET_STATS,

	IKE_SVC_GET_P1,
	IKE_SVC_DEL_P1,
	IKE_SVC_DUMP_P1S,
	IKE_SVC_FLUSH_P1S,

	IKE_SVC_GET_RULE,
	IKE_SVC_NEW_RULE,
	IKE_SVC_DEL_RULE,
	IKE_SVC_DUMP_RULES,
	IKE_SVC_READ_RULES,
	IKE_SVC_WRITE_RULES,

	IKE_SVC_GET_PS,
	IKE_SVC_NEW_PS,
	IKE_SVC_DEL_PS,
	IKE_SVC_DUMP_PS,
	IKE_SVC_READ_PS,
	IKE_SVC_WRITE_PS,

	IKE_SVC_DBG_RBDUMP,

	IKE_SVC_GET_DEFS,

	IKE_SVC_SET_PIN,
	IKE_SVC_DEL_PIN,

	IKE_SVC_DUMP_CERTCACHE,
	IKE_SVC_FLUSH_CERTCACHE,

	IKE_SVC_DUMP_GROUPS,
	IKE_SVC_DUMP_ENCRALGS,
	IKE_SVC_DUMP_AUTHALGS,

	IKE_SVC_ERROR
} ike_svccmd_t;

/* DPD status */

typedef enum dpd_status {
	DPD_NOT_INITIATED = 0,
	DPD_IN_PROGRESS,
	DPD_SUCCESSFUL,
	DPD_FAILURE
} dpd_status_t;

#define	IKE_SVC_MAX	IKE_SVC_ERROR


/*
 * Support structures/defines
 */

#define	IKEDOORROUNDUP(i)   P2ROUNDUP((i), sizeof (uint64_t))

/*
 * Debug categories.  The debug level is a bitmask made up of
 * flags indicating the desired categories; only 31 bits are
 * available, as the highest-order bit designates an invalid
 * setting.
 */
#define	D_INVALID	0x80000000

#define	D_CERT		0x00000001	/* certificate management */
#define	D_KEY		0x00000002	/* key management */
#define	D_OP		0x00000004	/* operational: config, init, mem */
#define	D_P1		0x00000008	/* phase 1 negotiation */
#define	D_P2		0x00000010	/* phase 2 negotiation */
#define	D_PFKEY		0x00000020	/* pf key interface */
#define	D_POL		0x00000040	/* policy management */
#define	D_PROP		0x00000080	/* proposal construction */
#define	D_DOOR		0x00000100	/* door server */
#define	D_CONFIG	0x00000200	/* config file processing */
#define	D_LABEL		0x00000400	/* MAC labels */

#define	D_HIGHBIT	0x00000400
#define	D_ALL		0x000007ff

/*
 * Access privilege levels: define level of access to keying information.
 * The privileges granted at each level is a superset of the privileges
 * granted at all lower levels.
 *
 * The door operations which require special privileges are:
 *
 *	- receiving keying material for SAs and preshared key entries
 *	  IKE_PRIV_KEYMAT must be set for this.
 *
 *	- get/dump/new/delete/read/write preshared keys
 *	  IKE_PRIV_KEYMAT or IKE_PRIV_MODKEYS must be set to do this.
 *	  If IKE_PRIV_MODKEYS is set, the information returned for a
 *	  get/dump request will not include the actual key; in order
 *	  to get the key itself, IKE_PRIV_KEYMAT must be set.
 *
 *	- modifying the privilege level: the daemon's privilege level
 *	  is set when the daemon is started; the level may only be
 *	  lowered via the door interface.
 *
 * All other operations are allowed at any privilege level.
 */
#define	IKE_PRIV_MINIMUM	0
#define	IKE_PRIV_MODKEYS	1
#define	IKE_PRIV_KEYMAT		2
#define	IKE_PRIV_MAXIMUM	2

/* global ike stats formatting structure */
typedef struct {
	uint32_t	st_init_p1_current;
	uint32_t	st_resp_p1_current;
	uint32_t	st_init_p1_total;
	uint32_t	st_resp_p1_total;
	uint32_t	st_init_p1_attempts;
	uint32_t	st_resp_p1_attempts;
	uint32_t	st_init_p1_noresp;   /* failed; no response from peer */
	uint32_t	st_init_p1_respfail; /* failed, but peer responded */
	uint32_t	st_resp_p1_fail;
	uint32_t	st_reserved;
	char		st_pkcs11_libname[PATH_MAX];
} ike_stats_t;

/* structure used to pass default values used by in.iked back to ikeadm */
typedef struct {
	uint32_t	rule_p1_lifetime_secs;
	uint32_t	rule_p1_minlife;
	uint32_t	rule_p1_nonce_len;
	uint32_t	rule_p2_lifetime_secs;
	uint32_t	rule_p2_softlife_secs;
	uint32_t	rule_p2_idletime_secs;
	uint32_t	sys_p2_lifetime_secs;
	uint32_t	sys_p2_softlife_secs;
	uint32_t	sys_p2_idletime_secs;
	uint32_t	rule_p2_lifetime_kb;
	uint32_t	rule_p2_softlife_kb;
	uint32_t	sys_p2_lifetime_bytes;
	uint32_t	sys_p2_softlife_bytes;
	uint32_t	rule_p2_minlife_hard_secs;
	uint32_t	rule_p2_minlife_soft_secs;
	uint32_t	rule_p2_minlife_idle_secs;
	uint32_t	rule_p2_minlife_hard_kb;
	uint32_t	rule_p2_minlife_soft_kb;
	uint32_t	rule_p2_maxlife_secs;
	uint32_t	rule_p2_maxlife_kb;
	uint32_t	rule_p2_nonce_len;
	uint32_t	rule_p2_pfs;
	uint32_t	rule_p2_mindiff_secs;
	uint32_t	rule_p2_mindiff_kb;
	uint32_t	conversion_factor;	/* for secs to kbytes */
	uint32_t	rule_max_certs;
	uint32_t	rule_ike_port;
	uint32_t	rule_natt_port;
	uint32_t	defaults_reserved;	/* For 64-bit alignment. */
} ike_defaults_t;

/* data formatting structures for P1 SA dumps */
typedef struct {
	struct sockaddr_storage	loc_addr;
	struct sockaddr_storage	rem_addr;
#define	beg_iprange	loc_addr
#define	end_iprange	rem_addr
} ike_addr_pr_t;

typedef struct {
	uint64_t	cky_i;
	uint64_t	cky_r;
} ike_cky_pr_t;

typedef struct {
	ike_cky_pr_t	p1hdr_cookies;
	uint8_t		p1hdr_major;
	uint8_t		p1hdr_minor;
	uint8_t		p1hdr_xchg;
	uint8_t		p1hdr_isinit;
	uint32_t	p1hdr_state;
	boolean_t	p1hdr_support_dpd;
	dpd_status_t	p1hdr_dpd_state;
	uint64_t	p1hdr_dpd_time;
} ike_p1_hdr_t;

/* values for p1hdr_xchg (aligned with RFC2408, section 3.1) */
#define	IKE_XCHG_NONE			0
#define	IKE_XCHG_BASE			1
#define	IKE_XCHG_IDENTITY_PROTECT	2
#define	IKE_XCHG_AUTH_ONLY		3
#define	IKE_XCHG_AGGRESSIVE		4
/* following not from RFC; used only for preshared key definitions */
#define	IKE_XCHG_IP_AND_AGGR		240
/* also not from RFC; used as wildcard */
#define	IKE_XCHG_ANY			256

/* values for p1hdr_state */
#define	IKE_SA_STATE_INVALID	0
#define	IKE_SA_STATE_INIT	1
#define	IKE_SA_STATE_SENT_SA	2
#define	IKE_SA_STATE_SENT_KE	3
#define	IKE_SA_STATE_SENT_LAST	4
#define	IKE_SA_STATE_DONE	5
#define	IKE_SA_STATE_DELETED	6

typedef struct {
	uint16_t	p1xf_dh_group;
	uint16_t	p1xf_encr_alg;
	uint16_t	p1xf_encr_low_bits;
	uint16_t	p1xf_encr_high_bits;
	uint16_t	p1xf_auth_alg;
	uint16_t	p1xf_auth_meth;
	uint16_t	p1xf_prf;
	uint16_t	p1xf_pfs;
	uint32_t	p1xf_max_secs;
	uint32_t	p1xf_max_kbytes;
	uint32_t	p1xf_max_keyuses;
	uint32_t	p1xf_reserved;	/* Alignment to 64-bit. */
} ike_p1_xform_t;

/* values for p1xf_dh_group (aligned with RFC2409, Appendix A) */
#define	IKE_GRP_DESC_MODP_768	1
#define	IKE_GRP_DESC_MODP_1024	2
#define	IKE_GRP_DESC_EC2N_155	3
#define	IKE_GRP_DESC_EC2N_185	4
/* values for p1xf_dh_group (aligned with RFC3526) */
#define	IKE_GRP_DESC_MODP_1536		5
#define	IKE_GRP_DESC_MODP_2048		14
#define	IKE_GRP_DESC_MODP_3072		15
#define	IKE_GRP_DESC_MODP_4096		16
#define	IKE_GRP_DESC_MODP_6144		17
#define	IKE_GRP_DESC_MODP_8192		18
#define	IKE_GRP_DESC_ECP_256		19
#define	IKE_GRP_DESC_ECP_384		20
#define	IKE_GRP_DESC_ECP_521		21
/* values for p1xf_dh_group (aligned with RFC5114) */
#define	IKE_GRP_DESC_MODP_1024_160 	22
#define	IKE_GRP_DESC_MODP_2048_224 	23
#define	IKE_GRP_DESC_MODP_2048_256 	24
#define	IKE_GRP_DESC_ECP_192		25
#define	IKE_GRP_DESC_ECP_224		26

/* values for p1xf_auth_meth (aligned with RFC2409, Appendix A) */
#define	IKE_AUTH_METH_PRE_SHARED_KEY	1
#define	IKE_AUTH_METH_DSS_SIG		2
#define	IKE_AUTH_METH_RSA_SIG		3
#define	IKE_AUTH_METH_RSA_ENCR		4
#define	IKE_AUTH_METH_RSA_ENCR_REVISED	5

/* values for p1xf_prf */
#define	IKE_PRF_NONE		0
#define	IKE_PRF_HMAC_MD5	1
#define	IKE_PRF_HMAC_SHA1	2
#define	IKE_PRF_HMAC_SHA256	5
#define	IKE_PRF_HMAC_SHA384	6
#define	IKE_PRF_HMAC_SHA512	7

typedef struct {
	/*
	 * NOTE: the new and del counters count the actual number of SAs,
	 * not the number of "suites", as defined in the ike monitoring
	 * mib draft; we do this because we don't have a good way of
	 * tracking the deletion of entire suites (we're notified of
	 * deleted qm sas individually).
	 */
	uint32_t	p1stat_new_qm_sas;
	uint32_t	p1stat_del_qm_sas;
	uint64_t	p1stat_start;
	uint32_t	p1stat_kbytes;
	uint32_t	p1stat_keyuses;
} ike_p1_stats_t;

typedef struct {
	uint32_t	p1err_decrypt;
	uint32_t	p1err_hash;
	uint32_t	p1err_otherrx;
	uint32_t	p1err_tx;
} ike_p1_errors_t;

typedef struct {
	uint32_t	p1key_type;
	uint32_t	p1key_len;
	/*
	 * followed by (len - sizeof (ike_p1_key_t)) bytes of hex data,
	 * 64-bit aligned (pad bytes are added at the end, if necessary,
	 * and NOT INCLUDED in the len value, which reflects the actual
	 * key size).
	 */
} ike_p1_key_t;

/* key info types for ike_p1_key_t struct */
#define	IKE_KEY_PRESHARED	1
#define	IKE_KEY_SKEYID		2
#define	IKE_KEY_SKEYID_D	3
#define	IKE_KEY_SKEYID_A	4
#define	IKE_KEY_SKEYID_E	5
#define	IKE_KEY_ENCR		6
#define	IKE_KEY_IV		7

typedef struct {
	ike_p1_hdr_t	p1sa_hdr;
	ike_p1_xform_t	p1sa_xform;
	ike_addr_pr_t	p1sa_ipaddrs;
	uint16_t	p1sa_stat_off;
	uint16_t	p1sa_stat_len;
	uint16_t	p1sa_error_off;
	uint16_t	p1sa_error_len;
	uint16_t	p1sa_localid_off;
	uint16_t	p1sa_localid_len;
	uint16_t	p1sa_remoteid_off;
	uint16_t	p1sa_remoteid_len;
	uint16_t	p1sa_key_off;
	uint16_t	p1sa_key_len;
	uint32_t	p1sa_reserved;
	/*
	 * variable-length structures will be included here, as
	 * indicated by offset/length fields.
	 * stats and errors will be formatted as ike_p1_stats_t and
	 * ike_p1_errors_t, respectively.
	 * key info will be formatted as a series of p1_key_t structs.
	 * local/remote ids will be formatted as sadb_ident_t structs.
	 */
} ike_p1_sa_t;


#define	MAX_LABEL_LEN	256


/* data formatting structure for policy (rule) dumps */

typedef struct {
	char		rule_label[MAX_LABEL_LEN];
	uint32_t	rule_kmcookie;
	uint16_t	rule_ike_mode;
	uint16_t	rule_local_idtype;	/* SADB_IDENTTYPE_* value */
	uint32_t	rule_p1_nonce_len;
	uint32_t	rule_p2_nonce_len;
	uint32_t	rule_p2_pfs;
	uint32_t	rule_p2_lifetime_secs;
	uint32_t	rule_p2_softlife_secs;
	uint32_t	rule_p2_idletime_secs;
	uint32_t	rule_p2_lifetime_kb;
	uint32_t	rule_p2_softlife_kb;
	uint16_t	rule_xform_cnt;
	uint16_t	rule_xform_off;
	uint16_t	rule_locip_cnt;
	uint16_t	rule_locip_off;
	uint16_t	rule_remip_cnt;
	uint16_t	rule_remip_off;
	uint16_t	rule_locid_inclcnt;
	uint16_t	rule_locid_exclcnt;
	uint16_t	rule_locid_off;
	uint16_t	rule_remid_inclcnt;
	uint16_t	rule_remid_exclcnt;
	uint16_t	rule_remid_off;
	/*
	 * Followed by several lists of variable-length structures, described
	 * by counts and offsets:
	 *	transforms			ike_p1_xform_t structs
	 *	ranges of local ip addrs	ike_addr_pr_t structs
	 *	ranges of remote ip addrs	ike_addr_pr_t structs
	 *	local identification strings	null-terminated ascii strings
	 *	remote identification strings	null-terminated ascii strings
	 */
} ike_rule_t;

/* data formatting structure for DH group dumps */
typedef struct {
	uint16_t	group_number;
	uint16_t	group_bits;
	char		group_label[MAX_LABEL_LEN];
} ike_group_t;

/* data formatting structure for encryption algorithm dumps */
typedef struct {
	uint_t		encr_value;
	char		encr_name[MAX_LABEL_LEN];
	int		encr_keylen_min;
	int		encr_keylen_max;
} ike_encralg_t;

/* data formatting structure for authentication algorithm dumps */
typedef struct {
	uint_t		auth_value;
	char		auth_name[MAX_LABEL_LEN];
} ike_authalg_t;

/*
 * data formatting structure for preshared keys
 * ps_ike_mode field uses the IKE_XCHG_* defs
 */
typedef struct {
	ike_addr_pr_t	ps_ipaddrs;
	uint16_t	ps_ike_mode;
	uint16_t	ps_localid_off;
	uint16_t	ps_localid_len;
	uint16_t	ps_remoteid_off;
	uint16_t	ps_remoteid_len;
	uint16_t	ps_key_off;
	uint16_t	ps_key_len;
	uint16_t	ps_key_bits;
	int		ps_localid_plen;
	int		ps_remoteid_plen;
	/*
	 * followed by variable-length structures, as indicated by
	 * offset/length fields.
	 * key info will be formatted as an array of bytes.
	 * local/remote ids will be formatted as sadb_ident_t structs.
	 */
} ike_ps_t;

#define	DN_MAX			1024
#define	CERT_OFF_WIRE		-1
#define	CERT_NO_PRIVKEY		0
#define	CERT_PRIVKEY_LOCKED	1
#define	CERT_PRIVKEY_AVAIL	2

/*
 * data formatting structure for cached certs
 */
typedef struct {
	uint32_t	cache_id;
	uint32_t	certclass;
	int		linkage;
	uint32_t	certcache_padding;	/* For 64-bit alignment. */
	char		subject[DN_MAX];
	char		issuer[DN_MAX];
} ike_certcache_t;

/* identification types */
#define	IKE_ID_IDENT_PAIR	1
#define	IKE_ID_ADDR_PAIR	2
#define	IKE_ID_CKY_PAIR		3
#define	IKE_ID_LABEL		4


/* locations for read/write requests */
#define	IKE_RW_LOC_DEFAULT	1
#define	IKE_RW_LOC_USER_SPEC	2


/* door interface error codes */
#define	IKE_ERR_NO_OBJ		1	/* nothing found to match the request */
#define	IKE_ERR_NO_DESC		2	/* fd was required with this request */
#define	IKE_ERR_ID_INVALID	3	/* invalid id info was provided */
#define	IKE_ERR_LOC_INVALID	4	/* invalid location info was provided */
#define	IKE_ERR_CMD_INVALID	5	/* invalid command was provided */
#define	IKE_ERR_DATA_INVALID	6	/* invalid data was provided */
#define	IKE_ERR_CMD_NOTSUP	7	/* unsupported command */
#define	IKE_ERR_REQ_INVALID	8	/* badly formatted request */
#define	IKE_ERR_NO_PRIV		9	/* privilege level not high enough */
#define	IKE_ERR_SYS_ERR		10	/* syserr occurred while processing */
#define	IKE_ERR_DUP_IGNORED	11	/* attempt to add a duplicate entry */
#define	IKE_ERR_NO_TOKEN	12	/* cannot login into pkcs#11 token */
#define	IKE_ERR_NO_AUTH		13	/* not authorized */
#define	IKE_ERR_IN_PROGRESS	14	/* operation already in progress */
#define	IKE_ERR_NO_MEM		15	/* insufficient memory */


/*
 * IKE_SVC_GET_DBG
 * Used to request the current debug level.
 *
 * Upon request, dbg_level is 0 (don't care).
 *
 * Upon return, dbg_level contains the current value.
 *
 *
 * IKE_SVC_SET_DBG
 * Used to request modification of the debug level.
 *
 * Upon request, dbg_level contains desired level.  If debug output is
 * to be directed to a different file, the fd should be passed in the
 * door_desc_t field of the door_arg_t param.  NOTE: if the daemon is
 * currently running in the background with no debug set, an output
 * file MUST be given.
 *
 * Upon return, dbg_level contains the old debug level, and acknowledges
 * successful completion of the request.  If an error is encountered,
 * ike_err_t is returned instead, with appropriate error value and cmd
 * IKE_SVC_ERROR.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	dbg_level;
} ike_dbg_t;

/*
 * IKE_SVC_GET_PRIV
 * Used to request the current privilege level.
 *
 * Upon request, priv_level is 0 (don't care).
 *
 * Upon return, priv_level contains the current value.
 *
 *
 * IKE_SVC_SET_PRIV
 * Used to request modification of the privilege level.
 *
 * Upon request, priv_level contains the desired level.  The level may
 * only be lowered via the door interface; it cannot be raised.  Thus,
 * if in.iked is started at the lowest level, it cannot be changed.
 *
 * Upon return, priv_level contains the old privilege level, and
 * acknowledges successful completion of the request.  If an error is
 * encountered, ike_err_t is returned instead, with appropriate error
 * value and cmd IKE_SVC_ERROR.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	priv_level;
} ike_priv_t;


/*
 * IKE_SVC_GET_STATS
 * Used to request current statistics on Phase 1 SA creation and
 * failures.  The statistics represent all activity in in.iked.
 *
 * Upon request, cmd is set, and stat_len does not matter.
 *
 * Upon successful return, stat_len contains the total size of the
 * returned buffer, which contains first the ike_statreq_t struct,
 * followed by the stat data in the ike_stats_t structure. In case
 * of an error in processing the request, ike_err_t is returned with
 * IKE_SVC_ERROR command and appropriate error code.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	stat_len;
} ike_statreq_t;

/*
 * IKE_SVC_GET_DEFS
 * Used to request default values from in.iked.
 *
 * Upon request, cmd is set, and stat_len does not matter.
 *
 * Upon successful return, stat_len contains the total size of the
 * returned buffer, this contains a pair of ike_defaults_t's.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	stat_len;
	uint32_t	version;
	uint32_t	defreq_reserved;	/* For 64-bit alignment. */
} ike_defreq_t;

/*
 * IKE_SVC_DUMP_{P1S|RULES|PS|CERTCACHE}
 * Used to request a table dump, and to return info for a single table
 * item.  The expectation is that all of the table data will be passed
 * through the door, one entry at a time; an individual request must be
 * sent for each entry, however (the door server can't send unrequested
 * data).
 *
 * Upon request: cmd is set, and dump_next contains the item number
 * requested (0 for first request).  dump_len is 0; no data follows.
 *
 * Upon return: cmd is set, and dump_next contains the item number of
 * the *next* item in the table (to be used in the subsequent request).
 * dump_next = 0 indicates that this is the last item in the table.
 * dump_len is the total length (data + struct) returned.  Data is
 * formatted as indicated by the cmd type:
 *   IKE_SVC_DUMP_P1S:		ike_p1_sa_t
 *   IKE_SVC_DUMP_RULES:	ike_rule_t
 *   IKE_SVC_DUMP_PS:		ike_ps_t
 *   IKE_SVC_DUMP_CERTCACHE:	ike_certcache_t
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	dump_len;
	union {
		struct {
			uint32_t	dump_unext;
			uint32_t	dump_ureserved;
		} dump_actual;
		uint64_t dump_alignment;
	} dump_u;
#define	dump_next dump_u.dump_actual.dump_unext
#define	dump_reserved dump_u.dump_actual.dump_ureserved
	/* dump_len - sizeof (ike_dump_t) bytes of data included here */
} ike_dump_t;


/*
 * IKE_SVC_GET_{P1|RULE|PS}
 * Used to request and return individual table items.
 *
 * Upon request: get_len is the total msg length (struct + id data);
 * get_idtype indicates the type of identification being used.
 *   IKE_SVC_GET_P1:		ike_addr_pr_t or ike_cky_pr_t
 *   IKE_SVC_GET_RULE:		char string (label)
 *   IKE_SVC_GET_PS:		ike_addr_pr_t or pair of sadb_ident_t
 *
 * Upon return: get_len is the total size (struct + data), get_idtype
 * is unused, and the data that follows is formatted according to cmd:
 *   IKE_SVC_GET_P1:		ike_p1_sa_t
 *   IKE_SVC_GET_RULE:		ike_rule_t
 *   IKE_SVC_GET_PS:		ike_ps_t
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	get_len;
	union {
		struct {
			uint32_t	getu_idtype;
			uint32_t	getu_reserved;
		} get_actual;
		uint64_t get_alignment;
	} get_u;
#define	get_idtype get_u.get_actual.getu_idtype
#define	get_reserved get_u.get_actual.getu_reserved
	/* get_len - sizeof (ike_get_t) bytes of data included here */
} ike_get_t;


/*
 * IKE_SVC_NEW_{RULE|PS}
 * Used to request and acknowledge insertion of a table item.
 *
 * Upon request: new_len is the total (data + struct) size passed, or 0.
 * new_len = 0 => a door_desc_t is also included with a file descriptor
 * for a file containing the data to be added.  The file should include
 * a single item: a rule, or a pre-shared key.  For new_len != 0, the
 * data is formatted according to the cmd type:
 *   IKE_SVC_NEW_RULE:		ike_rule_t
 *   IKE_SVC_NEW_PS:		ike_ps_t
 *
 * Upon return: new_len is 0; simply acknowledges successful insertion
 * of the requested item.  If insertion is not successful, ike_err_t is
 * returned instead with appropriate error value.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	new_len;
	/* new_len - sizeof (ike_new_t) bytes included here */
	uint64_t	new_align;	/* Padding for 64-bit alignment. */
} ike_new_t;


/*
 * IKE_SVC_DEL_{P1|RULE|PS}
 * Used to request and acknowledge the deletion of an individual table
 * item.
 *
 * Upon request: del_len is the total msg length (struct + id data);
 * del_idtype indicates the type of identification being used.
 *   IKE_SVC_DEL_P1:		ike_addr_pr_t or ike_cky_pr_t
 *   IKE_SVC_DEL_RULE:		char string (label)
 *   IKE_SVC_DEL_PS:		ike_addr_pr_t or pair of sadb_ident_t
 *
 * Upon return: acknowledges deletion of the requested item; del_len and
 * del_idtype are unspecified.  If deletion is not successful, ike_err_t
 * is returned instead with appropriate error value.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	del_len;
	uint32_t	del_idtype;
	uint32_t	del_reserved;
	/* del_len - sizeof (ike_del_t) bytes of data included here. */
} ike_del_t;


/*
 * IKE_SVC_READ_{RULES|PS}
 * Used to ask daemon to re-read particular configuration info.
 *
 * Upon request: rw_loc indicates where the info should be read from:
 * either from a user-supplied file descriptor(s), or from the default
 * location(s).  If rw_loc indicates user-supplied location, the file
 * descriptor(s) should be passed in the door_desc_t struct.  For the
 * IKE_SVC_READ_RULES cmd, two file descriptors should be specified:
 * first, one for the config file which contains the data to be read,
 * and second, one for the cookie file which will be written to as
 * in.iked process the config file.
 *
 * Upon return: rw_loc is unspecified; the message simply acknowledges
 * successful completion of the request.  If an error occurred,
 * ike_err_t is returned instead with appropriate error value.
 *
 *
 * IKE_SVC_WRITE_{RULES|PS}
 * Used to ask daemon to write its current config info to files.
 *
 * Request and return are handled the same as for the IKE_SVC_READ_*
 * cmds; however, the rw_loc MUST be a user-supplied location.  Also,
 * for the IKE_SVC_WRITE_RULES cmd, the cookie file fd is not required;
 * only a single fd, for the file to which the config info should be
 * written, should be passed in.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	rw_loc;
} ike_rw_t;


/*
 * IKE_SVC_FLUSH_P1S
 * IKE_SVC_FLUSH_CERTCACHE
 *
 * Used to request and acknowledge tear-down of all P1 SAs
 * or to flush the certificate cache.
 */
typedef struct {
	ike_svccmd_t	cmd;
} ike_flush_t;


#ifndef PKCS11_TOKSIZE
#define	PKCS11_TOKSIZE 32
#endif
#define	MAX_PIN_LEN 256
/*
 * IKE_SVC_SET_PIN
 * IKE_SVC_DEL_PIN
 *
 * Used to supply a pin for a PKCS#11 tokenj object.
 *
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	pin_reserved;	/* For 64-bit alignment. */
	char pkcs11_token[PKCS11_TOKSIZE];
	uchar_t token_pin[MAX_PIN_LEN];
} ike_pin_t;

/*
 * IKE_SVC_ERROR
 * Used on return if server encountered an error while processing
 * the request.  An appropriate error code is included (as defined
 * in this header file); in the case of IKE_ERR_SYS_ERR, a value
 * from the UNIX errno space is included in the ike_err_unix field.
 */
typedef struct {
	ike_svccmd_t	cmd;
	uint32_t	ike_err;
	uint32_t	ike_err_unix;
	uint32_t	ike_err_reserved;
} ike_err_t;

/*
 * Generic type for use when the request/reply type is unknown
 */
typedef struct {
	ike_svccmd_t	cmd;
} ike_cmd_t;


/*
 * Union containing all possible request/return structures.
 */
typedef union {
	ike_cmd_t	svc_cmd;
	ike_dbg_t	svc_dbg;
	ike_priv_t	svc_priv;
	ike_statreq_t	svc_stats;
	ike_dump_t	svc_dump;
	ike_get_t	svc_get;
	ike_new_t	svc_new;
	ike_del_t	svc_del;
	ike_rw_t	svc_rw;
	ike_flush_t	svc_flush;
	ike_pin_t	svc_pin;
	ike_err_t	svc_err;
	ike_defreq_t	svc_defaults;
} ike_service_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _IKEDOOR_H */
