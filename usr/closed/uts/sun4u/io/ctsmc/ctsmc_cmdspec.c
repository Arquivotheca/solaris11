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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ctsmc_cmdspec - SMC Command validation routines
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/strsun.h>

#include <sys/smc_commands.h>
#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

#define	SMC_BIT_COUNT(MASK)	(ctsmc_num_bits(MASK, NUM_BLOCKS))

#define	SMC_COMMAND_PROP	"reject-unknown-commands"

/*
 * cmd_flag definitions
 */
#define	SMC_REJECT_UNKNOWN_CMDS	0x1

typedef enum {
	SMC_REQUEST,		/* Normal request */
	SMC_REQ_PRIV,		/* Priviledged request */
	SMC_EVT_NOTIF,		/* asynchronous notifications */
	SMC_NOTIF_SPL,		/* special asynchronous notifications */
	SMC_UNKNOWN, 		/* Not defined yet, assume request */
	SMC_INVALID 		/* Invalid command */
} ctsmc_command_class_t;

typedef struct {
	int		can_send;
	uint_t		cmd_flags; /* Execution flags for command */
	uint_t		tblsize;
	uchar_t		*table;
	kmutex_t	lock;
} ctsmc_cmds_t;

static uint_t		ctsmc_cmd_max	= SMC_NUM_CMDS;
static ctsmc_cmds_t	ctsmc_cmds;

static uchar_t	ctsmc_cmd_proto[SMC_NUM_CMDS] = {
SMC_INVALID,	SMC_REQUEST,	SMC_REQ_PRIV,	SMC_REQ_PRIV,	/* x00-03 */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* x04-07 */
SMC_REQUEST,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x08-0b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x0c-0f */

SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x10-13 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x14-17 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x18-1b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x1c-1f */

SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_REQUEST,	SMC_NOTIF_SPL,	/* x20-23 */
SMC_REQUEST,	SMC_REQUEST,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x24-27 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x28-2b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_REQUEST,	SMC_REQUEST,	/* x2c-2f */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* x30-33 */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_UNKNOWN,	/* x34-37 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x38-3b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x3c-3f */

SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x40-43 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x44-47 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x48-4b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x4c-4f */

SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_REQUEST,	SMC_UNKNOWN,	/* x50-53 */
SMC_UNKNOWN,	SMC_REQUEST,	SMC_UNKNOWN,	SMC_REQUEST,	/* x54-57 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x58-5b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x5c-5f */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* x60-63 */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* x64-67 */
SMC_REQUEST,	SMC_UNKNOWN,	SMC_REQUEST,	SMC_UNKNOWN,	/* x68-6b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_REQUEST,	/* x6c-6f */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* x70-73 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x74-77 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x78-7b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x7c-7f */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* x80-83 */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_EVT_NOTIF,	/* x84-87 */
SMC_EVT_NOTIF,	SMC_REQUEST,	SMC_REQUEST,	SMC_EVT_NOTIF,	/* x88-8b */
SMC_REQUEST,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x8c-8f */

SMC_REQUEST,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x90-93 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x94-97 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x98-9b */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* x9c-9f */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_UNKNOWN,	/* xa0-a3 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xa4-a7 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xa8-ab */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xac-af */

SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xb0-b3 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xb4-b7 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xb8-bb */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xbc-bf */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* xc0-c3 */
SMC_REQUEST,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xc4-c7 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xc8-cb */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xcc-cf */

SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xd0-d3 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xd4-d7 */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xd8-db */
SMC_UNKNOWN,	SMC_UNKNOWN,	SMC_REQUEST,	SMC_REQUEST,	/* xdc-df */

SMC_UNKNOWN,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* xe0-e3 */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_EVT_NOTIF,	/* xe4-e7 */
SMC_UNKNOWN,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* xe8-eb */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* xec-ef */

SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	/* xf0-f3 */
SMC_REQUEST,	SMC_REQUEST,	SMC_UNKNOWN,	SMC_UNKNOWN,	/* xf4-f7 */
SMC_REQUEST,	SMC_REQUEST,	SMC_UNKNOWN,	SMC_REQUEST,	/* xf8-fb */
SMC_REQUEST,	SMC_REQUEST,	SMC_REQUEST,	SMC_UNKNOWN,	/* xfc-ff */
};

