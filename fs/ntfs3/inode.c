// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ntfs3/inode.c
 *
 * Copyright (C) 2019-2020 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/iversion.h>
#include <linux/mpage.h>
#include <linux/nls.h>
#include <linux/uio.h>
#include <linux/version.h>
#include <linux/writeback.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * ntfs_read_mft
 *
 * reads record and parses MFT
 */
static struct inode *ntfs_read_mft(struct inode *inode,
				   const struct cpu_str *name,
				   const MFT_REF *ref)
{
	int err = 0;
	ntfs_inode *ni = ntfs_i(inode);
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	mode_t mode = 0;
	ATTR_STD_INFO5 *std5 = NULL;
	ATTR_LIST_ENTRY *le;
	ATTRIB *attr;
	bool is_encrypted = false;
	bool is_match = false;
	bool is_root = false;
	bool is_dir;
	unsigned long ino = inode->i_ino;
	u32 rp_fa = 0, asize, t32;
	u16 roff, rsize, names = 0;
	const ATTR_FILE_NAME *fname = NULL;
	const INDEX_ROOT *root;
	REPARSE_DATA_BUFFER rp; // 0x18 bytes
	u64 t64;
	MFT_REC *rec;
	struct runs_tree *run;

	inode->i_op = NULL;

	err = mi_init(&ni->mi, sbi, ino);
	if (err)
		goto out;

	if (!sbi->mft.ni && ino == MFT_REC_MFT && !sb->s_root) {
		t64 = sbi->mft.lbo >> sbi->cluster_bits;
		t32 = bytes_to_cluster(sbi, MFT_REC_VOL * sbi->record_size);
		sbi->mft.ni = ni;
		init_rwsem(&ni->file.run_lock);

		if (!run_add_entry(&ni->file.run, 0, t64, t32)) {
			err = -ENOMEM;
			goto out;
		}
	}

	err = mi_read(&ni->mi, ino == MFT_REC_MFT);

	if (err)
		goto out;

	rec = ni->mi.mrec;

	if (sbi->flags & NTFS_FLAGS_LOG_REPLAING)
		;
	else if (ref->seq != rec->seq) {
		err = -EINVAL;
		ntfs_error(sb, "MFT: r=%lx, expect seq=%x instead of %x!", ino,
			   le16_to_cpu(ref->seq), le16_to_cpu(rec->seq));
		goto out;
	} else if (!is_rec_inuse(rec)) {
		err = -EINVAL;
		ntfs_error(sb, "Inode r=%x is not in use!", (u32)ino);
		goto out;
	}

	if (le32_to_cpu(rec->total) != sbi->record_size) {
		// bad inode?
		err = -EINVAL;
		goto out;
	}

	if (!is_rec_base(rec))
		goto Ok;

	/* record should contain $I30 root */
	is_dir = rec->flags & RECORD_FLAG_DIR;

	inode->i_generation = le16_to_cpu(rec->seq);

	/* Enumerate all struct Attributes MFT */
	le = NULL;
	attr = NULL;
next_attr:
	err = -EINVAL;
	attr = ni_enum_attr_ex(ni, attr, &le);
	if (!attr)
		goto end_enum;

	if (le && le->vcn) {
		if (ino == MFT_REC_MFT && attr->type == ATTR_DATA) {
			run = &ni->file.run;
			asize = le32_to_cpu(attr->size);
			goto attr_unpack_run;
		}
		goto next_attr;
	}

	roff = attr->non_res ? 0 : le16_to_cpu(attr->res.data_off);
	rsize = attr->non_res ? 0 : le32_to_cpu(attr->res.data_size);
	asize = le32_to_cpu(attr->size);

	if (attr->type != ATTR_STD)
		goto check_list;

	if (attr->non_res)
		goto out;

	if (asize < sizeof(ATTR_STD_INFO) + roff)
		goto out;
	if (rsize < sizeof(ATTR_STD_INFO))
		goto out;

	if (std5)
		goto next_attr;

	std5 = Add2Ptr(attr, roff);

#ifdef STATX_BTIME
	nt2kernel(std5->cr_time, &ni->i_crtime);
#endif
	nt2kernel(std5->a_time, &inode->i_atime);
	nt2kernel(std5->c_time, &inode->i_ctime);
	nt2kernel(std5->m_time, &inode->i_mtime);

	ni->std_fa = std5->fa;

	if (asize < sizeof(ATTR_STD_INFO5) + roff)
		goto next_attr;
	if (rsize < sizeof(ATTR_STD_INFO5))
		goto next_attr;

	ni->std_security_id = std5->security_id;
	goto next_attr;

check_list:
	if (attr->type != ATTR_LIST)
		goto check_name;

	if (attr->name_len)
		goto out;

	if (le)
		goto out;

	if (ino == MFT_REC_LOG)
		goto out;

	err = ntfs_load_attr_list(ni, attr);
	if (err)
		goto out;

	le = NULL;
	attr = NULL;
	goto next_attr;

check_name:
	if (attr->type != ATTR_NAME)
		goto check_data;

	if (attr->non_res)
		goto out;
	if (asize < SIZEOF_ATTRIBUTE_FILENAME + roff)
		goto out;
	if (rsize < SIZEOF_ATTRIBUTE_FILENAME)
		goto out;

	fname = Add2Ptr(attr, roff);
	if (fname->type == FILE_NAME_DOS)
		goto next_attr;

	names += 1;
	if (!name || name->len != fname->name_len)
		goto next_attr;

	if (!ntfs_cmp_names_cpu(name, (struct le_str *)&fname->name_len, NULL))
		is_match = true;

	goto next_attr;

check_data:
	run = NULL;
	if (attr->type != ATTR_DATA)
		goto check_root;

	if (is_dir)
		goto next_attr;

	if (ino == MFT_REC_BADCLUST && !attr->non_res)
		goto next_attr;

	if (!attr->name_len)
		goto check_data_attr;

	if ((ino != MFT_REC_BADCLUST || !attr->non_res ||
	     attr->name_len != ARRAY_SIZE(BAD_NAME) ||
	     memcmp(attr_name(attr), BAD_NAME, sizeof(BAD_NAME))) &&
	    (ino != MFT_REC_SECURE || !attr->non_res ||
	     attr->name_len != ARRAY_SIZE(SDS_NAME) ||
	     memcmp(attr_name(attr), SDS_NAME, sizeof(SDS_NAME)))) {
		goto next_attr;
	}

	/* $Secure::SDS, $BadClus::$Bad */
check_data_attr:

	if (is_attr_sparsed(attr))
		ni->std_fa |= FILE_ATTRIBUTE_SPARSE_FILE;
	else
		ni->std_fa &= ~FILE_ATTRIBUTE_SPARSE_FILE;

	if (is_attr_compressed(attr))
		ni->std_fa |= FILE_ATTRIBUTE_COMPRESSED;
	else
		ni->std_fa &= ~FILE_ATTRIBUTE_COMPRESSED;

	if (is_attr_encrypted(attr))
		ni->std_fa |= FILE_ATTRIBUTE_ENCRYPTED;
	else
		ni->std_fa &= ~FILE_ATTRIBUTE_ENCRYPTED;

	if (!attr->non_res) {
		ni->i_valid = inode->i_size = rsize;
		inode_set_bytes(inode, rsize);
		t32 = asize;
	} else {
		t32 = le16_to_cpu(attr->nres.run_off);
	}

	if (sbi->options.fmask) {
		/* use mount options "fmask" or "umask" */
		mode = S_IFREG | (0777 & sbi->options.fs_fmask);
	} else {
		/* by default ~(current->fs->umask) */
		mode = S_IFREG | (0777 & sbi->options.fs_fmask);
	}

	if (!attr->non_res) {
		ni->ni_flags |= NI_FLAG_RESIDENT;
		goto next_attr;
	}

	inode_set_bytes(inode, attr_ondisk_size(attr));

	ni->i_valid = le64_to_cpu(attr->nres.valid_size);
	inode->i_size = le64_to_cpu(attr->nres.data_size);
	if (!attr->nres.alloc_size)
		goto next_attr;

	run = ino == MFT_REC_BITMAP ? &sbi->used.bitmap.run : &ni->file.run;
	goto attr_unpack_run;

check_root:
	if (attr->type != ATTR_ROOT)
		goto check_alloc;

	if (attr->non_res)
		goto out;

	root = Add2Ptr(attr, roff);
	is_root = true;

	if (attr->name_len != ARRAY_SIZE(I30_NAME))
		goto next_attr;

	if (memcmp(attr_name(attr), I30_NAME, sizeof(I30_NAME)))
		goto next_attr;

	if (root->type != ATTR_NAME ||
	    root->rule != NTFS_COLLATION_TYPE_FILENAME)
		goto out;

	if (!is_dir)
		goto next_attr;

	ni->ni_flags |= NI_FLAG_DIR;

	err = indx_init(&ni->dir, sbi, attr, INDEX_MUTEX_I30);
	if (err)
		goto out;

	if (sbi->options.dmask) {
		/* use mount options "dmask" or "umask" */
		mode = S_IFDIR | (0777 & sbi->options.fs_dmask);
	} else if (!sb->s_root) {
		/* Read root inode while mounting */
		mode = S_IFDIR | 0777;
	} else {
		/* by default ~(current->fs->umask) */
		mode = S_IFDIR | (0777 & sbi->options.fs_dmask);
	}

	goto next_attr;

check_alloc:
	if (attr->type != ATTR_ALLOC)
		goto check_bitmap;
	if (!is_root)
		goto next_attr;

	if (attr->name_len != ARRAY_SIZE(I30_NAME))
		goto next_attr;

	if (memcmp(attr_name(attr), I30_NAME, sizeof(I30_NAME)))
		goto next_attr;

	inode->i_size = le64_to_cpu(attr->nres.data_size);
	ni->i_valid = le64_to_cpu(attr->nres.valid_size);
	inode_set_bytes(inode, le64_to_cpu(attr->nres.alloc_size));

	run = &ni->dir.alloc_run;
	goto attr_unpack_run;

check_bitmap:
	if (attr->type != ATTR_BITMAP)
		goto check_reparse;

	if (ino != MFT_REC_MFT)
		goto check_dir_bitmap;

	if (!attr->non_res)
		goto out;
#ifndef NTFS3_64BIT_CLUSTER
	/* 0x20000000 = 2^32 / 8 */
	if (le64_to_cpu(attr->nres.alloc_size) >= 0x20000000)
		goto out;
#endif
	run = &sbi->mft.bitmap.run;
	goto attr_unpack_run;

check_dir_bitmap:
	if (!is_dir)
		goto next_attr;

	if (attr->name_len != ARRAY_SIZE(I30_NAME))
		goto next_attr;
	if (memcmp(attr_name(attr), I30_NAME, sizeof(I30_NAME)))
		goto next_attr;

	if (!attr->non_res)
		goto next_attr;
	run = &ni->dir.bitmap_run;
	goto attr_unpack_run;

check_reparse:
	if (attr->type != ATTR_REPARSE)
		goto check_ea;

	if (attr->name_len)
		goto next_attr;

	rp_fa = ni_parse_reparse(ni, attr, &rp);
	switch (rp_fa) {
	case REPARSE_LINK:
		if (!attr->non_res) {
			inode->i_size = rsize;
			inode_set_bytes(inode, rsize);
			t32 = asize;
		} else {
			inode->i_size = le64_to_cpu(attr->nres.data_size);
			t32 = le16_to_cpu(attr->nres.run_off);
		}

		/* Looks like normal symlink */
		ni->i_valid = inode->i_size;

		/* Clear directory bit */
		if (ni->ni_flags & NI_FLAG_DIR) {
			indx_clear(&ni->dir);
			memset(&ni->dir, 0, sizeof(ni->dir));
			ni->ni_flags &= ~NI_FLAG_DIR;
		} else {
			run_close(&ni->file.run);
		}
		mode = S_IFLNK | 0777;
		is_dir = false;
		if (attr->non_res) {
			run = &ni->file.run;
			goto attr_unpack_run;
		}
		break;

	case REPARSE_COMPRESSED:
		break;

	case REPARSE_DEDUPLICATED:
		break;
	}
	goto next_attr;

check_ea:
	if (attr->type != ATTR_EA_INFO)
		goto check_logged;

	if (!attr->name_len)
		ni->ni_flags |= NI_FLAG_EA;
	goto next_attr;

check_logged:
	if (attr->type != ATTR_LOGGED_UTILITY_STREAM)
		goto next_attr;

	if (attr->name_len != ARRAY_SIZE(EFS_NAME))
		goto next_attr;
	if (memcmp(EFS_NAME, attr_name(attr), sizeof(EFS_NAME)))
		goto next_attr;
	is_encrypted = true;
	goto next_attr;

attr_unpack_run:
	if (!run)
		goto next_attr;

	roff = le16_to_cpu(attr->nres.run_off);

	err = run_unpack_ex(run, sbi, ino, le64_to_cpu(attr->nres.svcn),
			    le64_to_cpu(attr->nres.evcn), Add2Ptr(attr, roff),
			    asize - roff);
	if (err < 0)
		goto out;
	err = 0;
	goto next_attr;

end_enum:

	if (!std5)
		goto out;

	if (!is_match && name) {
		/* reuse rec as buffer for ascii name */
		err = -ENOENT;
		goto out;
	}

	if (std5->fa & FILE_ATTRIBUTE_READONLY)
		mode &= ~0222;

	/* Setup 'uid' and 'gid' */
	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;

	if (!names) {
		err = -EINVAL;
		goto out;
	}

	if (S_ISDIR(mode)) {
		ni->std_fa |= FILE_ATTRIBUTE_DIRECTORY;

		/*
		 * dot and dot-dot should be included in count but was not
		 * included in enumeration.
		 * Usually a hard links to directories are disabled
		 */
		set_nlink(inode, 1);
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = &ntfs_dir_operations;
		ni->i_valid = 0;
	} else if (S_ISLNK(mode)) {
		ni->std_fa &= ~FILE_ATTRIBUTE_DIRECTORY;
		inode->i_op = &ntfs_link_inode_operations;
		inode->i_fop = NULL;
		inode_nohighmem(inode); // ??
		set_nlink(inode, names);
	} else if (S_ISREG(mode)) {
		ni->std_fa &= ~FILE_ATTRIBUTE_DIRECTORY;

		set_nlink(inode, names);

		inode->i_op = &ntfs_file_inode_operations;
		inode->i_fop = &ntfs_file_operations;
		inode->i_mapping->a_ops =
			is_compressed(ni) ? &ntfs_aops_cmpr : &ntfs_aops;

		if (ino != MFT_REC_MFT)
			init_rwsem(&ni->file.run_lock);
	} else if (fname && fname->home.low == cpu_to_le32(MFT_REC_EXTEND) &&
		   fname->home.seq == cpu_to_le16(MFT_REC_EXTEND)) {
		/* Records in $Extend are not a files or general directories */
	} else {
		err = -EINVAL;
		goto out;
	}

	if ((sbi->options.sys_immutable &&
	     (std5->fa & FILE_ATTRIBUTE_SYSTEM)) &&
	    !S_ISFIFO(mode) && !S_ISSOCK(mode) && !S_ISLNK(mode)) {
		inode->i_flags |= S_IMMUTABLE;
	} else {
		inode->i_flags &= ~S_IMMUTABLE;
	}

	inode->i_mode = mode;

Ok:
	if (ino == MFT_REC_MFT && !sb->s_root)
		sbi->mft.ni = NULL;

	unlock_new_inode(inode);

	return inode;

out:
	if (ino == MFT_REC_MFT && !sb->s_root)
		sbi->mft.ni = NULL;

	iget_failed(inode);

	return ERR_PTR(err);
}

