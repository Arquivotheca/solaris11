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
 * Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/pfkeyv2.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <stdio.h>
#include <locale.h>
#include <ipsec_util.h>

#include "defs.h"

#include <ike/ssheloop.h>
#include <ike/isakmp_util.h>
#include <ike/isakmp_internal.h>

#define	SADB_MULTIBITS(alg) \
	(alg)->sadb_x_algdesc_minbits != (alg)->sadb_x_algdesc_maxbits
#define	INCR_MSG \
	"WARN: keylen increment is 0 for algorithm %d, check ipsecalgs(1M)"

/* ARGSUSED */
static void
phase2_notify(SshIkeNotifyMessageType error, SshIkeNegotiation phase2_neg,
    void *cookie)
{
	if (error != SSH_IKE_NOTIFY_MESSAGE_CONNECTED)
		PRTDBG(D_P2, ("Phase 2 negotiation error: code %d (%s).",
		    error, ssh_ike_error_code_to_string(error)));
}

/*
 * Take a new PF_KEY algorithm descriptor and insert it into the proposal.
 */
static boolean_t
handle_algdesc(sadb_x_ecomb_t *ecomb, SshIkePayloadP proposal,
    sadb_x_algdesc_t *algdesc, sadb_x_algdesc_t **esp_auths,
    phase1_t *p1, boolean_t tunnel_mode)
{
	SshIkePayloadPProtocol protocol = NULL, oldproto;
	SshIkePayloadT transform = NULL, oldxform;
	SshIkeSAAttributeList alist;
	uint8_t satype = algdesc->sadb_x_algdesc_satype;
	size_t spi_size;
	int i;
	boolean_t use_natt =
	    (p1->p1_pminfo != NULL &&
	    NEED_NATT(p1->p1_pminfo->p1_natt_state));
	int encap_mode, keysize;

	/*
	 * Set encapsulation mode value now.
	 *
	 * DOI NOTE: Currently using PF_KEY's sadb_address_proto attribute to
	 * determine tunnel vs. transport.  Also, figure out whether to use
	 * the I-D NAT-T values, or the RFC/IANA NAT-T values.
	 */
	if (tunnel_mode) {
		if (use_natt)
			encap_mode =
			    IPSEC_VALUES_ENCAPSULATION_MODE_UDP_TUNNEL;
		else
			encap_mode = IPSEC_VALUES_ENCAPSULATION_MODE_TUNNEL;
	} else {
		if (use_natt)
			encap_mode =
			    IPSEC_VALUES_ENCAPSULATION_MODE_UDP_TRANSPORT;
		else
			encap_mode =
			    IPSEC_VALUES_ENCAPSULATION_MODE_TRANSPORT;
	}

	switch (satype) {
	case SADB_SATYPE_ESP:
		spi_size = 4;
		break;
	case SADB_SATYPE_AH:
		spi_size = 4;
		break;
	default:
		EXIT_FATAL2("Can't understand SA type %d (not ESP or AH).",
		    satype);
	}

	/* DOI NOTE - Assume SADB_SATYPE_* == protocol->protocol_id */
	for (i = 0; i < proposal->number_of_protocols; i++)
		if (satype == proposal->protocols[i].protocol_id) {
			protocol = &(proposal->protocols[i]);
			break;
		}

	if (protocol == NULL) {
		/*
		 * Allocate a new protocol slot in the proposal.
		 */
		oldproto = proposal->protocols;
		proposal->protocols = ssh_realloc(proposal->protocols,
		    proposal->number_of_protocols * sizeof (*protocol),
		    (proposal->number_of_protocols + 1) * sizeof (*protocol));
		if (proposal->protocols == NULL) {
			proposal->protocols = oldproto;
			PRTDBG(D_P2, ("Not enough memory to add new protocol "
			    "slot to proposal"));
			return (B_FALSE);
		}

		protocol = proposal->protocols + proposal->number_of_protocols;
		proposal->number_of_protocols++;

		protocol->spi_size = spi_size;
		/* Allocate SPI, but fill in later. */
		protocol->spi = ssh_malloc(spi_size);
		if (protocol->spi == NULL) {
			PRTDBG(D_P2, ("Out of memory."));
			return (B_FALSE);
		}

		protocol->protocol_id = satype;
		protocol->number_of_transforms = 0;
		protocol->transforms = NULL;
	}

	/*
	 * We have found the protocol.  Now find a transform.
	 * This gets tricky if we have an auth algorithm with no existing
	 * cipher.
	 */

	switch (algdesc->sadb_x_algdesc_algtype) {
	case SADB_X_ALGTYPE_AUTH:
		if (satype == SADB_SATYPE_ESP) {
			EXIT_FATAL("Internal error: auth algs passed with "
			    "proto ESP, expected AH here.");
		}
		/* Add transform (and auth attribute) for AH. */
		break;
	case SADB_X_ALGTYPE_CRYPT:
		assert(satype == SADB_SATYPE_ESP);
		/* Add transform type for ESP. */
		break;
	case SADB_X_ALGTYPE_COMPRESS:
		/* No support yet. */
		/* FALLTHRU */
	default:
		EXIT_FATAL2("Algorithm type %d unknown.",
		    algdesc->sadb_x_algdesc_algtype);
	}

	/*
	 * Allocate new transform.
	 */

	keysize = 0;
#define	ALGDESC_MAX_XFORMS 255
	do {
		if (protocol->number_of_transforms >= ALGDESC_MAX_XFORMS) {
			/* Stop adding transforms in this case. */
			return (B_TRUE);
		}
		oldxform = protocol->transforms;
		protocol->transforms = ssh_realloc(protocol->transforms,
		    protocol->number_of_transforms * sizeof (*transform),
		    (protocol->number_of_transforms + 1) * sizeof (*transform));
		if (protocol->transforms == NULL) {
			protocol->transforms = oldxform;
			return (B_FALSE);
		}
		transform =
		    &(protocol->transforms[protocol->number_of_transforms]);
		protocol->number_of_transforms++;

		/*
		 * This _can_ be any monotonically increasing sequence!
		 *
		 * WARNING:  Even though transform_number is an int,
		 * what goes out on the wire is a uint8_t, so make sure
		 * you don't monotonically wrap over 255 in your transform
		 * numbers.
		 */
		transform->transform_number =
		    protocol->number_of_transforms + 1;

		/* Assume PF_KEY maps to transform ID DOI values. */
		transform->transform_id.generic = algdesc->sadb_x_algdesc_alg;

		/*
		 * Attributes we might want per transform include:
		 *
		 *	- Lifetime type   (uint16_t) (MUST)
		 *	- Lifetime duration (uint32_t or uint64_t) (MUST)
		 *	- Auth algorithm  (uint16_t) (AH or ESP w/MAC)
		 *	- Encapsulation mode (uint16_t)
		 *	- Group description for PFS. (uint16_t)
		 *	- Key length for variable keylen ciphers. (uint16_t)
		 *	- Key rounds for variable round ciphers. (uint16_t)
		 *
		 * Thanks to isakmp_doi.h and RFC 2407 for the help!
		 */

		/*
		 * Allocate a data attribute list.
		 *
		 * Routines for the list are in isakmp_util.c.  They do all of
		 * the bytesex conversion for ...add_int() and ...add_basic().
		 */
		alist = ssh_ike_data_attribute_list_allocate();
		/* This panics for us. :-P */

		/* Add the lifetime attributes now, if needed. */
		if (ecomb->sadb_x_ecomb_hard_bytes != 0 ||
		    p1->p1_rule->p2_lifetime_kb != 0) {
			ssh_ike_data_attribute_list_add_basic(alist,
			    IPSEC_CLASSES_SA_LIFE_TYPE,
			    IPSEC_VALUES_LIFE_TYPE_KILOBYTES);
			ssh_ike_data_attribute_list_add_int(alist,
			    IPSEC_CLASSES_SA_LIFE_DURATION,
			    (p1->p1_rule->p2_lifetime_kb != 0) ?
			    (uint64_t)(p1->p1_rule->p2_lifetime_kb) :
			    (uint64_t)(ecomb->sadb_x_ecomb_hard_bytes)
			    >> (uint64_t)10);
		}

		if (ecomb->sadb_x_ecomb_hard_addtime != 0) {
			ssh_ike_data_attribute_list_add_basic(alist,
			    IPSEC_CLASSES_SA_LIFE_TYPE,
			    IPSEC_VALUES_LIFE_TYPE_SECONDS);
			ssh_ike_data_attribute_list_add_int(alist,
			    IPSEC_CLASSES_SA_LIFE_DURATION,
			    (p1->p1_rule->p2_lifetime_secs == 0) ?
			    ecomb->sadb_x_ecomb_hard_addtime :
			    p1->p1_rule->p2_lifetime_secs);
		}

		/* Auth algorithm attribute */
		if (algdesc->sadb_x_algdesc_satype == SADB_SATYPE_AH ||
		    (esp_auths != NULL && *esp_auths != NULL)) {
			sadb_x_algdesc_t *authdesc;

			if (algdesc->sadb_x_algdesc_satype == SADB_SATYPE_AH)
				authdesc = algdesc;
			else
				authdesc = esp_auths[0];

			assert(authdesc->sadb_x_algdesc_algtype ==
			    SADB_X_ALGTYPE_AUTH);

			/*
			 * Stupid values for the auth alg attribute don't
			 * match the DOI.  <Grumble..>
			 */
			ssh_ike_data_attribute_list_add_basic(alist,
			    IPSEC_CLASSES_AUTH_ALGORITHM,
			    auth_id_to_attr(authdesc->sadb_x_algdesc_alg));
		}

		ssh_ike_data_attribute_list_add_basic(alist,
		    IPSEC_CLASSES_ENCAPSULATION_MODE, encap_mode);

		if (p1->p1_p2_group != 0)
			ssh_ike_data_attribute_list_add_basic(alist,
			    IPSEC_CLASSES_GRP_DESC, p1->p1_p2_group);

		/*
		 * If algorithm requires keylen attribute, supply one.
		 */
		if (algdesc->sadb_x_algdesc_satype == SADB_SATYPE_ESP) {
			const p2alg_t *p2alg;
			int keylen_incr;

			p2alg = find_esp_encr_alg(algdesc->sadb_x_algdesc_alg);

			/*
			 * To solve the 'SPD policy versus crypto module
			 * capabilities versus theoretical algorithm
			 * limits' problem we will just check for the
			 * presence of algorithm entry because PF_KEY
			 * (via p2_esp_encr_algs array) tells us the
			 * actual not theoretical key length limits.
			 */
			if (p2alg != NULL) {
				/*
				 * Check to see if we have a variable key-sized
				 * cipher. If so, we must send one or more
				 * transforms, for each key increment. Fixed
				 * key length ciphers don't need the key length
				 * attribute.
				 */
				if (p2alg->p2alg_min_bits !=
				    p2alg->p2alg_max_bits) {
					keylen_incr = p2alg->p2alg_key_len_incr;
					if (keysize == 0) {
						/*
						 * Initial case.  Take
						 * highest value from the
						 * ACQUIRE.  Kernel will set
						 * sane values.
						 */
						keysize = algdesc->
						    sadb_x_algdesc_maxbits;
					}
					ssh_ike_data_attribute_list_add_basic(
					    alist, IPSEC_CLASSES_KEY_LENGTH,
					    keysize);
					if (keysize - keylen_incr <
					    algdesc->sadb_x_algdesc_minbits)
						keysize = 0;
					else
						keysize -= keylen_incr;
					/*
					 * ipsecalgs entry could be missing
					 * increment value.
					 */
					if (keylen_incr == 0) {
						uint8_t alg_id = algdesc->
						    sadb_x_algdesc_alg;
						if (SADB_MULTIBITS(algdesc))
							PRTDBG(D_P2, (INCR_MSG,
							    alg_id));
						/* terminate the cycle */
						keysize = 0;
					}
				}
			} else {
				EXIT_FATAL2("Algorithm %d unsupported.",
				    algdesc->sadb_x_algdesc_alg);
			}
		}
		/*
		 * TODO: if algorithm requires "rounds" attribute, supply
		 * one here. (there are no such ciphers currently supported)
		 */

		/* Put the list into the transform structure... */
		transform->sa_attributes = ssh_ike_data_attribute_list_get(
		    alist, &transform->number_of_sa_attributes);
		/* Panics for us.  :-P */
		ssh_ike_data_attribute_list_free(alist);

		/*
		 * Catch corner case of esp_auths pointing to a NULL-only
		 * array.  (That is, a zero-length esp_auths list, in lieu of
		 * esp_auths == NULL.)
		 */
		if (keysize == 0 && esp_auths != NULL && *esp_auths == NULL)
			return (B_TRUE);

		/*
		 * NOTE: To deal with multiple-AES keysizes, loop on
		 * current_keysize FIRST, then check for esp_auth cases.
		 */
	} while (keysize != 0 || (esp_auths != NULL && *(++esp_auths) != NULL));

	return (B_TRUE);
}

