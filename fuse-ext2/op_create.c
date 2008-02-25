/**
 * Copyright (c) 2008 Alper Akcan
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "fuse-ext2.h"

int do_modetoext2lag (mode_t mode)
{
	if (S_ISREG(mode)) {
		return EXT2_FT_REG_FILE;
	} else if (S_ISDIR(mode)) {
		return EXT2_FT_DIR;
	} else if (S_ISCHR(mode)) {
		return EXT2_FT_CHRDEV;
	} else if (S_ISBLK(mode)) {
		return EXT2_FT_BLKDEV;
	} else if (S_ISFIFO(mode)) {
		return EXT2_FT_FIFO;
	} else if (S_ISSOCK(mode)) {
		return EXT2_FT_SOCK;
	} else if (S_ISLNK(mode)) {
		return EXT2_FT_SYMLINK;
	}
	return EXT2_FT_UNKNOWN;
}

int op_create (const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int rt;
	errcode_t rc;

	char *p_path;
	char *r_path;
	char *t_path;

	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_ino_t n_ino;

	struct fuse_context *ctx;
	
	debugf("enter");
	debugf("path = %s, mode: 0%o", path, mode);
	
	if (op_open(path, fi) == 0) {
		debugf("leave");
		return 0;
	}

	p_path = strdup(path);
	if (p_path == NULL) {
		debugf("strdup(%s); failed", path);
		return -ENOMEM;
	}
	t_path = strrchr(p_path, '/');
	if (t_path == NULL) {
		debugf("this should not happen");
		free(p_path);
		return -ENOENT;
	}
	*t_path = '\0';
	r_path = t_path + 1;
	debugf("parent: %s, child: %s", p_path, r_path);
	
	rt = do_readinode(p_path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", p_path);
		free(p_path);
		return rt;
	}
	
	rc = ext2fs_new_inode(priv.fs, ino, mode, 0, &n_ino);
	if (rc) {
		debugf("ext2fs_new_inode(ep.fs, ino, mode, 0, &n_ino); failed");
		return -ENOMEM;
	}

	do {
		debugf("calling ext2fs_link(priv.fs, %d, %s, %d, %d);", ino, r_path, n_ino, do_modetoext2lag(mode));
		rc = ext2fs_link(priv.fs, ino, r_path, n_ino, do_modetoext2lag(mode));
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(priv.fs, &d)", ino);
			if (ext2fs_expand_dir(priv.fs, ino)) {
				debugf("error while expanding directory %s (%d)", p_path, ino);
				free(p_path);
				return -ENOSPC;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc) {
		debugf("ext2fs_mkdir(priv.fs, %d, 0, %s); failed (%d)", ino, r_path, rc);
		debugf("priv.fs: %p, priv.fs->inode_map: %p", priv.fs, priv.fs->inode_map);
		free(p_path);
		return -EIO;
	}
	free(p_path);
	
	if (ext2fs_test_inode_bitmap(priv.fs->inode_map, n_ino)) {
		debugf("inode already set");
	}

	ext2fs_inode_alloc_stats2(priv.fs, n_ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	inode.i_mode = mode;
	inode.i_atime = inode.i_ctime = inode.i_mtime = time(NULL);
	inode.i_links_count = 1;
	inode.i_size = 0;
	ctx = fuse_get_context();
	if (ctx) {
		inode.i_uid = ctx->uid;
		inode.i_gid = ctx->gid;
	}
	
	rc = ext2fs_write_new_inode(priv.fs, n_ino, &inode);
	if (rc) {
		debugf("ext2fs_write_new_inode(e2fs, n_ino, &inode);");
		return -EIO;
	}
	
	if (op_open(path, fi)) {
		debugf("op_open(path, fi); failed");
		return -EIO;
	}

	debugf("leave");
	return 0;
}