/* returns 1 if match */
static int ntfs_test_inode(struct inode *inode, const MFT_REF *ref)
{
	return ino_get(ref) == inode->i_ino &&
	       ref->seq == ntfs_i(inode)->mi.mrec->seq;
}

static int ntfs_set_inode(struct inode *inode, const MFT_REF *ref)
{
	inode->i_ino = ino_get(ref);

	return 0;
}

struct inode *ntfs_iget5(struct super_block *sb, const MFT_REF *ref,
			 const struct cpu_str *name)
{
	struct inode *inode;

	inode = iget5_locked(sb, ino_get(ref),
			     (int (*)(struct inode *, void *))ntfs_test_inode,
			     (int (*)(struct inode *, void *))ntfs_set_inode,
			     (void *)ref);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);

	/* If this is a freshly allocated inode, need to read it now. */
	if (!(inode->i_state & I_NEW))
		return inode;

	return ntfs_read_mft(inode, name, ref);
}

enum get_block_ctx {
	GET_BLOCK_GENERAL = 0,
	GET_BLOCK_WRITE_BEGIN = 1,
	GET_BLOCK_DIRECT_IO_R = 2,
	GET_BLOCK_DIRECT_IO_W = 3,
	GET_BLOCK_BMAP = 4,
};

static noinline int ntfs_get_block_vbo(struct inode *inode, u64 vbo,
				       struct buffer_head *bh, int create,
				       enum get_block_ctx ctx)
{
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	ntfs_inode *ni = ntfs_i(inode);
	struct page *page;
	u64 bytes, pbo;
	u32 off;
	int err;
	CLST vcn, lcn, len;
	u8 cluster_bits = sbi->cluster_bits;
	bool new;