static unsigned char *
convert_sit_bitmap(SshUInt16 *len, uint8_t sens_len, uint64_t *sens_data)
{
	size_t bytes = SADB_64TO8(sens_len);
	unsigned char *bitmap = ssh_malloc(bytes);

	if (bitmap == NULL)
		return (NULL);

	(void) memcpy(bitmap, sens_data, bytes);
	*len = SADB_8TO1(bytes);

	return (bitmap);
}

static int
check_peer_label(sadb_sens_t *sens, phase1_t *p1)
{
	bslabel_t label;

	ipsec_convert_sens_to_bslabel(sens, &label);

	/*
	 * If the peer isn't flagged as label-aware, only allow traffic which
	 * precisely matches the label we'd compute for them.
	 */

	if (!p1->label_aware) {
		if (!blequal(&label, &p1->max_sl)) {
			PRTDBG(D_LABEL,
			    ("  Permission denied.  Phase 2 security label "
			    "does not match label established during phase "
			    "1."));
			prtdbg_label("  Phase 1 label", &p1->max_sl);
			prtdbg_label("  Phase 2 label", &label);
			return (EPERM);
		}

	/*
	 * does the label fall into the peer's label range?
	 */
	} else {
		if (!bldominates(&label, &p1->min_sl) ||
		    !bldominates(&p1->max_sl, &label)) {
			PRTDBG(D_LABEL,
			    ("  Permission denied.  Phase 2 security label is "
			    "not within label range established during phase "
			    "1."));
			prtdbg_label("  Phase 1 min label", &p1->min_sl);
			prtdbg_label("  Phase 1 max label", &p1->max_sl);
			prtdbg_label("  Phase 2 label", &label);
			return (EPERM);
		}
	}

	return (0);
}



/*
 * Convert the sensitivity extension into the ISAKMP situation equivalent.
 * XXX TBD TBD XXX
 * This is a rote conversion.  Policy will need to intervene here and
 * decide whether (a) to keep this local or (b) push it on the wire.
 */
