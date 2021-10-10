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

#ifndef _SMBD_H
#define	_SMBD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <syslog.h>
#include <thread.h>
#include <synch.h>
#include <pthread.h>
#include <gssapi/gssapi.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/smb_token.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smb_share.h>

int smbd_opipe_start(void);
void smbd_opipe_stop(void);
int smbd_nicmon_start(const char *);
void smbd_nicmon_stop(void);
int smbd_nicmon_refresh(void);
int smbd_dc_monitor_init(void);
void smbd_dc_monitor_refresh(void);
void smbd_user_auth(smb_authreq_t *, smb_authrsp_t *);
void smbd_user_logoff(smb_logoff_t *);
uint32_t smbd_discover_dc(smb_joininfo_t *);
uint32_t smbd_join(smb_joininfo_t *);
void smbd_set_secmode(int);
boolean_t smbd_online(void);
void smbd_online_wait(const char *);
void smbd_log(int, const char *fmt, ...);

void smbd_dyndns_start(void);
void smbd_dyndns_stop(void);
void smbd_dyndns_clear(void);
void smbd_dyndns_update(void);

void smbd_quota_init(void);
void smbd_quota_fini(void);
void smbd_quota_add_fs(const char *);
void smbd_quota_remove_fs(const char *);
uint32_t smbd_quota_query(smb_quota_query_t *, smb_quota_response_t *);
uint32_t smbd_quota_set(smb_quota_set_t *);
void smbd_quota_free(smb_quota_response_t *);

void smbd_share_start(void);
void smbd_share_stop(void);
void smbd_share_load_execinfo(void);
int smbd_share_publish_admin(char *);
void smbd_share_publish(smb_shr_notify_t *);
void smbd_share_unpublish(smb_shr_notify_t *);
void smbd_share_republish(smb_shr_notify_t *);
int smbd_share_exec(smb_shr_execinfo_t *);
uint32_t smbd_share_hostaccess(smb_inaddr_t *, char *, char *, char *,
    uint32_t);

void smbd_spool_init(void);
void smbd_spool_fini(void);
void smbd_spool_document(const smb_spooldoc_t *);
void smbd_load_printers(void);
void spoolss_initialize(void);
void spoolss_finalize(void);

smb_token_t *smbd_token_alloc(void);
void smbd_token_free(smb_token_t *);
void smbd_token_cleanup(smb_token_t *);
boolean_t smbd_token_setup_common(smb_token_t *);
uint32_t smbd_token_setup_domain(smb_token_t *, const smb_authinfo_t *);
uint32_t smbd_token_setup_local(smb_passwd_t *, smb_token_t *);
uint32_t smbd_token_setup_guest(smb_account_t *, smb_token_t *);
uint32_t smbd_token_setup_anon(smb_token_t *token);
uint32_t smbd_token_auth_local(smb_authreq_t *, smb_token_t *, smb_passwd_t *);
smb_authreq_t *smbd_authreq_decode(uint8_t *, uint32_t);
void smbd_authreq_free(smb_authreq_t *);

void smbd_gss_lock(void);
void smbd_gss_unlock(void);
void smbd_gss_log(char *, gss_OID, OM_uint32, OM_uint32);
void smbd_krb5_gsslog(char *, smb_authreq_t *, gss_OID, OM_uint32, OM_uint32);

int smbd_session_avl_init(void);
void smbd_session_connect(smb_sessionreq_t *, smb_sessionrsp_t *);
void smbd_session_disconnect(uint64_t);
void smbd_user_cleanup(uint64_t, uint16_t);

/*
 * NTLMSSP
 *
 * NTLM authentication is a challenge-response scheme, consisting of three
 * messages, commonly referred to as Type 1 (negotiate), Type 2 (challenge),
 * and Type 3 (authenticate) in the extended security context.
 * It basically works like this:
 *
 * 1) The client sends a Type 1 message to the server.  This primarily
 * contains a list of features supported by the client.
 * 2) The server responds with a Type 2 message. This contains a list of
 * features supported and agreed upon by the server.  Most importantly, it
 * contains a challenge generated by the server.
 * 3) The client replies to the challenge with a Type 3 message. This contains
 * serveral pieces of information about the client, including the domain and
 * username of the client user. It also contains one or more responses to the
 * Type 2 challenge.
 *
 * For more info, see MS-NLMP document.
 */
#define	NTLMSSP_SIGNATURE "NTLMSSP"
#define	NTLMSSP_SIGNATURE_SZ 8

/* NTLM message types */
#define	NTLMSSP_MSGTYPE_NEGOTIATE		1
#define	NTLMSSP_MSGTYPE_CHALLENGE		2
#define	NTLMSSP_MSGTYPE_AUTHENTICATE		3

#define	NTLMSSP_BASIC_NEG_FLAGS		\
	(NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET | \
	NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO)

