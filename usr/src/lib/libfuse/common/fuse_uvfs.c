/*
 * Fuse: Filesystem in Userspace
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "fuse.h"
#include "fuse_opt.h"
#include <libuvfs.h>
#include "fuse_impl.h"

#include <libintl.h>
#include <strings.h>
#include <thread.h>
#include <limits.h>
#include <synch.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <assert.h>
#include <umem.h>
#include <alloca.h>
#include <stddef.h>
#include <atomic.h>
#include <sys/avl.h>
#include <sys/sysmacros.h>
#include <stdio.h>
#include <ucred.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>

#define	FUSE_UNKNOWN_INO	0xffffffff

enum fuse_fs_stash_t {
	FUSE_ROOT_FID,
	FUSE_FS_INFO,
};

enum fuse_fid_stash_t {
	FUSE_FID_FH,
	FUSE_FID_OPEN_COUNT,
	FUSE_FID_DIR_HANDLE,
	FUSE_FID_HIDDEN,
};

typedef struct {
	char *hidden_full_path;
	char *hidden_component;
	libuvfs_fid_t hidden_dirfd;
} fuse_hidden_t;

typedef struct {
	libuvfs_fs_t	*dh_fs;
	libuvfs_fid_t	dh_fid;
	char		dh_path[MAXPATHLEN];
	boolean_t	dh_filled;
	void		*dh_data;
	void		*dh_cookie;
	/* The total length of the buffer. */
	int		dh_len;
	/* The amount of valid dirent data (out of dh_len) used. */
	int		dh_bytes_used;
	/* The size of the user's buffer. */
	int		dh_response_size;
	/* The amount of dirent data that fits in the user's buffer. */
	int		dh_return_bytes;
} dir_handle_t;

static pthread_key_t fuse_uvfs_tsd_key;

static struct fuse_context *
fuse_uvfs_get_context(void)
{
	struct fuse_context *ctx = pthread_getspecific(fuse_uvfs_tsd_key);
	if (ctx == NULL) {
		ctx = umem_zalloc(sizeof (struct fuse_context), UMEM_NOFAIL);
		(void) pthread_setspecific(fuse_uvfs_tsd_key, ctx);
	}
	return (ctx);
}

static void
fuse_uvfs_setup_context(ucred_t *cr, struct fuse_fs *fuse)
{
	struct fuse_context *ctx = fuse_uvfs_get_context();

	ctx->uid = ucred_geteuid(cr);
	ctx->gid = ucred_getegid(cr);
	ctx->pid = ucred_getpid(cr);
	ctx->fuse = fuse->fuse;
	ctx->private_data = fuse->user_data;
}

struct fuse_context *
fuse_get_context(void)
{
	return (fuse_uvfs_get_context());
}

static void
fuse_uvfs_return(void *res, size_t res_size, int error)
{
	libuvfs_common_res_t *cres = res;

	cres->lcr_error = abs(error);

	libuvfs_return(res, res_size);
}

static int
fuse_uvfs_stat_convert(libuvfs_fs_t *fs, libuvfs_stat_t *stat,
    struct stat *buf, libuvfs_fid_t *my_fid, libuvfs_fid_t *known_pfid)
{
	libuvfs_fid_t *rootfid;

	stat->l_atime[0] = buf->st_atim.tv_sec;
	stat->l_atime[1] = buf->st_atim.tv_nsec;
	stat->l_mtime[0] = buf->st_mtim.tv_sec;
	stat->l_mtime[1] = buf->st_mtim.tv_nsec;
	stat->l_ctime[0] = buf->st_ctim.tv_sec;
	stat->l_ctime[1] = buf->st_ctim.tv_nsec;
	stat->l_size = buf->st_size;
	stat->l_blksize = buf->st_blksize;
	stat->l_blocks = buf->st_blocks;
	stat->l_links = buf->st_nlink;
	stat->l_uid = buf->st_uid;
	stat->l_gid = buf->st_gid;
	stat->l_mode = buf->st_mode;
	stat->l_id = buf->st_ino;
	stat->l_rdev = libuvfs_expldev(buf->st_rdev);

	/* Caller can request we not set any fid data */
	if (my_fid == NULL && known_pfid == NULL)
		return (0);

	if (known_pfid != NULL) {
		stat->l_pfid = *known_pfid;
		return (0);
	}

	if ((rootfid = libuvfs_stash_fs_get(fs, FUSE_ROOT_FID, NULL)) == NULL)
		return (ESTALE);

	/* This dir is the root? */
	if (libuvfs_fid_compare(rootfid, my_fid) == 0) {
		stat->l_pfid = *my_fid;
		return (0);
	} else if (libuvfs_name_parent(fs, my_fid, 0, &stat->l_pfid) == 0) {
		return (ESTALE);
	}
	return (0);
}

static void
fuse_uvfs_path_fixup(char *path, char *name)
{
	if (path[0] == '/' && path[1] != '\0')
		(void) strlcat(path, "/", MAXPATHLEN);
	(void) strlcat(path, name, MAXPATHLEN);
}

static struct fuse_fs *
fuse_uvfs_fs_init(libuvfs_fs_t *fs, struct fuse_fs *fuse_fs)
{
	struct fuse_fs	*info;

	info = libuvfs_stash_fs_get(fs, FUSE_FS_INFO, NULL);
	if (info == NULL) {
		struct fuse_fs *winner;

		winner = libuvfs_stash_fs_store(fs, FUSE_FS_INFO, B_FALSE,
		    fuse_fs);
		if (winner != NULL)
			info = winner;
	}
	return (info);
}

#define	FUSE_UVFS_FS(fs) libuvfs_stash_fs_get(fs, FUSE_FS_INFO, NULL)

