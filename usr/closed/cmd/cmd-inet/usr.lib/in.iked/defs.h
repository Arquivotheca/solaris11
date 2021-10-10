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

#ifndef	_IKED_COMMON_DEFS_H
#define	_IKED_COMMON_DEFS_H

#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <net/if.h>
#include <sys/avl.h>

#include <ike/sshdistdefs.h>
#include <ike/sshincludes.h>
#include <ike/isakmp.h>
#include <ike/cmi.h>

#include <ike/certlib.h>

#include <ikedoor.h>
#include <rpcsvc/daemon_utils.h>
#include "ikerules.h"

#ifdef	__cplusplus
extern "C" {
#endif

#include <tsol/label.h>
#include <sys/tsol/tndb.h>
#include <sys/tsol/label_macro.h>

extern uint32_t	debug;
extern boolean_t do_natt;
extern FILE *debugfile;
extern boolean_t cflag;
extern boolean_t override_outer_label, hide_outer_label;

/*
 * Debug flags are defined in ikedoor.h...
 */

void dbgprintf(char *fmt, ...);

#define	PRTDBG(type, msg) \
		if (debug & (type)) dbgprintf msg

#define	DUMP_PFKEY(samsg) \
	if (debug & D_PFKEY) { \
		dbgprintf("PF_KEY message contents:"); \
		print_samsg(debugfile, (uint64_t *)(samsg), B_TRUE, B_TRUE, \
		    B_TRUE); \
	}

/*
 * daemon's global stat counters
 */
extern ike_stats_t	ikestats;
extern ike_defaults_t	ike_defs;

/*
 * smf(5) globals
 */
extern char *my_fmri;
extern boolean_t ignore_errors;

/* Identifier used to prevent multiple daemons */
#define	IKED "/usr/lib/inet/in.iked"

#define	CFGROOT	"/etc/inet"

/* Special file characters... */
#define	COMMENT_CHAR	'#'
#define	CONT_CHAR	'\\'

/* Make sure this file has 0600 permissions. */
#define	PRESHARED_KEY_FILE	CFGROOT"/secret/ike.preshared"

/* Where to find hard-wired crls? */
#define	CRL_DIR		CFGROOT"/ike/crls/"

/* Where to find the config file... */
#define	CONFIG_FILE		CFGROOT"/ike/config"

/* Where to write the pid file... */
#define	PID_FILE		_PATH_SYSVOL "/in.iked.pid"

/*
 * Minimum and maximum values for lifetimes
 */
#define	MIN_P1_LIFETIME 60
#define	MIN_P2_LIFETIME_SOFT_SECS 90
/* Minimum difference between SOFT and HARD or SOFT and IDLE, in seconds */
#define	MINDIFF_SECS 10
/* Minimum difference between SOFT and HARD, in kilobytes */
#define	MINDIFF_KB 100
/*
 * MIN_P2_LIFETIME_HARD_SECS is derived from MIN_P2_LIFETIME_SOFT_SECS
 * so that the following expressions hold true:
 *
 *   1. MIN_P2_LIFETIME_HARD ~= (1 / HARD2SOFT_FACTOR) * MIN_P2_LIFETIME_SOFT
 *   2. MIN_P2_LIFETIME_HARD - HARD2SOFT(MIN_P2_LIFETIME_HARD) >= MINDIFF
 *   3. HARD2SOFT(MIN_P2_LIFETIME_HARD) >= MIN_P2_LIFETIME_SOFT
 *
 * The expression #3 is necessary so that if HARD lifetime is set to minimum
 * value the SOFT lifetime does not underflow if computed from the HARD
 * lifetime.
 */
#define	MIN_P2_LIFETIME_HARD_SECS 100
/*
 * IDLE lifetime minimum could be really arbitrary (but smaller than
 * MIN_P2_LIFETIME_SOFT - MINDIFF) so set a reasonable small value.
 */
#define	MIN_P2_LIFETIME_IDLE_SECS 30
/*
 * We want the minimum p2 lifetime to be smaller than the lifetime that the
 * most paranoid user would want.
 */
#define	MIN_P2_LIFETIME_HARD_KB	1024
/*
 * MIN_P2_LIFETIME_SOFT_KB must be defined so that it's smaller than
 * HARD2SOFT(MIN_P2_LIFETIME_HARD_KB) and at least MINDIFF_KB away from
 * MIN_P2_LIFETIME_HARD_KB.
 */
#define	MIN_P2_LIFETIME_SOFT_KB	900
/*
 * Lifetimes expressed in seconds can be used for conversion into lifetimes
 * expressed in kilobytes and we only have 32 bits to represent the result
 * in ike_rule structure so the limit (for both SOFT and HARD lifetimes)
 * cannot be more than ((1 << 21) - 1) to avoid integer overflow.
 * The practical maximum should be lower than that since kilobyte values
 * can be inferred from values in seconds and should not be too high
 * (see below). Hence, use a reasonable maximum of 48 hours.
 */
#define	MAX_P2_LIFETIME_SECS	172800
/*
 * The maximum kilobyte lifetime in the parser is limited to 32-bit unsigned
 * value (4 TB). This is much higher than the number of blocks for 64-bit CBC
 * cipher where the probability of collision approaches 1 (2^32 64-bit blocks
 * ~ 32GB). The hard (enforced) limit should not be more than an order of
 * magnitude higher, so use a reasonable value of 512 GB.
 */
#define	MAX_P2_LIFETIME_KB	536870912
/*
 * The 32GB Threshold (used to issue a warning).
 */
#define	P2_LIFETIME_KB_THRESHOLD	33554432

/*
 * Default lifetime values.
 */
#define	DEF_P1_LIFETIME (MIN_P1_LIFETIME * 60)
/* The defaults should match those specified in ipsecesp/ipsecah modules. */
#define	DEF_P2_LIFETIME_HARD 28800
#define	DEF_P2_LIFETIME_SOFT 24000
/*
 * Default nonce length should be no smaller than the output of the
 * largest hash function IKE can use.  For now, that's SHA512, or 64 bytes.
 */
#define	DEF_NONCE_LENGTH 64
/*
 * SOFT timer should be 90 % of HARD timer by default. The multiply-first
 * approach is okay as long as the HARD2SOFT(MAX_P2_LIFETIME_SECS) does not
 * cause integer overflow for the type which represents the lifetimes.
 */
#define	HARD2SOFT_FACTOR	9 / 10
#define	HARD2SOFT(x) ((x) * HARD2SOFT_FACTOR)
#define	DEFSOFT(x, y) (x) = (HARD2SOFT(y))
/*
 * IDLE checking should be done once during the lifetime (in the middle of
 * HARD timer) and before SOFT timer which means:
 *
 *   IDLE = 1/2 * HARD = 1/2 * 10/9 * SOFT = 5/9 * SOFT
 */
#define	SOFT2IDLE_FACTOR	5 / 9
#define	SOFT2IDLE(x)	((x) * SOFT2IDLE_FACTOR)
#define	DEFIDLE(x, y)	(x) = SOFT2IDLE(y)
/* Default byte lifetime conversion factor is 1 Mbyte/S. */
#define	SECS2KBYTES(x)	((x) << 10)
#define	CONV_FACTOR	SECS2KBYTES(1)
#define	DEFKBYTES(x, y)	(x) = (SECS2KBYTES(y))
#define	ike_defaults ike_defaults_t;

/* Buffer sizes. */
#define	PF_KEY_ALLOC_LEN 1024
#define	LINELEN 160
#define	PIDMAXCHAR 8
#define	DEFAULT_MAX_CERTS 1024
/* 2 x MD5 output */
#define	RANDOM_FACTOR_LEN 32

/* For using numeric constants as strings. */
#define	MKSTR2(x) #x
#define	MKSTR(x) ((unsigned char *)MKSTR2(x))

/* For determining if we need NAT-T */
#define	NEED_NATT(natt_state) (natt_state == NATT_STATE_GOT_PORT)

typedef union sockaddr_u_s {
	struct sockaddr_storage *sau_ss;
	struct sockaddr_in *sau_sin;
	struct sockaddr_in6 *sau_sin6;
} sockaddr_u_t;

/* Parsed-out PF_KEY message. */
typedef struct parsedmsg_s {
	struct parsedmsg_s *pmsg_next;
	sadb_msg_t *pmsg_samsg;
	sadb_ext_t *pmsg_exts[SADB_EXT_MAX + 2]; /* 2 for alignment */
	sockaddr_u_t pmsg_sau;
#define	pmsg_sss pmsg_sau.sau_ss
#define	pmsg_ssin pmsg_sau.sau_sin
#define	pmsg_ssin6 pmsg_sau.sau_sin6
	sockaddr_u_t pmsg_dau;
#define	pmsg_dss pmsg_dau.sau_ss
#define	pmsg_dsin pmsg_dau.sau_sin
#define	pmsg_dsin6 pmsg_dau.sau_sin6
	sockaddr_u_t pmsg_psau;
#define	pmsg_psss pmsg_psau.sau_ss
#define	pmsg_pssin pmsg_psau.sau_sin
#define	pmsg_pssin6 pmsg_psau.sau_sin6
	sockaddr_u_t pmsg_pdau;
#define	pmsg_pdss pmsg_pdau.sau_ss
#define	pmsg_pdsin pmsg_pdau.sau_sin
#define	pmsg_pdsin6 pmsg_pdau.sau_sin6
	sockaddr_u_t pmsg_nlau;
#define	pmsg_nlss pmsg_nlau.sau_ss
#define	pmsg_nlsin pmsg_nlau.sau_sin
#define	pmsg_nlsin6 pmsg_nlau.sau_sin6
	sockaddr_u_t pmsg_nrau;
#define	pmsg_nrss pmsg_nrau.sau_ss
#define	pmsg_nrsin pmsg_rnau.sau_sin
#define	pmsg_nrsin6 pmsg_nrau.sau_sin6

} parsedmsg_t;

/* Phase 1 extra structure. */
typedef struct phase1_s {
	struct phase1_s *p1_next;
	struct phase1_s **p1_ptpn;
	struct sockaddr_storage p1_local;
	struct sockaddr_storage p1_remote;
	/* Identities - If non-NULL, then malloc()ed storage! */
	sadb_ident_t *p1_localid;
	sadb_ident_t *p1_remoteid;
	struct certlib_cert *p1_localcert;
	struct certlib_cert *p1_remotecert;
	SshIkePMPhaseI p1_pminfo;
#define	p1_negotiation	p1_pminfo->negotiation
	parsedmsg_t *p1_pmsg;
	parsedmsg_t *p1_pmsg_tail;
	int p1_p2_group;
	boolean_t p1_complete;	/* Only set by negotiation_done_isakmp. */
	struct ike_rule *p1_rule;
	struct ike_rulebase p1_rulebase;
	/* stat and error tracking... */
	ike_p1_stats_t	p1_stats;
	ike_p1_errors_t	p1_errs;
	/* quick cache of v4/v6ness */
	boolean_t p1_isv4;
	/* Negotiated lifetimes */
	unsigned p2_lifetime_secs;
	unsigned p2_lifetime_kb;
	int p1_group;
	boolean_t p1_use_dpd;
	dpd_status_t p1_dpd_status;
	time_t p1_dpd_time;
	boolean_t p1_create_phase2;
	parsedmsg_t *p1_dpd_pmsg;
	uint32_t p1_dpd_sent_seqnum;
	uint32_t p1_dpd_recv_seqnum;
	int	p1_num_dpd_reqsent;

	ucred_t *outer_ucred;
	sadb_sens_t *outer_label;
	int label_doi;
	bslabel_t min_sl, max_sl;
	boolean_t label_aware;

} phase1_t;

/* If this macro is used in an if, while, or for, use {}. */
#define	UNLINK_PHASE1(p1) \
	*((p1)->p1_ptpn) = (p1)->p1_next; \
	if ((p1)->p1_next != NULL) { \
		(p1)->p1_next->p1_ptpn = (p1)->p1_ptpn; \
		(p1)->p1_next = NULL; \
	} /* Leave out semicolon so we must use one with the macro. */ \
	(p1)->p1_ptpn = NULL


#define	ADDRCACHE_BUCKETS 16
/*
 * Assume ADDRCACHE_BUCKETS is a power of 2 for now, otherwise, use modulo
 * instead of bitwise AND.
 */
#define	ADDRCACHE_HASH_V4(ui32) \
	((((ui32) >> 24) ^ ((ui32) >> 16) ^ ((ui32) >> 8) ^ (ui32)) & \
		(ADDRCACHE_BUCKETS - 1))

#define	ADDRCACHE_HASH_V6(v6addr) ADDRCACHE_HASH_V4((*(uint32_t *)&(v6addr)) ^ \
	(*((uint32_t *)&(v6addr)) + 1) ^ (*((uint32_t *)&(v6addr)) + 2) ^ \
	(*((uint32_t *)&(v6addr)) + 3))

