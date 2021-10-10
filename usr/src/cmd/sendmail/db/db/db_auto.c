/* Do not edit: automatically built by dist/db_gen.sh. */
#include "config.h"

#ifndef NO_SYSTEM_INCLUDES
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "db_int.h"
#include "db_page.h"
#include "db_dispatch.h"
#include "db_am.h"
/*
 * PUBLIC: int __db_addrem_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     u_int32_t, u_int32_t, db_pgno_t, u_int32_t,
 * PUBLIC:     size_t, const DBT *, const DBT *, DB_LSN *));
 */
int __db_addrem_log(logp, txnid, ret_lsnp, flags,
	opcode, fileid, pgno, indx, nbytes, hdr,
	dbt, pagelsn)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u_int32_t opcode;
	u_int32_t fileid;
	db_pgno_t pgno;
	u_int32_t indx;
	size_t nbytes;
	const DBT *hdr;
	const DBT *dbt;
	DB_LSN * pagelsn;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t zero;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_addrem;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(opcode)
	    + sizeof(fileid)
	    + sizeof(pgno)
	    + sizeof(indx)
	    + sizeof(nbytes)
	    + sizeof(u_int32_t) + (hdr == NULL ? 0 : hdr->size)
	    + sizeof(u_int32_t) + (dbt == NULL ? 0 : dbt->size)
	    + sizeof(*pagelsn);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(bp, &opcode, sizeof(opcode));
	bp += sizeof(opcode);
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	memcpy(bp, &pgno, sizeof(pgno));
	bp += sizeof(pgno);
	memcpy(bp, &indx, sizeof(indx));
	bp += sizeof(indx);
	memcpy(bp, &nbytes, sizeof(nbytes));
	bp += sizeof(nbytes);
	if (hdr == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &hdr->size, sizeof(hdr->size));
		bp += sizeof(hdr->size);
		memcpy(bp, hdr->data, hdr->size);
		bp += hdr->size;
	}
	if (dbt == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &dbt->size, sizeof(dbt->size));
		bp += sizeof(dbt->size);
		memcpy(bp, dbt->data, dbt->size);
		bp += dbt->size;
	}
	if (pagelsn != NULL)
		memcpy(bp, pagelsn, sizeof(*pagelsn));
	else
		memset(bp, 0, sizeof(*pagelsn));
	bp += sizeof(*pagelsn);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_addrem_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_addrem_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_addrem_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_addrem_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_addrem: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\topcode: %lu\n", (u_long)argp->opcode);
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tpgno: %lu\n", (u_long)argp->pgno);
	printf("\tindx: %lu\n", (u_long)argp->indx);
	printf("\tnbytes: %lu\n", (u_long)argp->nbytes);
	printf("\thdr: ");
	for (i = 0; i < argp->hdr.size; i++) {
		ch = ((u_int8_t *)argp->hdr.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\tdbt: ");
	for (i = 0; i < argp->dbt.size; i++) {
		ch = ((u_int8_t *)argp->dbt.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\tpagelsn: [%lu][%lu]\n",
	    (u_long)argp->pagelsn.file, (u_long)argp->pagelsn.offset);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_addrem_read __P((void *, __db_addrem_args **));
 */
int
__db_addrem_read(recbuf, argpp)
	void *recbuf;
	__db_addrem_args **argpp;
{
	__db_addrem_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_addrem_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->opcode, bp, sizeof(argp->opcode));
	bp += sizeof(argp->opcode);
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->pgno, bp, sizeof(argp->pgno));
	bp += sizeof(argp->pgno);
	memcpy(&argp->indx, bp, sizeof(argp->indx));
	bp += sizeof(argp->indx);
	memcpy(&argp->nbytes, bp, sizeof(argp->nbytes));
	bp += sizeof(argp->nbytes);
	memcpy(&argp->hdr.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->hdr.data = bp;
	bp += argp->hdr.size;
	memcpy(&argp->dbt.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->dbt.data = bp;
	bp += argp->dbt.size;
	memcpy(&argp->pagelsn, bp,  sizeof(argp->pagelsn));
	bp += sizeof(argp->pagelsn);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_split_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     u_int32_t, u_int32_t, db_pgno_t, const DBT *,
 * PUBLIC:     DB_LSN *));
 */
int __db_split_log(logp, txnid, ret_lsnp, flags,
	opcode, fileid, pgno, pageimage, pagelsn)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u_int32_t opcode;
	u_int32_t fileid;
	db_pgno_t pgno;
	const DBT *pageimage;
	DB_LSN * pagelsn;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t zero;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_split;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(opcode)
	    + sizeof(fileid)
	    + sizeof(pgno)
	    + sizeof(u_int32_t) + (pageimage == NULL ? 0 : pageimage->size)
	    + sizeof(*pagelsn);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(bp, &opcode, sizeof(opcode));
	bp += sizeof(opcode);
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	memcpy(bp, &pgno, sizeof(pgno));
	bp += sizeof(pgno);
	if (pageimage == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &pageimage->size, sizeof(pageimage->size));
		bp += sizeof(pageimage->size);
		memcpy(bp, pageimage->data, pageimage->size);
		bp += pageimage->size;
	}
	if (pagelsn != NULL)
		memcpy(bp, pagelsn, sizeof(*pagelsn));
	else
		memset(bp, 0, sizeof(*pagelsn));
	bp += sizeof(*pagelsn);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_split_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_split_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_split_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_split_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_split: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\topcode: %lu\n", (u_long)argp->opcode);
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tpgno: %lu\n", (u_long)argp->pgno);
	printf("\tpageimage: ");
	for (i = 0; i < argp->pageimage.size; i++) {
		ch = ((u_int8_t *)argp->pageimage.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\tpagelsn: [%lu][%lu]\n",
	    (u_long)argp->pagelsn.file, (u_long)argp->pagelsn.offset);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_split_read __P((void *, __db_split_args **));
 */
int
__db_split_read(recbuf, argpp)
	void *recbuf;
	__db_split_args **argpp;
{
	__db_split_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_split_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->opcode, bp, sizeof(argp->opcode));
	bp += sizeof(argp->opcode);
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->pgno, bp, sizeof(argp->pgno));
	bp += sizeof(argp->pgno);
	memcpy(&argp->pageimage.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->pageimage.data = bp;
	bp += argp->pageimage.size;
	memcpy(&argp->pagelsn, bp,  sizeof(argp->pagelsn));
	bp += sizeof(argp->pagelsn);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_big_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     u_int32_t, u_int32_t, db_pgno_t, db_pgno_t,
 * PUBLIC:     db_pgno_t, const DBT *, DB_LSN *, DB_LSN *,
 * PUBLIC:     DB_LSN *));
 */