	/*clear previous state*/
	clear_buffer_new(bh);
	clear_buffer_uptodate(bh);

	/* direct write uses 'create=0'*/
	if (!create && vbo >= ni->i_valid && ctx != GET_BLOCK_DIRECT_IO_W)
		return 0;

	vcn = vbo >> cluster_bits;
	off = vbo & sbi->cluster_mask;
	new = false;

	err = attr_data_get_block(ni, vcn, &lcn, &len, create ? &new : NULL);
	if (err)
		goto out;

	bytes = ((u64)len << cluster_bits) - off;

	if (lcn == SPARSE_LCN) {
		if (!create) {
			if (bh->b_size > bytes)
				bh->b_size = bytes;

			return 0;
		}
		WARN_ON(1);
	}

	WARN_ON(lcn == RESIDENT_LCN);

	if (new) {
		set_buffer_new(bh);
		ntfs_sparse_cluster(inode, bh->b_page,
				    (loff_t)vcn << sbi->cluster_bits,
				    sbi->cluster_size);
	}

	pbo = ((u64)lcn << cluster_bits) + off;

	set_buffer_mapped(bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_blocknr = pbo >> sb->s_blocksize_bits;

	if (ctx == GET_BLOCK_DIRECT_IO_W) {
		/*ntfs_direct_IO will update ni->i_valid */
		if (vbo >= ni->i_valid)
			set_buffer_new(bh);
	} else if (create && ctx == GET_BLOCK_WRITE_BEGIN &&
		   vbo + bh->b_size > ni->i_valid) {
		u32 voff = ni->i_valid > vbo ? (ni->i_valid - vbo) : 0;

		off = bh_offset(bh);
		page = bh->b_page;

		zero_user_segment(page, off + voff, off + bh->b_size);
		set_buffer_uptodate(bh);
		ni->i_valid = vbo + bh->b_size;

		/* ntfs_write_end will update ni->i_valid*/
	} else if (create) {
		/*normal write*/
		if (vbo >= ni->i_valid) {
			set_buffer_new(bh);
			if (bytes > bh->b_size)
				bytes = bh->b_size;
			ni->i_valid = vbo + bytes;
			mark_inode_dirty(inode);
		}
	} else if (vbo >= ni->i_valid) {
		/* read out of valid data*/
		/* should never be here 'cause already checked */
		clear_buffer_mapped(bh);
	} else if (vbo + bytes <= ni->i_valid) {
		/* normal read */
	} else {
		/* here: vbo <= ni->i_valid && ni->i_valid < vbo + bytes */
		u64 valid_up =
			(ni->i_valid + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);

		bytes = valid_up - vbo;
		if (bytes < sb->s_blocksize)
			bytes = sb->s_blocksize;
	}

	if (bh->b_size > bytes)
		bh->b_size = bytes;

#ifndef __LP64__
	if (ctx == GET_BLOCK_DIRECT_IO_W || ctx == GET_BLOCK_DIRECT_IO_R) {
		static_assert(sizeof(size_t) < sizeof(loff_t));
		if (bytes > 0x40000000u)
			bh->b_size = 0x40000000u;
	}
#endif

	return 0;

out:
	return err;
}

/*ntfs_readpage*/
/*ntfs_readpages*/
/*ntfs_writepage*/
/*ntfs_writepages*/
/*ntfs_block_truncate_page*/
int ntfs_get_block(struct inode *inode, sector_t vbn,
		   struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)vbn << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_GENERAL);
}

/*ntfs_bmap*/
static int ntfs_get_block_bmap(struct inode *inode, sector_t vsn,
			       struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)vsn << 9, bh_result, create,
				  GET_BLOCK_BMAP);
}

static sector_t ntfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ntfs_get_block_bmap);
}

int ntfs_readpage(struct file *file, struct page *page)
{
	int err;
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	ntfs_inode *ni = ntfs_i(inode);
	u64 vbo = (u64)page->index << PAGE_SHIFT;
	u64 valid;
	ATTRIB *attr;
	const char *data;
	u32 data_size;

	if (!ni_has_resident_data(ni))
		goto read_non_resident;

	ni_lock(ni);

	if (!ni_has_resident_data(ni)) {
		ni_unlock(ni);
		goto read_non_resident;
	}

	attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0, NULL, NULL);
	if (!attr) {
		err = -EINVAL;
		ni_unlock(ni);
		unlock_page(page);
		goto out;
	}

	WARN_ON(attr->non_res);

	vbo = page->index << PAGE_SHIFT;
	data = resident_data(attr);
	data_size = le32_to_cpu(attr->res.data_size);

	if (vbo < data_size) {
		void *kaddr = kmap_atomic(page);

		err = data_size - vbo;
		if (err > PAGE_SIZE)
			err = PAGE_SIZE;

		memcpy(kaddr, data + vbo, err);
		flush_dcache_page(page);
		kunmap_atomic(kaddr);
		zero_user_segment(page, err, PAGE_SIZE);
		SetPageUptodate(page);
	} else if (!PageUptodate(page)) {
		zero_user_segment(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
	}

	ni_unlock(ni);
	unlock_page(page);
	return 0;

read_non_resident:
	if (is_compressed(ni)) {
		ni_lock(ni);
		err = ni_readpage_cmpr(ni, page);
		ni_unlock(ni);

		return err;
	}

	/* normal + sparse files */
	err = mpage_readpage(page, ntfs_get_block);
	if (err)
		goto out;

	valid = ni->i_valid;
	if (vbo < valid && valid < vbo + PAGE_SIZE) {
		if (PageLocked(page))
			wait_on_page_bit(page, PG_locked);
		if (PageError(page)) {
			ntfs_inode_warning(inode, "file garbadge at 0x%llx",
					   valid);
			goto out;
		}
		zero_user_segment(page, valid & (PAGE_SIZE - 1), PAGE_SIZE);
	}

out:
	return err;
}

