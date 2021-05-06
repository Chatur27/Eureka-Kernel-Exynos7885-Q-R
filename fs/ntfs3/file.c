// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ntfs3/file.c
 *
 * Copyright (C) 2019-2020 Paragon Software GmbH, All rights reserved.
 *
 *  regular file handling primitives for ntfs-based filesystems
 */
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/compat.h>
#include <linux/falloc.h>
#include <linux/msdos_fs.h> /* FAT_IOCTL_XXX */
#include <linux/nls.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

static int ntfs_ioctl_fitrim(ntfs_sb_info *sbi, unsigned long arg)
{
	struct fstrim_range __user *user_range;
	struct fstrim_range range;
	struct request_queue *q = bdev_get_queue(sbi->sb->s_bdev);
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	user_range = (struct fstrim_range __user *)arg;
	if (copy_from_user(&range, user_range, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(u32, range.minlen, q->limits.discard_granularity);

	err = ntfs_trim_fs(sbi, &range);
	if (err < 0)
		return err;

	if (copy_to_user(user_range, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

static long ntfs_ioctl(struct file *filp, u32 cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
		return put_user(le32_to_cpu(ntfs_i(inode)->std_fa), user_attr);

	case FAT_IOCTL_GET_VOLUME_ID:
		return put_user(sbi->volume.ser_num, user_attr);

	case FITRIM:
		return ntfs_ioctl_fitrim(sbi, arg);
	}
	return -ENOTTY; /* Inappropriate ioctl for device */
}

#ifdef CONFIG_COMPAT
static long ntfs_compat_ioctl(struct file *filp, u32 cmd, unsigned long arg)

{
	return ntfs_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

/*
 * inode_operations::getattr
 */
int ntfs_getattr(const struct path *path, struct kstat *stat, u32 request_mask,
		 u32 flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	ntfs_inode *ni = ntfs_i(inode);

	if (is_compressed(ni))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	if (is_encrypted(ni))
		stat->attributes |= STATX_ATTR_ENCRYPTED;

	stat->attributes_mask |= STATX_ATTR_COMPRESSED | STATX_ATTR_ENCRYPTED;

	generic_fillattr(inode, stat);

	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = ni->i_crtime.tv_sec;
	stat->btime.tv_nsec = ni->i_crtime.tv_nsec;
	stat->blksize = sbi->cluster_size;
	stat->blocks <<= sbi->cluster_bits - 9;

	return 0;
}

static int ntfs_extend_initialized_size(struct file *file, ntfs_inode *ni,
					const loff_t valid,
					const loff_t new_valid)
{
	struct inode *inode = &ni->vfs_inode;
	struct address_space *mapping = inode->i_mapping;
	ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	loff_t pos = valid;
	int err;

	WARN_ON(is_compressed(ni));
	WARN_ON(valid >= new_valid);

	for (;;) {
		u32 zerofrom, len;
		struct page *page;
		void *fsdata;
		u8 bits;
		CLST vcn, lcn, clen;

		if (is_sparsed(ni)) {
			bits = sbi->cluster_bits;
			vcn = pos >> bits;

			err = attr_data_get_block(ni, vcn, &lcn, &clen, NULL);

			if (err)
				goto out;

			if (lcn == SPARSE_LCN) {
				loff_t vbo = (loff_t)vcn << bits;
				loff_t to = vbo + ((loff_t)clen << bits);

				if (to <= new_valid) {
					ni->i_valid = to;
					pos = to;
					goto next;
				}

				if (vbo < pos)
					pos = vbo;
				else {
					to = (new_valid >> bits) << bits;
					if (pos < to) {
						ni->i_valid = to;
						pos = to;
						goto next;
					}
				}
			}
		}

		zerofrom = pos & (PAGE_SIZE - 1);
		len = PAGE_SIZE - zerofrom;

		if (pos + len > new_valid)
			len = new_valid - pos;

		err = pagecache_write_begin(file, mapping, pos, len, 0, &page,
					    &fsdata);
		if (err)
			goto out;

		zero_user_segment(page, zerofrom, PAGE_SIZE);

		/* this function in any case puts page*/
		err = pagecache_write_end(file, mapping, pos, len, len, page,
					  fsdata);
		if (err < 0)
			goto out;
		pos += len;

next:
		if (pos >= new_valid)
			break;
		balance_dirty_pages_ratelimited(mapping);
	}

	mark_inode_dirty(inode);

	return 0;

out:
	ni->i_valid = valid;
	ntfs_inode_warning(inode, "failed to extend initialized size to %llx.",
			   new_valid);
	return err;
}

static int ntfs_extend_initialized_size_cmpr(struct file *file, ntfs_inode *ni,
					     const loff_t valid,
					     const loff_t new_valid)
{
	struct inode *inode = &ni->vfs_inode;
	struct address_space *mapping = inode->i_mapping;
	ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	loff_t pos = valid;
	u8 bits = NTFS_LZNT_CUNIT + sbi->cluster_bits;
	int err;

	WARN_ON(!is_compressed(ni));
	WARN_ON(valid >= new_valid);

	for (;;) {
		u32 zerofrom, len;
		struct page *page;
		CLST frame, vcn, lcn, clen;

		frame = pos >> bits;
		vcn = frame << NTFS_LZNT_CUNIT;

		err = attr_data_get_block(ni, vcn, &lcn, &clen, NULL);

		if (err)
			goto out;

		if (lcn == SPARSE_LCN) {
			loff_t vbo = (loff_t)frame << bits;
			loff_t to = vbo + ((u64)clen << sbi->cluster_bits);

			if (to <= new_valid) {
				ni->i_valid = to;
				pos = to;
				goto next;
			}

			if (vbo >= pos) {
				to = (new_valid >> bits) << bits;
				if (pos < to) {
					ni->i_valid = to;
					pos = to;
					goto next;
				}
			}
		}

		zerofrom = pos & (PAGE_SIZE - 1);
		len = PAGE_SIZE - zerofrom;

		if (pos + len > new_valid)
			len = new_valid - pos;
again:
		page = find_or_create_page(mapping, pos >> PAGE_SHIFT,
					   mapping_gfp_constraint(mapping,
								  ~__GFP_FS));

		if (!page) {
			err = -ENOMEM;
			goto out;
		}

		if (zerofrom && !PageUptodate(page)) {
			err = ntfs_readpage(NULL, page);
			lock_page(page);
			if (page->mapping != mapping) {
				unlock_page(page);
				put_page(page);
				goto again;
			}
			if (!PageUptodate(page)) {
				err = -EIO;
				unlock_page(page);
				put_page(page);
				goto out;
			}
		}

		wait_on_page_writeback(page);

		zero_user_segment(page, zerofrom, PAGE_SIZE);
		if (!zerofrom)
			SetPageUptodate(page);

		ClearPageChecked(page);
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);
		pos += len;
		ni->i_valid = pos;
next:
		if (pos >= new_valid)
			break;
		balance_dirty_pages_ratelimited(mapping);
	}

	mark_inode_dirty(inode);

	return 0;

out:
	ni->i_valid = valid;
	ntfs_inode_warning(
		inode, "failed to extend initialized compressed size to %llx.",
		new_valid);
	return err;
}

/*
 * ntfs_sparse_cluster
 *
 * Helper function to zero a new allocated clusters
 */
void ntfs_sparse_cluster(struct inode *inode, struct page *page0, loff_t vbo,
			 u32 bytes)
{
	struct address_space *mapping = inode->i_mapping;
	ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	u32 blocksize = 1 << inode->i_blkbits;
	pgoff_t idx0 = page0 ? page0->index : -1;
	loff_t vbo_clst = vbo & sbi->cluster_mask_inv;
	loff_t end = ntfs_up_cluster(sbi, vbo + bytes);
	pgoff_t idx = vbo_clst >> PAGE_SHIFT;
	u32 from = vbo_clst & (PAGE_SIZE - 1);
	pgoff_t idx_end = (end + PAGE_SIZE - 1) >> PAGE_SHIFT;
	loff_t page_off;
	u32 to;
	bool partial;
	struct page *page;

	for (; idx < idx_end; idx += 1, from = 0) {
		page = idx == idx0 ? page0 : grab_cache_page(mapping, idx);

		if (!page)
			continue;

		page_off = (loff_t)idx << PAGE_SHIFT;
		to = (page_off + PAGE_SIZE) > end ? (end - page_off) :
						    PAGE_SIZE;
		partial = false;

		if ((from || PAGE_SIZE != to) &&
		    likely(!page_has_buffers(page))) {
			create_empty_buffers(page, blocksize, 0);
			if (!page_has_buffers(page)) {
				ntfs_inode_error(
					inode,
					"failed to allocate page buffers.");
				/*err = -ENOMEM;*/
				goto unlock_page;
			}
		}

		if (page_has_buffers(page)) {
			struct buffer_head *head, *bh;
			u32 bh_off = 0;

			bh = head = page_buffers(page);
			do {
				u32 bh_next = bh_off + blocksize;

				if (from <= bh_off && bh_next <= to) {
					set_buffer_uptodate(bh);
					mark_buffer_dirty(bh);
				} else if (!buffer_uptodate(bh))
					partial = true;
				bh_off = bh_next;
			} while (head != (bh = bh->b_this_page));
		}

		zero_user_segment(page, from, to);

		if (!partial) {
			if (!PageUptodate(page))
				SetPageUptodate(page);
			set_page_dirty(page);
		}

unlock_page:
		if (idx != idx0) {
			unlock_page(page);
			put_page(page);
		}
	}

	mark_inode_dirty(inode);
}

struct ntfs_file_vm_ops {
	atomic_t refcnt;
	loff_t to;

	const struct vm_operations_struct *base;
	struct vm_operations_struct vm_ops;
};

/*
 * vm_operations_struct::open
 */
static void ntfs_filemap_open(struct vm_area_struct *vma)
{
	struct ntfs_file_vm_ops *vm_ops;
	const struct vm_operations_struct *base;

	vm_ops = container_of(vma->vm_ops, struct ntfs_file_vm_ops, vm_ops);
	base = vm_ops->base;

	atomic_inc(&vm_ops->refcnt);

	if (base->open)
		base->open(vma);
}

/*
 * vm_operations_struct::close
 */
static void ntfs_filemap_close(struct vm_area_struct *vma)
{
	struct ntfs_file_vm_ops *vm_ops;
	const struct vm_operations_struct *base;
	struct inode *inode;
	ntfs_inode *ni;
	// unsigned long flags;

	vm_ops = container_of(vma->vm_ops, struct ntfs_file_vm_ops, vm_ops);

	if (!atomic_dec_and_test(&vm_ops->refcnt))
		return;

	base = vm_ops->base;
	if (!(vma->vm_flags & VM_WRITE))
		goto close_base;

	inode = file_inode(vma->vm_file);
	ni = ntfs_i(inode);

	// Update valid size
	// write_lock_irqsave( &ni->rwlock, flags );
	ni->i_valid = max_t(loff_t, ni->i_valid,
			    min_t(loff_t, i_size_read(inode), vm_ops->to));
	// write_unlock_irqrestore( &u->rwlock, flags );

close_base:
	if (base->close)
		base->close(vma);

	vma->vm_ops = base;
	ntfs_free(vm_ops);
}

/*
 * vm_operations_struct::fault
 */
static vm_fault_t ntfs_filemap_fault(struct vm_fault *vmf)
{
	vm_fault_t ret;
	struct ntfs_file_vm_ops *vm_ops;
	struct vm_area_struct *vma = vmf->vma;

	vm_ops = container_of(vma->vm_ops, struct ntfs_file_vm_ops, vm_ops);

	/* Call base function */
	ret = vm_ops->base->fault(vmf);

	if (VM_FAULT_LOCKED & ret) {
		/* Update maximum mapped range */
		loff_t to = (loff_t)(vmf->pgoff + 1) << PAGE_SHIFT;

		if (vm_ops->to < to)
			vm_ops->to = to;
	}

	return ret;
}

/*
 * file_operations::mmap
 */
static int ntfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_mapping->host;
	ntfs_inode *ni = ntfs_i(inode);
	u64 to, from = ((u64)vma->vm_pgoff << PAGE_SHIFT);
	bool rw = vma->vm_flags & VM_WRITE;
	struct ntfs_file_vm_ops *vm_ops = NULL;
	int err;

	if (is_encrypted(ni)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!rw)
		goto generic;

	if (is_compressed(ni)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/*
	 * Allocate and init small struct to keep track the mapping operations
	 * It is useful when mmap(size) + truncate(size/2) + unmap(). see
	 * xfstests/generic/039
	 */
	vm_ops = ntfs_alloc(sizeof(struct ntfs_file_vm_ops), 1);
	if (unlikely(!vm_ops)) {
		err = -ENOMEM;
		goto out;
	}

	// map for write
	inode_lock(inode);

	to = from + vma->vm_end - vma->vm_start;

	if (to > inode->i_size)
		to = inode->i_size;

	if (is_sparsed(ni)) {
		/* allocate clusters for rw map */
		ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
		CLST vcn, lcn, len;
		CLST end = bytes_to_cluster(sbi, to);
		bool new;

		for (vcn = from >> sbi->cluster_bits; vcn < end; vcn += len) {
			err = attr_data_get_block(ni, vcn, &lcn, &len, &new);
			if (err) {
				inode_unlock(inode);
				goto out;
			}
			if (!new)
				continue;
			ntfs_sparse_cluster(inode, NULL,
					    (u64)vcn << sbi->cluster_bits,
					    sbi->cluster_size);
		}
	}

	err = ni->i_valid < to ?
		      ntfs_extend_initialized_size(file, ni, ni->i_valid, to) :
		      0;

	inode_unlock(inode);
	if (err)
		goto out;

generic:
	err = generic_file_mmap(file, vma);
	if (err)
		goto out;

	if (rw) {
		atomic_set(&vm_ops->refcnt, 1);
		vm_ops->to = to;
		vm_ops->base = vma->vm_ops;
		memcpy(&vm_ops->vm_ops, vma->vm_ops,
		       sizeof(struct vm_operations_struct));
		vm_ops->vm_ops.fault = &ntfs_filemap_fault;
		vm_ops->vm_ops.open = &ntfs_filemap_open;
		vm_ops->vm_ops.close = &ntfs_filemap_close;
		vma->vm_ops = &vm_ops->vm_ops;
	}

out:
	if (err)
		ntfs_free(vm_ops);

	return err;
}

/*
 * file_operations::fsync
 */
int ntfs_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	return generic_file_fsync(filp, start, end, datasync);
}

static int ntfs_extend_ex(struct inode *inode, loff_t pos, size_t count,
			  struct file *file)
{
	ntfs_inode *ni = ntfs_i(inode);
	struct address_space *mapping = inode->i_mapping;
	loff_t end = pos + count;
	int err;
	bool extend_init = file && pos > ni->i_valid;

	if (end <= inode->i_size && !extend_init)
		return 0;

	/*mark rw ntfs as dirty. it will be cleared at umount*/
	ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_DIRTY);

	if (end > inode->i_size) {
		err = ntfs_set_size(inode, end);
		if (err)
			goto out;
		inode->i_size = end;
	}

	if (extend_init) {
		err = (is_compressed(ni) ? ntfs_extend_initialized_size_cmpr :
					   ntfs_extend_initialized_size)(
			file, ni, ni->i_valid, pos);
		if (err)
			goto out;
	}

	inode->i_ctime = inode->i_mtime = current_time(inode);
	ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
	mark_inode_dirty(inode);

	if (IS_SYNC(inode)) {
		int err2;

		err = filemap_fdatawrite_range(mapping, pos, end - 1);
		err2 = sync_mapping_buffers(mapping);
		if (!err)
			err = err2;
		err2 = write_inode_now(inode, 1);
		if (!err)
			err = err2;
		if (!err)
			err = filemap_fdatawait_range(mapping, pos, end - 1);
	}

out:
	return err;
}

/*
 * Preallocate space for a file. This implements ntfs's fallocate file
 * operation, which gets called from sys_fallocate system call. User
 * space requests len bytes at offset. If FALLOC_FL_KEEP_SIZE is set
 * we just allocate clusters without zeroing them out. Otherwise we
 * allocate and zero out clusters via an expanding truncate.
 */
static long ntfs_fallocate(struct file *file, int mode, loff_t offset,
			   loff_t len)
{
	struct inode *inode = file->f_mapping->host;
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	loff_t end;
	int err;

	/* No support for dir */
	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	/* Return error if mode is not supported */
	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
		     FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |
		     FALLOC_FL_INSERT_RANGE))
		return -EOPNOTSUPP;

	inode_lock(inode);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (!(mode & FALLOC_FL_KEEP_SIZE)) {
			err = -EINVAL;
			goto out;
		}
		/*TODO*/
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		if (mode & ~FALLOC_FL_COLLAPSE_RANGE) {
			err = -EINVAL;
			goto out;
		}

		/*TODO*/
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mode & FALLOC_FL_INSERT_RANGE) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mode & FALLOC_FL_ZERO_RANGE) {
		err = -EOPNOTSUPP;
		goto out;
	}

	end = offset + len;
	if (mode & FALLOC_FL_KEEP_SIZE) {
		/* Start the allocation.We are not zeroing out the clusters */
		err = ntfs_set_size(inode, bytes_to_cluster(sbi, end));
		goto out;
	}

	err = 0;

	if (end <= i_size_read(inode))
		goto out;

	/*
	 * Allocate clusters but does not change 'valid'
	 */
	err = ntfs_extend_ex(inode, offset, len, NULL);