/* Number of seconds slack to give the first phase 1 if a ton are starting. */
#define	INITIAL_CONTACT_PAUSE	30

/*
 * MAX number of retries after which we abort the DPD process and
 * delete IPsec SA.
 */
#define	MAX_DPD_RETRIES	3

/* Address cache entry */
typedef struct addrentry_s {
	struct addrentry_s *addrentry_next;
	struct addrentry_s **addrentry_ptpn;
	boolean_t addrentry_isv4;
	union {
		struct in_addr addrentryu_v4;
		struct in6_addr addrentryu_v6;
	} addrentry_u;
#define	addrentry_addr4 addrentry_u.addrentryu_v4
#define	addrentry_addr6 addrentry_u.addrentryu_v6
	time_t addrentry_timeout;
	time_t addrentry_addtime;
	uint32_t addrentry_scopeid;	/* For IPv6 */
	int	addrentry_num_p1_reqsent;
} addrentry_t;

/* Address cache structure */
typedef struct addrcache_s {
	addrentry_t *addrcache_bucket[ADDRCACHE_BUCKETS];
} addrcache_t;

/*
 * One per active local address on the system.
 */
typedef struct ike_server_s {
	avl_node_t ikesrv_link;
	struct sockaddr_storage ikesrv_addr;
	uint32_t ikesrv_addrref;
	addrcache_t ikesrv_addrcache;
	SshIkeServerContext ikesrv_ctx;
} ike_server_t;