int __db_big_log(logp, txnid, ret_lsnp, flags,
	opcode, fileid, pgno, prev_pgno, next_pgno, dbt,
	pagelsn, prevlsn, nextlsn)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u_int32_t opcode;
	u_int32_t fileid;
	db_pgno_t pgno;
	db_pgno_t prev_pgno;
	db_pgno_t next_pgno;
	const DBT *dbt;
	DB_LSN * pagelsn;
	DB_LSN * prevlsn;
	DB_LSN * nextlsn;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t zero;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_big;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(opcode)
	    + sizeof(fileid)
	    + sizeof(pgno)
	    + sizeof(prev_pgno)
	    + sizeof(next_pgno)
	    + sizeof(u_int32_t) + (dbt == NULL ? 0 : dbt->size)
	    + sizeof(*pagelsn)
	    + sizeof(*prevlsn)
	    + sizeof(*nextlsn);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(bp, &opcode, sizeof(opcode));
	bp += sizeof(opcode);
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	memcpy(bp, &pgno, sizeof(pgno));
	bp += sizeof(pgno);
	memcpy(bp, &prev_pgno, sizeof(prev_pgno));
	bp += sizeof(prev_pgno);
	memcpy(bp, &next_pgno, sizeof(next_pgno));
	bp += sizeof(next_pgno);
	if (dbt == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &dbt->size, sizeof(dbt->size));
		bp += sizeof(dbt->size);
		memcpy(bp, dbt->data, dbt->size);
		bp += dbt->size;
	}
	if (pagelsn != NULL)
		memcpy(bp, pagelsn, sizeof(*pagelsn));
	else
		memset(bp, 0, sizeof(*pagelsn));
	bp += sizeof(*pagelsn);
	if (prevlsn != NULL)
		memcpy(bp, prevlsn, sizeof(*prevlsn));
	else
		memset(bp, 0, sizeof(*prevlsn));
	bp += sizeof(*prevlsn);
	if (nextlsn != NULL)
		memcpy(bp, nextlsn, sizeof(*nextlsn));
	else
		memset(bp, 0, sizeof(*nextlsn));
	bp += sizeof(*nextlsn);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_big_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_big_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_big_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_big_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_big: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\topcode: %lu\n", (u_long)argp->opcode);
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tpgno: %lu\n", (u_long)argp->pgno);
	printf("\tprev_pgno: %lu\n", (u_long)argp->prev_pgno);
	printf("\tnext_pgno: %lu\n", (u_long)argp->next_pgno);
	printf("\tdbt: ");
	for (i = 0; i < argp->dbt.size; i++) {
		ch = ((u_int8_t *)argp->dbt.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\tpagelsn: [%lu][%lu]\n",
	    (u_long)argp->pagelsn.file, (u_long)argp->pagelsn.offset);
	printf("\tprevlsn: [%lu][%lu]\n",
	    (u_long)argp->prevlsn.file, (u_long)argp->prevlsn.offset);
	printf("\tnextlsn: [%lu][%lu]\n",
	    (u_long)argp->nextlsn.file, (u_long)argp->nextlsn.offset);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_big_read __P((void *, __db_big_args **));
 */
int
__db_big_read(recbuf, argpp)
	void *recbuf;
	__db_big_args **argpp;
{
	__db_big_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_big_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->opcode, bp, sizeof(argp->opcode));
	bp += sizeof(argp->opcode);
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->pgno, bp, sizeof(argp->pgno));
	bp += sizeof(argp->pgno);
	memcpy(&argp->prev_pgno, bp, sizeof(argp->prev_pgno));
	bp += sizeof(argp->prev_pgno);
	memcpy(&argp->next_pgno, bp, sizeof(argp->next_pgno));
	bp += sizeof(argp->next_pgno);
	memcpy(&argp->dbt.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->dbt.data = bp;
	bp += argp->dbt.size;
	memcpy(&argp->pagelsn, bp,  sizeof(argp->pagelsn));
	bp += sizeof(argp->pagelsn);
	memcpy(&argp->prevlsn, bp,  sizeof(argp->prevlsn));
	bp += sizeof(argp->prevlsn);
	memcpy(&argp->nextlsn, bp,  sizeof(argp->nextlsn));
	bp += sizeof(argp->nextlsn);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_ovref_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     u_int32_t, db_pgno_t, int32_t, DB_LSN *));
 */