out:
	if (err == -EFBIG)
		err = -ENOSPC;

	inode_unlock(inode);
	return err;
}

void ntfs_truncate_blocks(struct inode *inode, loff_t new_size)
{
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	ntfs_inode *ni = ntfs_i(inode);
	int err, dirty = 0;
	u32 vcn;
	u64 new_valid;

	if (!S_ISREG(inode->i_mode))
		return;

	vcn = bytes_to_cluster(sbi, new_size);
	new_valid = ntfs_up_block(sb, min(ni->i_valid, new_size));

	ni_lock(ni);
	down_write(&ni->file.run_lock);

	truncate_setsize(inode, new_size);

	err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run, new_size,
			    &new_valid, true, NULL);

	if (new_valid < ni->i_valid)
		ni->i_valid = new_valid;

	up_write(&ni->file.run_lock);
	ni_unlock(ni);

	ni->std_fa |= FILE_ATTRIBUTE_ARCHIVE;
	inode->i_ctime = inode->i_mtime = current_time(inode);
	if (!IS_DIRSYNC(inode)) {
		dirty = 1;
	} else {
		err = ntfs_sync_inode(inode);
		if (err)
			return;
	}

	inode->i_blocks = vcn;

	if (dirty)
		mark_inode_dirty(inode);

	/*ntfs_flush_inodes(inode->i_sb, inode, NULL);*/
}