/*
 * One per active logical interface.
 */
typedef struct ike_lif_s
{
	avl_node_t ikelif_link;
	char ikelif_name[LIFNAMSIZ];
	int ikelif_af;
} ike_lif_t;

typedef struct p2alg_s {
	uint32_t	p2alg_doi_num;
	uint32_t	p2alg_min_bits;
	uint32_t	p2alg_max_bits;
	uint32_t	p2alg_salt_bits;
	uint32_t	p2alg_key_len_incr;
	uint32_t	p2alg_default_incr;
	/*
	 *  someday...
	 *  uint32_t	p2alg_speed;
	 *  uint32_t	p2alg_strength;
	 *  uint32_t	p2alg_rounds;
	 */
} p2alg_t;

typedef struct vid_table_s {
	const char *desc;
	const char *hex_vid;
} vid_table_t;

typedef struct algindex_s {
	const char *desc;
	int doi_num;
} algindex_t;

extern p2alg_t *p2_ah_algs;
extern p2alg_t *p2_esp_auth_algs;
extern p2alg_t *p2_esp_encr_algs;

/* Server context functions... */
extern ike_server_t *get_server_context(struct sockaddr_storage *);
extern void flush_addrcache(void);

/* Phase 1 SA functions */
extern phase1_t *match_phase1(struct sockaddr_storage *,
    struct sockaddr_storage *, sadb_ident_t *, sadb_ident_t *, sadb_x_kmc_t *,
    boolean_t);