static void ntfs_readahead(struct readahead_control *rac)
{
	struct address_space *mapping = rac->mapping;
	struct inode *inode = mapping->host;
	ntfs_inode *ni = ntfs_i(inode);
	u64 valid;
	loff_t pos;

	if (ni_has_resident_data(ni))
		return;

	WARN_ON(is_compressed(ni));

	valid = ni->i_valid;
	pos = readahead_pos(rac);

	if (pos <= valid && valid < pos + readahead_length(rac))
		return;

	mpage_readahead(rac, ntfs_get_block);
}

/*ntfs_direct_IO*/
static int ntfs_get_block_direct_IO_R(struct inode *inode, sector_t iblock,
				      struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)iblock << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_DIRECT_IO_R);
}

static int ntfs_get_block_direct_IO_W(struct inode *inode, sector_t iblock,
				      struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)iblock << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_DIRECT_IO_W);
}

static void ntfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		ntfs_truncate_blocks(inode, inode->i_size);
	}
}

static ssize_t ntfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ntfs_inode *ni = ntfs_i(inode);
	size_t count = iov_iter_count(iter);
	loff_t vbo = iocb->ki_pos;
	loff_t end = vbo + count;
	int wr = iov_iter_rw(iter) & WRITE;
	const struct iovec *iov = iter->iov;
	unsigned long nr_segs = iter->nr_segs;
	loff_t valid;
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, inode, iter,
				 wr ? ntfs_get_block_direct_IO_W :
				      ntfs_get_block_direct_IO_R);
	valid = ni->i_valid;
	if (wr) {
		if (ret < 0)
			ntfs_write_failed(mapping, end);
		if (ret <= 0)
			goto out;

		vbo += ret;
		if (vbo > valid && !S_ISBLK(inode->i_mode)) {
			ni->i_valid = vbo;
			mark_inode_dirty(inode);
		}
	} else if (vbo < valid && valid < end) {
		/* fix page */
		unsigned long uaddr = ~0ul;
		struct page *page;
		long i, npages;
		size_t dvbo = valid - vbo;
		size_t off = 0;

		/*Find user address*/
		for (i = 0; i < nr_segs; i++) {
			if (off <= dvbo && dvbo < off + iov[i].iov_len) {
				uaddr = (unsigned long)iov[i].iov_base + dvbo -
					off;
				break;
			}
			off += iov[i].iov_len;
		}

		if (uaddr == ~0ul)
			goto fix_error;

		npages = get_user_pages_unlocked(uaddr, 1, &page, FOLL_WRITE);

		if (npages <= 0)
			goto fix_error;

		zero_user_segment(page, valid & (PAGE_SIZE - 1), PAGE_SIZE);
		put_page(page);
	}

out:
	return ret;
fix_error:
	ntfs_inode_warning(inode, "file garbadge at 0x%llx", valid);
	goto out;
}

int ntfs_set_size(struct inode *inode, u64 new_size)
{
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	ntfs_inode *ni = ntfs_i(inode);
	int err;

	/* Check for maximum file size */
	if (is_sparsed(ni) || is_compressed(ni)) {
		if (new_size > sbi->maxbytes_sparse) {
			err = -EFBIG;
			goto out;
		}
	} else if (new_size > sbi->maxbytes) {
		err = -EFBIG;
		goto out;
	}

	ni_lock(ni);
	down_write(&ni->file.run_lock);

	err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run, new_size,
			    &ni->i_valid, true, NULL);

	up_write(&ni->file.run_lock);
	ni_unlock(ni);

	mark_inode_dirty(inode);

out:
	return err;
}

static int ntfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ntfs_get_block, wbc);
}

static int ntfs_writepage_cmpr(struct page *page, struct writeback_control *wbc)
{
	int err;
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	ntfs_inode *ni = ntfs_i(inode);
	int sync = wbc->sync_mode == WB_SYNC_ALL;

	if (current->flags & PF_MEMALLOC) {
redirty:
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}

	if (sync)
		ni_lock(ni);
	else if (!ni_trylock(ni))
		goto redirty;

	err = ni_writepage_cmpr(page, sync);
	ni_unlock(ni);

	return err;
}

static int ntfs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, ntfs_get_block);
}

/*ntfs_write_begin*/
static int ntfs_get_block_write_begin(struct inode *inode, sector_t vbn,
				      struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)vbn << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_WRITE_BEGIN);
}

static int ntfs_write_begin(struct file *file, struct address_space *mapping,
			    loff_t pos, u32 len, u32 flags, struct page **pagep,
			    void **fsdata)
{
	int err;

	*pagep = NULL;

	err = block_write_begin(mapping, pos, len, flags, pagep,
				ntfs_get_block_write_begin);
	if (err < 0)
		ntfs_write_failed(mapping, pos + len);

	return err;
}

/* address_space_operations::write_end */
static int ntfs_write_end(struct file *file, struct address_space *mapping,
			  loff_t pos, u32 len, u32 copied, struct page *page,
			  void *fsdata)

{
	struct inode *inode = mapping->host;
	ntfs_inode *ni = ntfs_i(inode);
	u64 valid = ni->i_valid;
	int err;

	err = generic_write_end(file, mapping, pos, len, copied, page, fsdata);

	if (err < len)
		ntfs_write_failed(mapping, pos + len);
	if (err >= 0) {
		bool dirty = false;

		if (!(ni->std_fa & FILE_ATTRIBUTE_ARCHIVE)) {
			inode->i_ctime = inode->i_mtime = current_time(inode);
			ni->std_fa |= FILE_ATTRIBUTE_ARCHIVE;
			dirty = true;
		}

		if (valid != ni->i_valid) {
			/* ni->i_valid is changed in ntfs_get_block_vbo */
			dirty = true;
		}

		if (dirty)
			mark_inode_dirty(inode);
	}

	return err;
}

int reset_log_file(struct inode *inode)
{
	int err;
	loff_t pos = 0;
	u32 log_size = inode->i_size;
	struct address_space *mapping = inode->i_mapping;

	for (;;) {
		u32 len;
		void *kaddr;
		struct page *page;

		len = pos + PAGE_SIZE > log_size ? (log_size - pos) : PAGE_SIZE;

		err = block_write_begin(mapping, pos, len, 0, &page,
					ntfs_get_block_write_begin);
		if (err)
			goto out;

		kaddr = kmap_atomic(page);
		memset(kaddr, -1, len);
		kunmap_atomic(kaddr);
		flush_dcache_page(page);

		err = block_write_end(NULL, mapping, pos, len, len, page, NULL);
		if (err < 0)
			goto out;
		pos += len;

		if (pos >= log_size)
			break;
		balance_dirty_pages_ratelimited(mapping);
	}
out:
	mark_inode_dirty_sync(inode);

	return err;
}

int ntfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	if (WARN_ON_ONCE(current->flags & PF_MEMALLOC) ||
	    sb_rdonly(inode->i_sb))
		return 0;

	return _ni_write_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
}

int ntfs_sync_inode(struct inode *inode)
{
	return _ni_write_inode(inode, 1);
}

/*
 * helper function for ntfs_flush_inodes.  This writes both the inode
 * and the file data blocks, waiting for in flight data blocks before
 * the start of the call.  It does not wait for any io started
 * during the call
 */
static int writeback_inode(struct inode *inode)
{
	int ret = sync_inode_metadata(inode, 0);

	if (!ret)
		ret = filemap_fdatawrite(inode->i_mapping);
	return ret;
}

/*
 * write data and metadata corresponding to i1 and i2.  The io is
 * started but we do not wait for any of it to finish.
 *
 * filemap_flush is used for the block device, so if there is a dirty
 * page for a block already in flight, we will not wait and start the
 * io over again
 */
int ntfs_flush_inodes(struct super_block *sb, struct inode *i1,
		      struct inode *i2)
{
	int ret = 0;

	if (i1)
		ret = writeback_inode(i1);
	if (!ret && i2)
		ret = writeback_inode(i2);
	if (!ret)
		ret = filemap_flush(sb->s_bdev->bd_inode->i_mapping);
	return ret;
}