int __db_ovref_log(logp, txnid, ret_lsnp, flags,
	fileid, pgno, adjust, lsn)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u_int32_t fileid;
	db_pgno_t pgno;
	int32_t adjust;
	DB_LSN * lsn;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_ovref;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(fileid)
	    + sizeof(pgno)
	    + sizeof(adjust)
	    + sizeof(*lsn);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	memcpy(bp, &pgno, sizeof(pgno));
	bp += sizeof(pgno);
	memcpy(bp, &adjust, sizeof(adjust));
	bp += sizeof(adjust);
	if (lsn != NULL)
		memcpy(bp, lsn, sizeof(*lsn));
	else
		memset(bp, 0, sizeof(*lsn));
	bp += sizeof(*lsn);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_ovref_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_ovref_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_ovref_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_ovref_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_ovref: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tpgno: %lu\n", (u_long)argp->pgno);
	printf("\tadjust: %ld\n", (long)argp->adjust);
	printf("\tlsn: [%lu][%lu]\n",
	    (u_long)argp->lsn.file, (u_long)argp->lsn.offset);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_ovref_read __P((void *, __db_ovref_args **));
 */
int
__db_ovref_read(recbuf, argpp)
	void *recbuf;
	__db_ovref_args **argpp;
{
	__db_ovref_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_ovref_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->pgno, bp, sizeof(argp->pgno));
	bp += sizeof(argp->pgno);
	memcpy(&argp->adjust, bp, sizeof(argp->adjust));
	bp += sizeof(argp->adjust);
	memcpy(&argp->lsn, bp,  sizeof(argp->lsn));
	bp += sizeof(argp->lsn);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_relink_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     u_int32_t, u_int32_t, db_pgno_t, DB_LSN *,
 * PUBLIC:     db_pgno_t, DB_LSN *, db_pgno_t, DB_LSN *));
 */