/*
 * inode_operations::setattr
 */
int ntfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct super_block *sb = dentry->d_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode = d_inode(dentry);
	ntfs_inode *ni = ntfs_i(inode);
	u32 ia_valid = attr->ia_valid;
	umode_t mode = inode->i_mode;
	int err;

	if (sbi->options.no_acs_rules) {
		/* "no access rules" - force any changes of time etc. */
		attr->ia_valid |= ATTR_FORCE;
		/* and disable for editing some attributes */
		attr->ia_valid &= ~(ATTR_UID | ATTR_GID | ATTR_MODE);
		ia_valid = attr->ia_valid;
	}

	err = setattr_prepare(dentry, attr);
	if (err) {
		if (sbi->options.quiet)
			err = 0;
		goto out;
	}

	if (ia_valid & ATTR_SIZE) {
		loff_t oldsize = inode->i_size;

		inode_dio_wait(inode);

		if (attr->ia_size < oldsize) {
			err = block_truncate_page(inode->i_mapping,
						  attr->ia_size,
						  ntfs_get_block);
			if (err)
				goto out;
			ntfs_truncate_blocks(inode, attr->ia_size);
		} else if (attr->ia_size > oldsize) {
			err = ntfs_extend_ex(inode, attr->ia_size, 0, NULL);
			if (err)
				goto out;
		}

		ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
	}

	setattr_copy(inode, attr);

	if (mode != inode->i_mode) {
		err = ntfs_acl_chmod(inode);
		if (err)
			goto out;

		/* linux 'w' -> windows 'ro' */
		if (0222 & inode->i_mode)
			ni->std_fa &= ~FILE_ATTRIBUTE_READONLY;
		else
			ni->std_fa |= FILE_ATTRIBUTE_READONLY;
	}

	mark_inode_dirty(inode);