static int
construct_sensitive_sit(phase1_t *p1, SshIkeIpsecSituationPacket sit,
    sadb_sens_t *sens)
{
	uint64_t *sensbits = (uint64_t *)(sens + 1);
	uint64_t *integbits = sensbits + sens->sadb_sens_sens_len;
	int error;

	sit->secrecy_level_data = NULL;
	sit->integrity_level_data = NULL;
	sit->secrecy_category_bitmap_data = NULL;
	sit->integrity_category_bitmap_data = NULL;

	error = check_peer_label(sens, p1);
	if (error != 0)
		return (error);

	if (!is_ike_labeled())
		return (0);

	if (!p1->label_aware)
		return (0);

	sit->labeled_domain_identifier = sens->sadb_sens_dpd;

	if (sens->sadb_sens_sens_len != 0) {
		sit->situation_flags |= SSH_IKE_SIT_SECRECY;

		sit->secrecy_level_length = 1;
		sit->secrecy_level_data = ssh_malloc(1);
		if (sit->secrecy_level_data == NULL)
			goto fail;
		sit->secrecy_level_data[0] = sens->sadb_sens_sens_level;
		sit->secrecy_category_bitmap_data = convert_sit_bitmap(
		    &sit->secrecy_category_bitmap_length,
		    sens->sadb_sens_sens_len, sensbits);
		if (sit->secrecy_category_bitmap_data == NULL)
			goto fail;
	}
	if (sens->sadb_sens_integ_len != 0) {
		sit->situation_flags |= SSH_IKE_SIT_INTEGRITY;

		sit->integrity_level_length = 1;
		sit->integrity_level_data = ssh_malloc(1);
		if (sit->integrity_level_data == NULL)
			goto fail;
		sit->integrity_level_data[0] = sens->sadb_sens_integ_level;
		sit->integrity_category_bitmap_data = convert_sit_bitmap(
		    &sit->integrity_category_bitmap_length,
		    sens->sadb_sens_integ_len, integbits);
		if (sit->integrity_category_bitmap_data == NULL)
			goto fail;
	}
	return (0);
fail:

#define	FREEFIELD(x) \
	if (sit->x != NULL) {			\
		ssh_free(sit->x);		\
		sit->x = NULL;			\
	}

	FREEFIELD(secrecy_level_data);
	FREEFIELD(secrecy_category_bitmap_data);
	FREEFIELD(integrity_level_data);
	FREEFIELD(integrity_category_bitmap_data);

#undef FREEFIELD

	return (ENOMEM);
}

/*
 * Construct the IKE SA payload based on the PF_KEY _extended_ ACQUIRE.
 * (Hence the 'e' in 'eprops'.)
 */
static SshIkePayloadSA *
construct_eprops(p2initiate_t *p2p, sadb_prop_t *prop, phase1_t *p1,
    boolean_t tunnel_mode, sadb_sens_t *sens, int *error)
{
	SshIkePayloadSA *rc, sa_prop = NULL;
	SshIkePayloadP proposals;
	sadb_x_ecomb_t *ecomb = (sadb_x_ecomb_t *)(prop + 1), *next_ecomb;
	sadb_x_algdesc_t *algdesc, **oldad, **esp_auths, **esp_ciphers;
	int i, j, num_auths = 0, num_ciphers = 0;

	p2p->num_props = 1;
	*error = 0;

	sa_prop = (SshIkePayloadSA)ssh_calloc(1, sizeof (*sa_prop));
	if (sa_prop == NULL) {
		PRTDBG(D_P2, ("Out of memory constructing extended acquire"
		    " properties for SA payload."));
		*error = ENOMEM;
		return (NULL);
	}
	sa_prop->doi = SSH_IKE_DOI_IPSEC;
	sa_prop->situation.situation_flags = SSH_IKE_SIT_IDENTITY_ONLY;

	if (sens != NULL && (sens->sadb_sens_dpd != 0)) {
		*error = construct_sensitive_sit(p1, &sa_prop->situation, sens);
		if (*error != 0) {
			ssh_ike_free_sa_payload(sa_prop);
			return (NULL);
		}
	}

	sa_prop->number_of_proposals = prop->sadb_x_prop_numecombs;

	sa_prop->proposals = ssh_calloc(sa_prop->number_of_proposals,
	    sizeof (*proposals));
	if (sa_prop->proposals == NULL) {
		ssh_ike_free_sa_payload(sa_prop);
		PRTDBG(D_P2, ("Out of memory constructing proposals"
		    " for SA payload."));
		*error = ENOMEM;
		return (NULL);
	}

	proposals = sa_prop->proposals;

	for (i = 0; i < sa_prop->number_of_proposals; i++) {
		/*
		 * Step through the extended ACQUIRE and set up the proposal(s).
		 *
		 * In the extended ACQUIRE, sadb_x_ecomb_t will correspond
		 * to a new entry in the sa_prop->proposals array.
		 *
		 * An sadb_x_algdesc_t will correspond to a tuple <protocol,
		 * transform>.  This'll be mildly annoying, as I'll have to
		 * keep track of transforms and protocols separately, but
		 * it shouldn't be that painful.
		 */

		algdesc = (sadb_x_algdesc_t *)(ecomb + 1);
		next_ecomb = (sadb_x_ecomb_t *)
		    (algdesc + ecomb->sadb_x_ecomb_numalgs);

		/* For now, start at 0. */
		proposals[i].proposal_number = i;

		esp_auths = ssh_malloc(sizeof (sadb_x_algdesc_t *));
		esp_ciphers = ssh_malloc(sizeof (sadb_x_algdesc_t *));
		if (esp_auths == NULL || esp_ciphers == NULL) {
			PRTDBG(D_P2, ("Out of memory constructing AH/ESP in "
			    "extended acquire."));
			ssh_free(esp_auths);
			ssh_free(esp_ciphers);
			ssh_ike_free_sa_payload(sa_prop);
			*error = ENOMEM;
			return (NULL);
		}

		esp_auths[0] = NULL;
		esp_ciphers[0] = NULL;

		for (j = 0; j < ecomb->sadb_x_ecomb_numalgs; j++) {
			if (algdesc[j].sadb_x_algdesc_satype ==
			    SADB_SATYPE_ESP) {
				p2p->need_esp = B_TRUE;
				if (algdesc[j].sadb_x_algdesc_algtype ==
				    SADB_X_ALGTYPE_AUTH) {
					num_auths++;
					/*
					 * Remember, esp_auths is a NULL-
					 * terminated array.  So the size of
					 * it is num_auths (newly-incremented)
					 * plus 1.
					 */
					oldad = esp_auths;
					esp_auths = ssh_realloc(esp_auths,
					    num_auths *
					    sizeof (sadb_x_algdesc_t *),
					    ((num_auths + 1) *
					    sizeof (sadb_x_algdesc_t *)));
					if (esp_auths != NULL) {
						esp_auths[num_auths] = NULL;
						esp_auths[num_auths - 1] =
						    algdesc + j;
					} else {
						num_auths--;
						esp_auths = oldad;
					}
				} else {
					assert(algdesc[j].
					    sadb_x_algdesc_algtype ==
					    SADB_X_ALGTYPE_CRYPT);
					num_ciphers++;
					/*
					 * Remember, esp_ciphers is a NULL-
					 * terminated array.  So the size of
					 * it is num_ciphers (newly-incremented)
					 * plus 1.
					 */
					oldad = esp_ciphers;
					esp_ciphers = ssh_realloc(esp_ciphers,
					    num_ciphers *
					    sizeof (sadb_x_algdesc_t *),
					    ((num_ciphers + 1) *
					    sizeof (sadb_x_algdesc_t *)));
					if (esp_ciphers != NULL) {
						esp_ciphers[num_ciphers] = NULL;
						esp_ciphers[num_ciphers - 1] =
						    algdesc + j;
					} else {
						num_ciphers--;
						esp_ciphers = oldad;
					}
				}
			} else {
				boolean_t need_natt = (p1->p1_pminfo != NULL &&
				    NEED_NATT(p1->p1_pminfo->p1_natt_state));

				if (need_natt)
					PRTDBG(D_P2, ("AH not supported with "
					    "NAT-T."));

				p2p->need_ah = B_TRUE;
				if (need_natt ||
				    !handle_algdesc(ecomb, proposals + i,
				    algdesc + j, NULL, p1, tunnel_mode)) {
					/* handle_algdesc() logs error. */
					ssh_free(esp_auths);
					ssh_free(esp_ciphers);
					ssh_ike_free_sa_payload(sa_prop);
					*error = ENOMEM;
					return (NULL);
				}
			}
		}

		/*
		 * Handle ESP algs out of sequence, because of combinatoric
		 * weirdness and that ESP auth algs aren't transforms!
		 */
		if (num_ciphers == 0 && num_auths != 0) {
			EXIT_FATAL("PF_KEY didn't return the NULL "
			    "encryption algorithm.");
		}

		for (j = 0; j < num_ciphers; j++) {
			if (!handle_algdesc(ecomb, proposals + i,
			    esp_ciphers[j], esp_auths, p1, tunnel_mode)) {
				/* handle_algdesc() logs error. */
				ssh_free(esp_auths);
				ssh_free(esp_ciphers);
				ssh_ike_free_sa_payload(sa_prop);
				*error = ENOMEM;
				return (NULL);
			}
		}

		ssh_free(esp_auths);
		ssh_free(esp_ciphers);
		esp_auths = NULL;
		esp_ciphers = NULL;
		num_auths = 0;
		num_ciphers = 0;

		ecomb = next_ecomb;
	}

	rc = ssh_calloc(1, sizeof (sa_prop));
	if (rc == NULL)
		ssh_ike_free_sa_payload(sa_prop);
	else
		rc[0] = sa_prop;
	return (rc);
}