int __db_relink_log(logp, txnid, ret_lsnp, flags,
	opcode, fileid, pgno, lsn, prev, lsn_prev,
	next, lsn_next)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u_int32_t opcode;
	u_int32_t fileid;
	db_pgno_t pgno;
	DB_LSN * lsn;
	db_pgno_t prev;
	DB_LSN * lsn_prev;
	db_pgno_t next;
	DB_LSN * lsn_next;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_relink;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(opcode)
	    + sizeof(fileid)
	    + sizeof(pgno)
	    + sizeof(*lsn)
	    + sizeof(prev)
	    + sizeof(*lsn_prev)
	    + sizeof(next)
	    + sizeof(*lsn_next);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(bp, &opcode, sizeof(opcode));
	bp += sizeof(opcode);
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	memcpy(bp, &pgno, sizeof(pgno));
	bp += sizeof(pgno);
	if (lsn != NULL)
		memcpy(bp, lsn, sizeof(*lsn));
	else
		memset(bp, 0, sizeof(*lsn));
	bp += sizeof(*lsn);
	memcpy(bp, &prev, sizeof(prev));
	bp += sizeof(prev);
	if (lsn_prev != NULL)
		memcpy(bp, lsn_prev, sizeof(*lsn_prev));
	else
		memset(bp, 0, sizeof(*lsn_prev));
	bp += sizeof(*lsn_prev);
	memcpy(bp, &next, sizeof(next));
	bp += sizeof(next);
	if (lsn_next != NULL)
		memcpy(bp, lsn_next, sizeof(*lsn_next));
	else
		memset(bp, 0, sizeof(*lsn_next));
	bp += sizeof(*lsn_next);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_relink_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_relink_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_relink_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_relink_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_relink: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\topcode: %lu\n", (u_long)argp->opcode);
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tpgno: %lu\n", (u_long)argp->pgno);
	printf("\tlsn: [%lu][%lu]\n",
	    (u_long)argp->lsn.file, (u_long)argp->lsn.offset);
	printf("\tprev: %lu\n", (u_long)argp->prev);
	printf("\tlsn_prev: [%lu][%lu]\n",
	    (u_long)argp->lsn_prev.file, (u_long)argp->lsn_prev.offset);
	printf("\tnext: %lu\n", (u_long)argp->next);
	printf("\tlsn_next: [%lu][%lu]\n",
	    (u_long)argp->lsn_next.file, (u_long)argp->lsn_next.offset);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_relink_read __P((void *, __db_relink_args **));
 */
int
__db_relink_read(recbuf, argpp)
	void *recbuf;
	__db_relink_args **argpp;
{
	__db_relink_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_relink_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->opcode, bp, sizeof(argp->opcode));
	bp += sizeof(argp->opcode);
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->pgno, bp, sizeof(argp->pgno));
	bp += sizeof(argp->pgno);
	memcpy(&argp->lsn, bp,  sizeof(argp->lsn));
	bp += sizeof(argp->lsn);
	memcpy(&argp->prev, bp, sizeof(argp->prev));
	bp += sizeof(argp->prev);
	memcpy(&argp->lsn_prev, bp,  sizeof(argp->lsn_prev));
	bp += sizeof(argp->lsn_prev);
	memcpy(&argp->next, bp, sizeof(argp->next));
	bp += sizeof(argp->next);
	memcpy(&argp->lsn_next, bp,  sizeof(argp->lsn_next));
	bp += sizeof(argp->lsn_next);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_addpage_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     u_int32_t, db_pgno_t, DB_LSN *, db_pgno_t,
 * PUBLIC:     DB_LSN *));
 */
int __db_addpage_log(logp, txnid, ret_lsnp, flags,
	fileid, pgno, lsn, nextpgno, nextlsn)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	u_int32_t fileid;
	db_pgno_t pgno;
	DB_LSN * lsn;
	db_pgno_t nextpgno;
	DB_LSN * nextlsn;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_addpage;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(fileid)
	    + sizeof(pgno)
	    + sizeof(*lsn)
	    + sizeof(nextpgno)
	    + sizeof(*nextlsn);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	memcpy(bp, &pgno, sizeof(pgno));
	bp += sizeof(pgno);
	if (lsn != NULL)
		memcpy(bp, lsn, sizeof(*lsn));
	else
		memset(bp, 0, sizeof(*lsn));
	bp += sizeof(*lsn);
	memcpy(bp, &nextpgno, sizeof(nextpgno));
	bp += sizeof(nextpgno);
	if (nextlsn != NULL)
		memcpy(bp, nextlsn, sizeof(*nextlsn));
	else
		memset(bp, 0, sizeof(*nextlsn));
	bp += sizeof(*nextlsn);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_addpage_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_addpage_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_addpage_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_addpage_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_addpage: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tpgno: %lu\n", (u_long)argp->pgno);
	printf("\tlsn: [%lu][%lu]\n",
	    (u_long)argp->lsn.file, (u_long)argp->lsn.offset);
	printf("\tnextpgno: %lu\n", (u_long)argp->nextpgno);
	printf("\tnextlsn: [%lu][%lu]\n",
	    (u_long)argp->nextlsn.file, (u_long)argp->nextlsn.offset);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_addpage_read __P((void *, __db_addpage_args **));
 */