/*
 * Allocate the command table and initialize it to the
 * proto version.
 * This can be overriden later, if desired.
 */
void
ctsmc_cmd_alloc()
{
	if (ctsmc_cmds.table == NULL) {
		ctsmc_cmds.cmd_flags  = 0;
		mutex_init(&ctsmc_cmds.lock, NULL, MUTEX_DEFAULT, NULL);
		ctsmc_cmds.tblsize = ctsmc_cmd_max;
		ctsmc_cmds.table = NEW(ctsmc_cmds.tblsize, uchar_t);
		bcopy(ctsmc_cmd_proto, ctsmc_cmds.table, ctsmc_cmds.tblsize);
	}
}

/*
 * Set execution flags for SMC, e.g. if the property
 * "reject-unknown-commands" is present, set flag
 */
void
ctsmc_set_command_flags(ctsmc_state_t *ctsmc)
{
	if (ctsmc_cmds.table == NULL) {
		SMC_DEBUG0(SMC_CMD_DEBUG, "SMC command table not initialized");
		return;
	}

	if (ddi_prop_exists(DDI_DEV_T_ANY, ctsmc->ctsmc_dip,
		DDI_PROP_DONTPASS, SMC_COMMAND_PROP) == 1) {
		ctsmc_cmds.cmd_flags |= SMC_REJECT_UNKNOWN_CMDS;
		SMC_DEBUG0(SMC_CMD_DEBUG, "SMC will reject Unknown commands");
	}
}

/*
 * driver is going to be unloaded. Free the command table.
 */
void
ctsmc_cmd_fini()
{
	SMC_DEBUG0(SMC_CMD_DEBUG, "fini");
	if (ctsmc_cmds.table != NULL) {
		FREE(ctsmc_cmds.table, ctsmc_cmds.tblsize, uchar_t);
		mutex_destroy(&ctsmc_cmds.lock);
	}

	ctsmc_cmds.table = NULL;
}

/*
 * Find number of bits set in an array of 16 bit shorts
 */
static int
ctsmc_num_bits(uint16_t *arg, uint8_t cnt)
{
	int i, count = 0, num;

	for (i = 0; i < cnt; i++) {
		num = arg[i];
		for (; num; num >>= 1)
			count += num&1;
	}

	return (count);
}

/*
 * Find whether a request command is an exclusive one for a minor
 */
int
ctsmc_search_cmdspec_minor_spec(ctsmc_state_t *ctsmc, uint8_t cmd,
		uint8_t *attr, uint8_t *minor, uint16_t **min_mask)
{
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent = &(list->ctsmc_cmd_ent[cmd]);
	uint16_t *m_mask = ent->minor_mask;
	uint8_t count = SMC_BIT_COUNT(m_mask);

	if (count) {
		if (attr)
			*attr = ent->attribute;
		if (minor)
			*minor = ent->minor;
		if (min_mask)
			*min_mask = m_mask;

		return (SMC_SUCCESS);
	}

	return (SMC_FAILURE);
}

/*
 * Given a command, check whether this is a valid asynchronous
 * command
 */
int
ctsmc_async_valid(uint8_t cmd)
{
	ctsmc_command_class_t cmdtype = ctsmc_cmds.table[cmd];

	if ((cmdtype == SMC_EVT_NOTIF) ||
		(cmdtype == SMC_NOTIF_SPL))
		return (SMC_SUCCESS);
	else
		return (SMC_FAILURE);
}

/*
 * Given a command, check whether this is a valid SMC request command
 */
static int
ctsmc_request_valid(uint8_t cmd)
{
	ctsmc_command_class_t cmdtype = ctsmc_cmds.table[cmd];

	if ((cmdtype == SMC_REQUEST) ||
			(cmdtype == SMC_REQ_PRIV))
		return (SMC_SUCCESS);

	if (!(ctsmc_cmds.cmd_flags & SMC_REJECT_UNKNOWN_CMDS) &&
			cmdtype == SMC_UNKNOWN)
		return (SMC_SUCCESS);

	return (SMC_FAILURE);
}

/*
 * Given a command, check whether this is a valid SMC request command
 */