extern void delete_phase1(phase1_t *, boolean_t);
extern void create_receiver_phase1(SshIkePMPhaseI);
extern void flush_cache_and_p1s(void);

/* Policy/config functions */
extern void rulebase_datafree(struct ike_rulebase *);
extern boolean_t rulebase_dup(struct ike_rulebase *,
    const struct ike_rulebase *, boolean_t);
extern int rulebase_delete_rule(struct ike_rulebase *, int);
extern void rulebase_dbg_walk(struct ike_rulebase *);
extern int rulebase_lookup(const struct ike_rulebase *, char *, int);
extern int rulebase_lookup_nth(const struct ike_rulebase *, int);
extern SshIkePayloadID construct_local_id(const struct certlib_cert *,
    const struct sockaddr_storage *, const struct ike_rule *);
extern int config_write(int);
extern FILE *yyin;
extern int yyparse(void);
extern void get_scf_properties(void);
extern void check_rule(struct ike_rule *, boolean_t);

/* Per-server address cache (for initial contact stuff) functions */
extern void addrcache_add(addrcache_t *, struct sockaddr_storage *, time_t);
extern void addrcache_delete(addrcache_t *, struct sockaddr_storage *);
extern addrentry_t *addrcache_check(addrcache_t *, struct sockaddr_storage *);
extern void addrcache_destroy(addrcache_t *);