out:

	return err;
}

static ssize_t ntfs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t err;
	size_t count = iov_iter_count(iter);
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ntfs_inode *ni = ntfs_i(inode);

	if (is_encrypted(ni))
		return -EOPNOTSUPP;

	if (is_dedup(ni))
		return -EOPNOTSUPP;

	err = count ? generic_file_read_iter(iocb, iter) : 0;

	return err;
}

/*
 * on error we return an unlocked page and the error value
 * on success we return a locked page and 0
 */
static int prepare_uptodate_page(struct inode *inode, struct page *page,
				 u64 pos, bool force_uptodate)
{
	int err = 0;

	if (((pos & (PAGE_SIZE - 1)) || force_uptodate) &&
	    !PageUptodate(page)) {
		err = ntfs_readpage(NULL, page);
		if (err)
			return err;
		lock_page(page);
		if (!PageUptodate(page)) {
			unlock_page(page);
			return -EIO;
		}
		if (page->mapping != inode->i_mapping) {
			unlock_page(page);
			return -EAGAIN;
		}
	}
	return 0;
}

/*helper for ntfs_file_write_iter (compressed files)*/
static noinline ssize_t ntfs_compress_write(struct kiocb *iocb,
					    struct iov_iter *from)
{
	int err;
	struct file *file = iocb->ki_filp;
	size_t count = iov_iter_count(from);
	loff_t pos = iocb->ki_pos;
	loff_t end = pos + count;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	ntfs_inode *ni = ntfs_i(inode);
	// struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct page *page, **pages = NULL;
	size_t ip, max_pages, written = 0;
	bool force_uptodate = false;
	pgoff_t from_idx, end_idx;
	u32 off;
	gfp_t mask = mapping_gfp_constraint(mapping, ~__GFP_FS) | __GFP_WRITE;

