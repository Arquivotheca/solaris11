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

#include <libipmi.h>
#include <string.h>

#include "ipmi_impl.h"

typedef struct ipmi_user_impl {
	ipmi_list_t	iu_list;
	ipmi_user_t	iu_user;
} ipmi_user_impl_t;

/*
 * Get User Access.  See section 22.27.
 *
 * See libipmi.h for a complete description of IPMI reference material.
 */

typedef struct ipmi_get_user_access_req {
	DECL_BITFIELD2(
	    igua_channel		:4,
	    __reserved1			:4);
	DECL_BITFIELD2(
	    igua_uid			:2,
	    __reserved2			:6);
} ipmi_get_user_access_req_t;

#define	IPMI_CMD_GET_USER_ACCESS	0x44

typedef struct ipmi_get_user_access {
	DECL_BITFIELD2(
	    igua_max_uid		:4,
	    __reserved1			:4);
	DECL_BITFIELD2(
	    igua_enable_status		:4,
	    igua_enabled_uid		:4);
	DECL_BITFIELD2(
	    __reserved2			:4,
	    igua_fixed_uid		:4);
	DECL_BITFIELD5(
	    __reserved3			:1,
	    igua_only_callback		:1,
	    igua_link_auth_enable	:1,
	    igua_ipmi_msg_enable	:1,
	    igua_privilege_level	:4);
} ipmi_get_user_access_t;

#define	IPMI_USER_ENABLE_UNSPECIFIED	0x00
#define	IPMI_USER_ENABLE_SETPASSWD	0x01
#define	IPMI_USER_DISABLE_SETPASSWD	0x02

#define	IPMI_USER_CHANNEL_CURRENT	0xe

/*
 * Get User Name.  See section 22.29
 */

#define	IPMI_CMD_GET_USER_NAME		0x46

/*
 * Set User Password.  See section 22.30
 */

#define	IPMI_CMD_SET_USER_PASSWORD	0x47

typedef struct ipmi_set_user_password {
	DECL_BITFIELD3(
	    isup_uid		:6,
	    __reserved1		:1,
	    isup_len20		:1);
	DECL_BITFIELD2(
	    isup_op		:2,
	    __reserved2		:6);
	char		isup_passwd[20];
} ipmi_set_user_password_t;

#define	IPMI_PASSWORD_OP_DISABLE	0x0
#define	IPMI_PASSWORD_OP_ENABLE		0x1
#define	IPMI_PASSWORD_OP_SET		0x2
#define	IPMI_PASSWORD_OP_TEST		0x3

static ipmi_get_user_access_t *
ipmi_get_user_access(ipmi_handle_t *ihp, uint8_t channel, uint8_t uid)
{
	ipmi_cmd_t cmd, *resp;
	ipmi_get_user_access_req_t req = { 0 };

	req.igua_channel = channel;
	req.igua_uid = uid;

	cmd.ic_netfn = IPMI_NETFN_APP;
	cmd.ic_cmd = IPMI_CMD_GET_USER_ACCESS;
	cmd.ic_lun = 0;
	cmd.ic_data = &req;
	cmd.ic_dlen = sizeof (req);

	if ((resp = ipmi_send(ihp, &cmd)) == NULL) {
		/*
		 * If sessions aren't supported on the current channel, some
		 * service processors (notably Sun's ILOM) will return an
		 * invalid request completion code (0xCC).  For these SPs, we
		 * translate this to the more appropriate EIPMI_INVALID_COMMAND.
		 */
		if (ipmi_errno(ihp) == EIPMI_INVALID_REQUEST)
			(void) ipmi_set_error(ihp, EIPMI_INVALID_COMMAND,
			    NULL);
		return (NULL);
	}

	if (resp->ic_dlen < sizeof (ipmi_get_user_access_t)) {
		(void) ipmi_set_error(ihp, EIPMI_BAD_RESPONSE_LENGTH, NULL);
		return (NULL);
	}

	return (resp->ic_data);
}

static const char *
ipmi_get_user_name(ipmi_handle_t *ihp, uint8_t uid)
{
	ipmi_cmd_t cmd, *resp;

	cmd.ic_netfn = IPMI_NETFN_APP;
	cmd.ic_cmd = IPMI_CMD_GET_USER_NAME;
	cmd.ic_lun = 0;
	cmd.ic_data = &uid;
	cmd.ic_dlen = sizeof (uid);

	if ((resp = ipmi_send(ihp, &cmd)) == NULL)
		return (NULL);

	if (resp->ic_dlen < 16) {
		(void) ipmi_set_error(ihp, EIPMI_BAD_RESPONSE_LENGTH, NULL);
		return (NULL);
	}

	return (resp->ic_data);
}

void
ipmi_user_clear(ipmi_handle_t *ihp)
{
	ipmi_user_impl_t *uip;

	while ((uip = ipmi_list_next(&ihp->ih_users)) != NULL) {
		ipmi_list_delete(&ihp->ih_users, uip);
		ipmi_free(ihp, uip->iu_user.iu_name);
		ipmi_free(ihp, uip);
	}
}

/*
 * Returns user information in a well-defined structure.
 */
