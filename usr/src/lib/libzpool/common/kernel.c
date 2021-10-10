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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <sys/spa.h>
#include <sys/stat.h>
#include <sys/processor.h>
#include <sys/zfs_context.h>
#include <sys/zmod.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/api.h>
#include <sys/sha2.h>
#include <crypto/aes/aes_impl.h>

/*
 * Emulation of kernel services in userland.
 */

int aok;
uint64_t physmem;
vnode_t *rootdir = (vnode_t *)0xabcd1234;
char hw_serial[HW_HOSTID_LEN];

struct utsname utsname = {
	"userland", "libzpool", "1", "1", "na"
};

/* this only exists to have its address taken */
struct proc p0;

/*
 * =========================================================================
 * threads
 * =========================================================================
 */
/*ARGSUSED*/
kthread_t *
zk_thread_create(void (*func)(), void *arg)
{
	thread_t tid;

	VERIFY(thr_create(0, 0, (void *(*)(void *))func, arg, THR_DETACHED,
	    &tid) == 0);

	return ((void *)(uintptr_t)tid);
}

/*
 * =========================================================================
 * kstats
 * =========================================================================
 */
/*ARGSUSED*/
kstat_t *
kstat_create(char *module, int instance, char *name, char *class,
    uchar_t type, ulong_t ndata, uchar_t ks_flag)
{
	return (NULL);
}

/*ARGSUSED*/
void
kstat_install(kstat_t *ksp)
{}

/*ARGSUSED*/
void
kstat_delete(kstat_t *ksp)
{}

/*
 * =========================================================================
 * mutexes
 * =========================================================================
 */
void
zmutex_init(kmutex_t *mp)
{
	mp->m_owner = NULL;
	mp->initialized = B_TRUE;
	(void) _mutex_init(&mp->m_lock, USYNC_THREAD, NULL);
}

void
zmutex_destroy(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mp->m_owner == NULL);
	(void) _mutex_destroy(&(mp)->m_lock);
	mp->m_owner = (void *)-1UL;
	mp->initialized = B_FALSE;
}

void
mutex_enter(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mp->m_owner != (void *)-1UL);
	ASSERT(mp->m_owner != curthread);
	VERIFY(mutex_lock(&mp->m_lock) == 0);
	ASSERT(mp->m_owner == NULL);
	mp->m_owner = curthread;
}

int
mutex_tryenter(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mp->m_owner != (void *)-1UL);
	if (0 == mutex_trylock(&mp->m_lock)) {
		ASSERT(mp->m_owner == NULL);
		mp->m_owner = curthread;
		return (1);
	} else {
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	VERIFY(mutex_unlock(&mp->m_lock) == 0);
}

void *
mutex_owner(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	return (mp->m_owner);
}

/*
 * =========================================================================
 * rwlocks
 * =========================================================================
 */
/*ARGSUSED*/
void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	rwlock_init(&rwlp->rw_lock, USYNC_THREAD, NULL);
	rwlp->rw_owner = NULL;
	rwlp->rw_reentrant = (type == RW_REENTRANT);
	rwlp->initialized = B_TRUE;
}

void
rw_destroy(krwlock_t *rwlp)
{
	rwlock_destroy(&rwlp->rw_lock);
	rwlp->rw_owner = (void *)-1UL;
	rwlp->initialized = B_FALSE;
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);
	ASSERT(rwlp->rw_reentrant || !RW_LOCK_HELD(rwlp));
	ASSERT(rwlp->rw_reentrant || rwlp->rw_owner != curthread);
	VERIFY(!rwlp->rw_reentrant || rw != RW_WRITER);

	if (rw == RW_READER)
		VERIFY(rw_rdlock(&rwlp->rw_lock) == 0);
	else
		VERIFY(rw_wrlock(&rwlp->rw_lock) == 0);

	rwlp->rw_owner = curthread;
}

