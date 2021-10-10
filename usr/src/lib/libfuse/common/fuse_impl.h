/*
 * Fuse: Filesystem in Userspace
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#ifndef _SYS_FUSE_IMPL_H
#define	_SYS_FUSE_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

struct fuse_fs {
	struct fuse_operations op;
	libuvfs_callback_reg_t *callbacks;
	void *user_data;
	struct fuse *fuse;
};

struct fuse_chan {
	libuvfs_fs_t	*fuse_uvfs_fs;
	void		*fuse_opaque;
};

struct fuse {
	struct fuse_fs	*fuse_fs;
	libuvfs_fs_t	*fuse_uvfs_fs;
	int		fuse_exited;
};

#define	FUSE_OP_GETATTR(fs, path, stat)  \
	(fs->op.getattr ? fs->op.getattr(path, stat) : ENOSYS)
#define	FUSE_OP_OPEN(fs, path, fi)  \
	(fs->op.open ? fs->op.open(path, fi) : ENOSYS)
#define	FUSE_OP_OPENDIR(fs, path, fi) \
	(fs->op.opendir ? fs->op.opendir(path, fi) : 0)
#define	FUSE_OP_MKNOD(fs, path, mode, rdev) \
	(fs->op.mknod ? fs->op.mknod(path, mode, rdev) : ENOSYS)
#define	FUSE_OP_MKDIR(fs, path, mode) \
	(fs->op.mkdir ? fs->op.mkdir(path, mode) : ENOSYS)
#define	FUSE_OP_RELEASE(fs, path, fi) \
	(fs->op.release ? fs->op.release(path, fi) : 0)
#define	FUSE_OP_RELEASEDIR(fs, path, fi) \
	(fs->op.releasedir ? fs->op.releasedir(path, fi) : 0)
#define	FUSE_OP_RMDIR(fs, path) \
	(fs->op.rmdir ? fs->op.rmdir(path) : ENOSYS)
#define	FUSE_OP_UNLINK(fs, path) \
	(fs->op.unlink ? fs->op.unlink(path) : ENOSYS)
/*
 * Trick lint into not turning fuse_operation into readdir64
 */
#ifdef	__lint
#define	FUSE_OP_READDIR(fs, path, dirhp, fill_func, off, fi) \
	(fuse_uvfs_fill_dir((void *)fi, NULL, NULL, 0))
#else
#define	FUSE_OP_READDIR(fs, path, dirhp, fill_func, off, fi) \
	(fs->op.readdir ? \
	fs->op.readdir(path, dirhp, fill_func, off, fi) : ENOSYS)
#endif
#define	FUSE_OP_SYMLINK(fs, link, path) \
	(fs->op.symlink ? fs->op.symlink(link, path) : ENOSYS)
#define	FUSE_OP_READLINK(fs, path, link, len) \
	(fs->op.readlink ? fs->op.readlink(path, link, len) : ENOSYS)
#define	FUSE_OP_LINK(fs, src, dst) \
	(fs->op.link ? fs->op.link(src, dst) : ENOSYS)
#define	FUSE_OP_RENAME(fs, old, new) \
	(fs->op.rename ? fs->op.rename(old, new) : ENOSYS)
#define	FUSE_OP_CHMOD(fs, path, mode) \
	(fs->op.chmod ? fs->op.chmod(path, mode) : ENOSYS)
#define	FUSE_OP_CHOWN(fs, path, uid, gid) \
	(fs->op.chown ? fs->op.chown(path, uid, gid) : ENOSYS)
#ifdef	__lint
#define	FUSE_OP_TRUNCATE(fs, path, size) 0
#else
#define	FUSE_OP_TRUNCATE(fs, path, size) \
	((fs)->op.truncate ? (fs)->op.truncate(path, size) : ENOSYS)
#endif
#define	FUSE_OP_READ(fs, path, buf, size, off, fi, ret) { \
	if (fs->op.read) { \
		ret = fs->op.read(path, buf, size, off, fi); \
	} else { \
		errno = ENOSYS; \
		ret = -1; \
	} \
}
#define	FUSE_OP_WRITE(fs, path, buf, size, off, fi, ret) { \
	if (fs->op.write) { \
		ret = fs->op.write(path, buf, size, off, fi); \
	} else { \
		errno = ENOSYS; \
		ret = -1; \
	} \
}
#define	FUSE_OP_ACCESS(fs, path, mode) \
	(fs->op.access ? fs->op.access(path, mode) : ENOSYS)

#define	FUSE_OP_UTIMENS(fs, path, tv, ret) { \
	if (fs->op.utimens) { \
		ret = fs->op.utimens(path, tv); \
	} else if (fs->op.utime) {\
		struct utimbuf times; \
		times.actime = tv[0].tv_sec; \
		times.modtime = tv[1].tv_sec; \
		ret = fs->op.utime(path, &times); \
	} else { \
		ret = -1; \
		errno = ENOSYS; \
	} \
}
#define	FUSE_OP_FSYNCDIR(fs, path, flag, fi) \
	(fs->op.fsyncdir ? fs->op.fsyncdir(path, flag, fi) : ENOSYS)
#define	FUSE_OP_STATFS(fs, path, buf) \
	(fs->op.statfs ? fs->op.statfs(path, buf) : ENOSYS)
#define	FUSE_OP_FSYNC(fs, path, flag, fi) \
	(fs->op.fsync ? fs->op.fsync(path, flag, fi) : ENOSYS)
#define	FUSE_OP_FLUSH(fs, path, fi) \
	(fs->op.flush ? fs->op.flush(path, fi) : ENOSYS)
#define	FUSE_OP_LOCK(fs, path, fi, cmd, lock) \
	(fs->op.lock ? fs->op.lock(path, fi, cmd, lock) : ENOSYS)

int fuse_mount_option_process(struct fuse_args *args, char *mnt_opts);
int fuse_lib_option_process(struct fuse_args *args, struct fuse_fs *fs);
int fuse_lib_create_context_key(void);
void fuse_lib_delete_context_key(void);
struct fuse_chan *fuse_uvfs_fs_create(const char *, boolean_t);

struct fuse_fs *fuse_create_fs(struct fuse_args *args,
    const struct fuse_operations *op, size_t op_size, void *user_data);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_FUSE_IMPL_H */