int
ipmi_user_iter(ipmi_handle_t *ihp, int (*func)(ipmi_user_t *, void *),
    void *data)
{
	ipmi_get_user_access_t *resp;
	uint8_t i, uid_max;
	ipmi_user_impl_t *uip;
	ipmi_user_t *up;
	const char *name;
	uint8_t channel;
	uint32_t zero = 0;
	ipmi_deviceid_t *devid;

	ipmi_user_clear(ihp);

	channel = IPMI_USER_CHANNEL_CURRENT;

	/*
	 * Get the number of active users on the system by requesting the first
	 * user ID (1).
	 */
	if ((resp = ipmi_get_user_access(ihp, channel, 1)) == NULL ||
	    memcmp(resp, &zero, 4) == 0) {
		/*
		 * Some versions of the Sun ILOM have a bug which prevent the
		 * GET USER ACCESS command from succeeding over the default
		 * channel.  Also, some versions of ILOM will return a response,
		 * but the 4-byte reponse will be all zeroes, which we will also
		 * interpret as a failure.  If this fails and we are on ILOM,
		 * then attempt to use the standard channel (1) instead.
		 */
		if ((devid = ipmi_get_deviceid(ihp)) == NULL)
			return (-1);

		if (!ipmi_is_sun_ilom(devid))
			return (-1);

		channel = 1;
		if ((resp = ipmi_get_user_access(ihp, channel, 1)) == NULL)
			return (-1);
	}

	uid_max = resp->igua_max_uid;
	for (i = 1; i <= uid_max; i++) {
		if (i != 1 && (resp = ipmi_get_user_access(ihp,
		    channel, i)) == NULL)
			return (-1);

		if ((uip = ipmi_zalloc(ihp, sizeof (ipmi_user_impl_t))) == NULL)
			return (-1);

		up = &uip->iu_user;

		up->iu_enabled = resp->igua_enabled_uid;
		up->iu_uid = i;
		up->iu_ipmi_msg_enable = resp->igua_ipmi_msg_enable;
		up->iu_link_auth_enable = resp->igua_link_auth_enable;
		up->iu_priv = resp->igua_privilege_level;

		ipmi_list_append(&ihp->ih_users, uip);

		/*
		 * If we are requesting a username that doesn't have a
		 * supported username, we may get an INVALID REQUEST response.
		 * If this is the case, then continue as if there is no known
		 * username.
		 */
		if ((name = ipmi_get_user_name(ihp, i)) == NULL) {
			if (ipmi_errno(ihp) == EIPMI_INVALID_REQUEST)
				continue;
			else
				return (-1);
		}

		if (*name == '\0')
			continue;

		if ((up->iu_name = ipmi_strdup(ihp, name)) == NULL)
			return (-1);
	}

	for (uip = ipmi_list_next(&ihp->ih_users); uip != NULL;
	    uip = ipmi_list_next(uip)) {
		if (func(&uip->iu_user, data) != 0)
			return (-1);
	}

	return (0);
}

typedef struct ipmi_user_cb {
	const char	*uic_name;
	uint8_t		uic_uid;
	ipmi_user_t	*uic_result;
} ipmi_user_cb_t;

static int
ipmi_user_callback(ipmi_user_t *up, void *data)
{
	ipmi_user_cb_t *cbp = data;

	if (cbp->uic_result != NULL)
		return (0);

	if (up->iu_name) {
		if (strcmp(up->iu_name, cbp->uic_name) == 0)
			cbp->uic_result = up;
	} else if (up->iu_uid == cbp->uic_uid) {
		cbp->uic_result = up;
	}

	return (0);
}

ipmi_user_t *
ipmi_user_lookup_name(ipmi_handle_t *ihp, const char *name)
{
	ipmi_user_cb_t cb = { 0 };

	cb.uic_name = name;
	cb.uic_result = NULL;

	if (ipmi_user_iter(ihp, ipmi_user_callback, &cb) != 0)
		return (NULL);

	if (cb.uic_result == NULL)
		(void) ipmi_set_error(ihp, EIPMI_NOT_PRESENT,
		    "no such user");

	return (cb.uic_result);
}

ipmi_user_t *
ipmi_user_lookup_id(ipmi_handle_t *ihp, uint8_t uid)
{
	ipmi_user_cb_t cb = { 0 };

	cb.uic_uid = uid;
	cb.uic_result = NULL;

	if (ipmi_user_iter(ihp, ipmi_user_callback, &cb) != 0)
		return (NULL);

	if (cb.uic_result == NULL)
		(void) ipmi_set_error(ihp, EIPMI_NOT_PRESENT,
		    "no such user");

	return (cb.uic_result);
}

int
ipmi_user_set_password(ipmi_handle_t *ihp, uint8_t uid, const char *passwd)
{
	ipmi_set_user_password_t req = { 0 };
	ipmi_cmd_t cmd;

	req.isup_uid = uid;
	req.isup_op = IPMI_PASSWORD_OP_SET;

	if (strlen(passwd) > 19)
		return (ipmi_set_error(ihp, EIPMI_INVALID_REQUEST,
		    "password length must be less than 20 characters"));

	if (strlen(passwd) > 15)
		req.isup_len20 = 1;

	(void) strcpy(req.isup_passwd, passwd);

	cmd.ic_netfn = IPMI_NETFN_APP;
	cmd.ic_cmd = IPMI_CMD_SET_USER_PASSWORD;
	cmd.ic_lun = 0;
	cmd.ic_data = &req;
	if (req.isup_len20)
		cmd.ic_dlen = sizeof (req);
	else
		cmd.ic_dlen = sizeof (req) - 4;

	if (ipmi_send(ihp, &cmd) == NULL)
		return (-1);

	return (0);
}