static int
ctsmc_command_valid(uint8_t cmd)
{
	ctsmc_command_class_t cmdtype = ctsmc_cmds.table[cmd];

	if ((ctsmc_cmds.cmd_flags & SMC_REJECT_UNKNOWN_CMDS) &&
			(cmdtype == SMC_UNKNOWN))
		return (SMC_FAILURE);

	return (SMC_SUCCESS);
}

/*
 * verify that caller has permission to send the specified command
 * Validating original request message sent downstream would
 * already have validated other things.
 */
/*ARGSUSED*/
static int
ctsmc_cmd_valid(sc_reqmsg_t *msg, ctsmc_minor_t *mnode_p)
{
	uint8_t		attr, minor, cmd = SC_MSG_CMD(msg);

	if (ctsmc_request_valid(cmd) != SMC_SUCCESS)
		return (SMC_FAILURE);

	/*
	 * Check whether this command is an exclusive command on
	 * another stream
	 */
	if (ctsmc_search_cmdspec_minor_spec(mnode_p->ctsmc_state, cmd,
		&attr, &minor, NULL) == SMC_SUCCESS) {
		if ((attr == SC_ATTR_EXCLUSIVE) &&
				(minor != mnode_p->minor)) {
			SMC_DEBUG3(SMC_CMD_DEBUG, "Attempting to issue "
				"command 0x%x on minor "
				"%d, which is exclusively taken by minor %d",
				cmd, mnode_p->minor, minor);
			return (SMC_FAILURE);
		}
	}

	return (SMC_SUCCESS);
}

/*
 * Send an error response for a command request that failed.
 * The error response packet has the same header as the request.
 * Two data bytes are used to send back the completion code.
 * The first byte is the completion code which is SMC_CMD_FAILED,
 * and the first data byte is the errno. ioclen = length if
 * ioctl, else 0.
 */
int
ctsmc_cmd_error(queue_t *q, mblk_t *mp, ctsmc_minor_t *mnode_p, int ioclen)
{
	sc_reqmsg_t	msg;
	uchar_t		e, length;

	/*
	 * copy mblk_t into ctsmc_reqmsg_t
	 */
	ctsmc_msg_copy(mp, (uchar_t *)&msg);

	/*
	 * verify that this is a valid command.
	 */
	e = ctsmc_cmd_valid(&msg, mnode_p);
	/*
	 * If not an ioctl, construct a new response message
	 * and free the original, then send back a response
	 */
	if (e != 0) {
		sc_rspmsg_t	rmsg;
		/*
		 * construct an error reply to this command.
		 */
		ctsmc_msg_response(&msg, &rmsg, SMC_CC_INVALID_COMMAND, e);

		SMC_DEBUG(SMC_CMD_DEBUG, "error: error reply: %d", e);
		if (!ioclen) {
			freemsg(mp);
			mp = ctsmc_rmsg_message(&rmsg);
			if (mp != NULL)
				qreply(q, mp);
			} else {
				length = SC_RECV_HEADER + SC_MSG_LEN(&rmsg);
				if (ioclen >= length) {
					bcopy((void *)&rmsg,
						(void *)mp->b_rptr, length);
					mp->b_wptr = mp->b_rptr + length;
				}
			}
		return (SMC_FAILURE);
	}
	return (SMC_SUCCESS);
}

/*
 * Initialize Command spec lists. The first list if
 * for KCS commands from host, the second one for
 * async messages from SMC.
 */
void
ctsmc_init_cmdspec_list(ctsmc_state_t *ctsmc)
{
	ctsmc_cmdspec_list_t *list = NEW(1, ctsmc_cmdspec_list_t);

	ctsmc->ctsmc_cmdspec_list = list;
	mutex_init(&list->lock, NULL, MUTEX_DRIVER, NULL);
}

void
ctsmc_free_cmdspec_list(ctsmc_state_t *ctsmc)
{
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	mutex_destroy(&list->lock);
	FREE(list, 1, ctsmc_cmdspec_list_t);
	ctsmc->ctsmc_cmdspec_list = NULL;
}

typedef int (*ctsmc_cmdspec_ptr_t)(ctsmc_minor_t *, int8_t, sc_cmdspec_t *);
/*
 * Validate whether a request for insertion is valid. This routine is called
 * with all basic checking done
 */