int
__db_addpage_read(recbuf, argpp)
	void *recbuf;
	__db_addpage_args **argpp;
{
	__db_addpage_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_addpage_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->pgno, bp, sizeof(argp->pgno));
	bp += sizeof(argp->pgno);
	memcpy(&argp->lsn, bp,  sizeof(argp->lsn));
	bp += sizeof(argp->lsn);
	memcpy(&argp->nextpgno, bp, sizeof(argp->nextpgno));
	bp += sizeof(argp->nextpgno);
	memcpy(&argp->nextlsn, bp,  sizeof(argp->nextlsn));
	bp += sizeof(argp->nextlsn);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_debug_log
 * PUBLIC:     __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
 * PUBLIC:     const DBT *, u_int32_t, const DBT *, const DBT *,
 * PUBLIC:     u_int32_t));
 */
int __db_debug_log(logp, txnid, ret_lsnp, flags,
	op, fileid, key, data, arg_flags)
	DB_LOG *logp;
	DB_TXN *txnid;
	DB_LSN *ret_lsnp;
	u_int32_t flags;
	const DBT *op;
	u_int32_t fileid;
	const DBT *key;
	const DBT *data;
	u_int32_t arg_flags;
{
	DBT logrec;
	DB_LSN *lsnp, null_lsn;
	u_int32_t zero;
	u_int32_t rectype, txn_num;
	int ret;
	u_int8_t *bp;

	rectype = DB_db_debug;
	txn_num = txnid == NULL ? 0 : txnid->txnid;
	if (txnid == NULL) {
		ZERO_LSN(null_lsn);
		lsnp = &null_lsn;
	} else
		lsnp = &txnid->last_lsn;
	logrec.size = sizeof(rectype) + sizeof(txn_num) + sizeof(DB_LSN)
	    + sizeof(u_int32_t) + (op == NULL ? 0 : op->size)
	    + sizeof(fileid)
	    + sizeof(u_int32_t) + (key == NULL ? 0 : key->size)
	    + sizeof(u_int32_t) + (data == NULL ? 0 : data->size)
	    + sizeof(arg_flags);
	if ((ret = __os_malloc(logrec.size, NULL, &logrec.data)) != 0)
		return (ret);

	bp = logrec.data;
	memcpy(bp, &rectype, sizeof(rectype));
	bp += sizeof(rectype);
	memcpy(bp, &txn_num, sizeof(txn_num));
	bp += sizeof(txn_num);
	memcpy(bp, lsnp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	if (op == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &op->size, sizeof(op->size));
		bp += sizeof(op->size);
		memcpy(bp, op->data, op->size);
		bp += op->size;
	}
	memcpy(bp, &fileid, sizeof(fileid));
	bp += sizeof(fileid);
	if (key == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &key->size, sizeof(key->size));
		bp += sizeof(key->size);
		memcpy(bp, key->data, key->size);
		bp += key->size;
	}
	if (data == NULL) {
		zero = 0;
		memcpy(bp, &zero, sizeof(u_int32_t));
		bp += sizeof(u_int32_t);
	} else {
		memcpy(bp, &data->size, sizeof(data->size));
		bp += sizeof(data->size);
		memcpy(bp, data->data, data->size);
		bp += data->size;
	}
	memcpy(bp, &arg_flags, sizeof(arg_flags));
	bp += sizeof(arg_flags);
#ifdef DIAGNOSTIC
	if ((u_int32_t)(bp - (u_int8_t *)logrec.data) != logrec.size)
		fprintf(stderr, "Error in log record length");
#endif
	ret = log_put(logp, ret_lsnp, (DBT *)&logrec, flags);
	if (txnid != NULL)
		txnid->last_lsn = *ret_lsnp;
	__os_free(logrec.data, 0);
	return (ret);
}

/*
 * PUBLIC: int __db_debug_print
 * PUBLIC:    __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
 */