int inode_write_data(struct inode *inode, const void *data, size_t bytes)
{
	pgoff_t idx;

	/* Write non resident data */
	for (idx = 0; bytes; idx++) {
		size_t op = bytes > PAGE_SIZE ? PAGE_SIZE : bytes;
		struct page *page = ntfs_map_page(inode->i_mapping, idx);

		if (IS_ERR(page))
			return PTR_ERR(page);

		lock_page(page);
		WARN_ON(!PageUptodate(page));
		ClearPageUptodate(page);

		memcpy(page_address(page), data, op);

		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);

		ntfs_unmap_page(page);

		bytes -= op;
		data = Add2Ptr(data, PAGE_SIZE);
	}
	return 0;
}

int ntfs_create_inode(struct inode *dir, struct dentry *dentry,
		      struct file *file, umode_t mode, dev_t dev,
		      const char *symname, unsigned int size, int excl,
		      struct ntfs_fnd *fnd, struct inode **new_inode)
{
	int err;
	struct super_block *sb = dir->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	const struct qstr *name = &dentry->d_name;
	CLST ino = 0;
	ntfs_inode *dir_ni = ntfs_i(dir);
	ntfs_inode *ni = NULL;
	struct inode *inode = NULL;
	ATTRIB *attr;
	ATTR_STD_INFO5 *std5;
	ATTR_FILE_NAME *fname;
	MFT_REC *rec;
	u32 asize, dsize, sd_size;
	FILE_ATTRIBUTE fa;
	__le32 security_id = SECURITY_ID_INVALID;
	__le32 *def_security;
	CLST vcn;
	const void *sd;
	u16 t16, nsize = 0, aid = 0;
	INDEX_ROOT *root, *dir_root;
	NTFS_DE *e, *new_de = NULL;
	REPARSE_DATA_BUFFER *rp = NULL;
	typeof(rp->SymbolicLink2ReparseBuffer) *rb;
	__le16 *rp_name;
	bool is_dir = S_ISDIR(mode);
	bool rp_inserted = false;
	bool is_sp = S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) ||
		     S_ISSOCK(mode);

	if (is_sp)
		return -EOPNOTSUPP;

	dir_root = indx_get_root(&dir_ni->dir, dir_ni, NULL, NULL);
	if (!dir_root)
		return -EINVAL;

	fa = (is_dir ? (dir_ni->std_fa | FILE_ATTRIBUTE_DIRECTORY) :
		       S_ISLNK(mode) ?
		       FILE_ATTRIBUTE_REPARSE_POINT :
		       sbi->options.sparse ?
		       FILE_ATTRIBUTE_SPARSE_FILE :
		       (dir_ni->std_fa & FILE_ATTRIBUTE_COMPRESSED) ?
		       FILE_ATTRIBUTE_COMPRESSED :
		       0) |
	     FILE_ATTRIBUTE_ARCHIVE;

	if (!(mode & 0222)) {
		mode &= ~0222;
		fa |= FILE_ATTRIBUTE_READONLY;
	}

	new_de = __getname();
	if (!new_de) {
		err = -ENOMEM;
		goto out1;
	}

	/*mark rw ntfs as dirty. it will be cleared at umount*/
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	/* Step 1: allocate and fill new mft record */
	err = ntfs_look_free_mft(sbi, &ino, false, NULL, NULL);
	if (err)
		goto out2;

	ni = ntfs_new_inode(sbi, ino, is_dir);
	if (IS_ERR(ni)) {
		err = PTR_ERR(ni);
		ni = NULL;
		goto out3;
	}
	inode = &ni->vfs_inode;

	inode->i_atime = inode->i_mtime = inode->i_ctime = ni->i_crtime =
		current_time(inode);

	rec = ni->mi.mrec;
	rec->hard_links = cpu_to_le16(1);
	attr = Add2Ptr(rec, le16_to_cpu(rec->attr_off));

	/* Get default security id */
	if (is_dir) {
		sd = s_dir_security;
		sd_size = sizeof(s_dir_security);
		def_security = &sbi->security.def_dir_id;
	} else {
		sd = s_file_security;
		sd_size = sizeof(s_file_security);
		def_security = &sbi->security.def_file_id;
	}

	if (!is_nt5(sbi))
		goto insert_std;

	security_id = dir_ni->std_security_id;
	if (le32_to_cpu(security_id) >= SECURITY_ID_FIRST)
		goto insert_std;

	security_id = *def_security;

	if (security_id == SECURITY_ID_INVALID &&
	    !ntfs_insert_security(sbi, sd, sd_size, &security_id, NULL))
		*def_security = security_id;

insert_std:
	/* Insert standard info */
	std5 = Add2Ptr(attr, SIZEOF_RESIDENT);

	if (security_id == SECURITY_ID_INVALID) {
		dsize = sizeof(ATTR_STD_INFO);
	} else {
		dsize = sizeof(ATTR_STD_INFO5);
		std5->security_id = security_id;
	}
	asize = SIZEOF_RESIDENT + dsize;

	attr->type = ATTR_STD;
	attr->size = cpu_to_le32(asize);
	attr->id = cpu_to_le16(aid++);
	attr->res.data_off = SIZEOF_RESIDENT_LE;
	attr->res.data_size = cpu_to_le32(dsize);

	std5->cr_time = std5->m_time = std5->c_time = std5->a_time =
		kernel2nt(&inode->i_atime);

	ni->std_fa = fa;
	std5->fa = fa;

	attr = Add2Ptr(attr, asize);

	/* Insert file name */
	err = fill_name_de(sbi, new_de, name);
	if (err)
		goto out4;

	fname = (ATTR_FILE_NAME *)(new_de + 1);

	new_de->ref.low = cpu_to_le32(ino);
#ifdef NTFS3_64BIT_CLUSTER
	new_de->ref.high = cpu_to_le16(ino >> 32);
	fname->home.high = cpu_to_le16(dir->i_ino >> 32);