	from_idx = pos >> PAGE_SHIFT;
	end_idx = (end + PAGE_SIZE - 1) >> PAGE_SHIFT;
	max_pages = end_idx - from_idx;
	if (max_pages > 16)
		max_pages = 16;
	WARN_ON(end_idx <= from_idx);

	pages = ntfs_alloc(max_pages * sizeof(struct page *), 1);
	if (!pages)
		return -ENOMEM;

	current->backing_dev_info = inode_to_bdi(inode);
	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	while (count) {
		pgoff_t index = pos >> PAGE_SHIFT;
		size_t offset = offset_in_page(pos);
		size_t bytes = max_pages * PAGE_SIZE - offset;
		size_t wpages, copied;

		if (bytes > count)
			bytes = count;

		wpages = (offset + bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;

		WARN_ON(wpages > max_pages);

		if (unlikely(iov_iter_fault_in_readable(from, bytes))) {
			err = -EFAULT;
			goto out;
		}

		for (ip = 0; ip < wpages; ip++) {
again:
			page = find_or_create_page(mapping, index + ip, mask);
			if (!page) {
				err = -ENOMEM;
fail:
				while (ip--) {
					page = pages[ip];
					unlock_page(page);
					put_page(page);
				}

				goto out;
			}

			pages[ip] = page;

			if (!ip)
				err = prepare_uptodate_page(inode, page, pos,
							    force_uptodate);

			if (!err && ip == wpages - 1)
				err = prepare_uptodate_page(inode, page,
							    pos + bytes, false);

			if (err) {
				put_page(page);
				if (err == -EAGAIN) {
					err = 0;
					goto again;
				}
				goto fail;
			}
			wait_on_page_writeback(page);
		}

		WARN_ON(!bytes);
		copied = 0;
		ip = 0;
		off = offset_in_page(pos);

		for (;;) {
			size_t tail = PAGE_SIZE - off;
			size_t count = min(tail, bytes);
			size_t cp;

			page = pages[ip];

			cp = iov_iter_copy_from_user_atomic(page, from, off,
							    count);

			flush_dcache_page(page);

			if (!PageUptodate(page) && cp < count)
				cp = 0;

			iov_iter_advance(from, cp);
			copied += cp;
			bytes -= cp;
			if (!bytes || !cp)
				break;

			if (cp < tail)
				off += cp;
			else {
				ip++;
				off = 0;
			}
		}

		if (!copied)
			force_uptodate = true;
		else {
			size_t dpages;

			force_uptodate = false;
			dpages =
				(offset + copied + PAGE_SIZE - 1) >> PAGE_SHIFT;

			for (ip = 0; ip < dpages; ip++) {
				page = pages[ip];
				SetPageUptodate(page);
				ClearPageChecked(page);
				set_page_dirty(page);
			}
		}

		for (ip = 0; ip < wpages; ip++) {
			page = pages[ip];
			ClearPageChecked(page);
			unlock_page(page);
			put_page(page);
		}

		cond_resched();

		balance_dirty_pages_ratelimited(mapping);

		pos += copied;
		written += copied;

		count = iov_iter_count(from);
	}

out:
	ntfs_free(pages);

	current->backing_dev_info = NULL;

	if (err < 0)
		return err;

	iocb->ki_pos += written;
	if (iocb->ki_pos > ni->i_valid)
		ni->i_valid = iocb->ki_pos;

	return written;
}

/*
 * file_operations::write_iter
 */
static ssize_t ntfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;
	ntfs_inode *ni = ntfs_i(inode);