/*ARGSUSED*/
static void
fuse_uvfs_vfsroot(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	libuvfs_cb_vfsroot_res_t res;
	libuvfs_fid_t *rootfid;
	struct stat statbuf;
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	int error = 0;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(fuse_fs != NULL);

	rootfid = libuvfs_stash_fs_get(fs, FUSE_ROOT_FID, NULL);
	assert(rootfid != NULL);

	res.root_fid = *rootfid;
	if (FUSE_OP_GETATTR(fuse_fs, "/", &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}
	(void) fuse_uvfs_stat_convert(fs, &res.root_stat, &statbuf, NULL,
	    rootfid);
out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_vget(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	libuvfs_cb_vget_arg_t *args = varg;
	libuvfs_cb_vget_res_t res;
	struct stat statbuf;
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	char path[MAXPATHLEN];
	int error = 0;
	int len;

	assert(argsize == sizeof (*args));
	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, &args->lcvg_fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0) {
		error = ENOENT;
		goto out;
	}
	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	error = fuse_uvfs_stat_convert(fs, &res.lcvg_stat, &statbuf,
	    &args->lcvg_fid, NULL);

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

/*ARGSUSED*/
static void
fuse_uvfs_statvfs(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_statvfs_res_t res;
	struct statvfs buf;
	int error = 0;

	fuse_uvfs_setup_context(cr, fuse_fs);

	if ((error = FUSE_OP_STATFS(fuse_fs, "/", &buf)) == 0) {
		res.lcsa_frsize = buf.f_frsize;
		res.lcsa_bsize = buf.f_bsize;
		res.lcsa_blocks = buf.f_blocks;
		res.lcsa_bfree = buf.f_bfree;
		res.lcsa_bavail = buf.f_bavail;
		res.lcsa_ffree = buf.f_ffree;
		res.lcsa_favail = buf.f_favail;
		res.lcsa_files = buf.f_files;
		res.lcsa_namemax = buf.f_namemax;
	}

	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_remove_hidden(libuvfs_fs_t *fs, struct fuse_fs *fuse_fs,
    libuvfs_fid_t *fid, struct stat *statbuf)
{
	fuse_hidden_t *hidden;
	int error;

	hidden = libuvfs_stash_fid_get(fs, fid,
	    FUSE_FID_HIDDEN, NULL);

	if (hidden) {
		if ((statbuf->st_mode & S_IFMT) == S_IFDIR)
			error = FUSE_OP_RMDIR(fuse_fs,
			    hidden->hidden_full_path);
		else
			error = FUSE_OP_UNLINK(fuse_fs,
			    hidden->hidden_full_path);
		if (error == 0) {
			libuvfs_name_delete(fs, &hidden->hidden_dirfd,
			    hidden->hidden_component, NULL);
		}
		umem_free(hidden->hidden_component, MAXPATHLEN);
		umem_free(hidden->hidden_full_path, MAXPATHLEN);
		umem_free(hidden, sizeof (fuse_hidden_t));
		(void) libuvfs_stash_fid_remove(fs, fid, FUSE_FID_HIDDEN);
	}
}

static int
fuse_uvfs_close_common(libuvfs_fs_t *fs, libuvfs_fid_t *fid, uint64_t count)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	uint64_t *openp;
	char path[MAXPATHLEN];
	struct stat statbuf;
	struct fuse_file_info fi = { 0 };
	uint64_t *fhp;
	int error = 0;
	int len;

	len = libuvfs_name_path(fs, fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		return (ESTALE);
	}

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf)) {
		return (ESTALE);
	}

	if (libuvfs_name_fid_wrlock(fs, fid) != 0) {
		return (ESTALE);
	}

	fhp = libuvfs_stash_fid_get(fs, fid, FUSE_FID_FH, NULL);
	if (fhp)
		fi.fh = *fhp;

	openp = libuvfs_stash_fid_get(fs, fid,
	    FUSE_FID_OPEN_COUNT, NULL);
	if (openp == NULL) {
		error = ESTALE;
		goto out;
	}

	if (count == 0) {
		dir_handle_t *dirhp = libuvfs_stash_fid_get(fs, fid,
		    FUSE_FID_DIR_HANDLE, NULL);

		fuse_uvfs_remove_hidden(fs, fuse_fs, fid, &statbuf);
		if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
			error = FUSE_OP_RELEASEDIR(fuse_fs, path, &fi);
		} else {
			error = FUSE_OP_RELEASE(fuse_fs, path, &fi);
		}
		if (dirhp) {
			if (dirhp->dh_data)
				umem_free(dirhp->dh_data, dirhp->dh_len);
			umem_free(dirhp, sizeof (dir_handle_t));
			(void) libuvfs_stash_fid_remove(fs, fid,
			    FUSE_FID_DIR_HANDLE);
		}

		(void) libuvfs_stash_fid_remove(fs, fid, FUSE_FID_OPEN_COUNT);
		(void) libuvfs_stash_fid_remove(fs, fid, FUSE_FID_FH);
		umem_free(openp, sizeof (uint64_t));
		umem_free(fhp, sizeof (uint64_t));
	} else {
		(void) libuvfs_stash_fid_store(fs, fid, FUSE_FID_OPEN_COUNT,
		    B_TRUE, openp);
	}
out:
	(void) libuvfs_name_fid_unlock(fs, fid);
	return (error);
}

static void
fuse_uvfs_close(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_close_arg_t *args = varg;
	libuvfs_fid_t *fid = &args->lccf_fid;
	libuvfs_common_res_t res;
	int error = 0;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	error = fuse_uvfs_close_common(fs, fid, args->lccf_count);

	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_lookup(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	libuvfs_cb_lookup_arg_t *args = varg;
	libuvfs_cb_lookup_res_t res;
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_fid_t *dirfid = &args->lcla_dirfid;
	struct stat buf;
	libuvfs_fid_t found;
	char path[MAXPATHLEN];
	int len;
	int error = 0;

	assert(argsize == sizeof (*args));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, dirfid, 0, NULL, path, MAXPATHLEN);
	if (len == 0) {
		error = ENOENT;
		goto out;
	}
	fuse_uvfs_path_fixup(path, args->lcla_nm);

	if (FUSE_OP_GETATTR(fuse_fs, path, &buf)) {
		error = ENOENT;
		goto out;
	}

	(void) fuse_uvfs_stat_convert(fs, &res.lclr_stat, &buf, NULL, dirfid);

	/*
	 * Now add it to name store if it isn't already known
	 */
	libuvfs_name_lookup(fs, dirfid, args->lcla_nm, &found);
	if (found.uvfid_len == 0) {
		libuvfs_fid_unique(fs, &found);
		libuvfs_name_store(fs, dirfid, args->lcla_nm, &found, B_TRUE,
		    NULL);
	}

	res.lclr_fid = found;

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_open(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_open_arg_t *args = varg;
	libuvfs_fid_t *fid = &args->lcof_fid;
	libuvfs_common_res_t res;
	struct fuse_file_info fi = { 0 };
	char path[MAXPATHLEN];
	struct stat statbuf;
	uint64_t *openp;
	int found;
	int len;
	int error = 0;
	uint64_t *fhp;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	errno = 0;
	len = libuvfs_name_path(fs, fid, 0, NULL, path, MAXPATHLEN);
	if ((len == 0 || len > MAXPATHLEN ||
	    libuvfs_name_fid_wrlock(fs, fid) != 0)) {
		error = ESTALE;
		goto errout;
	}

	fhp = libuvfs_stash_fid_get(fs, fid, FUSE_FID_FH, NULL);

	if (fhp == NULL) {
		if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf)) {
			error = ESTALE;
			goto out;
		}

		fi.flags = O_RDWR;
		if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
			error = FUSE_OP_OPENDIR(fuse_fs, path, &fi);
		else
			error = FUSE_OP_OPEN(fuse_fs, path, &fi);

		if (error)
			goto out;

		fhp = umem_zalloc(sizeof (uint64_t), UMEM_DEFAULT);
		if (fhp == NULL) {
			error = ENOMEM;
			goto out;
		}
		*fhp = fi.fh;
		(void) libuvfs_stash_fid_store(fs, fid, FUSE_FID_FH,
		    B_TRUE, fhp);
	}

	openp = libuvfs_stash_fid_get(fs, fid, FUSE_FID_OPEN_COUNT, &found);
	if (openp == NULL) {
		openp = umem_zalloc(sizeof (uint64_t), UMEM_DEFAULT);
		if (openp == NULL) {
			error = ENOMEM;
			goto out;
		}
	}
	*openp = args->lcof_open_count;
	(void) libuvfs_stash_fid_store(fs, fid, FUSE_FID_OPEN_COUNT, B_TRUE,
	    openp);