static void start_phase2(p2initiate_t *);
static void alloc_spis(spiwait_t *);

/*
 * Convert a prefix length to a mask.
 * Returns B_TRUE if ok. B_FALSE otherwise.
 * Assumes the mask array is zero'ed by the caller.
 * XXX This function really needs to be put in libnsl.
 */
boolean_t
in_prefixlentomask(int prefixlen, int maxlen, uchar_t *mask)
{
	if (prefixlen < 0 || prefixlen > maxlen)
		return (B_FALSE);

	while (prefixlen > 0) {
		if (prefixlen >= 8) {
			*mask++ = 0xFF;
			prefixlen -= 8;
			continue;
		}
		*mask |= 1 << (8 - prefixlen);
		prefixlen--;
	}
	return (B_TRUE);
}

void
pfkey_inner_to_id4(SshIkePayloadID idp, int pfxlen, struct sockaddr_in *sin)
{
	if (pfxlen == 32) {
		idp->identification_len = sizeof (struct in_addr);
		idp->id_type = IPSEC_ID_IPV4_ADDR;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		*((uint32_t *)idp->identification.ipv4_addr) =
		    sin->sin_addr.s_addr;
	} else {
		idp->id_type = IPSEC_ID_IPV4_ADDR_SUBNET;
		idp->identification_len =
		    2 * sizeof (struct in_addr);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		*((uint32_t *)idp->identification.ipv4_addr_subnet) =
		    sin->sin_addr.s_addr;
		(void) in_prefixlentomask(pfxlen, 32,
		    idp->identification.ipv4_addr_netmask);
	}
}
void
pfkey_inner_to_id6(SshIkePayloadID idp, int pfxlen, struct sockaddr_in6 *sin6)
{
	if (pfxlen == 128) {
		idp->id_type = IPSEC_ID_IPV6_ADDR;
		idp->identification_len = sizeof (in6_addr_t);
		(void) memcpy(idp->identification.ipv6_addr,
		    &sin6->sin6_addr, sizeof (in6_addr_t));
	} else {
		idp->id_type = IPSEC_ID_IPV6_ADDR_SUBNET;
		idp->identification_len = 2 * sizeof (in6_addr_t);
		(void) memcpy(idp->identification.ipv6_addr_subnet,
		    &sin6->sin6_addr, sizeof (in6_addr_t));
		(void) in_prefixlentomask(pfxlen, 128,
		    idp->identification.ipv6_addr_netmask);
	}
}

/*
 * We're finished with the Phase 1 negotiation (that I as an initiator
 * kicked off with ssh_ike_connect()).  Now kick off a Phase 2 negotiation.
 * The ACQUIRE message that triggered us is attached to the phase1_t in the
 * cookie.  (As well as any other ACQUIREs that got saddled up.)
 */
