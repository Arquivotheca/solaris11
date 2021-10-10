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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ast.h"

static void
I2CWriteClock(struct ast_softc *softc, UCHAR data)
{
	UCHAR	ujCRB7, jtemp;
	ULONG	i;

	for (i = 0; i < 0x10000; i++) {
		ujCRB7 = ((data & 0x01) ? 0:1);		/* low active */
		SetIndexRegMask(CRTC_PORT, 0xB7, 0xFE, ujCRB7);
		GetIndexRegMask(CRTC_PORT, 0xB7, 0x01, jtemp);
		if (ujCRB7 == jtemp) break;
	}
}


static void
I2CWriteData(struct ast_softc *softc, UCHAR data)
{
	UCHAR	volatile ujCRB7, jtemp;
	ULONG	i;

	for (i = 0; i < 0x1000; i++) {
		ujCRB7 = ((data & 0x01) ? 0:1) << 2;	/* low active */
		SetIndexRegMask(CRTC_PORT, 0xB7, 0xFB, ujCRB7);
		GetIndexRegMask(CRTC_PORT, 0xB7, 0x04, jtemp);
		if (ujCRB7 == jtemp) break;
	}

}

static Bool
I2CReadClock(struct ast_softc *softc)
{
	UCHAR	volatile ujCRB7;

	GetIndexRegMask(CRTC_PORT, 0xB7, 0x10, ujCRB7);
	ujCRB7 >>= 4;

	return ((ujCRB7 & 0x01) ? 1:0);
}

static int
I2CReadData(struct ast_softc *softc)
{
	unsigned char	volatile ujCRB7;

	GetIndexRegMask(CRTC_PORT, 0xB7, 0x20, ujCRB7);
	ujCRB7 >>= 5;

	return ((ujCRB7 & 0x01) ? 1:0);
}


static void
I2CDelay(struct ast_softc *softc)
{
	unsigned int	i;
	/* LINTED set but not used in function: jtemp (E_FUNC_SET_NOT_USED) */
	unsigned char	jtemp;

	for (i = 0; i < 150; i++)
		GetReg(SEQ_PORT, jtemp);
}


static void
I2CStart(struct ast_softc *softc)
{
	I2CWriteClock(softc, 0x00);		/* Set Clk Low */
	I2CDelay(softc);
	I2CWriteData(softc, 0x01);		/* Set Data High */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);		/* Set Clk High */
	I2CDelay(softc);
	I2CWriteData(softc, 0x00);		/* Set Data Low */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);		/* Set Clk High */
	I2CDelay(softc);
}


static void
I2CStop(struct ast_softc *softc)
{
	I2CWriteClock(softc, 0x00);			/* Set Clk Low */
	I2CDelay(softc);
	I2CWriteData(softc, 0x00);			/* Set Data Low */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);			/* Set Clk High */
	I2CDelay(softc);
	I2CWriteData(softc, 0x01);			/* Set Data High */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);			/* Set Clk High */
	I2CDelay(softc);
}


static Bool
CheckACK(struct ast_softc *softc)
{
	unsigned char Data;

	I2CWriteClock(softc, 0x00);			/* Set Clk Low */
	I2CDelay(softc);
	I2CWriteData(softc, 0x01);			/* Set Data High */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);			/* Set Clk High */
	I2CDelay(softc);
	I2CDelay(softc);
	I2CDelay(softc);
	Data = (unsigned char) I2CReadData(softc);	/* Set Data High */

	return ((Data & 0x01) ? 0:1);
}


static void
SendACK(struct ast_softc *softc)
{
	I2CWriteClock(softc, 0x00);			/* Set Clk Low */
	I2CDelay(softc);
	I2CWriteData(softc, 0x00);			/* Set Data low */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);			/* Set Clk High */
	I2CDelay(softc);
}


static void
SendNACK(struct ast_softc *softc)
{
	I2CWriteClock(softc, 0x00);			/* Set Clk Low */
	I2CDelay(softc);
	I2CWriteData(softc, 0x01);			/* Set Data high */
	I2CDelay(softc);
	I2CWriteClock(softc, 0x01);			/* Set Clk High */
	I2CDelay(softc);
}


static void
SendI2CDataByte(struct ast_softc *softc, unsigned char data)
{
	unsigned char jData;
	int i;

	for (i = 7; i >= 0; i--) {
		I2CWriteClock(softc, 0x00);		/* Set Clk Low */
		I2CDelay(softc);

		jData = ((data >> i) & 0x01) ? 1:0;
		I2CWriteData(softc, jData);		/* Set Data Low */
		I2CDelay(softc);

		I2CWriteClock(softc, 0x01);		/* Set Clk High */
		I2CDelay(softc);
	}
}


static unsigned char
ReceiveI2CDataByte(struct ast_softc *softc)
{
	unsigned char jData = 0, jTempData;
	int i, j;

	for (i = 7; i >= 0; i--) {
		I2CWriteClock(softc, 0x00);		/* Set Clk Low */
		I2CDelay(softc);

		I2CWriteData(softc, 0x01);		/* Set Data High */
		I2CDelay(softc);

		I2CWriteClock(softc, 0x01);		/* Set Clk High */
		I2CDelay(softc);

		for (j = 0; j < 0x1000; j++) {
			if (I2CReadClock(softc))
				break;
		}

		jTempData =  I2CReadData(softc);
		jData |= ((jTempData & 0x01) << i);

		I2CWriteClock(softc, 0x0);		/* Set Clk Low */
		I2CDelay(softc);
	}

	return ((UCHAR)jData);
}


static Bool
GetVGAEDID(struct ast_softc *softc, unsigned char *pEDIDBuffer, int *length)
{
	unsigned char *pjDstEDID;
	unsigned char jData;
	unsigned int  i;
	unsigned int  len = *length - 1;

	pjDstEDID = (UCHAR *) pEDIDBuffer;

	/* Force to DDC2 */
	I2CWriteClock(softc, 0x01);			/* Set Clk Low */
	I2CDelay(softc);
	I2CDelay(softc);
	I2CWriteClock(softc, 0x00);			/* Set Clk Low */
	I2CDelay(softc);

	I2CStart(softc);

	SendI2CDataByte(softc, 0xA0);
	if (!CheckACK(softc)) {
#ifdef AST_DEBUG
		printf("[GetVGAEDID] Check ACK Failed \n");
#endif /* AST_DEBUG */

		return (FALSE);
	}

	SendI2CDataByte(softc, 0x00);
	if (!CheckACK(softc)) {
#ifdef AST_DEBUG
		printf("[GetVGAEDID] Check ACK Failed \n");
#endif /* AST_DEBUG */

		return (FALSE);
	}

	I2CStart(softc);

	SendI2CDataByte(softc, 0xA1);
	if (!CheckACK(softc)) {
#ifdef AST_DEBUG
		printf("[GetVGAEDID] Check ACK Failed \n");
#endif /* AST_DEBUG */

		return (FALSE);
	}

	for (i = 0; i < len; i++) {
		jData = ReceiveI2CDataByte(softc);
		SendACK(softc);

		*pjDstEDID++ = jData;
	}

	jData = ReceiveI2CDataByte(softc);
	SendNACK(softc);
	*pjDstEDID = jData;

	I2CStop(softc);

	return (TRUE);
}


int
ast_read_edid(struct ast_softc *softc, caddr_t pEDIDBuffer,
    unsigned int *length)
{
	if (!GetVGAEDID(softc, (unsigned char *)pEDIDBuffer, (int *)length)) {
		*length = 0;
	}

	return (0);
}