#endif
	new_de->ref.seq = rec->seq;

	fname->home.low = cpu_to_le32(dir->i_ino & 0xffffffff);
	fname->home.seq = dir_ni->mi.mrec->seq;

	fname->dup.cr_time = fname->dup.m_time = fname->dup.c_time =
		fname->dup.a_time = std5->cr_time;
	fname->dup.alloc_size = fname->dup.data_size = 0;
	fname->dup.fa = std5->fa;
	fname->dup.ea_size = fname->dup.reparse = 0;

	dsize = le16_to_cpu(new_de->key_size);
	asize = QuadAlign(SIZEOF_RESIDENT + dsize);

	attr->type = ATTR_NAME;
	attr->size = cpu_to_le32(asize);
	attr->res.data_off = SIZEOF_RESIDENT_LE;
	attr->res.flags = RESIDENT_FLAG_INDEXED;
	attr->id = cpu_to_le16(aid++);
	attr->res.data_size = cpu_to_le32(dsize);
	memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), fname, dsize);

	attr = Add2Ptr(attr, asize);

	if (security_id == SECURITY_ID_INVALID) {
		/* Insert security attribute */
		asize = SIZEOF_RESIDENT + QuadAlign(sd_size);

		attr->type = ATTR_SECURE;
		attr->size = cpu_to_le32(asize);
		attr->id = cpu_to_le16(aid++);
		attr->res.data_off = SIZEOF_RESIDENT_LE;
		attr->res.data_size = cpu_to_le32(sd_size);
		memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), sd, sd_size);

		attr = Add2Ptr(attr, asize);
	}

	if (is_dir) {
		/* Create root of directory */
		dsize = sizeof(INDEX_ROOT) + sizeof(NTFS_DE);
		asize = sizeof(I30_NAME) + SIZEOF_RESIDENT + dsize;

		attr->type = ATTR_ROOT;
		attr->size = cpu_to_le32(asize);
		attr->id = cpu_to_le16(aid++);

		attr->name_len = ARRAY_SIZE(I30_NAME);
		attr->name_off = SIZEOF_RESIDENT_LE;
		attr->res.data_off =
			cpu_to_le16(sizeof(I30_NAME) + SIZEOF_RESIDENT);
		attr->res.data_size = cpu_to_le32(dsize);
		memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), I30_NAME,
		       sizeof(I30_NAME));

		root = Add2Ptr(attr, sizeof(I30_NAME) + SIZEOF_RESIDENT);
		memcpy(root, dir_root, offsetof(INDEX_ROOT, ihdr));
		root->ihdr.de_off = cpu_to_le32(sizeof(INDEX_HDR)); // 0x10
		root->ihdr.used =
			cpu_to_le32(sizeof(INDEX_HDR) + sizeof(NTFS_DE));
		root->ihdr.total = root->ihdr.used;

		e = Add2Ptr(root, sizeof(INDEX_ROOT));
		e->size = cpu_to_le16(sizeof(NTFS_DE));
		e->flags = NTFS_IE_LAST;

		ni->ni_flags |= NI_FLAG_DIR;

		err = indx_init(&ni->dir, sbi, attr, INDEX_MUTEX_I30);
		if (err)
			goto out4;
	} else if (S_ISLNK(mode)) {
		/* Create symlink */
		dsize = 0;
		asize = SIZEOF_RESIDENT;

		/* insert empty ATTR_DATA */
		attr->type = ATTR_DATA;
		attr->size = cpu_to_le32(SIZEOF_RESIDENT);
		attr->id = cpu_to_le16(aid++);
		attr->name_off = SIZEOF_RESIDENT_LE;
		attr->res.data_off = SIZEOF_RESIDENT_LE;

		attr = Add2Ptr(attr, asize);

		/*
		 * Insert ATTR_REPARSE
		 * Assume each symbol is coded with 2 unicodes and zero
		 */
		rp = ntfs_alloc(ntfs_reparse_bytes(2 * size + 2), 1);
		if (!rp) {
			err = -ENOMEM;
			goto out4;
		}
		rb = &rp->SymbolicLink2ReparseBuffer;
		rp_name = rb->PathBuffer;

		/* Convert link name to unicode */
		err = x8_to_uni(sbi, symname, size,
				(struct cpu_str *)(rp_name - 1), 2 * size,
				UTF16_LITTLE_ENDIAN);
		if (err < 0)
			goto out4;

		if (err > 2 * size) {
			/* Convert to UTF16 requires more than 4 bytes per symbol? */
			err = -EINVAL;
			goto out4;
		}

		/* err = the length of unicode name of symlink */
		nsize = ntfs_reparse_bytes(err);

		if (nsize > sbi->reparse.max_size) {
			ntfs_warning(sb, "Symbolic link %u is too big", size);
			err = -EFBIG;
			goto out4;
		}

		rp->ReparseTag = IO_REPARSE_TAG_SYMLINK;
		rp->ReparseDataLength = cpu_to_le16(
			(nsize - offsetof(REPARSE_DATA_BUFFER,
					  SymbolicLink2ReparseBuffer)));
		rb = &rp->SymbolicLink2ReparseBuffer;
		rb->SubstituteNameOffset = cpu_to_le16(sizeof(short) * err);
		rb->SubstituteNameLength = cpu_to_le16(sizeof(short) * err + 8);
		rb->PrintNameLength = rb->SubstituteNameOffset;
		rb->Flags = 0;

		memmove(rp_name + err + 4, rp_name, sizeof(short) * err);

		rp_name += err;
		rp_name[0] = cpu_to_le16('\\');
		rp_name[1] = cpu_to_le16('?');
		rp_name[2] = cpu_to_le16('?');
		rp_name[3] = cpu_to_le16('\\');

		attr->type = ATTR_REPARSE;
		attr->id = cpu_to_le16(aid++);

		/* resident or non resident? */
		asize = QuadAlign(SIZEOF_RESIDENT + nsize);
		t16 = PtrOffset(rec, attr);

		if (asize + t16 + 8 > sbi->record_size) {
			CLST alen;
			CLST clst = bytes_to_cluster(sbi, nsize);

			/* bytes per runs */
			t16 = sbi->record_size - t16 - SIZEOF_NONRESIDENT;

			attr->non_res = 1;
			attr->nres.evcn = cpu_to_le64(clst - 1);
			attr->name_off = SIZEOF_NONRESIDENT_LE;
			attr->nres.run_off = attr->name_off;
			attr->nres.data_size = cpu_to_le64(nsize);
			attr->nres.valid_size = attr->nres.data_size;
			attr->nres.alloc_size =
				cpu_to_le64(ntfs_up_cluster(sbi, nsize));

			err = attr_allocate_clusters(sbi, &ni->file.run, 0, 0,
						     clst, NULL, 0, &alen, 0,
						     NULL);
			if (err)
				goto out5;

			err = run_pack(&ni->file.run, 0, clst,
				       Add2Ptr(attr, SIZEOF_NONRESIDENT), t16,
				       &vcn);
			if (err < 0)
				goto out5;

			if (vcn != clst) {
				err = -EINVAL;
				goto out5;
			}

			asize = SIZEOF_NONRESIDENT + QuadAlign(err);
			inode->i_size = nsize;
		} else {
			attr->res.data_off = SIZEOF_RESIDENT_LE;
			attr->res.data_size = cpu_to_le32(nsize);
			memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), rp, nsize);
			inode->i_size = nsize;
			nsize = 0;
		}

		attr->size = cpu_to_le32(asize);

		err = ntfs_insert_reparse(sbi, IO_REPARSE_TAG_SYMLINK,
					  &new_de->ref);
		if (err)
			goto out5;

		rp_inserted = true;
	} else {
		attr->type = ATTR_DATA;
		attr->id = cpu_to_le16(aid++);
		/* Create non resident data attribute */
		attr->non_res = 1;
		attr->nres.evcn = cpu_to_le64(-1ll);
		if (fa & FILE_ATTRIBUTE_SPARSE_FILE) {
			attr->size = cpu_to_le32(SIZEOF_NONRESIDENT_EX + 8);
			attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
			attr->flags = ATTR_FLAG_SPARSED;
			asize = SIZEOF_NONRESIDENT_EX + 8;
		} else if (fa & FILE_ATTRIBUTE_COMPRESSED) {
			attr->size = cpu_to_le32(SIZEOF_NONRESIDENT_EX + 8);
			attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
			attr->flags = ATTR_FLAG_COMPRESSED;
			attr->nres.c_unit = COMPRESSION_UNIT;
			asize = SIZEOF_NONRESIDENT_EX + 8;
		} else {
			attr->size = cpu_to_le32(SIZEOF_NONRESIDENT + 8);
			attr->name_off = SIZEOF_NONRESIDENT_LE;
			asize = SIZEOF_NONRESIDENT + 8;
		}
		attr->nres.run_off = attr->name_off;
	}

	attr = Add2Ptr(attr, asize);
	attr->type = ATTR_END;

	rec->used = cpu_to_le32(PtrOffset(rec, attr) + 8);
	rec->next_attr_id = cpu_to_le16(aid);

	/* Step 2: Add new name in index */
	err = indx_insert_entry(&dir_ni->dir, dir_ni, new_de, sbi, fnd);
	if (err)
		goto out6;

	/* Update current directory record */
	mark_inode_dirty(dir);

	/* Fill vfs inode fields */
	inode->i_uid = sbi->options.uid ? sbi->options.fs_uid : current_fsuid();
	inode->i_gid =
		sbi->options.gid ?
			sbi->options.fs_gid :
			(dir->i_mode & S_ISGID) ? dir->i_gid : current_fsgid();
	inode->i_generation = le16_to_cpu(rec->seq);

	inode->i_mode = mode;

	dir->i_mtime = dir->i_ctime = inode->i_atime;

	if (is_dir) {
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = &ntfs_dir_operations;
	} else if (S_ISLNK(mode)) {
		inode->i_op = &ntfs_link_inode_operations;
		inode->i_fop = NULL;
		inode->i_mapping->a_ops = &ntfs_aops;
	} else {
		inode->i_op = &ntfs_file_inode_operations;
		inode->i_fop = &ntfs_file_operations;
		inode->i_mapping->a_ops =
			is_compressed(ni) ? &ntfs_aops_cmpr : &ntfs_aops;
		init_rwsem(&ni->file.run_lock);
	}

	/* call 'd_instantiate' after inode->i_op is set */
	d_instantiate(dentry, inode);

	/* Write non resident data */
	if (nsize) {
		err = ntfs_sb_write_run(sbi, &ni->file.run, 0, rp, nsize);
		if (err)
			goto out7;
	}