void
phase1_notify(SshIkeNotifyMessageType error, SshIkeNegotiation phase1_neg,
    void *cookie)
{
	phase1_t *p1 = (phase1_t *)cookie;
	parsedmsg_t *pmsg;
	sadb_msg_t *samsg;
	sadb_prop_t *prop;
	sadb_address_t *srcext, *dstext;
	/* TODO: IPv6, sin6 too. */
	struct sockaddr_storage *src;
	SshIkePayloadID local_id = NULL, remote_id = NULL;
	sadb_ext_t **exts;
	p2initiate_t *p2p;
	boolean_t tunnel_mode = B_FALSE;
	sadb_sens_t *sens;
	int errno;

	switch (error) {
	case SSH_IKE_NOTIFY_MESSAGE_CONNECTED:
		/* Handle outside the switch. */
		break;
	case SSH_IKE_NOTIFY_MESSAGE_NO_PROPOSAL_CHOSEN:
		/*
		 * TODO: Send PF_KEY error to this ACQUIRE.
		 */
		PRTDBG(D_P1, ("Receiver didn't like our proposals, Phase 1 "
		    "negotiation unsuccessful."));
		/*
		 * ssh_policy_isakmp_sa_freed() will cleanup the phase1_t.
		 */
		return;
	case SSH_IKE_NOTIFY_MESSAGE_TIMEOUT:
		p1->p1_dpd_status = DPD_FAILURE;
		/* FALLTHRU */
	default:
		/* TODO: Send PF_KEY error to this ACQUIRE. */
		PRTDBG(D_P1, ("Phase 1 error: code %d (%s).",
		    error, ssh_ike_error_code_to_string(error)));
		/*
		 * ssh_policy_isakmp_sa_freed() will cleanup the phase1_t.
		 */
		return;
	}

	assert(p1->p1_negotiation == phase1_neg);

	PRTDBG(D_P1|D_P2, ("Phase 1 negotiation done. "));
	if (!p1->p1_create_phase2) {
		PRTDBG(D_P1|D_P2, ("Not starting Phase 2 as "
		    "Phase 1 negotiation initiated for DPD handshake"));

		/*
		 * free the pmsg here, since we are not starting
		 * the phase 2.
		 */
		while (p1->p1_pmsg != NULL) {
			pmsg = p1->p1_pmsg;
			p1->p1_pmsg = pmsg->pmsg_next;
			pmsg->pmsg_next = NULL;
			if (p1->p1_use_dpd &&
			    p1->p1_dpd_status != DPD_IN_PROGRESS) {
				/* Kick DPD in the pants now! */
				p1->p1_dpd_status = DPD_IN_PROGRESS;
				rewhack_dpd_expire(pmsg);
				start_dpd_process(pmsg);
			} else {
				free_pmsg(pmsg);
			}
		}
		p1->p1_pmsg_tail = NULL;
		return;
	}

	/*
	 * We now have a working Phase 1 IPsec SA.
	 *
	 * Let's continue handling the ACQUIRE message that got us started, as
	 * well as any ACQUIRE messages that got queued up on this phase1 SA.
	 * This means turning the ACQUIRE into a quick mode exchange, in other
	 * words, a call to ssh_ike_connect_ipsec().
	 */

	PRTDBG(D_P1|D_P2, ("Getting ready for phase 2 (Quick Mode)."));
	while (p1->p1_pmsg != NULL) {
		struct sockaddr_in *ssin, *dsin;
		struct sockaddr_in6 *ssin6, *dsin6;
		pmsg = p1->p1_pmsg;
		samsg = pmsg->pmsg_samsg;
		p1->p1_pmsg = pmsg->pmsg_next;
		if (p1->p1_pmsg == NULL)
			p1->p1_pmsg_tail = NULL;
		pmsg->pmsg_next = NULL;
		exts = pmsg->pmsg_exts;

		if (samsg->sadb_msg_satype != SADB_SATYPE_ESP &&
		    samsg->sadb_msg_satype != SADB_SATYPE_AH &&
		    samsg->sadb_msg_satype != SADB_SATYPE_UNSPEC) {
			if (samsg->sadb_msg_errno != 0)
				PRTDBG(D_P2,
				    ("Non AH/ESP/extended ACQUIRE."));
			free_pmsg(pmsg);
			continue;
		}

		if (samsg->sadb_msg_type != SADB_ACQUIRE) {
			/*
			 * DPD-initiated Phase I message.  Skip this.
			 */
			if (p1->p1_use_dpd &&
			    p1->p1_dpd_status != DPD_IN_PROGRESS) {
				/* Kick DPD in the pants now! */
				p1->p1_dpd_status = DPD_IN_PROGRESS;
				rewhack_dpd_expire(pmsg);
				start_dpd_process(pmsg);
			} else {
				PRTDBG(D_P2,
				    ("DPD expire message, no action needed."));
				free_pmsg(pmsg);
			}
			continue;
		}

		/* NULL out extension pointers. */
		prop = (sadb_prop_t *)exts[SADB_X_EXT_EPROP];
		if (prop == NULL) {
			if (exts[SADB_EXT_PROPOSAL] == NULL) {
				free_pmsg(pmsg);
				continue;
			} else {
				prop = (sadb_prop_t *)exts[SADB_EXT_PROPOSAL];
			}
		}

		/* This should be sufficient - inner src never 0.0.0.0/0? */
		srcext = (sadb_address_t *)exts[SADB_X_EXT_ADDRESS_INNER_SRC];
		if (srcext != NULL) {
			/* Set tunnel mode and use proxy address values */
			dstext =
			    (sadb_address_t *)
			    exts[SADB_X_EXT_ADDRESS_INNER_DST];
			tunnel_mode = B_TRUE;
			src = pmsg->pmsg_psss;
			ssin = pmsg->pmsg_pssin;
			dsin = pmsg->pmsg_pdsin;
			ssin6 = pmsg->pmsg_pssin6;
			dsin6 = pmsg->pmsg_pdsin6;
			PRTDBG(D_P2, ("  Tunnel mode [ACQUIRE]"));
		} else {
			srcext = (sadb_address_t *)exts[SADB_EXT_ADDRESS_SRC];
			src = pmsg->pmsg_sss;
			ssin = pmsg->pmsg_ssin;
			dsin = pmsg->pmsg_dsin;
			ssin6 = pmsg->pmsg_ssin6;
			dsin6 = pmsg->pmsg_dsin6;
			PRTDBG(D_P2, ("  Transport mode [ACQUIRE]"));
		}

		/*
		 * Construct local_id and remote_id.  For phase 2 Transport,
		 * mode	this is probably only allowed to be IP addresses
		 * (or perhaps the ports as well...).
		 *
		 * For tunnel mode, this is inner IP addresses or IP addresses
		 * and masks.  We don't deal with ranges for the time being.
		 *
		 *	1.) Use SADB_EXT_ADDRESS_INNER_SRC for inner-src and
		 *	    SADB_EXT_ADDRESS_INNER_DST inner-dst.
		 *	2.) Along those lines, have outbound SA selection apply
		 *	    and inbound SA enforcement apply to such SAs.
		 *	3.) Change the inner identities to match the tunnel
		 *	    stuff.
		 */

		/*
		 * While the SSH library will allow us to pass
		 * in NULL for local_id and remote_id, at
		 * least some other ike implementations get
		 * unhappy with null phase II identities, so
		 * always send something, even if they're
		 * identical to the phase I ip addresses..
		 *
		 * For TCP and UDP, fill in the port
		 * information; the kernel will have sent up
		 * zeros for the ports if they didn't matter.
		 */
		local_id = ssh_calloc(1, sizeof (struct SshIkePayloadIDRec));
		if (local_id == NULL)
			break;	/* out of switch. */
		remote_id = ssh_calloc(1, sizeof (struct SshIkePayloadIDRec));
		if (remote_id == NULL) {
			ssh_free(local_id);
			local_id = NULL;
			break;	/* out of switch. */
		}

		local_id->protocol_id = srcext->sadb_address_proto;
		remote_id->protocol_id = srcext->sadb_address_proto;
		/*
		 * Since sockaddr_in and sockaddr_in6 keep their
		 * ports in the same places, we can cheat here.
		 */
		local_id->port_number = ntohs(ssin->sin_port);
		remote_id->port_number = ntohs(dsin->sin_port);

		if (src->ss_family == AF_INET) {
			if (!tunnel_mode) {
				/*
				 * Transport mode and old-style Solaris 9
				 * tunnel negotiations which look and act like
				 * transport mode and are dealt with here.
				 */
				local_id->id_type = IPSEC_ID_IPV4_ADDR;
				local_id->identification_len =
				    sizeof (ipaddr_t);
				/* LINTED E_BAD_PTR_CAST_ALIGN */
				*((uint32_t *)local_id->
				    identification.ipv4_addr) =
				    ssin->sin_addr.s_addr;
				remote_id->id_type = IPSEC_ID_IPV4_ADDR;
				remote_id->identification_len =
				    sizeof (ipaddr_t);
				/* LINTED E_BAD_PTR_CAST_ALIGN */
				*((uint32_t *)remote_id->
				    identification.ipv4_addr) =
				    dsin->sin_addr.s_addr;
			} else {
				/*
				 * Otherwise we need to explicitly set the
				 * inner ID type to be an IP address or
				 * subnet.  The general case of a range with
				 * fully populated netmask is *NOT* sufficient
				 * and the negotiation will be rejected.
				 */
				pfkey_inner_to_id4(local_id, srcext->
				    sadb_address_prefixlen, ssin);
				pfkey_inner_to_id4(remote_id, dstext->
				    sadb_address_prefixlen, dsin);
			}
		} else {
			assert(src->ss_family == AF_INET6);
			if (!tunnel_mode) {
				/*
				 * Transport mode and old-style Solaris 9
				 * tunnel negotiations which look and act like
				 * transport mode and are dealt with here.
				 */
				local_id->id_type = IPSEC_ID_IPV6_ADDR;
				local_id->identification_len =
				    sizeof (in6_addr_t);
				(void) memcpy(local_id->
				    identification.ipv6_addr,
				    &pmsg->pmsg_ssin6->sin6_addr,
				    sizeof (in6_addr_t));
				remote_id->id_type = IPSEC_ID_IPV6_ADDR;
				remote_id->identification_len =
				    sizeof (in6_addr_t);
				(void) memcpy(remote_id->
				    identification.ipv6_addr,
				    &pmsg->pmsg_dsin6->sin6_addr,
				    sizeof (in6_addr_t));
			} else {
				/*
				 * Otherwise we need to explicitly set the
				 * inner ID type to be an IP address or
				 * subnet.  The general case of a range with
				 * fully populated netmask is *NOT* sufficient
				 * and the negotiation will be rejected.
				 */
				pfkey_inner_to_id6(local_id, srcext->
				    sadb_address_prefixlen, ssin6);
				pfkey_inner_to_id6(remote_id, dstext->
				    sadb_address_prefixlen, dsin6);
			}
		}

		p2p = ssh_malloc(sizeof (*p2p));
		if (p2p == NULL) {
			PRTDBG(D_P2, ("  Out of memory creating p2 initiator"));
			ssh_free(local_id);
			ssh_free(remote_id);
			local_id = NULL;
			remote_id = NULL;
			send_negative_acquire(pmsg, ENOMEM);
			free_pmsg(pmsg);
			continue;
		}
		/*
		 * Construct IPsec SA proposals based on ACQUIRE message.
		 */
		assert(prop->sadb_prop_exttype == SADB_X_EXT_EPROP);
		p2p->need_ah = B_FALSE;
		p2p->need_esp = B_FALSE;

		DUMP_PFKEY(samsg);

		sens = (sadb_sens_t *)exts[SADB_EXT_SENSITIVITY];

		p2p->props = construct_eprops(p2p, prop, p1, tunnel_mode,
		    sens, &errno);
		if (p2p->props == NULL) {
			/* Error message already printed at lower level. */
			ssh_free(local_id);
			ssh_free(remote_id);
			local_id = NULL;
			remote_id = NULL;
			send_negative_acquire(pmsg, errno);
			free_pmsg(pmsg);
			ssh_free(p2p);
			continue;
		}
		p2p->pmsg = pmsg;
		p2p->phase1_neg = phase1_neg;
		p2p->spiwait.sw_context = p2p;
		p2p->local_id = local_id;
		p2p->remote_id = remote_id;
		p2p->group = p1->p1_p2_group;

		local_id = NULL;
		remote_id = NULL;

		/*
		 * Now, queue some requests to fill in the SPI's and continue
		 * once we have them.
		 */
		alloc_spis(&p2p->spiwait);
	}
}