	if (is_encrypted(ni)) {
		ntfs_inode_warning(inode, "encrypted i/o not supported");
		return -EOPNOTSUPP;
	}

	if (is_compressed(ni) && (iocb->ki_flags & IOCB_DIRECT)) {
		ntfs_inode_warning(inode,
				   "direct i/o + compressed not supported");
		return -EOPNOTSUPP;
	}

	if (ni->ni_flags & NI_FLAG_COMPRESSED_MASK) {
		ntfs_inode_warning(
			inode,
			"write into external compressed file not supported (temporary)");
		return -EOPNOTSUPP;
	}

	if (is_dedup(ni)) {
		ntfs_inode_warning(inode,
				   "write into deduplicated not supported");
		return -EOPNOTSUPP;
	}

	if (!inode_trylock(inode)) {
		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;
		inode_lock(inode);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	ret = ntfs_extend_ex(inode, iocb->ki_pos, ret, file);
	if (ret)
		goto out;

	ret = is_compressed(ni) ? ntfs_compress_write(iocb, from) :
				  __generic_file_write_iter(iocb, from);

out:
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);

	return ret;
}

/*
 * file_operations::open
 */
int ntfs_file_open(struct inode *inode, struct file *file)
{
	ntfs_inode *ni = ntfs_i(inode);

	if (unlikely((is_compressed(ni) || is_encrypted(ni)) &&
		     (file->f_flags & O_DIRECT))) {
		return -ENOTBLK;
	}

	return generic_file_open(inode, file);
}

