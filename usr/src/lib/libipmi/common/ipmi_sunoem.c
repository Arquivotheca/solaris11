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

#include "libipmi.h"
#include <stddef.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>

#include "ipmi_impl.h"

#define	IPMI_CMD_SUNOEM_LED_GET		0x21
#define	IPMI_CMD_SUNOEM_LED_SET		0x22

typedef struct ipmi_cmd_sunoem_led_set {
	DECL_BITFIELD2(
	    ic_sls_channel_msb		:1,	/* device slave address */
	    ic_sls_slaveaddr		:7);	/* (from SDR record) */
	uint8_t		ic_sls_type;		/* led type */
	DECL_BITFIELD2(
	    __reserved			:1,	/* device access address */
	    ic_sls_accessaddr		:7);	/* (from SDR record */
	uint8_t		ic_sls_hwinfo;		/* OEM hardware info */
	uint8_t		ic_sls_mode;		/* LED mode */
	uint8_t		ic_sls_eid;		/* entity ID */
	uint8_t		ic_sls_einst;		/* entity instance */
	uint8_t		ic_sls_force;		/* force direct access */
	uint8_t		ic_sls_role;		/* BMC authorization */
} ipmi_cmd_sunoem_led_set_t;

typedef struct ipmi_cmd_sunoem_led_get {
	DECL_BITFIELD2(
	    ic_slg_channel_msb		:1,	/* device slave address */
	    ic_slg_slaveaddr		:7);	/* (from SDR record) */
	uint8_t		ic_slg_type;		/* led type */
	DECL_BITFIELD2(
	    __reserved			:1,	/* device access address */
	    ic_slg_accessaddr		:7);	/* (from SDR record */
	uint8_t		ic_slg_hwinfo;		/* OEM hardware info */
	uint8_t		ic_slg_eid;		/* entity ID */
	uint8_t		ic_slg_einst;		/* entity instance */
	uint8_t		ic_slg_force;		/* force direct access */
} ipmi_cmd_sunoem_led_get_t;

#define	IPMI_SUNOEM_LED_TYPE_OK2RM	0
#define	IPMI_SUNOEM_LED_TYPE_SERVICE	1
#define	IPMI_SUNOEM_LED_TYPE_ACT	2
#define	IPMI_SUNOEM_LED_TYPE_LOCATE	3
#define	IPMI_SUNOEM_LED_TYPE_ANY	0xFF

boolean_t
ipmi_is_sun_ilom(ipmi_deviceid_t *dp)
{
	return (ipmi_devid_manufacturer(dp) == IPMI_OEM_SUN &&
	    ipmi_devid_product(dp) == IPMI_PROD_SUN_ILOM);
}

static int
check_sunoem(ipmi_handle_t *ihp)
{
	ipmi_deviceid_t *devid;

	if ((devid = ipmi_get_deviceid(ihp)) == NULL)
		return (-1);

	if (!ipmi_is_sun_ilom(devid))
		return (ipmi_set_error(ihp, EIPMI_INVALID_COMMAND, NULL));

	return (0);
}

static int
ipmi_send_sunoem_led_set(ipmi_handle_t *ihp, ipmi_cmd_sunoem_led_set_t *req)
{
	ipmi_cmd_t cmd, *resp;

	cmd.ic_netfn = IPMI_NETFN_OEM;
	cmd.ic_cmd = IPMI_CMD_SUNOEM_LED_SET;
	cmd.ic_lun = 0;
	cmd.ic_data = req;
	cmd.ic_dlen = sizeof (*req);

	if ((resp = ipmi_send(ihp, &cmd)) == NULL)
		return (-1);

	if (resp->ic_dlen != 0)
		return (ipmi_set_error(ihp, EIPMI_BAD_RESPONSE_LENGTH, NULL));

	return (0);
}

static int
ipmi_send_sunoem_led_get(ipmi_handle_t *ihp, ipmi_cmd_sunoem_led_get_t *req,
    uint8_t *result)
{
	ipmi_cmd_t cmd, *resp;

	cmd.ic_netfn = IPMI_NETFN_OEM;
	cmd.ic_cmd = IPMI_CMD_SUNOEM_LED_GET;
	cmd.ic_lun = 0;
	cmd.ic_data = req;
	cmd.ic_dlen = sizeof (*req);

	if ((resp = ipmi_send(ihp, &cmd)) == NULL)
		return (-1);

	if (resp->ic_dlen != 1)
		return (ipmi_set_error(ihp, EIPMI_BAD_RESPONSE_LENGTH, NULL));

	*result = *((uint8_t *)resp->ic_data);
	return (0);
}