/* Algorithm lookup functions. */
const p2alg_t *find_esp_encr_alg(uint_t);
char *kef_alg_to_string(int, int, char *);
uint8_t encr_alg_lookup(const char *name, int *, int *);

/*
 * Callback state blocks, used at various places within iked where we
 * have to defer processing until we get a response from PF_KEY.
 */

/*
 * pfkeyreq_t: basic callback block, used either on its own or as part of
 * another transaction.
 */

typedef struct pfkeyreq_s
{
	struct pfkeyreq_s	*pr_next;
	struct pfkeyreq_s	*pr_prev;
	sadb_msg_t 		*pr_req;
	void 			(*pr_handler)
					(struct pfkeyreq_s *, sadb_msg_t *);
	void			*pr_context;
} pfkeyreq_t;

/*
 * spiwait_t: generic "I'm waiting for GETSPI" state.
 */

typedef struct spiwait_s
{
	pfkeyreq_t 		sw_req;
	void 			(*sw_done)(struct spiwait_s *);
	uint8_t 		*sw_spi_ptr;
	void 			*sw_context;
} spiwait_t;

/*
 * saselect_t, used on responder when choosing among proposals from a peer;
 * this covers both INVERSE_ACQUIRE and GETSPI.
 */

typedef struct saselect_s
{
	pfkeyreq_t 		ssa_pfreq;
	spiwait_t		ssa_spiwait;
	SshIkePMPhaseQm 	ssa_pm_info;
	SshIkeNegotiation 	ssa_negotiation;
	int 			ssa_nsas;
	SshIkePayload 		*ssa_sas;
	SshPolicyQmSACB 	ssa_callback;
	void 			*ssa_context;
	boolean_t		tunnel_mode;
	struct sockaddr_storage ssa_local;
	struct sockaddr_storage ssa_remote;

	SshIkeIpsecSelectedSAIndexes ssa_selection;
	int			ssa_sit;

	void (*ssa_complete)(struct saselect_s *, parsedmsg_t *);
} saselect_t;

/*
 * p2initiate_t, used on initiator when the initiator needs to do one
 * or more GETSPI's before making a phase-II proposal.
 */
typedef struct p2initiate_s
{
	spiwait_t		spiwait;
	parsedmsg_t 		*pmsg;
	SshIkeNegotiation	phase1_neg;
	int			group;	/* Oakley group for PFS (0 for none) */
	SshIkePayloadID 	local_id;
	SshIkePayloadID 	remote_id;
	SshIkePayloadSA 	*props;
	int			num_props;
	unsigned int
		need_ah : 1,
		need_esp : 1,
		pad_bits : 30;
	uint32_t	ah_spi;
	uint32_t	esp_spi;
} p2initiate_t;

/* PF_KEY and related functions */
extern int auth_id_to_attr(int);
extern int auth_ext_to_doi(int);
extern boolean_t open_pf_key(void);
extern void pf_key_init(void);
extern void our_sa_handler(SshIkeNegotiation, SshIkePMPhaseQm, int,
    SshIkeIpsecSelectedSA, SshIkeIpsecKeymat, void *);
extern void getspi(spiwait_t *, struct sockaddr_storage *,
    struct sockaddr_storage *, uint8_t, uint8_t, uint8_t *,
    void (*sw_done)(spiwait_t *), void *);
extern void free_pmsg(parsedmsg_t *);
extern void update_sa_lifetime(uint32_t, uint64_t, uint64_t,
    struct sockaddr_storage *, uint8_t);