#ifdef NTFS3_PREALLOCATE
/*
 * file_operations::release
 */
static int ntfs_file_release(struct inode *inode, struct file *file)
{
	ntfs_inode *ni = ntfs_i(inode);
	int err;

	/* if we are the last writer on the inode, drop the block reservation */
	if (!(file->f_mode & FMODE_WRITE) ||
	    atomic_read(&inode->i_writecount) != 1)
		return 0;

	ni_lock(ni);

	err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run,
			    inode->i_size, &ni->i_valid, false, NULL);

	ni_unlock(ni);

	/*congestion_wait(BLK_RW_ASYNC, HZ / 10);*/

	return err;
}
#endif

const struct inode_operations ntfs_file_inode_operations = {
	.getattr = ntfs_getattr,
	.setattr = ntfs_setattr,
	.listxattr = ntfs_listxattr,
	.permission = ntfs_permission,
	.get_acl = ntfs_get_acl,
	.set_acl = ntfs_set_acl,
};

const struct file_operations ntfs_file_operations = {
	.llseek = generic_file_llseek,
	.read_iter = ntfs_file_read_iter,
	.write_iter = ntfs_file_write_iter,
	.unlocked_ioctl = ntfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ntfs_compat_ioctl,
#endif
	.splice_read = generic_file_splice_read,
	.mmap = ntfs_file_mmap,
	.open = ntfs_file_open,
	.fsync = ntfs_file_fsync,
	.splice_write = iter_file_splice_write,
	.fallocate = ntfs_fallocate,
#ifdef NTFS3_PREALLOCATE
	.release = ntfs_file_release,
#endif
};