static void
alloc_spis(spiwait_t *swp)
{
	p2initiate_t *p2p = swp->sw_context;
	int i, j, k;
	parsedmsg_t *pmsg = p2p->pmsg;
	SshIkePayloadP proposal;
	SshIkePayloadSA sa_prop;
	struct sockaddr_storage *src = pmsg->pmsg_sss;
	struct sockaddr_storage *dst = pmsg->pmsg_dss;

	PRTDBG(D_P2, ("Allocating SPI for Phase 2."));

	if (p2p->need_ah) {
		p2p->need_ah = B_FALSE;
		getspi(&p2p->spiwait, src, dst, SADB_SATYPE_AH, 0,
		    (uint8_t *)&p2p->ah_spi, alloc_spis, p2p);
		return;
	}

	if (p2p->need_esp) {
		p2p->need_esp = B_FALSE;
		getspi(&p2p->spiwait, src, dst, SADB_SATYPE_ESP, 0,
		    (uint8_t *)&p2p->esp_spi, alloc_spis, p2p);
		return;
	}

	for (k = 0; k < p2p->num_props; k++) {
		sa_prop = p2p->props[k];

		for (j = 0; j < sa_prop->number_of_proposals; j++) {
			proposal = sa_prop->proposals + j;

			for (i = 0; i < proposal->number_of_protocols; i++) {
				assert(proposal->protocols[i].spi != NULL);
				assert((proposal->protocols[i].protocol_id ==
				    SADB_SATYPE_AH && p2p->ah_spi != 0) ||
				    (proposal->protocols[i].protocol_id ==
				    SADB_SATYPE_ESP && p2p->esp_spi != 0));

				(void) memcpy(proposal->protocols[i].spi,
				    ((proposal->protocols[i].protocol_id ==
				    SADB_SATYPE_ESP) ? &p2p->esp_spi :
				    &p2p->ah_spi), 4);
			}
		}
	}
	start_phase2(p2p);
}

static void
start_phase2(p2initiate_t *p2p)
{
	parsedmsg_t *pmsg = p2p->pmsg;
	SshIkeNegotiation phase1_neg = p2p->phase1_neg;
	SshIkePayloadID local_id = p2p->local_id;
	SshIkePayloadID remote_id = p2p->remote_id;
	SshIkePayloadSA *props = p2p->props;
	int num_props = p2p->num_props;
	struct sockaddr_storage *src = pmsg->pmsg_sss;
	uint32_t flags = 0;
	ike_server_t *ikesrv;
	SshIkeErrorCode rc;
	SshIkeNegotiation phase2_neg;

	/* Set flags.  Use PFS if indicated by phase 2 group. */
	if (p2p->group != 0) {
		PRTDBG(D_P2, ("Setting PFS for phase 2."));
		flags |= SSH_IKE_IPSEC_FLAGS_WANT_PFS;
	}

	ikesrv = get_server_context(src);
	if (ikesrv == NULL) {
		PRTDBG(D_P2, ("IKE daemon not servicing local address."));
		return;
	}

	/*
	 * Be sure to send pmsg to ssh_ike_connect_ipsec().  This way,
	 * the add_new_sa() function will be able to use the
	 * appropriate sequence number to twiddle the right kernel
	 * ACQUIRE record.
	 */
	PRTDBG(D_P2, ("Starting Phase 2 negotiation..."));
	rc = ssh_ike_connect_ipsec(ikesrv->ikesrv_ctx, &phase2_neg,
	    phase1_neg, NULL, NULL, local_id, remote_id, num_props,
	    props, pmsg, flags, phase2_notify, NULL);

	switch (rc) {
	case SSH_IKE_ERROR_OK:
		if (phase2_neg == NULL) {
			/*
			 * It's a soon-to-be error.  pmsg is freed, as was
			 * local_id and remote_id (included in p2p).
			 */
			PRTDBG(D_P2,
			    ("  Phase 2 negotiation NULL, but still in "
			    " first step of negotiation, okay for now."));
		}
		break;
	default:
		/* Error. */
		PRTDBG(D_P2,
		    ("  Phase 2 SA negotiation failed, error: %d (%s).",
		    rc, ike_connect_error_to_string(rc)));
		free_pmsg(pmsg);
		ssh_free(local_id);
		ssh_free(remote_id);
		while (num_props != 0) {
			num_props--;
			ssh_ike_free_sa_payload(props[num_props]);
		}
		ssh_free(props);
	}

	ssh_free(p2p);
}

/*
 * Construct phase 1 transform list based on policy, etc. when we are the
 * initiator.
 *
 * Kivinen from SSH has pointed out that many vendors choke on proposal counts
 * that exceed, oh, say, 10.
 *
 * Pull the proposal list out of the selected rule.
 *
 * TODO:  Identities from the ACQUIRE message.
 */