int
__db_debug_print(notused1, dbtp, lsnp, notused2, notused3)
	DB_LOG *notused1;
	DBT *dbtp;
	DB_LSN *lsnp;
	int notused2;
	void *notused3;
{
	__db_debug_args *argp;
	u_int32_t i;
	u_int ch;
	int ret;

	i = 0;
	ch = 0;
	notused1 = NULL;
	notused2 = 0;
	notused3 = NULL;

	if ((ret = __db_debug_read(dbtp->data, &argp)) != 0)
		return (ret);
	printf("[%lu][%lu]db_debug: rec: %lu txnid %lx prevlsn [%lu][%lu]\n",
	    (u_long)lsnp->file,
	    (u_long)lsnp->offset,
	    (u_long)argp->type,
	    (u_long)argp->txnid->txnid,
	    (u_long)argp->prev_lsn.file,
	    (u_long)argp->prev_lsn.offset);
	printf("\top: ");
	for (i = 0; i < argp->op.size; i++) {
		ch = ((u_int8_t *)argp->op.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\tfileid: %lu\n", (u_long)argp->fileid);
	printf("\tkey: ");
	for (i = 0; i < argp->key.size; i++) {
		ch = ((u_int8_t *)argp->key.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\tdata: ");
	for (i = 0; i < argp->data.size; i++) {
		ch = ((u_int8_t *)argp->data.data)[i];
		if (isprint(ch) || ch == 0xa)
			putchar(ch);
		else
			printf("%#x ", ch);
	}
	printf("\n");
	printf("\targ_flags: %lu\n", (u_long)argp->arg_flags);
	printf("\n");
	__os_free(argp, 0);
	return (0);
}

/*
 * PUBLIC: int __db_debug_read __P((void *, __db_debug_args **));
 */
int
__db_debug_read(recbuf, argpp)
	void *recbuf;
	__db_debug_args **argpp;
{
	__db_debug_args *argp;
	u_int8_t *bp;
	int ret;

	ret = __os_malloc(sizeof(__db_debug_args) +
	    sizeof(DB_TXN), NULL, &argp);
	if (ret != 0)
		return (ret);
	argp->txnid = (DB_TXN *)&argp[1];
	bp = recbuf;
	memcpy(&argp->type, bp, sizeof(argp->type));
	bp += sizeof(argp->type);
	memcpy(&argp->txnid->txnid,  bp, sizeof(argp->txnid->txnid));
	bp += sizeof(argp->txnid->txnid);
	memcpy(&argp->prev_lsn, bp, sizeof(DB_LSN));
	bp += sizeof(DB_LSN);
	memcpy(&argp->op.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->op.data = bp;
	bp += argp->op.size;
	memcpy(&argp->fileid, bp, sizeof(argp->fileid));
	bp += sizeof(argp->fileid);
	memcpy(&argp->key.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->key.data = bp;
	bp += argp->key.size;
	memcpy(&argp->data.size, bp, sizeof(u_int32_t));
	bp += sizeof(u_int32_t);
	argp->data.data = bp;
	bp += argp->data.size;
	memcpy(&argp->arg_flags, bp, sizeof(argp->arg_flags));
	bp += sizeof(argp->arg_flags);
	*argpp = argp;
	return (0);
}

/*
 * PUBLIC: int __db_init_print __P((DB_ENV *));
 */
int
__db_init_print(dbenv)
	DB_ENV *dbenv;
{
	int ret;

	if ((ret = __db_add_recovery(dbenv,
	    __db_addrem_print, DB_db_addrem)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_split_print, DB_db_split)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_big_print, DB_db_big)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_ovref_print, DB_db_ovref)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_relink_print, DB_db_relink)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_addpage_print, DB_db_addpage)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_debug_print, DB_db_debug)) != 0)
		return (ret);
	return (0);
}

/*
 * PUBLIC: int __db_init_recover __P((DB_ENV *));
 */
int
__db_init_recover(dbenv)
	DB_ENV *dbenv;
{
	int ret;

	if ((ret = __db_add_recovery(dbenv,
	    __db_addrem_recover, DB_db_addrem)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_split_recover, DB_db_split)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_big_recover, DB_db_big)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_ovref_recover, DB_db_ovref)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_relink_recover, DB_db_relink)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_addpage_recover, DB_db_addpage)) != 0)
		return (ret);
	if ((ret = __db_add_recovery(dbenv,
	    __db_debug_recover, DB_db_debug)) != 0)
		return (ret);
	return (0);
}