static int
ctsmc_validate_cmdspec_ins(ctsmc_minor_t *mnode_p, int8_t n_cmds,
		sc_cmdspec_t *cmdspec)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	uint8_t cmd, count, minor = mnode_p->minor,
			attr = cmdspec->attribute;
	ctsmc_cmdspec_ent_t *ent;

	uint16_t *m_mask, i;
	uint8_t block = NUM_TO_BLOCK(minor),
			offset = NUM_OFFSET(minor);

	/*
	 * Ignore the request and return success if we as for adding
	 * identical entry. If the request is to downgrade from
	 * EXCLUSIVE to SHARED on the same minor, allow this request.
	 */
	for (i = 0; i < n_cmds; i++) {
		cmd = cmdspec->args[i];
		ent = &(list->ctsmc_cmd_ent[cmd]);
		m_mask = ent->minor_mask;
		count = SMC_BIT_COUNT(m_mask);
		if (count) {
			if ((attr == ent->attribute) &&
					BITTEST(m_mask[block], offset))
				continue;

			if ((attr == SC_ATTR_SHARED) &&
					(attr != ent->attribute)) {
				/*
				 * Downgrading from EXCLUSIVE to SHARED
				 * on same minor ?
				 */
				if (BITTEST(m_mask[block], offset))
					continue;
				else
					return (SMC_FAILURE);
			}
			/*
			 * If we are trying to add an exclusive entry when a
			 * shared entry is already present with different
			 * minor#, deny this insertion
			 */
			if (attr == SC_ATTR_EXCLUSIVE) {
				if (!BITTEST(m_mask[block], offset) ||
					(count > 1))
					return (SMC_FAILURE);
			}
		} else
			continue;
	}

	return (SMC_SUCCESS);
}

/*
 * validate a request to remove a a set of entries, should be called after
 * basic validation is completed.
 */
static int
ctsmc_validate_cmdspec_rem(ctsmc_minor_t *mnode_p, int8_t n_cmds,
		sc_cmdspec_t *cmdspec)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	uint8_t cmd, minor = mnode_p->minor;
	int i;

	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent;
	uint16_t *m_mask;
	uint8_t count;
	uint8_t block = NUM_TO_BLOCK(minor),
			offset = NUM_OFFSET(minor);

	for (i = 0; i < n_cmds; i++) {
		cmd = cmdspec->args[i];
		ent = &(list->ctsmc_cmd_ent[cmd]);
		m_mask = ent->minor_mask;
		count = SMC_BIT_COUNT(m_mask);
		/*
		 * First check whether there is any entry present for this
		 * command
		 */
		if (count == 0)
			return (SMC_FAILURE);

		/*
		 * return failure if access to this command is never
		 * requested on this minor number
		 */
		if (!BITTEST(m_mask[block], offset))
			return (SMC_FAILURE);
	}

	return (SMC_SUCCESS);
}

/*
 * Make sure all entries in the list are valid and there
 * is no duplicate among the entries
 */
int
ctsmc_detect_duplicate(int len, uchar_t *list, intFnPtr iFn)
{
	int i, j;

	/*
	 * Check for duplicate
	 */
	for (i = 0; i < len - 1; i++)
		for (j = i+1; j < len; j++)
			if (list[i] == list[j])
				return (SMC_FAILURE);
	/*
	 * Check whether each one is a valid command
	 */
	if (iFn) {
		for (i = 0; i < len; i++) {
			if ((*iFn)(list[i]))
				return (SMC_FAILURE);
		}
	}

	return (SMC_SUCCESS);
}

/*
 * Validate the cmdset specified as part of REQ/CLR_ASYNC, SET/CLR_EXCL
 * ioctls. We pass the length passed from ioctl and the sc_cmdspec_t *
 * returns SMC_SUCCESS is no error, else SMC_FAILURE.
 */