void
rw_exit(krwlock_t *rwlp)
{
	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	rwlp->rw_owner = NULL;
	VERIFY(rw_unlock(&rwlp->rw_lock) == 0);
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int rv;

	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	if (rw == RW_READER)
		rv = rw_tryrdlock(&rwlp->rw_lock);
	else
		rv = rw_trywrlock(&rwlp->rw_lock);

	if (rv == 0) {
		rwlp->rw_owner = curthread;
		return (1);
	}

	return (0);
}

/*ARGSUSED*/
int
rw_tryupgrade(krwlock_t *rwlp)
{
	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	return (0);
}

/*
 * =========================================================================
 * condition variables
 * =========================================================================
 */
/*ARGSUSED*/
void
cv_init(kcondvar_t *cv, char *name, int type, void *arg)
{
	VERIFY(cond_init(cv, type, NULL) == 0);
}

void
cv_destroy(kcondvar_t *cv)
{
	VERIFY(cond_destroy(cv) == 0);
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	int ret = cond_wait(cv, &mp->m_lock);
	VERIFY(ret == 0 || ret == EINTR);
	mp->m_owner = curthread;
}

clock_t
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	timestruc_t ts;
	clock_t delta;

top:
	delta = abstime - ddi_get_lbolt();
	if (delta <= 0)
		return (-1);

	ts.tv_sec = delta / hz;
	ts.tv_nsec = (delta % hz) * (NANOSEC / hz);

	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	error = cond_reltimedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == ETIME)
		return (-1);

	if (error == EINTR)
		goto top;

	ASSERT(error == 0);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	VERIFY(cond_signal(cv) == 0);
}

void
cv_broadcast(kcondvar_t *cv)
{
	VERIFY(cond_broadcast(cv) == 0);
}

/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */
/*
 * Note: for the xxxat() versions of these functions, we assume that the
 * starting vp is always rootdir (which is true for spa_directory.c, the only
 * ZFS consumer of these interfaces).  We assert this is true, and then emulate
 * them by adding '/' in front of the path.
 */

/*ARGSUSED*/
int
vn_open(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2, int x3)
{
	int fd;
	vnode_t *vp;
	int old_umask;
	char realpath[MAXPATHLEN];
	struct stat64 st;

	/*
	 * If we're accessing a real disk from userland, we need to use
	 * the character interface to avoid caching.  This is particularly
	 * important if we're trying to look at a real in-kernel storage
	 * pool from userland, e.g. via zdb, because otherwise we won't
	 * see the changes occurring under the segmap cache.
	 * On the other hand, the stupid character device returns zero
	 * for its size.  So -- gag -- we open the block device to get
	 * its size, and remember it for subsequent VOP_GETATTR().
	 */
	if (strncmp(path, "/dev/", 5) == 0) {
		char *dsk;
		fd = open64(path, O_RDONLY);
		if (fd == -1)
			return (errno);
		if (fstat64(fd, &st) == -1) {
			close(fd);
			return (errno);
		}
		close(fd);
		(void) sprintf(realpath, "%s", path);
		dsk = strstr(path, "/dsk/");
		if (dsk != NULL)
			(void) sprintf(realpath + (dsk - path) + 1, "r%s",
			    dsk + 1);
	} else {
		(void) sprintf(realpath, "%s", path);
		if (!(flags & FCREAT) && stat64(realpath, &st) == -1)
			return (errno);
	}

	if (flags & FCREAT)
		old_umask = umask(0);

	/*
	 * The construct 'flags - FREAD' conveniently maps combinations of
	 * FREAD and FWRITE to the corresponding O_RDONLY, O_WRONLY, and O_RDWR.
	 */
	fd = open64(realpath, flags - FREAD, mode);

	if (flags & FCREAT)
		(void) umask(old_umask);

	if (fd == -1)
		return (errno);

	if (fstat64(fd, &st) == -1) {
		close(fd);
		return (errno);
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	*vpp = vp = umem_zalloc(sizeof (vnode_t), UMEM_NOFAIL);

	vp->v_fd = fd;
	vp->v_size = st.st_size;
	vp->v_path = spa_strdup(path);

	return (0);
}

/*ARGSUSED*/
int
vn_openat(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2,
    int x3, vnode_t *startvp, int fd)
{
	char *realpath = umem_alloc(strlen(path) + 2, UMEM_NOFAIL);
	int ret;

	ASSERT(startvp == rootdir);
	(void) sprintf(realpath, "/%s", path);

	/* fd ignored for now, need if want to simulate nbmand support */
	ret = vn_open(realpath, x1, flags, mode, vpp, x2, x3);

	umem_free(realpath, strlen(path) + 2);

	return (ret);
}

/*ARGSUSED*/
int
vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len, offset_t offset,
	int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp)
{
	ssize_t iolen, split;

	if (uio == UIO_READ) {
		iolen = pread64(vp->v_fd, addr, len, offset);
	} else {
		/*
		 * To simulate partial disk writes, we split writes into two
		 * system calls so that the process can be killed in between.
		 */
		split = (len > 0 ? rand() % len : 0);
		iolen = pwrite64(vp->v_fd, addr, split, offset);
		iolen += pwrite64(vp->v_fd, (char *)addr + split,
		    len - split, offset + split);
	}

	if (iolen == -1)
		return (errno);
	if (residp)
		*residp = len - iolen;
	else if (iolen != len)
		return (EIO);
	return (0);
}