out:
	(void) libuvfs_name_fid_unlock(fs, fid);
errout:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_addmap(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_map_arg_t *args = varg;
	libuvfs_fid_t *fid = &args->lcma_fid;
	libuvfs_common_res_t res;
	uint64_t *openp;
	int found;
	int error = 0;
	uint64_t *fhp;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	if (libuvfs_name_fid_wrlock(fs, fid) != 0) {
		error = ESTALE;
		goto errout;
	}

	errno = 0;
	fhp = libuvfs_stash_fid_get(fs, fid, FUSE_FID_FH, NULL);
	if (fhp == NULL) {
		error = ESTALE;
		goto out;
	}

	openp = libuvfs_stash_fid_get(fs, fid, FUSE_FID_OPEN_COUNT, &found);
	if (openp == NULL) {
		error = ESTALE;
		goto out;
	}
	*openp = args->lcma_count;
	(void) libuvfs_stash_fid_store(fs, fid, FUSE_FID_OPEN_COUNT, B_TRUE,
	    openp);

out:
	(void) libuvfs_name_fid_unlock(fs, fid);
errout:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_delmap(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_map_arg_t *args = varg;
	libuvfs_fid_t *fid = &args->lcma_fid;
	libuvfs_common_res_t res;
	int error;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	error = fuse_uvfs_close_common(fs, fid, args->lcma_count);
	fuse_uvfs_return(&res, sizeof (res), error);
}

static int
fuse_uvfs_fill_dir(void *h, const char *name, const struct stat *statp,
    off_t off)
{
	dir_handle_t *dirhp = (dir_handle_t *)h;
	libuvfs_fs_t *fs = dirhp->dh_fs;
	uint64_t inode;
	int bytes;

	if (!statp) {
		char path[MAXPATHLEN];
		libuvfs_fid_t found;

		(void) strlcpy(path, dirhp->dh_path, MAXPATHLEN);
		if (path[0])
			(void) strlcat(path, "/", MAXPATHLEN);
		(void) strlcat(path, name, MAXPATHLEN);
		libuvfs_name_lookup(fs, &dirhp->dh_fid, name, &found);
		if (found.uvfid_len != 0)
			inode = libuvfs_fid_to_id(fs, &found);
		else
			inode = FUSE_UNKNOWN_INO;

	} else {
		inode = statp->st_ino;
	}

	bytes = libuvfs_add_direntry(dirhp->dh_data,
	    dirhp->dh_len, name, inode, off == 0 ? -1 : off, &dirhp->dh_cookie);
	if (bytes == 0 && off == 0) {
		void *old_data;
		int cookie_off;

		old_data = dirhp->dh_data;
		dirhp->dh_data = umem_zalloc(dirhp->dh_len * 2, UMEM_DEFAULT);
		if (dirhp->dh_data == NULL) {
			dirhp->dh_filled = 0;
			return (1);
		}
		bcopy(old_data, dirhp->dh_data, dirhp->dh_len);
		cookie_off = (char *)dirhp->dh_cookie - (char *)old_data;
		dirhp->dh_cookie = (char *)dirhp->dh_data + cookie_off;
		umem_free(old_data, dirhp->dh_len);
		dirhp->dh_len = dirhp->dh_len * 2;
		bytes = libuvfs_add_direntry(dirhp->dh_data,
		    dirhp->dh_len, name, inode, off == 0 ? -1 : off,
		    &dirhp->dh_cookie);
		if (bytes == 0)
			return (1);
	}
	if (dirhp->dh_return_bytes == -1 &&
	    dirhp->dh_bytes_used + bytes > dirhp->dh_response_size) {
		dirhp->dh_return_bytes = dirhp->dh_bytes_used;
	}

	dirhp->dh_bytes_used += bytes;
	assert(dirhp->dh_bytes_used <= dirhp->dh_len);

	return (bytes ? 0 : 1);
}

static void
fuse_uvfs_fill_buf(dir_handle_t *dirhp, size_t size)
{
	dirhp->dh_cookie = NULL;
	dirhp->dh_bytes_used = 0;
	dirhp->dh_filled = 0;
	dirhp->dh_response_size = size;
	dirhp->dh_return_bytes = -1;
	if (dirhp->dh_data)
		umem_free(dirhp->dh_data, dirhp->dh_len);
	dirhp->dh_data = umem_zalloc(dirhp->dh_len, UMEM_DEFAULT);
}

/*ARGSUSED*/
static void
fuse_uvfs_readdir(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_readdir_arg_t *args = varg;
	libuvfs_cb_readdir_res_t *resp;
	size_t ressize = sizeof (*resp);
	libuvfs_fid_t *fid = &args->lcrda_fid;
	dir_handle_t *dirhp;
	int len;
	int64_t off;
	uintptr_t buf;
	int error = 0;

	fuse_uvfs_setup_context(cr, fuse_fs);

	ressize += args->lcrda_length;
	resp = alloca(ressize);
	buf = (uintptr_t)resp + sizeof (*resp);

	off = resp->lcrdr_offset = args->lcrda_offset;

	if (libuvfs_name_fid_wrlock(fs, fid) != 0) {
		error = ESTALE;
		goto errout;
	}

	dirhp = libuvfs_stash_fid_get(fs, fid,
	    FUSE_FID_DIR_HANDLE, NULL);
	if (dirhp == NULL) {
		dirhp = umem_zalloc(sizeof (dir_handle_t), UMEM_DEFAULT);
		if (dirhp == NULL) {
			error = ENOMEM;
			goto out;
		}
		dirhp->dh_fs = fs;
		dirhp->dh_fid = *fid;
		dirhp->dh_len = args->lcrda_length;
		if (off) {
			dirhp->dh_data = umem_zalloc(dirhp->dh_len,
			    UMEM_DEFAULT);
			if (dirhp->dh_data == NULL) {
				umem_free(dirhp, sizeof (dir_handle_t));
				error = ENOMEM;
				goto out;
			}
		}
		dirhp->dh_response_size = args->lcrda_length;
		dirhp->dh_return_bytes = -1;
		len = libuvfs_name_path(fs, fid, 0, NULL,
		    dirhp->dh_path, MAXPATHLEN);
		if (len == 0 || len >= MAXPATHLEN) {
			umem_free(dirhp->dh_data, dirhp->dh_len);
			umem_free(dirhp, sizeof (dir_handle_t));
			error = ESTALE;
			goto out;
		}

		(void) libuvfs_stash_fid_store(fs, fid, FUSE_FID_DIR_HANDLE,
		    B_FALSE, dirhp);
	}

	if (off == 0) {
		struct fuse_file_info fi = { 0 };
		uint64_t *fhp;

		fuse_uvfs_fill_buf(dirhp, args->lcrda_length);

		if (dirhp->dh_data == NULL) {
			(void) libuvfs_stash_fid_remove(fs, fid,
			    FUSE_FID_DIR_HANDLE);
			umem_free(dirhp, sizeof (dir_handle_t));
			error = ENOMEM;
			goto out;
		}

		fhp = libuvfs_stash_fid_get(fs, fid, FUSE_FID_FH, NULL);
		if (fhp)
			fi.fh = *fhp;

		if ((error = FUSE_OP_READDIR(fuse_fs, dirhp->dh_path,
		    dirhp, fuse_uvfs_fill_dir, off, &fi)) != 0)
			goto out;

		/*
		 * No errors, set dh_filled to 1 to indicate that we have
		 * the data.
		 */
		dirhp->dh_filled = 1;
	} else {
		dirent_t *dp;
		void *start;
		int bytes = 0;

		/*
		 * There is a similar check below, but we must also do the
		 * check here so we don't bother with the for loop.
		 * If the offset requested is equal to or greater than
		 * the amount of dirent data that we have in the dirhp
		 * buffer, return eof.
		 */
		if (off >= dirhp->dh_bytes_used) {
			resp->lcrdr_length = 0;
			resp->lcrdr_eof = 1;
			goto out;
		}

		/*
		 * Determine how much dirent data will fit into the user's
		 * buffer.
		 */
		start = (char *)dirhp->dh_data + off;
		dp = (dirent_t *)start;
		for (;;) {
			if (bytes + dp->d_reclen > args->lcrda_length) {
				break;
			}

			if (dp->d_reclen == 0) {
				break;
			}

			bytes += dp->d_reclen;

			dp = (dirent_t *)(uintptr_t)((char *)dp + dp->d_reclen);
			if ((char *)dp > (char *)dirhp->dh_data +
			    dirhp->dh_bytes_used) {
				break;
			}
		}
		/* This is how much will fit into the user's buffer. */
		dirhp->dh_return_bytes = bytes;
	}

	/*
	 * Copy the data from the dirhp buffer into the user's buffer.
	 */
	if (off < dirhp->dh_bytes_used) {
		uintptr_t data = (uintptr_t)dirhp->dh_data + off;
		int copy_amount;

		if (dirhp->dh_return_bytes != -1)
			copy_amount = dirhp->dh_return_bytes;
		else
			copy_amount = dirhp->dh_bytes_used - off;

		bcopy((void *)data, (void *)buf, copy_amount);

		resp->lcrdr_offset = copy_amount + off;

		if (copy_amount + off >= dirhp->dh_bytes_used)
			resp->lcrdr_eof = 1;
		else
			resp->lcrdr_eof = 0;

		resp->lcrdr_length = copy_amount;
	} else {
		resp->lcrdr_length = 0;
		resp->lcrdr_eof = 1;
	}

out:
	(void) libuvfs_name_fid_unlock(fs, fid);
errout:
	fuse_uvfs_return(resp, ressize, error);
}

static void
fuse_uvfs_getattr(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_getattr_res_t res;
	libuvfs_common_vop_arg_t *args = varg;
	libuvfs_fid_t *fid = &args->lca_fid;
	char path[MAXPATHLEN];
	struct stat statbuf;
	int len;
	int error = 0;

	assert(argsize == sizeof (*args));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	/*
	 * Add mount option to cache attributes?
	 */
	if ((error = FUSE_OP_GETATTR(fuse_fs, path, &statbuf)) == 0) {
		error = fuse_uvfs_stat_convert(fs, &res.lcgr_stat, &statbuf,
		    fid, NULL);
	}
out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_symlink(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_symlink_arg_t *args = varg;
	libuvfs_fid_t *dirfid = &args->lcsl_dirfid;
	libuvfs_common_res_t res;
	libuvfs_fid_t newfid;
	int error = 0;
	char path[MAXPATHLEN];
	int len;

	assert(argsize == sizeof (*args));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, dirfid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}
	fuse_uvfs_path_fixup(path, args->lcsl_name);
	if ((error = FUSE_OP_SYMLINK(fuse_fs, args->lcsl_link, path)) != 0)
		goto out;

	libuvfs_fid_unique(fs, &newfid);
	libuvfs_name_store(fs, dirfid, args->lcsl_name, &newfid, B_TRUE, NULL);
out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_readlink(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_readlink_arg_t *args = varg;
	libuvfs_cb_readlink_res_t *resp;
	libuvfs_fid_t *fid = &args->lcrl_dirfid;
	char link[MAXPATHLEN];
	size_t len;
	size_t ressize = sizeof (*resp);
	int error = 0;
	uintptr_t buffy;
	char path[MAXPATHLEN];

	assert(argsize == sizeof (*args));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}
	(void) memset(link, 0, MAXPATHLEN);
	if ((error = FUSE_OP_READLINK(fuse_fs, path, link, MAXPATHLEN)) != 0) {
		goto out;
	}

	ressize += strlen(link);
	resp = alloca(ressize);
	buffy = (uintptr_t)resp + sizeof (*resp);
	(void) strcpy((void *)buffy, link);

	resp->lcrl_length = strlen(link);
out:
	fuse_uvfs_return(resp, ressize, error);
}

static void
fuse_uvfs_link(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_link_arg_t *args = varg;
	libuvfs_fid_t *dirfid = &args->lclf_dirfid;
	libuvfs_fid_t *srcfid = &args->lclf_sfid;
	libuvfs_common_res_t res;
	int error = 0;
	char spath[MAXPATHLEN];
	char tpath[MAXPATHLEN];
	int len;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	len = libuvfs_name_path(fs, srcfid, 0, NULL, spath, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	len = libuvfs_name_path(fs, dirfid, 0, NULL, tpath, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	fuse_uvfs_path_fixup(tpath, args->lclf_name);

	if ((error = FUSE_OP_LINK(fuse_fs, spath, tpath)) != 0)
		goto out;

	libuvfs_name_store(fs, dirfid, args->lclf_name, srcfid, B_FALSE, NULL);
out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static int
fuse_uvfs_hide_node(libuvfs_fs_t *fs, libuvfs_fid_t *dirfid,
    libuvfs_fid_t *fid, char *dirpath, char *name)
{
	struct stat statbuf;
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	char *newname;
	char oldpath[MAXPATHLEN];
	char *path = NULL;
	int i;
	int error;
	int f;

	f = open("/dev/random", O_RDONLY);
	if (f == -1)
		return (1);

	newname = umem_zalloc(MAXPATHLEN, UMEM_DEFAULT);
	if (newname == NULL)
		return (ENOMEM);
	for (i = 0; i != 10; ) {
		uint64_t random_bytes;
		libuvfs_fid_t found;
		int error;

		(void) read(f, &random_bytes, sizeof (random_bytes));

		(void) snprintf(newname, MAXPATHLEN,
		    ".fuse_hidden%08llx%08llx", libuvfs_fid_to_id(fs, fid),
		    random_bytes);

		libuvfs_name_lookup(fs, dirfid, newname, &found);
		if (found.uvfid_len != 0)
			continue;

		path = umem_zalloc(MAXPATHLEN, UMEM_DEFAULT);
		if (path == NULL) {
			umem_free(newname, MAXPATHLEN);
			return (ENOMEM);
		}
		(void) strlcpy(path, dirpath, MAXPATHLEN);
		fuse_uvfs_path_fixup(path, newname);
		error = FUSE_OP_GETATTR(fuse_fs, path, &statbuf);
		if (error == -ENOENT)
			break;
		i++;
		umem_free(path, MAXPATHLEN);
		path = NULL;
	}
	(void) close(f);
	if (path == NULL)
		return (EBUSY);
	(void) strlcpy(oldpath, dirpath, MAXPATHLEN);
	fuse_uvfs_path_fixup(oldpath, name);
	error = FUSE_OP_RENAME(fuse_fs, oldpath, path);
	if (error == 0) {
		fuse_hidden_t *hidden;

		hidden = libuvfs_stash_fid_get(fs, fid,
		    FUSE_FID_HIDDEN, NULL);

		if (hidden) {
			umem_free(hidden->hidden_full_path, MAXPATHLEN);
			umem_free(hidden->hidden_component, MAXPATHLEN);
		} else {
			hidden = umem_zalloc(sizeof (fuse_hidden_t),
			    UMEM_DEFAULT);
			if (hidden == NULL) {
				umem_free(path, MAXPATHLEN);
				umem_free(newname, MAXPATHLEN);
				return (ENOMEM);
			}
		}
		hidden->hidden_full_path = path;
		hidden->hidden_component = newname;
		hidden->hidden_dirfd = *dirfid;
		libuvfs_name_store(fs, dirfid, newname, fid, B_FALSE, NULL);
		libuvfs_name_delete(fs, dirfid, name, fid);
		(void) libuvfs_stash_fid_store(fs, fid, FUSE_FID_HIDDEN,
		    B_TRUE, hidden);
	}
	return (error);
}

static void
fuse_uvfs_rmdir(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_rmdir_arg_t *args = varg;
	libuvfs_fid_t *dirfid = &args->lcrd_fid;
	libuvfs_fid_t fid;
	libuvfs_common_res_t res;
	char dirpath[MAXPATHLEN];
	char path[MAXPATHLEN];
	int len;
	int error = 0;
	uint64_t *openp;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	if (libuvfs_name_fid_wrlock(fs, dirfid) != 0) {
		error = ESTALE;
		goto errout;
	}

	len = libuvfs_name_path(fs, dirfid, 0, NULL, dirpath, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	libuvfs_name_lookup(fs, dirfid, args->lcrd_name, &fid);
	if (fid.uvfid_len == 0) {
		error = ENOENT;
		goto out;
	}
	(void) strlcpy(path, dirpath, MAXPATHLEN);
	fuse_uvfs_path_fixup(path, args->lcrd_name);

	/*
	 * if busy then rename entry
	 */

	openp = libuvfs_stash_fid_get(fs, &fid,
	    FUSE_FID_OPEN_COUNT, NULL);

	if (openp && *openp != 0) {
		error = fuse_uvfs_hide_node(fs, dirfid, &fid, dirpath,
		    args->lcrd_name);
	} else {
		error = FUSE_OP_RMDIR(fuse_fs, path);
		if (error == 0) {
			libuvfs_name_delete(fs, dirfid, args->lcrd_name, NULL);
		}
	}
out:
	(void) libuvfs_name_fid_unlock(fs, dirfid);
errout:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_remove(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_remove_arg_t *args = varg;
	libuvfs_fid_t *dirfid = &args->lcrf_fid;
	libuvfs_fid_t fid;
	libuvfs_common_res_t res;
	char path[MAXPATHLEN];
	char dirpath[MAXPATHLEN];
	int len;
	int error = 0;
	uint64_t *openp;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*args));

	if (libuvfs_name_fid_wrlock(fs, dirfid) != 0) {
		error = ESTALE;
		goto errout;
	}

	libuvfs_name_lookup(fs, dirfid, args->lcrf_name, &fid);
	if (fid.uvfid_len == 0) {
		error = ENOENT;
		goto out;
	}

	len = libuvfs_name_path(fs, dirfid, 0, NULL, dirpath, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	(void) strlcpy(path, dirpath, MAXPATHLEN);
	fuse_uvfs_path_fixup(path, args->lcrf_name);

	/*
	 * Check to see if file is busy.  If so then rename it
	 */

	openp = libuvfs_stash_fid_get(fs, &fid,
	    FUSE_FID_OPEN_COUNT, NULL);

	if (openp && *openp != 0) {
		error = fuse_uvfs_hide_node(fs, dirfid, &fid, dirpath,
		    args->lcrf_name);
	} else {
		if ((error = FUSE_OP_UNLINK(fuse_fs, path)) != 0) {
			goto out;
		}
	}

	libuvfs_name_delete(fs, dirfid, args->lcrf_name, NULL);
out:
	(void) libuvfs_name_fid_unlock(fs, dirfid);
errout:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_rename(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_rename_arg_t *args = varg;
	libuvfs_common_res_t res;
	libuvfs_fid_t *sdfid = &args->lcrn_sdfid;
	libuvfs_fid_t *tdfid = &args->lcrn_tdfid;
	libuvfs_fid_t sfid, tfid;
	char spath[MAXPATHLEN];
	char tpath[MAXPATHLEN];
	char tdirpath[MAXPATHLEN];
	uint64_t *openp = NULL;
	int error = 0;
	int len;

	assert(argsize == sizeof (*args));

	fuse_uvfs_setup_context(cr, fuse_fs);

	if (libuvfs_name_fid_wrlock(fs, sdfid) != 0) {
		error = ESTALE;
		goto errout;
	}

	/*
	 * Find source/target fids and paths
	 */

	libuvfs_name_lookup(fs, sdfid, args->lcrn_sname, &sfid);
	if (sfid.uvfid_len == 0) {
		error = ENOENT;
		goto out;
	}

	len = libuvfs_name_path(fs, sdfid, 0, NULL, spath, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	fuse_uvfs_path_fixup(spath, args->lcrn_sname);

	/* Now get path to target directory */
	len = libuvfs_name_path(fs, tdfid, 0, NULL, tdirpath, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		error = ENOENT;
		goto out;
	}

	/*
	 * Does target exist?
	 */
	libuvfs_name_lookup(fs, tdfid, args->lcrn_tname, &tfid);
	(void) strlcpy(tpath, tdirpath, MAXPATHLEN);
	fuse_uvfs_path_fixup(tpath, args->lcrn_tname);

	/*
	 * If file is busy then hide it
	 */

	if (tfid.uvfid_len != 0) {
		openp = libuvfs_stash_fid_get(fs, &tfid,
		    FUSE_FID_OPEN_COUNT, NULL);

		if (openp && *openp != 0) {
			if (error = fuse_uvfs_hide_node(fs, tdfid, &tfid,
			    tdirpath, args->lcrn_tname))
			goto out;
		}
	}

	if ((error = FUSE_OP_RENAME(fuse_fs, spath, tpath)) != 0)
		goto out;

	libuvfs_name_delete(fs, sdfid, args->lcrn_sname, NULL);
	libuvfs_name_store(fs, tdfid, args->lcrn_tname, &sfid, B_TRUE, NULL);
out:
	(void) libuvfs_name_fid_unlock(fs, sdfid);
errout:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_setattr(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_setattr_arg_t *arg = varg;
	libuvfs_cb_setattr_res_t res;
	struct timespec tv[2];
	struct stat statbuf;
	uint64_t mask = arg->lcsa_mask;
	char path[MAXPATHLEN];
	int len;
	int error = 0;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*arg));

	len = libuvfs_name_path(fs, &arg->lcsa_fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len >= MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	if (mask & AT_MODE) {
		mode_t mode;
		if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
			error = ESTALE;
			goto out;
		}
		mode = statbuf.st_mode & S_IFMT;
		mode |= (mode_t)(arg->lcsa_attributes.l_mode & ~S_IFMT);
		if ((error = FUSE_OP_CHMOD(fuse_fs, path, mode)) != 0)
			goto out;
	}

	if ((mask & AT_UID) || (mask & AT_GID)) {
		uid_t uid, gid;
		if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
			error = ESTALE;
			goto out;
		}
		uid = (mask & AT_UID) ? (uid_t)arg->lcsa_attributes.l_uid :
		    statbuf.st_uid;
		gid = (mask & AT_GID) ? (uid_t)arg->lcsa_attributes.l_gid :
		    statbuf.st_gid;

		if ((error = FUSE_OP_CHOWN(fuse_fs, path, uid, gid)) != 0)
			goto out;
	}

	if (mask & AT_SIZE)
		if ((error = FUSE_OP_TRUNCATE(fuse_fs, path,
		    (off_t)arg->lcsa_attributes.l_size)) != 0)
			goto out;


	if ((mask & AT_ATIME) || (mask & AT_MTIME)) {
		if (mask & AT_ATIME) {
			tv[0].tv_sec = arg->lcsa_attributes.l_atime[0];
			tv[0].tv_nsec = arg->lcsa_attributes.l_atime[1];
		}

		if (mask & AT_MTIME) {
			tv[1].tv_sec = arg->lcsa_attributes.l_mtime[0];
			tv[1].tv_nsec = arg->lcsa_attributes.l_mtime[1];
		}

		FUSE_OP_UTIMENS(fuse_fs, path, tv, error);
		if (error)
			goto out;
	}

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	error = fuse_uvfs_stat_convert(fs, &res.set_attributes, &statbuf,
	    &arg->lcsa_fid, NULL);

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_read(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_read_arg_t *arg = varg;
	libuvfs_cb_read_res_t *resp;
	size_t ressize = sizeof (*resp);
	char path[MAXPATHLEN];
	int len;
	int bytes;
	uintptr_t buffy;
	struct stat statbuf;
	struct fuse_file_info ffi = {0};
	uint64_t *fhp;
	int error = 0;

	assert(argsize == sizeof (*arg));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, &arg->lcra_fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len >= MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	ressize += arg->lcra_len;
	resp = alloca(ressize);
	buffy = (uintptr_t)resp + sizeof (*resp);

	fhp = libuvfs_stash_fid_get(fs, &arg->lcra_fid, FUSE_FID_FH, NULL);
	if (fhp)
		ffi.fh = *fhp;

	/*
	 * bytes assigned by the FUSE_OP_READ macro, negative val indicates err
	 */
	FUSE_OP_READ(fuse_fs, path, (char *)buffy, arg->lcra_len,
	    (size_t)arg->lcra_offset, &ffi, bytes);

	if (bytes < 0) {
		error = bytes;
		goto out;
	}

	assert(bytes < ressize);
	resp->lcrr_length = bytes;

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	error = fuse_uvfs_stat_convert(fs, &resp->lcrr_stat, &statbuf,
	    &arg->lcra_fid, NULL);

out:
	fuse_uvfs_return(resp, ressize, error);
}

/*ARGSUSED*/
static void
fuse_uvfs_write(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_write_arg_t *arg = varg;
	libuvfs_cb_write_res_t res;
	uintptr_t data = (uintptr_t)arg + sizeof (*arg);
	char path[MAXPATHLEN];
	struct fuse_file_info ffi = { 0 };
	struct stat statbuf;
	int len;
	int bytes;
	uint64_t *fhp;
	int error = 0;
	int found;

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, &arg->lcwa_fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len >= MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	fhp = libuvfs_stash_fid_get(fs, &arg->lcwa_fid, FUSE_FID_FH, &found);
	if (fhp)
		ffi.fh = *fhp;

	/*
	 * bytes assigned by the FUSE_OP_WRITE macro, negative val indicates err
	 */
	FUSE_OP_WRITE(fuse_fs, path, (void *)data, arg->lcwa_length,
	    arg->lcwa_offset, &ffi, bytes);

	if (bytes < 0) {
		error = bytes;
		goto out;
	}
	res.lcwr_bytes_written = bytes;

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	error = fuse_uvfs_stat_convert(fs, &res.lcwr_stat, &statbuf,
	    &arg->lcwa_fid, NULL);

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_create(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_create_arg_t *arg = varg;
	libuvfs_cb_create_res_t res;
	libuvfs_fid_t *dirfid = &arg->lccf_dirfid;
	libuvfs_fid_t newfid;
	struct stat statbuf;
	char path[MAXPATHLEN];
	dev_t rdev;
	int len;
	int error = 0;

	fuse_uvfs_setup_context(cr, fuse_fs);

	assert(argsize == sizeof (*arg));

	if (libuvfs_name_fid_rdlock(fs, dirfid) != 0) {
		error = ESTALE;
		goto out;
	}

	len = libuvfs_name_path(fs, dirfid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		(void) libuvfs_name_fid_unlock(fs, dirfid);
		error = ESTALE;
		goto out;
	}

	fuse_uvfs_path_fixup(path, arg->lccf_name);

	rdev = libuvfs_cmpldev(arg->lccf_creation_attrs.l_rdev);

	if ((error = FUSE_OP_MKNOD(fuse_fs, path,
	    arg->lccf_creation_attrs.l_mode, rdev)) != 0) {
		(void) libuvfs_name_fid_unlock(fs, dirfid);
		goto out;
	}

	libuvfs_fid_unique(fs, &newfid);
	libuvfs_name_store(fs, dirfid, arg->lccf_name, &newfid, B_TRUE, NULL);

	res.lccf_fid = newfid;
	res.lccf_stat.l_fid = newfid;

	(void) libuvfs_name_fid_unlock(fs, dirfid);

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	(void) fuse_uvfs_stat_convert(fs, &res.lccf_stat, &statbuf,
	    NULL, dirfid);

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_mkdir(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_mkdir_arg_t *arg = varg;
	libuvfs_cb_mkdir_res_t res;
	libuvfs_fid_t *dirfid = &arg->lcmd_dirfid;
	libuvfs_fid_t newfid;
	struct stat statbuf;
	char path[MAXPATHLEN];
	int len;
	int error = 0;

	assert(argsize == sizeof (*arg));

	fuse_uvfs_setup_context(cr, fuse_fs);

	if (libuvfs_name_fid_rdlock(fs, dirfid) != 0) {
		error = ESTALE;
		goto out;
	}

	len = libuvfs_name_path(fs, dirfid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len > MAXPATHLEN) {
		(void) libuvfs_name_fid_unlock(fs, dirfid);
		error = ESTALE;
		goto out;
	}

	fuse_uvfs_path_fixup(path, arg->lcmd_name);

	if ((error = FUSE_OP_MKDIR(fuse_fs, path,
	    arg->lcmd_creation_attrs.l_mode)) != 0) {
		(void) libuvfs_name_fid_unlock(fs, dirfid);
		goto out;
	}

	libuvfs_fid_unique(fs, &newfid);
	libuvfs_name_store(fs, dirfid, arg->lcmd_name, &newfid, B_TRUE, NULL);

	res.lcmd_fid = newfid;
	res.lcmd_stat.l_fid = newfid;

	(void) libuvfs_name_fid_unlock(fs, dirfid);

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	(void) fuse_uvfs_stat_convert(fs, &res.lcmd_stat, &statbuf,
	    NULL, dirfid);
out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_space(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_space_arg_t *arg = varg;
	libuvfs_common_res_t res;
	char path[MAXPATHLEN];
	int len;
	int error = 0;

	assert(argsize == sizeof (*arg));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, &arg->lcfs_fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len >= MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	/*
	 * Only truncation is supported by fuse
	 */
	error = FUSE_OP_TRUNCATE(fuse_fs, path, (off_t)arg->lcfs_offset);

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static void
fuse_uvfs_fsync(libuvfs_fs_t *fs, void *varg, size_t argsize, ucred_t *cr)
{
	struct fuse_fs *fuse_fs = FUSE_UVFS_FS(fs);
	libuvfs_cb_fsync_arg_t *arg = varg;
	libuvfs_common_res_t res;
	char path[MAXPATHLEN];
	struct stat statbuf;
	struct fuse_file_info fi = { 0 };
	uint64_t *fhp;
	int len;
	int error = 0;

	assert(argsize == sizeof (*arg));

	fuse_uvfs_setup_context(cr, fuse_fs);

	len = libuvfs_name_path(fs, &arg->lcfs_fid, 0, NULL, path, MAXPATHLEN);
	if (len == 0 || len >= MAXPATHLEN) {
		error = ESTALE;
		goto out;
	}

	if (FUSE_OP_GETATTR(fuse_fs, path, &statbuf) != 0) {
		error = ESTALE;
		goto out;
	}

	fhp = libuvfs_stash_fid_get(fs, &arg->lcfs_fid, FUSE_FID_FH, NULL);

	if (fhp == NULL) {
		error = ESTALE;
		goto out;
	}

	fi.fh = *fhp;
	if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
		error = FUSE_OP_FSYNCDIR(fuse_fs, path,
		    arg->lcfs_syncflag, &fi);
	else
		error = FUSE_OP_FSYNC(fuse_fs, path, arg->lcfs_syncflag, &fi);

out:
	fuse_uvfs_return(&res, sizeof (res), error);
}

static libuvfs_callback_reg_t uvfs_all[] = {
	{UVFS_CB_VFS_ROOT, fuse_uvfs_vfsroot},
	{UVFS_CB_VFS_VGET, fuse_uvfs_vget},
	{UVFS_CB_VFS_STATVFS, fuse_uvfs_statvfs},
	{UVFS_CB_VOP_LOOKUP, fuse_uvfs_lookup},
	{UVFS_CB_VOP_OPEN, fuse_uvfs_open},
	{UVFS_CB_VOP_CLOSE, fuse_uvfs_close},
	{UVFS_CB_VOP_GETATTR, fuse_uvfs_getattr},
	{UVFS_CB_VOP_READ, fuse_uvfs_read},
	{UVFS_CB_VOP_WRITE, fuse_uvfs_write},
	{UVFS_CB_VOP_SETATTR, fuse_uvfs_setattr},
	{UVFS_CB_VOP_READDIR, fuse_uvfs_readdir},
	{UVFS_CB_VOP_SYMLINK, fuse_uvfs_symlink},
	{UVFS_CB_VOP_READLINK, fuse_uvfs_readlink},
	{UVFS_CB_VOP_LINK, fuse_uvfs_link},
	{UVFS_CB_VOP_CREATE, fuse_uvfs_create},
	{UVFS_CB_VOP_MKDIR, fuse_uvfs_mkdir},
	{UVFS_CB_VOP_RMDIR, fuse_uvfs_rmdir},
	{UVFS_CB_VOP_REMOVE, fuse_uvfs_remove},
	{UVFS_CB_VOP_FSYNC, fuse_uvfs_fsync},
	{UVFS_CB_VOP_SPACE, fuse_uvfs_space},
	{UVFS_CB_VOP_RENAME, fuse_uvfs_rename},
	{UVFS_CB_VOP_ADDMAP, fuse_uvfs_addmap},
	{UVFS_CB_VOP_DELMAP, fuse_uvfs_delmap},
	{NULL, NULL},
};

typedef void *fuse_conn_info;

static pthread_mutex_t tsd_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint64_t tsd_ref;

static void
fuse_uvfs_tsd_free(void *data)
{
	umem_free(data, sizeof (struct fuse_context));
}

void
fuse_fs_init(struct fuse_fs *fs, struct fuse_conn_info *conn)
{
	if (fs->op.init)
		fs->user_data = fs->op.init(conn);
}

void
fuse_fs_destroy(struct fuse_fs *fs)
{
	if (fs->op.destroy)
		fs->op.destroy(fs->user_data);
	umem_free(fs, sizeof (struct fuse_fs));
}

void
fuse_exit(struct fuse *f)
{
	f->fuse_exited = 1;
}

/*ARGSUSED*/
struct fuse *
fuse_new(struct fuse_chan *ch, struct fuse_args *args,
    const struct fuse_operations *op, size_t op_size, void *user_data)
{
	struct fuse_fs *fs;
	struct fuse *fuse;
	libuvfs_fid_t *rootfid;
	struct fuse_context *ctx;
	struct fuse_conn_info fci;

	fuse = umem_zalloc(sizeof (struct fuse), UMEM_DEFAULT);
	if (fuse == NULL)
		return (NULL);

	(void) pthread_mutex_lock(&tsd_mtx);
	if (tsd_ref == 0) {
		if (pthread_key_create(&fuse_uvfs_tsd_key,
		    fuse_uvfs_tsd_free) != 0) {
			(void) pthread_mutex_unlock(&tsd_mtx);
			return (NULL);
		}
	}
	tsd_ref++;
	(void) pthread_mutex_unlock(&tsd_mtx);

	if (sizeof (struct fuse_operations) < op_size) {
		(void) fprintf(stderr,
		    gettext("fuse: warning: library too old, "));
		(void) fprintf(stderr,
		    gettext("some operations may not not work\n"));
		op_size = sizeof (struct fuse_operations);
	}

	fs = (struct fuse_fs *)umem_zalloc(sizeof (struct fuse_fs),
	    UMEM_DEFAULT);
	if (!fs) {
		umem_free(fuse, sizeof (struct fuse));
		(void) fprintf(stderr,
		    gettext("fuse: failed to allocate fuse_fs\n"));
		return (NULL);
	}

	fs->user_data = user_data;
	if (op)
		(void) memcpy(&fs->op, op, op_size);
	/*
	 * Set up context in case client needs to call
	 * fuse_get_context() during init function.
	 * Most likely use is probably for calling fuse_exit()
	 */
	ctx = fuse_get_context();
	ctx->uid = geteuid();
	ctx->gid = getegid();
	ctx->pid = getpid();
	ctx->fuse = fuse;
	ctx->private_data = user_data;

	fuse_fs_init(fs, &fci);

	if (fuse->fuse_exited) {
		umem_free(fuse, sizeof (struct fuse));
		fuse_fs_destroy(fs);
		return (NULL);
	}

	/*
	 * Store fuse_fs and other stuff
	 * in fs stash and setup root dir.
	 */
	rootfid = umem_alloc(sizeof (*rootfid), UMEM_NOFAIL);
	libuvfs_fid_unique(ch->fuse_uvfs_fs, rootfid);
	(void) libuvfs_stash_fs_store(ch->fuse_uvfs_fs, FUSE_ROOT_FID,
	    B_TRUE, rootfid);

	fuse->fuse_fs = fs;
	fuse->fuse_uvfs_fs = ch->fuse_uvfs_fs;
	fuse->fuse_fs->fuse = fuse;

	(void) fuse_uvfs_fs_init(ch->fuse_uvfs_fs, fs);
	libuvfs_name_root_create(ch->fuse_uvfs_fs, rootfid);
	if (libuvfs_register_callbacks(ch->fuse_uvfs_fs, uvfs_all) == -1) {
		umem_free(fuse, sizeof (struct fuse));
		umem_free(rootfid, sizeof (*rootfid));
		fuse_fs_destroy(fs);
		return (NULL);
	}

	return (fuse);
}

/*ARGSUSED*/
int
fuse_main_common(int argc, char *argv[], const struct fuse_operations *op,
    size_t op_size, void *user_data, int compat)
{
	struct fuse *fuse;
	struct fuse_chan *fch;
	int error;
	char *mountpoint;
	int multithreaded;
	int foreground;
	int is_smf = libuvfs_is_daemon();
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	if (is_smf) {
		mountpoint = "";
		fch = fuse_uvfs_fs_create(mountpoint, B_TRUE);
		if (fch == NULL)
			return (1);
	} else {
		if (error = fuse_parse_cmdline(&args, &mountpoint,
		    &multithreaded, &foreground))
			return (error);
		if ((fch = fuse_mount(mountpoint, &args)) == NULL)
			return (1);
	}

	fuse = fuse_new(fch, &args, op, op_size, user_data);
	if (fuse == NULL) {
		if (!is_smf)
			fuse_unmount(mountpoint, fch);
		return (1);
	}

	/*
	 * Call fuse_loop() to start processing upcalls.
	 */
	error = fuse_loop(fuse);

	if (!is_smf)
		fuse_unmount(mountpoint, fch);

	fuse_destroy(fuse);

	return (error);
}

void
fuse_destroy(struct fuse *f)
{
	fuse_fs_destroy(f->fuse_fs);

	umem_free(f, sizeof (struct fuse));
	(void) pthread_mutex_lock(&tsd_mtx);
	tsd_ref--;
	if (tsd_ref == 0) {
		umem_free(pthread_getspecific(fuse_uvfs_tsd_key),
		    sizeof (struct fuse_context));
		(void) pthread_key_delete(fuse_uvfs_tsd_key);
	}
	(void) pthread_mutex_unlock(&tsd_mtx);
}

int
fuse_loop(struct fuse *f)
{
	if (f == NULL || f->fuse_exited)
		return (-1);

	return (libuvfs_daemon_ready(f->fuse_uvfs_fs));
}

int
fuse_main_real(int argc, char *argv[], const struct fuse_operations *op,
	size_t op_size, void *user_data)
{
	return (fuse_main_common(argc, argv, op, op_size, user_data, 0));
}

#undef fuse_main
int
fuse_main(void)
{
	return (-1);
}