#ifdef NTFS_COUNT_CONTAINED
	if (S_ISDIR(mode))
		inc_nlink(dir);
#endif
	if (file) {
		if (is_dir)
			err = finish_no_open(file, NULL);
		else
			err = finish_open(file, dentry, ntfs_file_open);

		if (err)
			goto out7;
		file->f_mode |= FMODE_CREATED;
	}

	/* normal exit */
	mark_inode_dirty(inode);
	goto out2;

out7:

	/* undo 'indx_insert_entry' */
	indx_delete_entry(&dir_ni->dir, dir_ni, new_de + 1,
			  le16_to_cpu(new_de->key_size), sbi);
out6:
	if (rp_inserted)
		ntfs_remove_reparse(sbi, IO_REPARSE_TAG_SYMLINK, &new_de->ref);

out5:
	if (is_dir || run_is_empty(&ni->file.run))
		goto out4;

	run_deallocate(sbi, &ni->file.run, false);

out4:
	clear_rec_inuse(rec);
	iput(inode);
out3:
	ntfs_mark_rec_free(sbi, ino);

out2:
	__putname(new_de);
	ntfs_free(rp);

out1:
	if (err)
		return err;

	*new_inode = inode;
	return 0;
}

int ntfs_link_inode(struct inode *inode, struct dentry *dentry)
{
	int err;
	struct inode *dir = d_inode(dentry->d_parent);
	ntfs_inode *dir_ni = ntfs_i(dir);
	ntfs_inode *ni = ntfs_i(inode);
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	const struct qstr *name = &dentry->d_name;
	NTFS_DE *new_de = NULL;
	ATTR_FILE_NAME *fname;
	ATTRIB *attr;
	u16 key_size;
	INDEX_ROOT *dir_root;

	dir_root = indx_get_root(&dir_ni->dir, dir_ni, NULL, NULL);
	if (!dir_root)
		return -EINVAL;

	new_de = __getname();
	if (!new_de)
		return -ENOMEM;

	/*mark rw ntfs as dirty. it will be cleared at umount*/
	ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_DIRTY);

	// Insert file name
	err = fill_name_de(sbi, new_de, name);
	if (err)
		goto out;

	key_size = le16_to_cpu(new_de->key_size);
	fname = (ATTR_FILE_NAME *)(new_de + 1);

	err = ni_insert_resident(ni, key_size, ATTR_NAME, NULL, 0, &attr, NULL);
	if (err)
		goto out;

	new_de->ref.low = cpu_to_le32(inode->i_ino);
#ifdef NTFS3_64BIT_CLUSTER
	new_de->ref.high = cpu_to_le16(inode->i_ino >> 32);
	fname->home.high = cpu_to_le16(dir->i_ino >> 32);
#endif
	new_de->ref.seq = ni->mi.mrec->seq;

	fname->home.low = cpu_to_le32(dir->i_ino & 0xffffffff);
	fname->home.seq = dir_ni->mi.mrec->seq;

	fname->dup.cr_time = fname->dup.m_time = fname->dup.c_time =
		fname->dup.a_time = kernel2nt(&inode->i_ctime);
	fname->dup.alloc_size = fname->dup.data_size = 0;
	fname->dup.fa = ni->std_fa;
	fname->dup.ea_size = fname->dup.reparse = 0;

	memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), fname, key_size);

	err = indx_insert_entry(&dir_ni->dir, dir_ni, new_de, sbi, NULL);
	if (err)
		goto out;

	le16_add_cpu(&ni->mi.mrec->hard_links, 1);
	ni->mi.dirty = true;

out:
	__putname(new_de);
	return err;
}

/*
 * ntfs_unlink_inode
 *
 * inode_operations::unlink
 * inode_operations::rmdir
 */
int ntfs_unlink_inode(struct inode *dir, const struct dentry *dentry)
{
	int err;
	struct super_block *sb = dir->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode = d_inode(dentry);
	ntfs_inode *ni = ntfs_i(inode);
	const struct qstr *name = &dentry->d_name;
	ntfs_inode *dir_ni = ntfs_i(dir);
	ntfs_index *indx = &dir_ni->dir;
	struct cpu_str *uni = NULL;
	ATTR_FILE_NAME *fname;
	u8 name_type;
	ATTR_LIST_ENTRY *le;
	MFT_REF ref;
	bool is_dir = S_ISDIR(inode->i_mode);
	INDEX_ROOT *dir_root;

	dir_root = indx_get_root(indx, dir_ni, NULL, NULL);
	if (!dir_root)
		return -EINVAL;

	if (is_dir && !dir_is_empty(inode)) {
		err = -ENOTEMPTY;
		goto out1;
	}

	if (ntfs_is_meta_file(sbi, inode->i_ino)) {
		err = -EINVAL;
		goto out1;
	}

	uni = __getname();
	if (!uni) {
		err = -ENOMEM;
		goto out1;
	}

	/* Convert input string to unicode */
	err = x8_to_uni(sbi, name->name, name->len, uni, NTFS_NAME_LEN,
			UTF16_HOST_ENDIAN);
	if (err < 0)
		goto out4;

	le = NULL;

	/*mark rw ntfs as dirty. it will be cleared at umount*/
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	/* find name in record */
#ifdef NTFS3_64BIT_CLUSTER
	ref.low = cpu_to_le32(dir->i_ino & 0xffffffff);
	ref.high = cpu_to_le16(dir->i_ino >> 32);
#else
	ref.low = cpu_to_le32(dir->i_ino & 0xffffffff);
	ref.high = 0;
#endif
	ref.seq = dir_ni->mi.mrec->seq;

	fname = ni_fname_name(ni, uni, &ref, &le);
	if (!fname) {
		err = -ENOENT;
		goto out3;
	}

	name_type = paired_name(fname->type);

	err = indx_delete_entry(indx, dir_ni, fname, fname_full_size(fname),
				sbi);
	if (err)
		goto out4;

	/* Then remove name from mft */
	ni_remove_attr_le(ni, attr_from_name(fname), le);

	le16_add_cpu(&ni->mi.mrec->hard_links, -1);
	ni->mi.dirty = true;

	if (name_type == FILE_NAME_POSIX)
		goto skip_short;

	/* Now we should delete name by type */
	fname = ni_fname_type(ni, name_type, &le);
	if (!fname)
		goto skip_short;

	err = indx_delete_entry(indx, dir_ni, fname, fname_full_size(fname),
				sbi);
	if (err)
		goto out4;

	ni_remove_attr_le(ni, attr_from_name(fname), le);

	le16_add_cpu(&ni->mi.mrec->hard_links, -1);

skip_short:
out4:
	switch (err) {
	case 0:
		drop_nlink(inode);
	case -ENOTEMPTY:
	case -ENOSPC:
	case -EROFS:
		break;
	default:
		make_bad_inode(inode);
	}

	dir->i_mtime = dir->i_ctime = current_time(dir);
	mark_inode_dirty(dir);
	inode->i_ctime = dir->i_ctime;
	if (inode->i_nlink)
		mark_inode_dirty(inode);

#ifdef NTFS_COUNT_CONTAINED
	if (is_dir) {
		clear_nlink(inode);
		drop_nlink(dir);
		mark_inode_dirty(dir);
	}
#endif

out3:
	__putname(uni);
out1:
	return err;
}

void ntfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);

	if (inode->i_nlink)
		_ni_write_inode(inode, inode_needs_sync(inode));

	invalidate_inode_buffers(inode);
	clear_inode(inode);

	ni_clear(ntfs_i(inode));
}

static noinline int ntfs_readlink_hlp(struct inode *inode, char *buffer,
				      int buflen)
{
	int err = 0;
	ntfs_inode *ni = ntfs_i(inode);
	struct super_block *sb = inode->i_sb;
	ntfs_sb_info *sbi = sb->s_fs_info;
	u64 i_size = inode->i_size;
	u16 nlen = 0;
	void *to_free = NULL;
	REPARSE_DATA_BUFFER *rp;
	struct le_str *uni;
	ATTRIB *attr;

	/* Reparse data present. Try to parse it */
	static_assert(!offsetof(REPARSE_DATA_BUFFER, ReparseTag));
	static_assert(sizeof(u32) == sizeof(rp->ReparseTag));

	*buffer = 0;

	/* Read into temporal buffer */
	if (i_size > sbi->reparse.max_size || i_size <= sizeof(u32)) {
		err = -EINVAL;
		goto out;
	}

	attr = ni_find_attr(ni, NULL, NULL, ATTR_REPARSE, NULL, 0, NULL, NULL);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	if (!attr->non_res) {
		rp = resident_data_ex(attr, i_size);
		if (!rp) {
			err = -EINVAL;
			goto out;
		}
	} else {
		rp = ntfs_alloc(i_size, 0);
		if (!rp) {
			err = -ENOMEM;
			goto out;
		}
		to_free = rp;
		err = ntfs_read_run_nb(sbi, &ni->file.run, 0, rp, i_size, NULL);
		if (err)
			goto out;
	}

	err = -EINVAL;

	/* Microsoft Tag */
	switch (rp->ReparseTag) {
	case IO_REPARSE_TAG_MICROSOFT | IO_REPARSE_TAG_SYMBOLIC_LINK:
		/* Symbolic link */
		/* Can we use 'Rp->SymbolicLinkReparseBuffer.PrintNameLength'? */
		if (i_size <= offsetof(REPARSE_DATA_BUFFER,
				       SymbolicLinkReparseBuffer.PathBuffer))
			goto out;
		uni = Add2Ptr(rp,
			      offsetof(REPARSE_DATA_BUFFER,
				       SymbolicLinkReparseBuffer.PathBuffer) +
				      le16_to_cpu(rp->SymbolicLinkReparseBuffer
							  .PrintNameOffset) -
				      2);
		nlen = le16_to_cpu(
			rp->SymbolicLinkReparseBuffer.PrintNameLength);
		goto check_result;

	case IO_REPARSE_TAG_MOUNT_POINT:
		/* Mount points and junctions */
		/* Can we use 'Rp->MountPointReparseBuffer.PrintNameLength'? */
		if (i_size <= offsetof(REPARSE_DATA_BUFFER,
				       MountPointReparseBuffer.PathBuffer))
			goto out;
		uni = Add2Ptr(rp,
			      offsetof(REPARSE_DATA_BUFFER,
				       MountPointReparseBuffer.PathBuffer) +
				      le16_to_cpu(rp->MountPointReparseBuffer
							  .PrintNameOffset) -
				      2);
		nlen = le16_to_cpu(rp->MountPointReparseBuffer.PrintNameLength);
		goto check_result;

	case IO_REPARSE_TAG_SYMLINK:
		/* FolderSymbolicLink */
		/* Can we use 'Rp->SymbolicLink2ReparseBuffer.PrintNameLength'? */
		if (i_size <= offsetof(REPARSE_DATA_BUFFER,
				       SymbolicLink2ReparseBuffer.PathBuffer))
			goto out;
		uni = Add2Ptr(rp,
			      offsetof(REPARSE_DATA_BUFFER,
				       SymbolicLink2ReparseBuffer.PathBuffer) +
				      le16_to_cpu(rp->SymbolicLink2ReparseBuffer
							  .PrintNameOffset) -
				      2);
		nlen = le16_to_cpu(
			rp->SymbolicLink2ReparseBuffer.PrintNameLength);
		goto check_result;

	case IO_REPARSE_TAG_CLOUD:
	case IO_REPARSE_TAG_CLOUD_1:
	case IO_REPARSE_TAG_CLOUD_2:
	case IO_REPARSE_TAG_CLOUD_3:
	case IO_REPARSE_TAG_CLOUD_4:
	case IO_REPARSE_TAG_CLOUD_5:
	case IO_REPARSE_TAG_CLOUD_6:
	case IO_REPARSE_TAG_CLOUD_7:
	case IO_REPARSE_TAG_CLOUD_8:
	case IO_REPARSE_TAG_CLOUD_9:
	case IO_REPARSE_TAG_CLOUD_A:
	case IO_REPARSE_TAG_CLOUD_B:
	case IO_REPARSE_TAG_CLOUD_C:
	case IO_REPARSE_TAG_CLOUD_D:
	case IO_REPARSE_TAG_CLOUD_E:
	case IO_REPARSE_TAG_CLOUD_F:
		err = sizeof("OneDrive") - 1;
		if (err > buflen)
			err = buflen;
		memcpy(buffer, "OneDrive", err);
		goto out;

	default:
		if (IsReparseTagMicrosoft(rp->ReparseTag))
			goto out;
	}

	if (!IsReparseTagNameSurrogate(rp->ReparseTag) ||
	    i_size <= sizeof(REPARSE_POINT)) {
		goto out;
	}

	/* Users tag */
	uni = Add2Ptr(rp, sizeof(REPARSE_POINT) - 2);
	nlen = le16_to_cpu(((REPARSE_POINT *)rp)->ReparseDataLength) -
	       sizeof(REPARSE_POINT);

check_result:
	/* Convert nlen from bytes to UNICODE chars */
	nlen >>= 1;

	/* Check that name is available */
	if (!nlen || &uni->name[nlen] > (__le16 *)Add2Ptr(rp, i_size))
		goto out;

	/* If name is already zero terminated then truncate it now */
	if (!uni->name[nlen - 1])
		nlen -= 1;
	uni->len = nlen;

	err = uni_to_x8(sbi, uni, buffer, buflen);

	if (err < 0)
		goto out;

	/* Always set last zero */
	buffer[err] = 0;

out:
	ntfs_free(to_free);

	return err;
}

static const char *ntfs_get_link(struct dentry *de, struct inode *inode,
				 struct delayed_call *done)
{
	int err;
	char *ret;

	if (!de)
		return ERR_PTR(-ECHILD);

	ret = kmalloc(PAGE_SIZE, GFP_NOFS);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	err = ntfs_readlink_hlp(inode, ret, PAGE_SIZE);
	if (err < 0) {
		kfree(ret);
		return ERR_PTR(err);
	}

	set_delayed_call(done, kfree_link, ret);

	return ret;
}

const struct inode_operations ntfs_link_inode_operations = {
	.get_link = ntfs_get_link,
	.setattr = ntfs_setattr,
	.listxattr = ntfs_listxattr,
	.permission = ntfs_permission,
	.get_acl = ntfs_get_acl,
	.set_acl = ntfs_set_acl,
};

const struct address_space_operations ntfs_aops = { .readpage = ntfs_readpage,
						    .readahead = ntfs_readahead,
						    .writepage = ntfs_writepage,
						    .writepages =
							    ntfs_writepages,
						    .write_begin =
							    ntfs_write_begin,
						    .write_end = ntfs_write_end,
						    .direct_IO = ntfs_direct_IO,
						    .bmap = ntfs_bmap };

const struct address_space_operations ntfs_aops_cmpr = {
	.readpage = ntfs_readpage,
	.writepage = ntfs_writepage_cmpr,
	.set_page_dirty = __set_page_dirty_nobuffers,
};