void
vn_close(vnode_t *vp)
{
	close(vp->v_fd);
	spa_strfree(vp->v_path);
	umem_free(vp, sizeof (vnode_t));
}

/*
 * At a minimum we need to update the size since vdev_reopen()
 * will no longer call vn_openat().
 */
int
fop_getattr(vnode_t *vp, vattr_t *vap)
{
	struct stat64 st;

	if (fstat64(vp->v_fd, &st) == -1) {
		close(vp->v_fd);
		return (errno);
	}

	vap->va_size = st.st_size;
	return (0);
}

#ifdef ZFS_DEBUG

/*
 * =========================================================================
 * Figure out which debugging statements to print
 * =========================================================================
 */

static char *dprintf_string;
static int dprintf_print_all;

int
dprintf_find_string(const char *string)
{
	char *tmp_str = dprintf_string;
	int len = strlen(string);

	/*
	 * Find out if this is a string we want to print.
	 * String format: file1.c,function_name1,file2.c,file3.c
	 */

	while (tmp_str != NULL) {
		if (strncmp(tmp_str, string, len) == 0 &&
		    (tmp_str[len] == ',' || tmp_str[len] == '\0'))
			return (1);
		tmp_str = strchr(tmp_str, ',');
		if (tmp_str != NULL)
			tmp_str++; /* Get rid of , */
	}
	return (0);
}

void
dprintf_setup(int *argc, char **argv)
{
	int i, j;

	/*
	 * Debugging can be specified two ways: by setting the
	 * environment variable ZFS_DEBUG, or by including a
	 * "debug=..."  argument on the command line.  The command
	 * line setting overrides the environment variable.
	 */

	for (i = 1; i < *argc; i++) {
		int len = strlen("debug=");
		/* First look for a command line argument */
		if (strncmp("debug=", argv[i], len) == 0) {
			dprintf_string = argv[i] + len;
			/* Remove from args */
			for (j = i; j < *argc; j++)
				argv[j] = argv[j+1];
			argv[j] = NULL;
			(*argc)--;
		}
	}

	if (dprintf_string == NULL) {
		/* Look for ZFS_DEBUG environment variable */
		dprintf_string = getenv("ZFS_DEBUG");
	}

	/*
	 * Are we just turning on all debugging?
	 */
	if (dprintf_find_string("on"))
		dprintf_print_all = 1;
}

/*
 * =========================================================================
 * debug printfs
 * =========================================================================
 */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	if (dprintf_print_all ||
	    dprintf_find_string(newfile) ||
	    dprintf_find_string(func)) {
		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%d ", getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%u ", thr_self());
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("%s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	}
}

#endif /* ZFS_DEBUG */

/*
 * =========================================================================
 * cmn_err() and panic()
 * =========================================================================
 */
