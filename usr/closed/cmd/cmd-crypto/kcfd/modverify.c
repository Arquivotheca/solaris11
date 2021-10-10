/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * kcfd provides the ELF signature verification service for a zone.
 * Each zone has their own kcfd process.
 *
 * Some of the code for thread creation is similar to the nfsd
 * thread creation library code.
 */

#include <errno.h>
#include <thread.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <door.h>
#include <ucred.h>
#include <priv_utils.h>

#include <md5.h>
#include <libelfsign.h>
#include <sys/crypto/elfsign.h>
#include <cryptoutil.h>
#include <sys/crypto/ioctladmin.h>

#include "kcfd.h"

/*
 * Obscuration definitions for modverify internal interfaces
 */
#define	ERROR_MSG_RESET			_C01A7100
#define	MAX_ERROR_MSGS			_C01A7102
#define	can_complain			_C01A7103
#define	cnt_error_msgs			_C01A7104
#define	error_msg_lock			_C01A7105
#define	MAX_DOOR_SERVER_THREADS		_C01A7201
#define	kcfd_mk_door_thr_pool		_C01A7313
#define	create_door_thr			_C01A7314
#define	cnt_servers			_C01A7315
#define	create_cnt_lock			_C01A7316
#define	pass_door_to_kernel		_C01A7317
#define	kcfd_process_request		_C01A7318
#define	kcfd_process_request_impl	_C01A7319
#define	did				_C01A7FD2
#define	kcfd_certCA_callback		_C01A7CB2
#define	kcfd_certver_callback		_C01A7CB3
#define	kcfd_sigver_callback		_C01A7CB1


static int did = -1;
static mutex_t create_cnt_lock = DEFAULTMUTEX;
static mutex_t error_msg_lock = DEFAULTMUTEX;
static uint_t cnt_servers = 0;
static uint_t cnt_error_msgs = 0;
static const uint_t MAX_DOOR_SERVER_THREADS = 80;
static const uint_t MAX_ERROR_MSGS = 32;
static time_t ERROR_MSG_RESET = 60*60*24;


static enum ret_e pass_door_to_kernel(int cafd, int did);
static void kcfd_mk_door_thr_pool(door_info_t *dip);
static void kcfd_process_request(void *cookie, char *argp, size_t arg_size,
    door_desc_t *dp, uint_t n_desc);
static boolean_t can_complain();

