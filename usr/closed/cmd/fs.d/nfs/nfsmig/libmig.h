/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBMIG_H
#define	_LIBMIG_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define	SRC	0
#define	DEST	1

/*
 * The first three fields are related to the error codes. Rest of the
 * fields comprise the "sense data" that captures any relevant information
 * about the error codes.
 */
typedef struct {
	uint32_t	mes_syserr;	/* system call related error code */
	uint32_t	mes_liberr;	/* library related error code */
	uint32_t	mes_migerr;	/* migration specific error context */
	uint32_t	mes_fsstat;	/* current state of the file system */
	char		*mes_hint;	/* location hint */
} mig_errsig_t;

/*
 * The following error codes capture the errors that happen in the miglib.
 * These error conditions are not represented in the kernel by the mig specific
 * error codes, nor are they captured by any errno.
 */
enum liberr {
	LIB_OK = 0,
	LIBERR_PARSE,
	LIBERR_ZFSCREATE,
	LIBERR_SETFSID,
	LIBERR_RESETFSID,
	LIBERR_GETFSID,
	LIBERR_REMOVEFSID,
	LIBERR_NOTZFS,
	LIBERR_REPARSE,
	LIBERR_NOTMNTPT,
	LIBERR_NOSNAPS,
	LIBERR_NOSNAPREC,
	LIBERR_UNDEFARCH,
	LIBERR_PATHNAME,
	LIBERR_BADFSID,
	LIBERR_RECMDEXEC,
	LIBERR_LOCMDEXEC,
	LIBERR_SHARECMD,
	LIBERR_UNSHARECMD,
	LIBERR_MOUNTCMD,
	LIBERR_UMOUNTCMD,
	LIBERR_ZFSLISTCMD,
	LIBERR_FCLOSE,
	LIBERR_NESTED
};
typedef enum liberr liberr_t;

extern int popen_cmd(char *, char *, FILE **);
extern int close_file(FILE **);
extern char *format_junction(char *, char *, mig_errsig_t *);
extern int make_referral(char *, char *, char *, mig_errsig_t *);
extern int provision(char *, char *, mig_errsig_t *);
extern int setfsid(char *, uint64_t, mig_errsig_t *);
extern int resetfsid(char *, uint64_t, mig_errsig_t *);
extern int getfsid(char *, uint64_t *, mig_errsig_t *);
extern int removefsid(char *, uint64_t *, mig_errsig_t *);
extern int freeze(char *, mig_errsig_t *);
extern int grace(char *, mig_errsig_t *);
extern int thaw(char *, mig_errsig_t *);
extern int harvest(char *, mig_errsig_t *);
extern int hydrate(char *, mig_errsig_t *);
extern int convert(char *, char *, mig_errsig_t *);
extern int unconvert(char *, mig_errsig_t *);
extern int update(char *, char *, mig_errsig_t *);
extern int status(char *, uint32_t *, mig_errsig_t *);
extern int clear(char *, int, mig_errsig_t *);

#ifdef  __cplusplus
}
#endif

#endif	/* _LIBMIG_H */