static char ce_prefix[CE_IGNORE][10] = { "", "NOTICE: ", "WARNING: ", "" };
static char ce_suffix[CE_IGNORE][2] = { "", "\n", "\n", "" };

void
vpanic(const char *fmt, va_list adx)
{
	(void) fprintf(stderr, "error: ");
	(void) vfprintf(stderr, fmt, adx);
	(void) fprintf(stderr, "\n");

	abort();	/* think of it as a "user-level crash dump" */
}

void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vpanic(fmt, adx);
	va_end(adx);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	if (ce == CE_PANIC)
		vpanic(fmt, adx);
	if (ce != CE_NOTE) {	/* suppress noise in userland stress testing */
		(void) fprintf(stderr, "%s", ce_prefix[ce]);
		(void) vfprintf(stderr, fmt, adx);
		(void) fprintf(stderr, "%s", ce_suffix[ce]);
	}
}

/*PRINTFLIKE2*/
void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}

/*
 * =========================================================================
 * kobj interfaces
 * =========================================================================
 */
struct _buf *
kobj_open_file(char *name)
{
	struct _buf *file;
	vnode_t *vp;

	/* set vp as the _fd field of the file */
	if (vn_openat(name, UIO_SYSSPACE, FREAD, 0, &vp, 0, 0, rootdir,
	    -1) != 0)
		return ((void *)-1UL);

	file = umem_zalloc(sizeof (struct _buf), UMEM_NOFAIL);
	file->_fd = (intptr_t)vp;
	return (file);
}

int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	ssize_t resid;

	vn_rdwr(UIO_READ, (vnode_t *)file->_fd, buf, size, (offset_t)off,
	    UIO_SYSSPACE, 0, 0, 0, &resid);

	return (size - resid);
}

void
kobj_close_file(struct _buf *file)
{
	vn_close((vnode_t *)file->_fd);
	umem_free(file, sizeof (struct _buf));
}

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
	struct stat64 st;
	vnode_t *vp = (vnode_t *)file->_fd;

	if (fstat64(vp->v_fd, &st) == -1) {
		vn_close(vp);
		return (errno);
	}
	*size = st.st_size;
	return (0);
}

/*
 * =========================================================================
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
	poll(0, 0, ticks * (1000 / hz));
}

/*
 * Find highest one bit set.
 *	Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * High order bit is 31 (or 63 in _LP64 kernel).
 */