int
ipmi_sunoem_led_set(ipmi_handle_t *ihp, ipmi_sdr_generic_locator_t *dev,
    uint8_t mode)
{
	ipmi_cmd_sunoem_led_set_t cmd = { 0 };

	if (check_sunoem(ihp) != 0)
		return (-1);

	cmd.ic_sls_slaveaddr = dev->is_gl_slaveaddr;
	cmd.ic_sls_channel_msb = dev->is_gl_channel_msb;
	cmd.ic_sls_type = dev->is_gl_oem;
	cmd.ic_sls_accessaddr = dev->is_gl_accessaddr;
	cmd.ic_sls_hwinfo = dev->is_gl_oem;
	cmd.ic_sls_mode = mode;
	cmd.ic_sls_eid = dev->is_gl_entity;
	cmd.ic_sls_einst = dev->is_gl_instance;

	return (ipmi_send_sunoem_led_set(ihp, &cmd));
}

int
ipmi_sunoem_led_get(ipmi_handle_t *ihp, ipmi_sdr_generic_locator_t *dev,
    uint8_t *mode)
{
	ipmi_cmd_sunoem_led_get_t cmd = { 0 };

	if (check_sunoem(ihp) != 0)
		return (-1);

	cmd.ic_slg_slaveaddr = dev->is_gl_slaveaddr;
	cmd.ic_slg_channel_msb = dev->is_gl_channel_msb;
	cmd.ic_slg_type = dev->is_gl_oem;
	cmd.ic_slg_accessaddr = dev->is_gl_accessaddr;
	cmd.ic_slg_hwinfo = dev->is_gl_oem;
	cmd.ic_slg_eid = dev->is_gl_entity;
	cmd.ic_slg_einst = dev->is_gl_instance;

	return (ipmi_send_sunoem_led_get(ihp, &cmd, mode));
}

int
ipmi_sunoem_uptime(ipmi_handle_t *ihp, uint32_t *uptime, uint32_t *gen)
{
	ipmi_cmd_t cmd, *resp;
	uint8_t unused;

	if (check_sunoem(ihp) != 0)
		return (-1);

	cmd.ic_netfn = IPMI_NETFN_OEM;
	cmd.ic_lun = 0;
	cmd.ic_cmd = IPMI_CMD_SUNOEM_UPTIME;
	cmd.ic_dlen = sizeof (unused);
	cmd.ic_data = &unused;

	if ((resp = ipmi_send(ihp, &cmd)) == NULL)
		return (-1);

	if (resp->ic_dlen != 2 * sizeof (uint32_t))
		return (ipmi_set_error(ihp, EIPMI_BAD_RESPONSE_LENGTH, NULL));

	if (uptime)
		*uptime = BE_IN32(&((uint32_t *)resp->ic_data)[0]);
	if (gen)
		*gen = BE_IN32(&((uint32_t *)resp->ic_data)[1]);

	return (0);
}

int
ipmi_sunoem_update_fru(ipmi_handle_t *ihp, ipmi_sunoem_fru_t *req)
{
	ipmi_cmd_t cmd, *resp;

	if (check_sunoem(ihp) != 0)
		return (-1);

	switch (req->isf_type) {
	case IPMI_SUNOEM_FRU_DIMM:
		req->isf_datalen = sizeof (req->isf_data.dimm);
		break;

	case IPMI_SUNOEM_FRU_CPU:
		req->isf_datalen = sizeof (req->isf_data.cpu);
		break;

	case IPMI_SUNOEM_FRU_BIOS:
		req->isf_datalen = sizeof (req->isf_data.bios);
		break;

	case IPMI_SUNOEM_FRU_DISK:
		req->isf_datalen = sizeof (req->isf_data.disk);
		break;
	}

	cmd.ic_netfn = IPMI_NETFN_OEM;
	cmd.ic_cmd = IPMI_CMD_SUNOEM_FRU_UPDATE;
	cmd.ic_lun = 0;
	cmd.ic_dlen = offsetof(ipmi_sunoem_fru_t, isf_data) +
	    req->isf_datalen;
	cmd.ic_data = req;

	if ((resp = ipmi_send(ihp, &cmd)) == NULL)
		return (-1);

	if (resp->ic_dlen != 0)
		return (ipmi_set_error(ihp, EIPMI_BAD_RESPONSE_LENGTH, NULL));

	return (0);
}

#define	SUNOEM_FIRST_POLL_RETRIES	10
#define	SUNOEM_NEXT_POLL_RETRIES	3
#define	SUNOEM_POLL_DELAY		500000 /* .5 secs */