static SshIkePayloadT
construct_phase1_xforms(phase1_t *p1, int *num_xforms)
{
	SshIkePayloadT xforms_rc = NULL, xform;
	int num_xforms_rc = 0;
	SshIkeSAAttributeList alist;
	int i;
	int keysize = 0;
	struct ike_rule *rule = p1->p1_rule;

	for (i = 0; i < rule->num_xforms; ++i) {
		struct ike_xform *prop;
		int encr;
		int auth;
		int grp;
		int keysize_set = 0;
		char bitstr[32];

		prop = rule->xforms[i];
		encr = prop->encr_alg;
		auth = prop->auth_alg;
		grp = prop->oakley_group;

		/*
		 * Check if we have a cert for a certificate-based auth
		 * method.
		 */
		if (prop->auth_method !=
		    SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY &&
		    p1->p1_localcert == NULL) {
			int i;

			PRTDBG(D_P1, ("  WARNING: error in rule \"%s\"!",
			    rule->label));
			PRTDBG(D_P1, ("  Key and Certificate for local "
			    "identity not available."));
			PRTDBG(D_P1, ("  Likely typo or key and cert missing "
			    "from certificate store."));
			for (i = 0; i < rule->local_id.num_includes; i++) {
				if (rule->local_id.includes[i] != NULL)
					PRTDBG(D_P1, ("    Identity: \"%s\"",
					    rule->local_id.includes[i]));
			}
			PRTDBG(D_P1, ("  Skipping transform %d.", i));
			continue;
		}

		/* check if grp supported */
		if (!ike_group_supported(grp)) {
			PRTDBG(D_P1, ("  Unsupported IKE group %d.", grp));
			PRTDBG(D_P1, ("  Skipping transform %d.", i));
			continue;	/* not supported */
		}

		/* check if encr supported */
		if (!ike_cipher_supported(encr)) {
			PRTDBG(D_P1, ("  Unsupported IKE encr value %d.",
			    encr));
			PRTDBG(D_P1, ("  Skipping transform %d.", i));
			continue;	/* not supported */
		}

		/* check if auth supported */
		if (!ike_hash_supported(auth)) {
			PRTDBG(D_P1, ("  Unsupported IKE auth value %d.",
			    auth));
			PRTDBG(D_P1, ("  Skipping transform %d.", i));
			continue;	/* not supported */
		}

		xform = ssh_realloc(xforms_rc,
		    num_xforms_rc * sizeof (struct SshIkePayloadTRec),
		    (num_xforms_rc + 1) * sizeof (struct SshIkePayloadTRec));
		if (xform == NULL) {
			/*
			 * We could be in the middle of processing
			 * variable-sized cipher - we give up
			 * for the rest of keysizes.
			 */
			keysize = 0;
			/* Skip over xforms we can't handle for now. */
			continue;
		} else {
			xforms_rc = xform;
		}

		xform += num_xforms_rc;
		num_xforms_rc++;

		xform->transform_number = num_xforms_rc;
		xform->transform_id.isakmp = SSH_IKE_ISAKMP_TRANSFORM_KEY_IKE;

		/*
		 * Add attributes the new SSH way...
		 */
		alist = ssh_ike_data_attribute_list_allocate();
		/* This panics for us. :-P */

		ssh_ike_data_attribute_list_add_basic(alist,
		    SSH_IKE_CLASSES_AUTH_METH, prop->auth_method);

		ssh_ike_data_attribute_list_add_basic(alist,
		    SSH_IKE_CLASSES_HASH_ALG, auth);
		ssh_ike_data_attribute_list_add_basic(alist,
		    SSH_IKE_CLASSES_ENCR_ALG, encr);
		if (encr == SSH_IKE_VALUES_ENCR_ALG_AES_CBC) {
			/*
			 * Be conservative in what you send.
			 * Always send transforms for each of the different
			 * key sizes in AES.
			 */
			if (keysize == 0) {
				/*
				 * Initial case, take highest value.
				 * Trust parser to have set sane values.
				 */
				keysize = prop->encr_high_bits;
			}
			assert(keysize == 256 || keysize == 192 ||
			    keysize == 128);
			keysize_set = keysize;
			if (debug & (D_POL | D_P1))
				(void) ssh_snprintf(bitstr, sizeof (bitstr),
				    "\n\tkey_length = %d bits",
				    keysize_set);
			ssh_ike_data_attribute_list_add_basic(alist,
			    SSH_IKE_CLASSES_KEY_LEN, keysize);

			if (keysize - 64 < prop->encr_low_bits)
				keysize = 0;
			else
				keysize -= 64;
		}

		if (keysize != 0) {
			/*
			 * Rein back the rule transforms - we've a
			 * variable-keysized one we aren't through with
			 * yet.
			 */
			i--;
		}

		ssh_ike_data_attribute_list_add_basic(alist,
		    SSH_IKE_CLASSES_GRP_DESC, grp);

		ssh_ike_data_attribute_list_add_basic(alist,
		    SSH_IKE_CLASSES_LIFE_TYPE,
		    SSH_IKE_VALUES_LIFE_TYPE_SECONDS);
		ssh_ike_data_attribute_list_add_int(alist,
		    SSH_IKE_CLASSES_LIFE_DURATION,
		    prop->p1_lifetime_secs);

		PRTDBG((D_POL | D_P1),
		    ("Constructing Phase 1 Transforms:\n\tOur Proposal:\n"
		    "\tRule: \"%s\" ; transform %d\n"
		    "\tauth_method = %d (%s)\n"
		    "\thash_alg = %d (%s)\n\tencr_alg = %d (%s)%s\n"
		    "\toakley_group = %d",
		    rule->label, i, prop->auth_method,
		    ike_auth_method_to_string(prop->auth_method),
		    auth, ike_hash_alg_to_string(auth),
		    encr, ike_encryption_alg_to_string(encr),
		    keysize_set != 0 ? bitstr : "", grp));

		xform->sa_attributes =
		    ssh_ike_data_attribute_list_get(alist,
		    &xform->number_of_sa_attributes);
		ssh_ike_data_attribute_list_free(alist);
	}


bail:
	*num_xforms = num_xforms_rc;
	return (xforms_rc);
}

/* XXX following should be in header file */
extern SshUdpPacketContext ssh_udp_platform_create_context(void *, void *);


/*
 * Kick off a phase 1 negotiation.
 */