int
highbit(ulong_t i)
{
	register int h = 1;

	if (i == 0)
		return (0);
#ifdef _LP64
	if (i & 0xffffffff00000000ul) {
		h += 32; i >>= 32;
	}
#endif
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

/*
 * Dummy stubs for the Cryptographic Framework APIs that are used
 * for encryption in the ZIO layer.
 *
 * NOTE that these do not do encryption.  In the future they could
 * be recoded to use PKCS#11 or call into libsoftcrypto.
 *
 * They exist really just to make libzpool compile and to allow ztest
 * to run as much of the crypto transforms as possible, without actually
 * encrypting the data.
 */

/*ARGSUSED*/
crypto_mech_type_t
crypto_mech2id(crypto_mech_name_t name)
{
	/*
	 * Since these are dummy functions it doesn't
	 * really mater what the values are so make
	 * them match PKCS#11 #defines.
	 */
	if (strcmp(name, SUN_CKM_AES_CBC) == 0) {
		return (0x00001080);
	}
	if (strcmp(name, SUN_CKM_AES_CTR) == 0) {
		return (0x00001086);
	}
	if (strcmp(name, SUN_CKM_AES_GCM) == 0) {
		return (0x00001087);
	}
	if (strcmp(name, SUN_CKM_AES_CCM) == 0) {
		return (0x00001088);
	}
	return (CRYPTO_MECH_INVALID);
}

int
crypto_create_ctx_template(crypto_mechanism_t *mech,
    crypto_key_t *key, crypto_ctx_template_t *tmpl, int kmflag)
{
	return (NULL);
}

/*ARGSUSED*/
void
crypto_destroy_ctx_template(crypto_ctx_template_t tmpl)
{
}


/*ARGSUSED*/
int
crypto_encrypt(crypto_mechanism_t *mech, crypto_data_t *plaintext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *ciphertext,
    crypto_call_req_t *cr)
{
	if (plaintext->cd_format == CRYPTO_DATA_RAW &&
	    ciphertext->cd_format == CRYPTO_DATA_RAW) {
		bcopy(plaintext->cd_raw.iov_base, ciphertext->cd_raw.iov_base,
		    plaintext->cd_length);
	} else if (plaintext->cd_format == CRYPTO_DATA_RAW &&
	    ciphertext->cd_format == CRYPTO_DATA_UIO) {
		bcopy(plaintext->cd_raw.iov_base,
		    ciphertext->cd_uio->uio_iov[0].iov_base,
		    plaintext->cd_length);
	} else if (plaintext->cd_format == CRYPTO_DATA_UIO &&
	    ciphertext->cd_format == CRYPTO_DATA_UIO) {
		for (int i = 0; i < plaintext->cd_uio->uio_iovcnt; i++) {
			bcopy(plaintext->cd_uio->uio_iov[i].iov_base,
			    ciphertext->cd_uio->uio_iov[i].iov_base,
			    plaintext->cd_uio->uio_iov[i].iov_len);
		}
	} else {
		ASSERT(0);
	}
	return (0);
}

/*ARGSUSED*/
int
crypto_decrypt(crypto_mechanism_t *mech, crypto_data_t *ciphertext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *plaintext,
    crypto_call_req_t *cr)
{
	if (plaintext == NULL & ciphertext != NULL) {
		/* in place decrypt */
		return (0);
	}
	if (ciphertext->cd_format == CRYPTO_DATA_RAW &&
	    ciphertext->cd_format == CRYPTO_DATA_RAW) {
		bcopy(ciphertext->cd_raw.iov_base, plaintext->cd_raw.iov_base,
		    plaintext->cd_length);
	} else if (plaintext->cd_format == CRYPTO_DATA_RAW &&
	    ciphertext->cd_format == CRYPTO_DATA_UIO) {
		bcopy(ciphertext->cd_uio->uio_iov[0].iov_base,
		    plaintext->cd_raw.iov_base,
		    plaintext->cd_length);
	} else if (plaintext->cd_format == CRYPTO_DATA_UIO &&
	    ciphertext->cd_format == CRYPTO_DATA_UIO) {
		for (int i = 0; i < plaintext->cd_uio->uio_iovcnt; i++) {
			bcopy(ciphertext->cd_uio->uio_iov[i].iov_base,
			    plaintext->cd_uio->uio_iov[i].iov_base,
			    plaintext->cd_uio->uio_iov[i].iov_len);
		}
	} else {
		ASSERT(0);
	}
	return (0);
}

/*
 * This one should be trivial to turn into HMAC-SHA256 which is all
 * we actually need, but for now just run a simple SHA256 instead.
 * Ideally libmd would have had the HMAC variants.
 */
/*ARGSUSED*/
int
crypto_mac(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t impl,
    crypto_data_t *mac, crypto_call_req_t *cr)
{
	char buf[SHA256_DIGEST_LENGTH];
	SHA2_CTX ctx;

	SHA2Init(SHA256, &ctx);
	SHA2Update(&ctx, data->cd_raw.iov_base, data->cd_raw.iov_len);
	SHA2Final(&buf, &ctx);
	bcopy(buf, mac->cd_raw.iov_base, mac->cd_raw.iov_len);

	return (0);
}

static int random_fd = -1, urandom_fd = -1;

static int
random_get_bytes_common(uint8_t *ptr, size_t len, int fd)
{
	size_t resid = len;
	ssize_t bytes;

	ASSERT(fd != -1);

	while (resid != 0) {
		bytes = read(fd, ptr, resid);
		ASSERT3S(bytes, >=, 0);
		ptr += bytes;
		resid -= bytes;
	}

	return (0);
}

int
random_get_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, random_fd));
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, urandom_fd));
}