static int
ctsmc_validate_cmdspec(ctsmc_minor_t *mnode_p, int ioclen,
		sc_cmdspec_t *cmdspec)
{
	uint8_t misclen = sizeof (sc_cmdspec_t) - MAX_CMDS * sizeof (uint8_t),
			n_cmds = ioclen - misclen;
	uint8_t attr = cmdspec->attribute;
	intFnPtr iFn;
	ctsmc_cmdspec_ptr_t cfnPtr;
	int i;

	if ((attr != SC_ATTR_CLEAR) &&
			(attr != SC_ATTR_CLEARALL) &&
			(attr != SC_ATTR_SHARED) &&
			(attr != SC_ATTR_EXCLUSIVE))
		return (SMC_FAILURE);

	/* Nothing more to check for SC_ATTR_CLEARALL */
	if (attr == SC_ATTR_CLEARALL)
		return (SMC_SUCCESS);

	/* If not CLEARALL, must have at least one command */
	if (n_cmds < 1)
		return (SMC_FAILURE);
	iFn = ctsmc_command_valid;

	if (ctsmc_detect_duplicate(n_cmds, cmdspec->args, iFn) !=
			SMC_SUCCESS)
		return (SMC_FAILURE);

	if (attr == SC_ATTR_CLEAR)
		cfnPtr = ctsmc_validate_cmdspec_rem;
	else
		cfnPtr = ctsmc_validate_cmdspec_ins;

	for (i = 0; i < n_cmds; i++) {
		if ((*cfnPtr)(mnode_p, n_cmds, cmdspec) !=
				SMC_SUCCESS)
			return (SMC_FAILURE);
	}

	return (SMC_SUCCESS);
}

/*
 * Insert a minor entry to command spec list
 */
static int
ctsmc_insert_cmdspec_ent(ctsmc_minor_t *mnode_p, int8_t n_cmds,
		sc_cmdspec_t *cmdspec)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	uint8_t minor = mnode_p->minor, cmd, attr = cmdspec->attribute;
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent;
	uint16_t *m_mask;
	uint8_t count, block = NUM_TO_BLOCK(minor), offset = NUM_OFFSET(minor);
	uint8_t ret = SMC_SUCCESS;
	int i;

	for (i = 0; i < n_cmds; i++) {
		cmd = cmdspec->args[i];
		ent = &(list->ctsmc_cmd_ent[cmd]);
		m_mask = ent->minor_mask;
		count = SMC_BIT_COUNT(m_mask);
		/*
		 * Ignore the request and return success if we ask for adding
		 * identical entry. If the request is to downgrade from
		 * EXCLUSIVE to SHARED on the same minor, allow this request.
		 */
		if (count) {
			if ((attr == ent->attribute) &&
					BITTEST(m_mask[block], offset)) {
				ret = SMC_FAILURE;
				break;
			}

			if ((attr == SC_ATTR_SHARED) &&
					(attr != ent->attribute)) {
				/* Downgrading from EXCLUSIVE to SHARED */
				if (BITTEST(m_mask[block], offset)) {
					ent->minor = minor;
					ent->attribute = attr;

					continue;
				}
				ret = SMC_FAILURE;
				break;
			}
		}


		/*
		 * If we are trying to add an exclusive entry when a
		 * shared entry is already present with same minor
		 * node, obey this request
		 */
		if ((attr == SC_ATTR_EXCLUSIVE) && count) {
			if (!BITTEST(m_mask[block], offset) ||
					(count > 1)) {
				ret = SMC_FAILURE;
				break;
			} else { /* upgrade from shared to exclusive */
				ent->attribute = attr;
				ent->minor = minor;	/* Cache minor */

				continue;
			}
		}

		/*
		 * Either adding first exclusive entry -or- additional
		 * shared entry Set bit for this minor number in bitmask
		 */
		ent->attribute = attr;
		ent->minor = minor;	/* Cache minor */
		SETBIT(m_mask[block], offset);
	}

	return (ret);
}

/*
 * Remove an entry from async command handler
 */
static int
ctsmc_remove_cmdspec_ent(ctsmc_minor_t *mnode_p, int8_t n_cmds,
		sc_cmdspec_t *cmdspec)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	int i;
	uint8_t ret = SMC_SUCCESS, cmd, minor = mnode_p->minor;
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent;
	uint16_t *m_mask;
	uint8_t count, block = NUM_TO_BLOCK(minor),
		offset = NUM_OFFSET(minor);

	for (i = 0; i < n_cmds; i++) {
		cmd = cmdspec->args[i];
		ent = &(list->ctsmc_cmd_ent[cmd]);
		m_mask = ent->minor_mask;
		count = SMC_BIT_COUNT(m_mask);
		/*
		 * First check whether there is any entry
		 * present for this command
		 */
		if (count == 0) {
			ret = SMC_FAILURE;
			break;
		}

		/*
		 * Now check whether another entry with same minor &
		 * sequence is present in list of entries for this
		 * command; if present, delete this entry
		 */
		CLRBIT(m_mask[block], offset);
		ent->attribute = SC_ATTR_SHARED;
	}

	return (ret);
}