int
initiate_phase1(phase1_t *p1, SshIkeNegotiation *p1_neg)
{
	parsedmsg_t *pmsg;
	struct sockaddr_storage *src, *dst;
	ike_server_t *ikesrv;
	SshIkePayloadSA sa_proposal;	/* Must be malloc()ed. */
	uchar_t remote_addr[INET6_ADDRSTRLEN + IFNAMSIZ + 2];
	SshIkePayloadID local_id;
	uint32_t flags;
	SshIkeExchangeType xchg_type;
	struct ike_rule *rule = p1->p1_rule;
	SshUdpPacketContext ctx = NULL;

	assert(rule != NULL);

	if (p1->p1_localcert == NULL)
		p1->p1_localcert = certlib_find_local_ident(&rule->local_id);

	pmsg = p1->p1_pmsg;
	src = pmsg->pmsg_sss;
	dst = pmsg->pmsg_dss;

	/*
	 * Construct structures for call to ssh_ike_connect().
	 */
	if (!sockaddr_to_string(dst, remote_addr)) {
		PRTDBG(D_P1, ("Internal error: sockaddr_to_string() failed."));
		return (SSH_IKE_ERROR_INTERNAL);
	}

	/*
	 * If p1_localid is non-NULL, it means we must use the particular ID
	 * from the acquire message, finding a certificate for it if needed.
	 * If p1_localid is NULL, we construct local_id from the local
	 * certificate, local address, and the rule, and make p1_localid match
	 * that.
	 */
	if (p1->p1_localid != NULL) {
		int af;
		if (pmsg->pmsg_psss != NULL)
			af = pmsg->pmsg_psss->ss_family;
		else
			af = pmsg->pmsg_sss->ss_family;
		local_id = pfkeyid_to_payload(p1->p1_localid, af);

#ifdef NOTYET
		/*
		 * TODO: We need a function to find a certificate for a given
		 * local identity.  We could do an ssh_cm_find_local_cert
		 * (which is what the SSH code will do internally anyway) but
		 * this won't give us a certlib_cert for p1_localcert.
		 */
		if (rule->xforms[0].auth_method !=
		    SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY)
			p1->p1_localcert = find_local_cert_for_ident(local_id);
#endif
	} else {
		local_id = construct_local_id(p1->p1_localcert, &p1->p1_local,
		    rule);
		p1->p1_localid = payloadid_to_pfkey(local_id, B_FALSE);
	}

	if (local_id == NULL) {
		/* Probably an allocation failure. */
		return (SSH_IKE_ERROR_OUT_OF_MEMORY);
	}

	/*
	 * Filling in a proposal is... complex.  Either split sub-fields
	 * into separate pointers, or hope that the compiler does common
	 * subexpression elimination really well.
	 */
	sa_proposal = (SshIkePayloadSA)
	    ssh_calloc(1, sizeof (struct SshIkePayloadSARec));
	if (sa_proposal == NULL) {
		PRTDBG(D_P1, ("Out of memory while initiating Phase 1"));
		return (SSH_IKE_ERROR_OUT_OF_MEMORY);
	}

	sa_proposal->doi = SSH_IKE_DOI_IPSEC;
	/*
	 * MLS NOTE: situation_flags would reflect MLS integrity/secrecy stuff.
	 */
	sa_proposal->situation.situation_flags = SSH_IKE_SIT_IDENTITY_ONLY;

	/*
	 * 1 proposal?  Yes, for the ISAKMP Phase 1 SA.
	 * See "there MUST NOT be multiple Proposal Payloads" in 2409.
	 */
	sa_proposal->number_of_proposals = 1;

	sa_proposal->proposals = ssh_calloc(sa_proposal->number_of_proposals,
	    sizeof (struct SshIkePayloadPRec));
	if (sa_proposal->proposals == NULL) {
		ssh_free(sa_proposal);
		PRTDBG(D_P1, ("Out of memory while initiating Phase 1"));
		return (SSH_IKE_ERROR_OUT_OF_MEMORY);
	}

	sa_proposal->proposals[0].proposal_number = 1;

	/* No looping on protocols, as for phase 1, it's only ISAKMP. */
	sa_proposal->proposals[0].number_of_protocols = 1;

	sa_proposal->proposals[0].protocols =
	    ssh_calloc(1, sizeof (struct SshIkePayloadPProtocolRec));
	if (sa_proposal->proposals[0].protocols == NULL) {
		ssh_free(sa_proposal->proposals);
		ssh_free(sa_proposal);
		PRTDBG(D_P1, ("Out of memory while initiating Phase 1"));
		return (SSH_IKE_ERROR_OUT_OF_MEMORY);
	}

	sa_proposal->proposals[0].protocols[0].protocol_id =
	    SSH_IKE_PROTOCOL_ISAKMP;
	/* How big is an ISAKMP SPI? */
	sa_proposal->proposals[0].protocols[0].spi_size = 0;
	sa_proposal->proposals[0].protocols[0].spi = NULL;

	sa_proposal->proposals[0].protocols[0].number_of_transforms = 0;
	sa_proposal->proposals[0].protocols[0].transforms = NULL;

	xchg_type = rule->mode;
	if (xchg_type == SSH_IKE_XCHG_TYPE_ANY)
		/* Default to main mode when initiating. */
		xchg_type = SSH_IKE_XCHG_TYPE_IP;

	sa_proposal->proposals[0].protocols[0].transforms =
	    construct_phase1_xforms(p1,
	    &(sa_proposal->proposals[0].protocols[0].number_of_transforms));

	PRTDBG(D_P1, ("Phase 1 exchange type=%d (%s), %d transform(s).",
	    xchg_type,
	    ike_xchg_type_to_string(xchg_type),
	    sa_proposal->proposals[0].protocols[0].number_of_transforms));
	if (sa_proposal->proposals[0].protocols[0].number_of_transforms == 0)
		PRTDBG(D_P1, ("WARNING: No valid transforms for this rule!"));

	/*
	 * For now...
	 * ...connect with main mode
	 * ...NULL for policy_manager_data (We might want samsg state
	 *    to be here)
	 *
	 * samsg will be automatically freed if bad things happen.
	 */
	ikesrv = get_server_context(src);
	if (ikesrv == NULL) {
		/*
		 * What if get_server_context() is NULL?  Try -1 for now.
		 * Find another return value if this is wrong.
		 */
		PRTDBG(D_P1, ("IKE daemon not servicing this address (%s).",
		    sap(src)));
		ssh_free(sa_proposal->proposals[0].protocols);
		ssh_free(sa_proposal->proposals);
		ssh_free(sa_proposal);
		ssh_ike_id_free(local_id);
		return (-1);
	}

	/*
	 * Lots of tweaking here.  (SSH_IKE_IKE_FLAGS_TRUST_ICMP_MESSAGES,
	 * etc.)  Initialize flags with some constants...
	 *
	 * Interoperability testing indicates that some implementations
	 * don't like seeing the crl in the cert chain; might need to make
	 * SSH_IKE_FLAGS_DO_NOT_SEND_CRLS a tunable option.
	 */
	flags = SSH_IKE_FLAGS_USE_DEFAULTS;

#if 1
	/*
	 * Send INITIAL-CONTACT if we don't have this destination in the
	 * IKE-server's cache.
	 */
	if ((addrcache_check(&ikesrv->ikesrv_addrcache, dst)) == NULL) {
		if (p1->p1_create_phase2)
			flags |= SSH_IKE_IKE_FLAGS_SEND_INITIAL_CONTACT;
		/*
		 * Add the address to the addrcache for a small amount of
		 * time.  This way if we get multiple "initial" phase 1s,
		 * only one will send an initial-contact.
		 *
		 * On a machine's boot, several ACQUIRE messages can result
		 * in multiple independent phase 1 SAs.  (This is due to
		 * policies that may require separate Phase 1 identities for
		 * various groups of ACQUIRE messages.)  This prevents
		 * multiple legitimate phase1 initiations from shooting SAs
		 * derived from slightly earlier phase1 SAs in the foot.
		 *
		 * Basically, if the addrcache_add in negotiation_done_isakmp()
		 * happened sooner, then we wouldn't need this.
		 */
		addrcache_add(&ikesrv->ikesrv_addrcache, dst,
		    ssh_time() + INITIAL_CONTACT_PAUSE);
	}
#else
	/* Peform addrcache_check _AFTER_ we've established the phase 1 SA. */
#endif

	if (p1->outer_ucred != NULL) {
		PRTDBG(D_P1, ("creating packet context as initiator\n"));

		ctx = ssh_udp_platform_create_context(NULL, p1->outer_ucred);
	}


	/*
	 * phase1_notify() will cleanup p1 linkages.
	 */
	ikestats.st_init_p1_attempts++;
	return (ssh_ike_connect(ikesrv->ikesrv_ctx, p1_neg, ctx, remote_addr,
	    MKSTR(IPPORT_IKE), local_id, sa_proposal, xchg_type, p1, flags,
	    phase1_notify, p1));
}