int
ipmi_sunoem_cli(ipmi_handle_t *ihp, char **commands, char *output, uint_t olen,
    boolean_t force)
{
	char *curcmd;
	ipmi_cmd_t cmd, *resp;
	ipmi_sunoem_cli_msg_t msg, *msg_resp;
	int c = 0, i = 0, retry = 0;
	int cmd_delay = 1500000; /* Default of 1.5 secs */
	int tmp_delay;
	char *delay_env;
	int max_retries;

	if (check_sunoem(ihp) != 0)
		return (-1);

	delay_env = getenv("LIBIPMI_CLI_TIMEOUT");
	if (delay_env != NULL) {
		tmp_delay = atoi(delay_env);

		if (tmp_delay > 0) {
			cmd_delay = tmp_delay;
		}
	}

	/*
	 * Do some basic input validation
	 */
	if (ihp->ih_transport != &ipmi_transport_bmc || !commands) {
		(void) ipmi_set_error(ihp, EIPMI_INVALID_REQUEST, NULL);
		return (-1);
	}

	cmd.ic_netfn = IPMI_NETFN_OEM;
	cmd.ic_cmd = IPMI_CMD_SUNOEM_CLI;
	cmd.ic_lun = 0;
	cmd.ic_dlen = sizeof (ipmi_sunoem_cli_msg_t);
	cmd.ic_data = &msg;

	/*
	 * Before we can send the actual command line we want ILOM to execute,
	 * we have to initiate the transaction.  ILOM made a design decision
	 * that only one CLI transaction can be open at any one time.  Therefore
	 * if we get a response code that indicates a CLI transaction is already
	 * in progress, we'll return EIPMI_BUSY.  Client code can then either
	 * retry the call, or try the call with force set to B_TRUE, which will
	 * cause ILOM to cancel any pre-existing transaction.  Upon success,
	 * ILOM will return a 4-byte handle that we'll use for all subsequent
	 * requests involving this particular command line transaction.
	 */
	bzero(&msg, sizeof (ipmi_sunoem_cli_msg_t));
	msg.icli_version = IPMI_SUNOEM_CLI_VERSION;
	if (force)
		msg.icli_cmdresp = IPMI_SUNOEM_CLI_FORCE;
	else
		msg.icli_cmdresp = IPMI_SUNOEM_CLI_OPEN;
	msg.icli_buf[0] = '\0';

	if ((resp = ipmi_send(ihp, &cmd)) == NULL)
		return (-1);

	msg_resp = (ipmi_sunoem_cli_msg_t *)resp->ic_data;
	if (msg_resp->icli_cmdresp == 2) {
		(void) ipmi_set_error(ihp, EIPMI_BUSY, NULL);
		return (-1);
	}
	msg.icli_cmdresp = IPMI_SUNOEM_CLI_POLL;
	(void) memcpy(&msg.icli_handle[0], &msg_resp->icli_handle[0], 4);

	/* Delay, allowing spsh to start on SP */
	(void) usleep(cmd_delay);

	curcmd = commands[c];
	while (strlen(curcmd) > 0) {
		/*
		 * Send the command in the POLL on the first poll only.
		 * All other poll requests will read the data.
		 * The command literally gets passed to spsh, so we need to tack
		 * on a carriage-return.
		 */
		(void) snprintf(msg.icli_buf, IPMI_SUNOEM_CLI_INBUFSZ, "%s\r",
		    curcmd);

		max_retries = SUNOEM_FIRST_POLL_RETRIES;

		/*
		 * This loop polls for output from the command. We will break
		 * out of this loop if any of the following conditions occur:
		 *
		 * 1. We get a response with just a null-char in the data field
		 *    (indicating that there is no more output to be read)
		 *
		 * 2. We've filled up the caller-supplied buffer.  Any
		 *    additional output from the command will be lost.
		 *
		 * 3. We were not able to send the command.
		 */
		/*LINTED*/
		while (1) {

			/*
			 * If ILOM spshell isn't ready yet, sending
			 * the POLL followed by a CLOSE can cause
			 * the spshell to hang.  Therefore retry to
			 * allow ILOM to respond.
			 */
			for (retry = 0; retry < max_retries; retry++) {

				resp = ipmi_send(ihp, &cmd);
				if (resp != NULL) {
					msg_resp =
					    (ipmi_sunoem_cli_msg_t *)
					    resp->ic_data;

					if (strlen(msg_resp->icli_buf) != 0) {
						/* Received valid data */
						break;
					}

					/* Don't send command on next poll */
					msg.icli_buf[0] = '\0';
				}

				/* Delay, allowing SP to respond */
				(void) usleep(SUNOEM_POLL_DELAY);
			}

			if (retry == max_retries) {
				/* Not able to get data, terminate */
				break;
			}

			if ((i + strlen(msg_resp->icli_buf)) >= olen) {
				/*
				 * caller-supplied buffer was too small, so
				 * output will be truncated.
				 */
				(void) strncpy(output+i, msg_resp->icli_buf,
				    olen - i);
				break;
			}

			/* Store the data */
			(void) strcpy(output+i, msg_resp->icli_buf);
			i += strlen(msg_resp->icli_buf);

			/*
			 * For all subsequent polls after first. use
			 * shorter delay.
			 */
			max_retries = SUNOEM_NEXT_POLL_RETRIES;

		}
		curcmd = commands[++c];
	}

	/*
	 * Ok - all done.  Send a CLOSE command to close out this transaction
	 */
	msg.icli_cmdresp = IPMI_SUNOEM_CLI_CLOSE;

	for (retry = 0; retry < max_retries; retry++) {
		if ((resp = ipmi_send(ihp, &cmd)) != NULL) {
			/* Close sent */
			break;
		}
	}

	if (retry == max_retries) {
		/* Not able to send close */
		return (-1);
	}

	return (0);
}