/*
 * Removes all entries which match a given minor number. This
 * will be used to free all entries of a specific type for
 * a given minor number, in response to SC_CMDSPEC_CLEARALL
 */
/* ARGSUSED */
static int
ctsmc_clear_cmdspec_refs(ctsmc_minor_t *mnode_p, int8_t n_cmds,
		sc_cmdspec_t *cmdspec)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	int i;
	uint8_t minor = mnode_p->minor;
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent;
	uint8_t block = NUM_TO_BLOCK(minor),
			offset = NUM_OFFSET(minor);

	for (i = 0; i < SMC_NUM_CMDS; i++) {
		ent = &(list->ctsmc_cmd_ent[i]);
		if (BITTEST(ent->minor_mask[block], offset)) {
			CLRBIT(ent->minor_mask[block], offset);
		}
	}

	return (SMC_SUCCESS);
}

#define	ATTR(X)	((X) == SC_ATTR_SHARED ? "SC_ATTR_SHARED" : \
		"SC_ATTR_EXCLUSIVE")
#ifdef DEBUG
static void
ctsmc_print_cmdspec_list(ctsmc_state_t *ctsmc)
{
	int i = 0, j = 0, k = 0;
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent;
	uint16_t mask, *m_mask;

	for (i = 0; i < SMC_NUM_CMDS; i++) {
		ent = &(list->ctsmc_cmd_ent[i]);
		m_mask = ent->minor_mask;
		for (j = 0; j < NUM_BLOCKS; j++) {
			mask = m_mask[j];
			for (k = 0; mask; mask >>= 1, k++) {
				if (mask&1)
					SMC_DEBUG3(SMC_UTILS_DEBUG,
						"CMD = %x, attr = %s, "
						"[minor = %d]", i,
						ATTR(ent->attribute),
						j * SMC_BLOCK_SZ + k);
			}
		}
	}
}
#endif	/* DEBUG */

int
ctsmc_update_cmdspec_list(ctsmc_minor_t *mnode_p, uint8_t ioclen,
		sc_cmdspec_t *cmdspec)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	uint8_t misclen = sizeof (sc_cmdspec_t) -
			MAX_CMDS * sizeof (uint8_t);
	int8_t n_cmds = ioclen - misclen, ret = SMC_SUCCESS;
	sc_cmd_attr_t attr = cmdspec->attribute;
	ctsmc_cmdspec_ptr_t cfnPtr;

	LOCK_DATA(list);
	if (ctsmc_validate_cmdspec(mnode_p, ioclen, cmdspec) !=
			SMC_SUCCESS) {
		UNLOCK_DATA(list);
		return (SMC_FAILURE);
	}

	if (attr == SC_ATTR_CLEARALL)
		cfnPtr = ctsmc_clear_cmdspec_refs;
	else
	if (attr == SC_ATTR_CLEAR)
		cfnPtr = ctsmc_remove_cmdspec_ent;
	else
		cfnPtr = ctsmc_insert_cmdspec_ent;

	ret = (*cfnPtr)(mnode_p, n_cmds, cmdspec);
#ifdef DEBUG
	ctsmc_print_cmdspec_list(ctsmc);
#endif

	UNLOCK_DATA(list);

	return (ret);
}

/*
 * Removes all entries which match a given minor number. This
 * will be used to clean up this table when a minor number
 * is closed.
 */
void
ctsmc_clear_cmdspec_refs_all(ctsmc_state_t *ctsmc, uint8_t minor)
{
	ctsmc_cmdspec_list_t *list = ctsmc->ctsmc_cmdspec_list;
	ctsmc_cmdspec_ent_t *ent;
	uint16_t i;
	uint8_t block = NUM_TO_BLOCK(minor),
			offset = NUM_OFFSET(minor);

	LOCK_DATA(list);
	for (i = 0; i < SMC_NUM_CMDS; i++) {
		ent = &(list->ctsmc_cmd_ent[i]);
		if (BITTEST(ent->minor_mask[block], offset)) {
			CLRBIT(ent->minor_mask[block], offset);
		}
	}

	UNLOCK_DATA(list);
}