typedef enum {
	BUFCONV_UNICODE,	/* unicode string */
	BUFCONV_ASCII_STRING,	/* null terminated */
	BUFCONV_NOTERM		/* non null terminated */
} smbd_ntlm_bufconv_t;

typedef struct smbd_ntlm_bufinfo {
	uint16_t	b_len;
	uint16_t	b_maxlen;
	uint32_t	b_offset;
} smbd_ntlm_bufinfo_t;

/*
 * AV_PAIR structure defines an attribute/value pair. Sequences of AV_PAIR
 * structures are used in NTLM Type 2 and 3 messages. av_id field defines
 * the information type in the av_value.
 */
typedef struct smbd_ntlm_avpair {
	uint16_t	av_id;
	uint16_t	av_len;
	smb_wchar_t	av_value[MAXHOSTNAMELEN];
} smbd_ntlm_avpair_t;

/* Possible values of the av_id */
#define	SMBD_NTLM_AVID_EOL		0
#define	SMBD_NTLM_AVID_NB_NAME		1
#define	SMBD_NTLM_AVID_NB_DOMAIN	2
#define	SMBD_NTLM_AVID_DNS_NAME		3
#define	SMBD_NTLM_AVID_DNS_DOMAIN	4
#define	SMBD_NTLM_AVID_DNS_TREE		5
#define	SMBD_NTLM_AVID_FLAGS		6
#define	SMBD_NTLM_AVID_TIMESTAMP	7
#define	SMBD_NTLM_AVID_RESTRICTIONS	8


typedef struct smbd_ntlm_version {
	uint8_t		v_major;
	uint8_t		v_minor;
	uint16_t	v_build;
	uint8_t		v_reserved[3];
	uint8_t		v_revision;
} smbd_ntlm_version_t;

/*
 * NTLM Type 1 (negotiate) message
 *
 * The version field is defined in the MS-NLMP document but I haven't
 * seen Windows clients encode that information in the Type 1 message.
 */
typedef struct smbd_ntlm_neg_hdr {
	char			*nh_signature;
	uint32_t		nh_type;
	uint32_t		nh_negflags;
	smbd_ntlm_bufinfo_t	nh_domain;
	smbd_ntlm_bufinfo_t	nh_wkst;
	/* smbd_ntlm_version_t	nh_version; */

} smbd_ntlm_neg_hdr_t;

typedef struct smbd_ntlm_neg_msg {
	smb_msgbuf_t		nm_mb;
	smbd_ntlm_neg_hdr_t	nm_hdr;
	int8_t			*nm_domain;
	int8_t			*nm_wkst;
} smbd_ntlm_neg_msg_t;

/*
 * NTLM Type 2 (challenge) message)
 *
 * The version field is defined in the MS-NLMP document but I haven't
 * seen Windows servers encode that information in the Type 2 message.
 */
typedef struct smbd_ntlm_challenge_hdr {
	char			ch_signature[NTLMSSP_SIGNATURE_SZ];
	uint32_t		ch_type;
	smbd_ntlm_bufinfo_t	ch_target_name;
	uint32_t		ch_negflags;
	uint8_t			ch_srv_challenge[8];
	uint64_t		ch_reserved;
	smbd_ntlm_bufinfo_t	ch_target_info;
	/* smbd_ntlm_version_t	ch_version; */
} smbd_ntlm_challenge_hdr_t;

typedef struct smbd_ntlm_challenge_msg {
	smb_msgbuf_t			cm_mb;
	smbd_ntlm_challenge_hdr_t	cm_hdr;
	smb_wchar_t 			cm_target_namebuf[MAXHOSTNAMELEN];
	smbd_ntlm_avpair_t		*cm_avpairs;
} smbd_ntlm_challenge_msg_t;

/*
 * NTLM Type 3 (authenticate) message
 *
 * The version field is defined in the MS-NLMP document but I haven't
 * seen Windows clients encode that information in the Type 3 message.
 *
 * Clients are expected not to send MIC in the Type 3 message since
 * our Type2 message doesn't contain MsvAvTimestamp AV pair.
 */
typedef struct smbd_ntlm_auth_hdr {
	char			*ah_signature;
	uint32_t		ah_type;
	smbd_ntlm_bufinfo_t	ah_lm;
	smbd_ntlm_bufinfo_t	ah_ntlm;
	smbd_ntlm_bufinfo_t	ah_domain;
	smbd_ntlm_bufinfo_t	ah_user;
	smbd_ntlm_bufinfo_t	ah_wkst;
	smbd_ntlm_bufinfo_t	ah_ssnkey;
	uint32_t		ah_negflags;
	/* smbd_ntlm_version_t	ah_version; */
	/* uint8_t		ah_mic[16]; */

} smbd_ntlm_auth_hdr_t;