int
kcfd_modverify(int cafd, int kernel_server)
{
	int dfd = -1, ret = KCFD_EXIT_OKAY;

	errno = 0;
	if ((dfd = open(_PATH_KCFD_DOOR,
	    O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_NOLINKS | O_RDWR,
	    S_IRUSR | S_IRGRP | S_IROTH)) == -1) {
		cryptoerror(LOG_ERR, "open %s failed: %s", _PATH_KCFD_DOOR,
		    strerror(errno));
		return (KCFD_EXIT_SERVICE_CREATE);
	}
	if (fchown(dfd, DAEMON_UID, DAEMON_GID) == -1) {
		cryptoerror(LOG_ERR, "fchown %s failed: %s", _PATH_KCFD_DOOR,
		    strerror(errno));
		return (KCFD_EXIT_SERVICE_CREATE);
	}

	(void) door_server_create(kcfd_mk_door_thr_pool);

	if ((did = door_create(kcfd_process_request, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) == -1) {
		cryptoerror(LOG_ERR, "door_create failed: %s, Exiting.",
		    strerror(errno));
		return (KCFD_EXIT_SERVICE_CREATE);
	}

	if (fattach(did, _PATH_KCFD_DOOR) == -1) {
		/* Try cleanup from dead kcfd (eg pkill -9 kcfd) */
		struct door_info dinfo;
		int info_ret;
		cryptodebug("stale " _PATH_KCFD_DOOR " or existing daemon");

		errno = 0;
		info_ret = door_info(dfd, &dinfo);
		if (info_ret == -1 || dinfo.di_target == -1) {
			(void) fdetach(_PATH_KCFD_DOOR);
			if (fattach(did, _PATH_KCFD_DOOR) == -1) {
				cryptoerror(LOG_ERR, "fattach on %s failed: %s",
				    _PATH_KCFD_DOOR, strerror(errno));
				(void) close(dfd);
				return (KCFD_EXIT_SERVICE_CREATE);
			}
		} else {
			cryptoerror(LOG_ERR, "already running as pid %d",
			    dinfo.di_target);
			return (KCFD_EXIT_ALREADY_RUNNING);
		}
	}
	(void) close(dfd);

	/*
	 * In the global zone we pass the door (did) to the kernel
	 */
	if (kernel_server) {
		enum ret_e pdtk;

		pdtk = pass_door_to_kernel(cafd, did);
		if (pdtk != KCFD_EXIT_OKAY)
			return (pdtk);
	}

	return (ret);
}

int
kcfd_modverify_exit(void)
{
	int	ret = KCFD_EXIT_OKAY;

	/*
	 * Shutdown the Crypto signature validation:
	 * 1. Shutdown the doorserver
	 * 2. Remove the filesystem presence.
	 */
	if (door_revoke(did) == -1) {
		cryptoerror(LOG_ERR, "failed to door_revoke(%d) %s, Exiting.",
		    did, strerror(errno));
		ret = KCFD_EXIT_SERVICE_DESTROY;
	}

	if (fdetach(_PATH_KCFD_DOOR) == -1) {
		cryptoerror(LOG_ERR, "failed to fdetach %s: %s, Exiting.",
		    _PATH_KCFD_DOOR, strerror(errno));
		ret = KCFD_EXIT_SERVICE_DESTROY;
	}

	(void) close(did);

	return (ret);
}

typedef struct {
	int	validity;
	size_t	sig_len;
	ELFCert_t	fullvercert;
	struct filesignatures	*fssp;
} kfcd_sigverctx_t;

#define	KSV_SIGN	0x10		/* signature seen */
#define	KSV_CERT	0x20		/* certificate seen */
#define	KSV_MTCH	0x40		/* certificate match */

#define	KSV_ENABLED	(KSV_SIGN | KSV_CERT | KSV_MTCH)



static ELFCert_t *knownCAs;
static int	knownCA_cnt;

/*
 * verify a CA as valid and record its location
 */
/*ARGSUSED*/
static void
kcfd_certCA_callback(void *ctx, ELFCert_t cert, char *path)
{
	MD5_CTX md5ctx;
	uchar_t hash[MD5_DIGEST_LENGTH];
	char hashstr[(MD5_DIGEST_LENGTH * 2) + 1];
	const char *CACERTFP[] = {
	    "2646d63d62617aeae629d85cbd5daefc",	/* CACERT */
	    "4ede9ecb4868c0d2683b602f71596085", /* OBJCACERT, full */
	    "0dbd0ef087dfa578380c98069f1dbfb9", /* OBJCACERT, clean */
	    "db1301e85405febb870f3ca42ef66bf5", /* SECACERT */
	    NULL }, **cacertfpp;
	int	certfd = -1;
	uchar_t	*certbuf = NULL;
	struct stat	sbuf;

	if (cert == NULL)
		return;
	if ((certfd = open(path, O_RDONLY)) == -1) {
		cryptoerror(LOG_ERR, "unable to open certificate file %s: %s",
		    path, strerror(errno));
		return;
	}
	if ((fstat(certfd, &sbuf) == -1) || (!S_ISREG(sbuf.st_mode))) {
		cryptoerror(LOG_ERR, "certificate %s not a file",
		    path, strerror(errno));
		goto cleanup;
	}
	certbuf = malloc(sbuf.st_size);
	if (certbuf == NULL ||
	    read(certfd, certbuf, sbuf.st_size) != sbuf.st_size) {
		goto cleanup;
	}

	/*
	 * check this is the Real CA Cert
	 */
	MD5Init(&md5ctx);
	MD5Update(&md5ctx, certbuf, sbuf.st_size);
	MD5Final(hash, &md5ctx);

	tohexstr(hash, sizeof (hash), hashstr, sizeof (hashstr));

	for (cacertfpp = CACERTFP; *cacertfpp != NULL; cacertfpp++)
		if (strcmp(*cacertfpp, hashstr) != 0)
			break;
	if (*cacertfpp == NULL)
		goto cleanup;
	/* depends on elfcertlib's serialization of certCA loading */
	knownCA_cnt++;
	knownCAs = realloc(knownCAs, knownCA_cnt * sizeof (ELFCert_t));
	if (knownCAs != NULL)
		knownCAs[knownCA_cnt - 1] = cert;
	else
		knownCA_cnt = 0;
cleanup:
	free(certbuf);
	(void) close(certfd);
	cryptodebug("kcfd_certCA_callback: cert=%x, path=%s, cnt=%d",
	    cert, path, knownCA_cnt);
}

static void
kcfd_certver_callback(void *ctx, ELFCert_t cert, ELFCert_t CA)
{
	kfcd_sigverctx_t *kscp = ctx;
	char	*cDN;
	ELFCert_t	*cpp;
	int	i;

	if ((cDN = elfcertlib_getdn(cert)) == NULL) {
		cDN = "<missing>";
		goto done;
	}

	cpp = &kscp->fullvercert;

	for (i = 0; i < knownCA_cnt; i++) {
		if (knownCAs[i] == CA) {
			/*
			 * if the signature was already verified
			 * check if this cert was used to verify
			 */
			if ((kscp->validity & KSV_SIGN) &&
			    *cpp == cert)
				kscp->validity |= KSV_MTCH;

			kscp->validity |= KSV_CERT;
			*cpp = cert;
			break;
		}
	}
done:
	cryptodebug("kcfd_certver_callback: cert=%x, DN=%s, CA=%x, "
	    "fullvercert=%x, valid=%02x",
	    cert, cDN, CA, kscp->fullvercert, kscp->validity);
}

static void
kcfd_sigver_callback(void *ctx, void *sig, size_t sig_len, ELFCert_t cert)
{
	kfcd_sigverctx_t *kscp = ctx;
	struct filesignatures *fssp = sig;
	char	*cDN;
	ELFCert_t	*cpp;

	if ((cDN = elfcertlib_getdn(cert)) == NULL) {
		cDN = "<missing>";
		goto done;
	}

	cpp = &kscp->fullvercert;

	kscp->validity |= KSV_SIGN;
	*cpp = cert;
	/*
	 * if the cert was already validated
	 * check if it was used to verify
	 */
	if ((kscp->validity & KSV_CERT) && *cpp == cert)
		kscp->validity |= KSV_MTCH;

	/*
	 * Use filesig_cnt as a magic number for a filesignatures structure
	 *	In an activation, the file signature length is at the
	 *	same offset, typically with a value of 128 or more.
	 */
	if (fssp->filesig_cnt == 1) {
		if ((kscp->fssp = malloc(sig_len)) != NULL)
			(void) memcpy(kscp->fssp, fssp, sig_len);
		kscp->sig_len = sig_len;
	}
done:
	cryptodebug("kcfd_sigver_callback: cert=%x, DN=%s, sig=%x, sig_len=%d, "
	    "fullvercert=%x, valid=%02x",
	    cert, cDN, sig, sig_len, kscp->fullvercert, kscp->validity);
}


/*
 * This performs the signature verification
 */
static ELFsign_status_t
kcfd_process_request_impl(char *argp, size_t arg_size, kcf_door_arg_t **rkda,
	enum ES_ACTION action)
{
	ELFsign_t ess = NULL;
	char elfobjpath[MAXPATHLEN];
	kfcd_sigverctx_t	ksc;
	kcf_door_arg_t *kda;
	ELFsign_status_t res = ELFSIGN_UNKNOWN;
	uchar_t hash[SIG_MAX_LENGTH];
	size_t  hash_len = sizeof (hash);


	/*LINTED E_BAD_PTR_CAST_ALIGN*/
	kda = (kcf_door_arg_t *)argp;

	/* We don't know that the incoming data is actually NUL terminated. */
	(void) strlcpy(elfobjpath, kda->da_u.filename, sizeof (elfobjpath));

	if ((res = elfsign_begin(elfobjpath, action, &ess)) !=
	    ELFSIGN_SUCCESS) {
		if (can_complain())
			cryptoerror(LOG_ERR, "unable to attempt verification "
			    "of %s failed: %s.", elfobjpath,
			    elfsign_strerror(res));
		goto bail;
	}

	(void) memset(&ksc, 0, sizeof (ksc));
	elfsign_setcallbackctx(ess, &ksc);
	elfcertlib_setcertCAcallback(ess, kcfd_certCA_callback);
	elfcertlib_setcertvercallback(ess, kcfd_certver_callback);
	elfsign_setsigvercallback(ess, kcfd_sigver_callback);

	if (elfsign_hash_mem_resident(ess, hash, &hash_len) !=
	    ELFSIGN_SUCCESS) {
		res = ELFSIGN_FAILED;
		if (can_complain())
			cryptoerror(LOG_ERR,
			    "elfsign_hash_mem_resident failed to hash"
			    " for %s: %s",
			    elfobjpath, elfsign_strerror(res));
		goto bail;
	}

	res = elfsign_verify_signature(ess, NULL);
	cryptodebug("kcfd_process_request_impl() = %d, valid = 0x%x\n", res,
	    ksc.validity);

	/*
	 * check consistency between a positive verification result
	 *	and the cross checks made by the callbacks
	 */
	if (res == ELFSIGN_SUCCESS &&
	    (ksc.validity != KSV_ENABLED)) {
		/* found an inconsistency between result and callbacks! */
		res = ELFSIGN_FAILED;
		if (can_complain())
			cryptoerror(LOG_ERR,
			    "failed to verify certificate"
			    " for %s: %s"" res = %d, valid = 0x%x\n",
			    elfobjpath, elfsign_strerror(res),
			    res, ksc.validity);
		goto bail;
	} else if (res == ELFSIGN_SUCCESS && ksc.fssp == NULL) {
		/* we should have a copy of the signature block */
		res = ELFSIGN_NOTSIGNED;
		if (can_complain())
			cryptoerror(LOG_ERR,
			    "elfsign_section failed to retrieve signature"
			    " for %s: %s",
			    elfobjpath, elfsign_strerror(res));
		goto bail;
	}
bail:
	elfsign_end(ess);
	/*LINTED E_BAD_PTR_CAST_ALIGN*/
	*rkda = (kcf_door_arg_t *)argp;
	if ((res == ELFSIGN_SUCCESS) &&
	    ((sizeof (**rkda) + ksc.sig_len + hash_len) <= arg_size)) {
		(void) memcpy((*rkda)->da_u.result.signature, ksc.fssp,
		    ksc.sig_len);
		(void) memcpy((*rkda)->da_u.result.signature + ksc.sig_len,
		    hash, hash_len);
		(*rkda)->da_u.result.siglen = ksc.sig_len + hash_len;
	} else
		(*rkda)->da_u.result.siglen = 0;

	free(ksc.fssp);

	return (res);
}

/*
 * Function that handles the door call from a kernel or userland consumer.
 * This verifies the incoming data before processing for verification.
 */
/* ARGSUSED */
static void
kcfd_process_request(void *cookie, char *argp, size_t arg_size,
    door_desc_t *dp, uint_t n_desc)
{
	kcf_door_arg_t *kda;
	kcf_door_arg_t *rkda;
	ucred_t *dcu = NULL;
	pid_t req_pid;
	ELFsign_status_t res = ELFSIGN_UNKNOWN;

	if (door_ucred(&dcu) == -1) {
		if (can_complain())
			cryptoerror(LOG_ERR, "Failed to retrieve client creds");
		goto end;
	}
	req_pid = ucred_getpid(dcu);
	ucred_free(dcu);

	if (arg_size < sizeof (kcf_door_arg_t)) {
		if (can_complain())
			cryptoerror(LOG_ERR,
			    "Request from %d with invalid size (%d): failed",
			    req_pid, arg_size);
		goto end;
	}

	/*LINTED E_BAD_PTR_CAST_ALIGN*/
	kda = (kcf_door_arg_t *)argp;
	/* Verify version value is supported */
	if (kda->da_version != KCF_KCFD_VERSION1) {
		if (can_complain())
			cryptoerror(LOG_ERR,
			    "Request for unknown protocol version %d",
			    kda->da_version);

		/*LINTED E_BAD_PTR_CAST_ALIGN*/
		rkda = (kcf_door_arg_t *)argp;
		rkda->da_version = KCF_KCFD_VERSION1;
		goto bail;
	}

	/*
	 * If this is a kernel request we need to setup the kernel thread pool.
	 * If this is a user land request then don't bother.
	 */
	if (kda->da_iskernel) {
		kcf_svcinit();
	}

	cryptodebug("verify request from %d", req_pid);
	res = kcfd_process_request_impl(argp, arg_size, &rkda, ES_GET_CRYPTO);

bail:
	rkda->da_u.result.status = res;
	cryptodebug("returning with status = %d to pid %d", res, req_pid);
	if (door_return((char *)rkda, arg_size, NULL, 0) == -1) {
		if (can_complain())
			cryptoerror(LOG_ERR, "door_return error: %s",
			    strerror(errno));
	}
end:
	/* make a call guaranteed to return the thread */
	(void) door_return(NULL, 0, NULL, 0);
	/* NOTREACHED */
}

static enum ret_e
pass_door_to_kernel(int cafd, int did)
{
	crypto_load_door_t load_door;

	load_door.ld_did = did;
	if (ioctl(cafd, CRYPTO_LOAD_DOOR, &load_door) == -1 ||
	    (load_door.ld_return_value != CRYPTO_SUCCESS)) {
		cryptoerror(LOG_ERR, "Can't load door ID: %s, Exiting.",
		    strerror(errno));
		return (KCFD_EXIT_SERVICE_CREATE);
	}

	return (KCFD_EXIT_OKAY);
}

/*ARGSUSED*/
static void *
create_door_thr(void *arg)
{
	(void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	(void) door_return(NULL, 0, NULL, 0);
	return (NULL);
}

/*ARGSUSED*/
static void
kcfd_mk_door_thr_pool(door_info_t *dip)
{
	(void) mutex_lock(&create_cnt_lock);
	/*
	 * The number of servers is currently limited to
	 * MAX_DOOR_SERVER_THREADS, while this is hardcoded it really
	 * should be enough.  Because this is just a pool of the active
	 * threads we can still service more than that number of requests
	 * but those clients that can't get a pool in the thread just block
	 * waiting for one.
	 */
	if (cnt_servers < MAX_DOOR_SERVER_THREADS &&
	    thr_create(NULL, 0, create_door_thr, NULL,
	    THR_BOUND | THR_DETACHED, NULL) == 0) {
		cnt_servers++;
		(void) mutex_unlock(&create_cnt_lock);
		return;
	}
	(void) mutex_unlock(&create_cnt_lock);
}

static boolean_t
can_complain()
{
	static time_t	started;
	time_t	now;
	boolean_t	ret;

	now = time(NULL);
	(void) mutex_lock(&error_msg_lock);
	if (cnt_error_msgs == 0)
		started = now;
	cnt_error_msgs++;
	if ((now - started) >= ERROR_MSG_RESET) {
		cnt_error_msgs = 1;
		started = now;
	}
	ret = (cnt_error_msgs < MAX_ERROR_MSGS);
	(void) mutex_unlock(&error_msg_lock);
	return (ret);
}