int
ddi_strtoul(const char *hw_serial, char **nptr, int base, unsigned long *result)
{
	char *end;

	*result = strtoul(hw_serial, &end, base);
	if (*result == 0)
		return (errno);
	return (0);
}

int
ddi_strtoull(const char *str, char **nptr, int base, u_longlong_t *result)
{
	char *end;

	*result = strtoull(str, &end, base);
	if (*result == 0)
		return (errno);
	return (0);
}

/*
 * =========================================================================
 * kernel emulation setup & teardown
 * =========================================================================
 */
static int
umem_out_of_memory(void)
{
	char errmsg[] = "out of memory -- generating core dump\n";

	write(fileno(stderr), errmsg, sizeof (errmsg));
	abort();
	return (0);
}

void
kernel_init(int mode)
{
	umem_nofail_callback(umem_out_of_memory);

	physmem = sysconf(_SC_PHYS_PAGES);

	dprintf("physmem = %llu pages (%.2f GB)\n", physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	(void) snprintf(hw_serial, sizeof (hw_serial), "%ld",
	    (mode & FWRITE) ? gethostid() : 0);

	VERIFY((random_fd = open("/dev/random", O_RDONLY)) != -1);
	VERIFY((urandom_fd = open("/dev/urandom", O_RDONLY)) != -1);

	system_taskq_init();
	uname(&utsname);

	io_mvec_init();
	io_tag_init();

	spa_init(mode);
}

void
kernel_fini(void)
{
	spa_fini();

	io_tag_fini();
	io_mvec_fini();

	system_taskq_fini();

	close(random_fd);
	close(urandom_fd);

	random_fd = -1;
	urandom_fd = -1;
}

int
z_uncompress(void *dst, size_t *dstlen, const void *src, size_t srclen)
{
	int ret;
	uLongf len = *dstlen;

	if ((ret = uncompress(dst, &len, src, srclen)) == Z_OK)
		*dstlen = (size_t)len;

	return (ret);
}

int
z_compress_level(void *dst, size_t *dstlen, const void *src, size_t srclen,
    int level)
{
	int ret;
	uLongf len = *dstlen;

	if ((ret = compress2(dst, &len, src, srclen, level)) == Z_OK)
		*dstlen = (size_t)len;

	return (ret);
}

uid_t
crgetuid(cred_t *cr)
{
	return (0);
}

gid_t
crgetgid(cred_t *cr)
{
	return (0);
}

int
crgetngroups(cred_t *cr)
{
	return (0);
}

gid_t *
crgetgroups(cred_t *cr)
{
	return (NULL);
}

int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	return (0);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	return (0);
}

ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
	ksiddomain_t *kd;

	kd = umem_zalloc(sizeof (ksiddomain_t), UMEM_NOFAIL);
	kd->kd_name = spa_strdup(dom);
	return (kd);
}

void
ksiddomain_rele(ksiddomain_t *ksid)
{
	spa_strfree(ksid->kd_name);
	umem_free(ksid, sizeof (ksiddomain_t));
}

/*
 * Do not change the length of the returned string; it must be freed
 * with strfree().
 */
char *
kmem_asprintf(const char *fmt, ...)
{
	int size;
	va_list adx;
	char *buf;

	va_start(adx, fmt);
	size = vsnprintf(NULL, 0, fmt, adx) + 1;
	va_end(adx);

	buf = kmem_alloc(size, KM_SLEEP);

	va_start(adx, fmt);
	size = vsnprintf(buf, size, fmt, adx);
	va_end(adx);

	return (buf);
}

/* ARGSUSED */
int
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	*minorp = 0;
	return (0);
}

/* ARGSUSED */
void
zfs_onexit_fd_rele(int fd)
{
}

/* ARGSUSED */
int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uint64_t *action_handle)
{
	return (0);
}

/* ARGSUSED */
int
zfs_onexit_del_cb(minor_t minor, uint64_t action_handle, boolean_t fire)
{
	return (0);
}

/* ARGSUSED */
int
zfs_onexit_cb_data(minor_t minor, uint64_t action_handle, void **data)
{
	return (0);
}