typedef struct smbd_ntlm_auth_msg {
	smb_msgbuf_t		am_mb;
	smbd_ntlm_auth_hdr_t	am_hdr;
	uint8_t			*am_lm;
	uint8_t			*am_ntlm;
	int8_t			*am_domain;
	int8_t			*am_user;
	int8_t			*am_wkst;
	uint8_t			*am_ssnkey;

} smbd_ntlm_auth_msg_t;

void smbd_ntlm_init(void);
uint32_t smbd_ntlm_spng2mech(smb_authreq_t *);
uint32_t smbd_ntlm_mech2spng(smb_authreq_t *, smb_authrsp_t *);

/* authentication mechanism types */
typedef enum {
	SMBD_MECH_NTLM = 0,
	SMBD_MECH_NTLMSSP,
	SMBD_MECH_RAW_NTLMSSP,
	SMBD_MECH_KRB,
	SMBD_MECH_MAX
} smbd_mech_type_t;

/* authentication mechanism */
typedef struct smbd_mech {
	smbd_mech_type_t m_type;
	uint32_t (*m_pre_auth)(smb_authreq_t *);
	uint32_t (*m_auth)(smb_authreq_t *, smb_authrsp_t *);
	uint32_t (*m_post_auth)(smb_authreq_t *, smb_authrsp_t *);
} smbd_mech_t;

void smbd_mech_register(smbd_mech_t *);
uint32_t smbd_mech_preauth_noop(smb_authreq_t *);
uint32_t smbd_mech_postauth_noop(smb_authreq_t *, smb_authrsp_t *);
uint32_t smbd_mech_secblob2token(const smb_buf16_t *, smb_buf16_t *);

void smbd_krb5_init(void);

smb_authreq_t *smbd_authreq_decode(uint8_t *, uint32_t);
void smbd_authreq_free(smb_authreq_t *);

uint8_t *smbd_authrsp_encode(smb_authrsp_t *, uint32_t *);
void smbd_authrsp_free(smb_authrsp_t *);

uint32_t smbd_pac_get_userinfo(smb_authreq_t *, ndr_buf_t *,
    smb_session_key_t *, kerb_validation_info_t *);

int smbd_vss_get_count(const char *, uint32_t *);
void smbd_vss_get_snapshots(const char *, uint32_t, uint32_t *,
    uint32_t *, char **);
int smbd_vss_map_gmttoken(const char *, char *, char *);

typedef struct smbd_thread_list {
	pthread_mutex_t	tl_mutex;
	pthread_cond_t	tl_cv;
	list_t		tl_list;
	uint32_t	tl_count;
} smbd_thread_list_t;

typedef struct smbd_thread {
	list_node_t	st_lnd;
	char		*st_name;
	pthread_t	st_tid;
	void		*st_arg;
} smbd_thread_t;

typedef void *(*smbd_launch_t)(void *);

int smbd_thread_create(const char *, smbd_launch_t, void *);
int smbd_thread_run(const char *, smbd_launch_t func, void *);
void smbd_thread_exit(void);
void smbd_thread_kill(const char *, int);

typedef struct smbd {
	const char	*s_version;	/* smbd version string */
	const char	*s_pname;	/* basename to use for messages */
	pid_t		s_pid;		/* process-ID of current daemon */
	uid_t		s_uid;		/* UID of current daemon */
	gid_t		s_gid;		/* GID of current daemon */
	int		s_fg;		/* Run in foreground */
	boolean_t	s_initialized;
	boolean_t	s_shutting_down;
	boolean_t	s_service_fini;
	volatile uint_t	s_sigval;
	volatile uint_t	s_refreshes;
	boolean_t	s_kbound;	/* B_TRUE if bound to kernel */
	int		s_door_srv;
	int		s_door_opipe;
	int		s_secmode;	/* Current security mode */
	char		s_site[MAXHOSTNAMELEN];
	smb_inaddr_t	s_pdc;
	boolean_t	s_dscfg_changed;
	smbd_thread_list_t	s_tlist;
	smb_log_hdl_t	s_loghd;
} smbd_t;

#define	SMBD_LOGNAME		"smbd"
#define	SMBD_LOGSIZE		1024

#define	SMBD_DOOR_NAMESZ	16

#define	SMBD_PRINTER_NAME_DEFAULT "Postscript"

typedef struct smbd_door {
	mutex_t		sd_mutex;
	cond_t		sd_cv;
	uint32_t	sd_ncalls;
	char		sd_name[SMBD_DOOR_NAMESZ];
} smbd_door_t;

int smbd_door_start(void);
void smbd_door_stop(void);
void smbd_door_init(smbd_door_t *, const char *);
void smbd_door_fini(smbd_door_t *);
void smbd_door_enter(smbd_door_t *);
void smbd_door_return(smbd_door_t *, char *, size_t, door_desc_t *, uint_t);

#ifdef __cplusplus
}
#endif

#endif /* _SMBD_H */