extern sadb_ident_t *payloadid_to_pfkey(SshIkePayloadID, boolean_t);
extern SshIkePayloadID pfkeyid_to_payload(sadb_ident_t *, int);
extern void delete_assoc(uint32_t, struct sockaddr_storage *,
    struct sockaddr_storage *, SshIkePayloadID, SshIkePayloadID, uint8_t);
extern void send_negative_acquire(parsedmsg_t *, int);
extern void inverse_acquire(saselect_t *);
extern void update_assoc_lifetime(uint32_t, uint64_t, uint64_t, uint64_t,
    uint64_t, struct sockaddr_storage *sa, uint8_t);

/* DPD functions */
extern void start_dpd_process(parsedmsg_t *);
extern void rewhack_dpd_expire(parsedmsg_t *);
extern void pfkey_idle_timer(void *);
extern void handle_dpd_action(parsedmsg_t *, uint8_t);


/* Misc. utility functions. */
extern const char *sap(const struct sockaddr_storage *);
boolean_t in6_addr_cmp(in6_addr_t *, in6_addr_t *);
void pfkey_inner_to_id4(SshIkePayloadID, int, struct sockaddr_in *);
void pfkey_inner_to_id6(SshIkePayloadID, int, struct sockaddr_in6 *);
/* XXX This one really needs to be put in libnsl */
boolean_t in_prefixlentomask(int, int, uchar_t *);
void fix_p1_label_range(phase1_t *);
void label_update(void);

/* Initiator functions */
extern phase1_t *get_phase1(parsedmsg_t *, boolean_t);
extern void phase1_notify(SshIkeNotifyMessageType, SshIkeNegotiation, void *);
extern int initiate_phase1(phase1_t *, SshIkeNegotiation *);

/* Certificate initialization functions */
extern boolean_t cmi_init(void);
extern void cmi_reload(void);

/* Private key functions */
extern void setup_private_key(struct certlib_cert *);

/* Sockaddr/string converters. */
extern boolean_t sockaddr_to_string(const struct sockaddr_storage *, uchar_t *);
extern boolean_t string_to_sockaddr(uchar_t *, struct sockaddr_storage *);

/* Door initialization/destruction. */
extern void ike_door_destroy(void);
extern void ike_door_init(void);

/* Preshared key functions. */
extern struct preshared_entry_s *lookup_pre_shared_key(SshIkePMPhaseI);
extern int sadb2psid(int);
extern void preshared_init(void);
extern void preshared_reload(void);

/* Labeling related functions */
void init_system_label(sadb_sens_t *);
void set_ike_label_aware(void);
boolean_t is_ike_labeled(void);
boolean_t sit_to_bslabel(SshIkeIpsecSituationPacket, bslabel_t *);
void prtdbg_label(char *, bslabel_t *);
bslabel_t *string_to_label(char *);
extern void init_rule_label(struct ike_rule *, struct ike_rule *);
extern void set_outer_label(struct ike_rule *, boolean_t, bslabel_t *);

extern char *ldap_path;
extern char *pkcs11_path;

extern uint16_t nat_t_port;

extern struct SshIkeParamsRec ike_params;

extern SshX509Config x509config;

extern boolean_t ike_group_supported(int);
extern boolean_t ike_cipher_supported(int);
extern boolean_t ike_hash_supported(int);
int ike_get_cipher(int, const keywdtab_t **);
int ike_get_hash(int, const keywdtab_t **);

extern void p1_localcert_reset(void);

/* Structure for linked list passed in from in.iked */
typedef struct cachent_s {
	ike_certcache_t *cache_ptr;
	struct cachent_s *next;
} cachent_t;

extern cachent_t *cacheptr_head;
extern cachent_t *cacheptr_current;

extern void get_cert_cache(void);
extern void free_cert_cache(void);
extern void flush_cert_cache(void);
extern boolean_t cache_error;

extern int del_private(struct certlib_keys *);
extern int accel_private(struct certlib_keys *);

extern void get_host_label_range(struct sockaddr_storage *ss,
    int *doi, bslabel_t *min, bslabel_t *max, boolean_t *);
extern size_t roundup_bits_to_64(size_t);


#ifdef	__cplusplus
}
#endif

#endif	/* _IKED_COMMON_DEFS_H */
