/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_N2_CRYPTO_DR_H
#define	_SYS_N2_CRYPTO_DR_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Crypto DR domain service control protocol
 */

#include <sys/ds.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Crypto DR domain service message header
 */

#define	N2_DR_MAX_CRYPTO_UNITS		32

typedef struct {
	uint64_t	req_num;	/* request number */
	uint32_t	msg_type;	/* message type */
	uint32_t	num_records;	/* number of records */
} dr_crypto_hdr_t;


/* Crypto DR operation results */
typedef struct {
	uint32_t	cpuid;
	uint32_t	result;
	uint32_t	status;
} dr_crypto_res_t;


/*
 * Crypto DR request and response messages
 */

#define	DR_CRYPTO_MAU_ID		"dr-crypto-mau"
#define	DR_CRYPTO_CWQ_ID		"dr-crypto-cwq"

/* request message types */

#define	DR_CRYPTO_CONFIG		('C')
#define	DR_CRYPTO_UNCONFIG		('U')
#define	DR_CRYPTO_FORCE_UNCONFIG	('F')
#define	DR_CRYPTO_STATUS		('S')

/* response messages */

#define	DR_CRYPTO_OK			('o')
#define	DR_CRYPTO_ERROR			('e')

/* result-status message */
typedef struct {
	uint32_t	cpuid;	/* virtual cpu associated with crypto unit */
	uint32_t	result;	/* result of the operation */
	uint32_t	status;	/* status of the crypto unit */
} dr_crypto_stat_t;


/* result codes */
#define	DR_CRYPTO_RES_OK			0
#define	DR_CRYPTO_RES_FAILURE			1
#define	DR_CRYPTO_RES_BAD_CPU			2
#define	DR_CRYPTO_RES_BAD_CRYPTO		3


/* status codes */
#define	DR_CRYPTO_STAT_NOT_PRESENT		0
#define	DR_CRYPTO_STAT_UNCONFIGURED		1
#define	DR_CRYPTO_STAT_CONFIGURED		2

/*
 * macros
 */
#define	DR_CRYPTO_CMD_CPUIDS(_hdr)	((uint32_t *)((_hdr) + 1))
#define	DR_CRYPTO_RESP_STATS(_hdr)	((dr_crypto_stat_t *)((_hdr) + 1))


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_N2_CRYPTO_DR_H */
