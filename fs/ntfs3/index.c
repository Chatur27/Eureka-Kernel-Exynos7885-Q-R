// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ntfs3/index.c
 *
 * Copyright (C) 2019-2020 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/nls.h>
#include <linux/sched/signal.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

static const struct INDEX_NAMES {
	const __le16 *name;
	u8 name_len;
} s_index_names[INDEX_MUTEX_TOTAL] = {
	{ I30_NAME, ARRAY_SIZE(I30_NAME) }, { SII_NAME, ARRAY_SIZE(SII_NAME) },
	{ SDH_NAME, ARRAY_SIZE(SDH_NAME) }, { SO_NAME, ARRAY_SIZE(SO_NAME) },
	{ SQ_NAME, ARRAY_SIZE(SQ_NAME) },   { SR_NAME, ARRAY_SIZE(SR_NAME) },
};

static int cmp_fnames(const ATTR_FILE_NAME *f1, size_t l1,
		      const ATTR_FILE_NAME *f2, size_t l2,
		      const ntfs_sb_info *sbi)
{
	int diff;
	u16 fsize2;
	const u16 *upcase = sbi->upcase;
	const struct cpu_str *s1;
	const struct le_str *s2;

	if (l2 <= offsetof(ATTR_FILE_NAME, name))
		return -1;

	fsize2 = fname_full_size(f2);
	if (l2 < fsize2)
		return -1;

	if (l1)
		goto compare_fnames;

	s1 = (struct cpu_str *)f1;
	s2 = (struct le_str *)&f2->name_len;

	diff = ntfs_cmp_names_cpu(s1, s2, upcase);

	if (diff)
		goto out1;

	/*
	 * If names are equal (case insensitive)
	 * try to compare it case sensitive
	 */
	if (/*sbi->options.nocase || */ f2->type == FILE_NAME_DOS)
		goto out1;

	diff = ntfs_cmp_names_cpu(s1, s2, NULL);

out1:
	return diff;

compare_fnames:

	diff = ntfs_cmp_names(f1->name, f1->name_len, f2->name, f2->name_len,
			      upcase);

	if (diff)
		goto out2;

	/*
	 * If names are equal (case insensitive)
	 * try to compare it case sensitive
	 */
	if (/*sbi->options.nocase || */ f2->type == FILE_NAME_DOS)
		goto out2;

	diff = ntfs_cmp_names(f1->name, f1->name_len, f2->name, f2->name_len,
			      NULL);

out2:
	return diff;
}

static int cmp_uint(const u32 *k1, size_t l1, const u32 *k2, size_t l2,
		    const void *p)
{
	if (l2 < sizeof(u32))
		return -1;

	if (*k1 < *k2)
		return -1;
	if (*k1 > *k2)
		return 1;
	return 0;
}

static int cmp_sdh(const SECURITY_KEY *k1, size_t l1, const SECURITY_KEY *k2,
		   size_t l2, const void *p)
{
	u32 t1, t2;

	if (l2 < sizeof(SECURITY_KEY))
		return -1;

	t1 = le32_to_cpu(k1->hash);
	t2 = le32_to_cpu(k2->hash);

	/* First value is a hash value itself */
	if (t1 < t2)
		return -1;
	if (t1 > t2)
		return 1;

	/* Second value is security Id */
	if (p) {
		t1 = le32_to_cpu(k1->sec_id);
		t2 = le32_to_cpu(k2->sec_id);
		if (t1 < t2)
			return -1;
		if (t1 > t2)
			return 1;
	}

	return 0;
}

static int cmp_uints(const __le32 *k1, size_t l1, const __le32 *k2, size_t l2,
		     const void *p)
{
	size_t count;

	if (l2 < sizeof(int))
		return -1;

	for (count = min(l1, l2) >> 2; count > 0; --count, ++k1, ++k2) {
		u32 t1 = le32_to_cpu(*k1);
		u32 t2 = le32_to_cpu(*k2);

		if (t1 > t2)
			return 1;
		if (t1 < t2)
			return -1;
	}

	if (l1 > l2)
		return 1;
	if (l1 < l2)
		return -1;

	return 0;
}

static inline NTFS_CMP_FUNC get_cmp_func(const INDEX_ROOT *root)
{
	switch (root->type) {
	case ATTR_NAME:
		if (root->rule == NTFS_COLLATION_TYPE_FILENAME)
			return (NTFS_CMP_FUNC)&cmp_fnames;
		break;
	case ATTR_ZERO:
		switch (root->rule) {
		case NTFS_COLLATION_TYPE_UINT:
			return (NTFS_CMP_FUNC)&cmp_uint;
		case NTFS_COLLATION_TYPE_SECURITY_HASH:
			return (NTFS_CMP_FUNC)&cmp_sdh;
		case NTFS_COLLATION_TYPE_UINTS:
			return (NTFS_CMP_FUNC)&cmp_uints;
		default:
			break;
		}
	default:
		break;
	}

	return NULL;
}

struct bmp_buf {
	ATTRIB *b;
	mft_inode *mi;
	struct buffer_head *bh;
	ulong *buf;
	size_t bit;
	u32 nbits;
	u64 new_valid;
};

static int bmp_buf_get(ntfs_index *indx, ntfs_inode *ni, size_t bit,
		       struct bmp_buf *bbuf)
{
	ATTRIB *b;
	size_t data_size, valid_size, vbo, off = bit >> 3;
	ntfs_sb_info *sbi = ni->mi.sbi;
	CLST vcn = off >> sbi->cluster_bits;
	ATTR_LIST_ENTRY *le = NULL;
	struct buffer_head *bh;
	struct super_block *sb;
	u32 blocksize;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	bbuf->bh = NULL;

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 &vcn, &bbuf->mi);
	bbuf->b = b;
	if (!b)
		return -EINVAL;

	if (!b->non_res) {
		data_size = le32_to_cpu(b->res.data_size);

		if (off >= data_size)
			return -EINVAL;

		bbuf->buf = (ulong *)resident_data(b);
		bbuf->bit = 0;
		bbuf->nbits = data_size * 8;

		return 0;
	}

	data_size = le64_to_cpu(b->nres.data_size);
	if (off >= data_size) {
		WARN_ON(1);
		return -EINVAL;
	}

	valid_size = le64_to_cpu(b->nres.valid_size);

	bh = ntfs_bread_run(sbi, &indx->bitmap_run, off);
	if (!bh)
		return -EIO;

	if (IS_ERR(bh))
		return PTR_ERR(bh);

	bbuf->bh = bh;

	if (buffer_locked(bh))
		__wait_on_buffer(bh);

	lock_buffer(bh);

	sb = sbi->sb;
	blocksize = sb->s_blocksize;

	vbo = off & ~(size_t)sbi->block_mask;

	bbuf->new_valid = vbo + blocksize;
	if (bbuf->new_valid <= valid_size)
		bbuf->new_valid = 0;
	else if (bbuf->new_valid > data_size)
		bbuf->new_valid = data_size;

	if (vbo >= valid_size) {
		memset(bh->b_data, 0, blocksize);
	} else if (vbo + blocksize > valid_size) {
		u32 voff = valid_size & sbi->block_mask;

		memset(bh->b_data + voff, 0, blocksize - voff);
	}

	bbuf->buf = (ulong *)bh->b_data;
	bbuf->bit = 8 * (off & ~(size_t)sbi->block_mask);
	bbuf->nbits = 8 * blocksize;

	return 0;
}

static void bmp_buf_put(struct bmp_buf *bbuf, bool dirty)
{
	struct buffer_head *bh = bbuf->bh;
	ATTRIB *b = bbuf->b;

	if (!bh) {
		if (b && !b->non_res && dirty)
			bbuf->mi->dirty = true;
		return;
	}

	if (!dirty)
		goto out;

	if (bbuf->new_valid) {
		b->nres.valid_size = cpu_to_le64(bbuf->new_valid);
		bbuf->mi->dirty = true;
	}

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);

out:
	unlock_buffer(bh);
	put_bh(bh);
}

/*
 * indx_mark_used
 *
 * marks the bit 'bit' as used
 */
static int indx_mark_used(ntfs_index *indx, ntfs_inode *ni, size_t bit)
{
	int err;
	struct bmp_buf bbuf;

	err = bmp_buf_get(indx, ni, bit, &bbuf);
	if (err)
		return err;

	__set_bit(bit - bbuf.bit, bbuf.buf);

	bmp_buf_put(&bbuf, true);

	return 0;
}

/*
 * indx_mark_free
 *
 * the bit 'bit' as free
 */
static int indx_mark_free(ntfs_index *indx, ntfs_inode *ni, size_t bit)
{
	int err;
	struct bmp_buf bbuf;

	err = bmp_buf_get(indx, ni, bit, &bbuf);
	if (err)
		return err;

	__clear_bit(bit - bbuf.bit, bbuf.buf);

	bmp_buf_put(&bbuf, true);

	return 0;
}

static int scan_nres_bitmap(ntfs_sb_info *sbi, ATTRIB *bitmap,
			    struct runs_tree *run, size_t from,
			    bool (*fn)(const ulong *buf, u32 bit, u32 bits,
				       size_t *ret),
			    size_t *ret)
{
	struct super_block *sb = sbi->sb;
	u32 nbits = sb->s_blocksize * 8;
	u32 blocksize = sb->s_blocksize;
	u64 valid_size = le64_to_cpu(bitmap->nres.valid_size);
	u64 data_size = le64_to_cpu(bitmap->nres.data_size);
	sector_t eblock = bytes_to_block(sb, data_size);
	size_t vbo = from >> 3;
	sector_t blk = (vbo & sbi->cluster_mask) >> sb->s_blocksize_bits;
	sector_t vblock = vbo >> sb->s_blocksize_bits;
	sector_t blen, block;
	CLST lcn, len;
	size_t idx;
	struct buffer_head *bh;

	*ret = MINUS_ONE_T;

	if (vblock >= eblock)
		return 0;

	from &= nbits - 1;

	if (!run_lookup_entry(run, vbo >> sbi->cluster_bits, &lcn, &len,
			      &idx)) {
		return -ENOENT;
	}

	blen = (sector_t)len * sbi->blocks_per_cluster;
	block = (sector_t)lcn * sbi->blocks_per_cluster;

next_run:
	for (; blk < blen; blk++, from = 0) {
		bool ok;

		bh = ntfs_bread(sb, block + blk);

		if (!bh)
			return -EIO;

		vbo = (u64)vblock << sb->s_blocksize_bits;
		if (vbo >= valid_size)
			memset(bh->b_data, 0, blocksize);
		else if (vbo + blocksize > valid_size) {
			u32 voff = valid_size & sbi->block_mask;

			memset(bh->b_data + voff, 0, blocksize - voff);
		}

		if (vbo + blocksize > data_size)
			nbits = 8 * (data_size - vbo);

		ok = nbits > from ?
			     (*fn)((ulong *)bh->b_data, from, nbits, ret) :
			     false;
		put_bh(bh);

		if (ok) {
			*ret += 8 * vbo;
			return 0;
		}

		if (++vblock >= eblock) {
			*ret = MINUS_ONE_T;
			return 0;
		}
	}

	if (!run_get_entry(run, ++idx, NULL, &lcn, &len))
		return -ENOENT;

	blk = 0;
	blen = (sector_t)len * sbi->blocks_per_cluster;
	block = (sector_t)lcn * sbi->blocks_per_cluster;
	goto next_run;
}

static bool scan_for_free(const ulong *buf, u32 bit, u32 bits, size_t *ret)
{
	size_t pos = find_next_zero_bit(buf, bits, bit);

	if (pos >= bits)
		return false;
	*ret = pos;
	return true;
}

/*
 * indx_find_free
 *
 * looks for free bit
 * returns -1 if no free bits
 */
static int indx_find_free(ntfs_index *indx, ntfs_inode *ni, size_t *bit,
			  ATTRIB **bitmap)
{
	ATTRIB *b;
	ATTR_LIST_ENTRY *le = NULL;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 NULL, NULL);

	if (!b)
		return -ENOENT;

	*bitmap = b;
	*bit = MINUS_ONE_T;

	if (!b->non_res) {
		u32 nbits = 8 * le32_to_cpu(b->res.data_size);
		size_t pos = find_next_zero_bit(resident_data(b), nbits, 0);

		if (pos < nbits)
			*bit = pos;
	} else {
		int err = scan_nres_bitmap(ni->mi.sbi, b, &indx->bitmap_run, 0,
					   &scan_for_free, bit);

		if (err)
			return err;
	}

	return 0;
}

static bool scan_for_used(const ulong *buf, u32 bit, u32 bits, size_t *ret)
{
	size_t pos = find_next_bit(buf, bits, bit);

	if (pos >= bits)
		return false;
	*ret = pos;
	return true;
}

/*
 * indx_used_bit
 *
 * looks for used bit
 * returns MINUS_ONE_T if no used bits
 */
int indx_used_bit(ntfs_index *indx, ntfs_inode *ni, size_t *bit)
{
	ATTRIB *b;
	ATTR_LIST_ENTRY *le = NULL;
	size_t from = *bit;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 NULL, NULL);

	if (!b)
		return -ENOENT;

	*bit = MINUS_ONE_T;

	if (!b->non_res) {
		u32 nbits = le32_to_cpu(b->res.data_size) * 8;
		size_t pos = find_next_bit(resident_data(b), nbits, from);

		if (pos < nbits)
			*bit = pos;
	} else {
		int err = scan_nres_bitmap(ni->mi.sbi, b, &indx->bitmap_run,
					   from, &scan_for_used, bit);
		if (err)
			return err;
	}

	return 0;
}

/*
 * hdr_find_split
 *
 * finds a point at which the index allocation buffer would like to
 * be split.
 * NOTE: This function should never return 'END' entry NULL returns on error
 */
static inline const NTFS_DE *hdr_find_split(const INDEX_HDR *hdr)
{
	size_t o;
	const NTFS_DE *e = hdr_first_de(hdr);
	u32 used_2 = le32_to_cpu(hdr->used) >> 1;
	u16 esize = le16_to_cpu(e->size);

	if (!e || de_is_last(e))
		return NULL;

	for (o = le32_to_cpu(hdr->de_off) + esize; o < used_2; o += esize) {
		const NTFS_DE *p = e;

		e = Add2Ptr(hdr, o);

		/* We must not return END entry */
		if (de_is_last(e))
			return p;

		esize = le16_to_cpu(e->size);
	}

	return e;
}

/*
 * hdr_insert_head
 *
 * inserts some entries at the beginning of the buffer.
 * It is used to insert entries into a newly-created buffer.
 */
static inline const NTFS_DE *hdr_insert_head(INDEX_HDR *hdr, const void *ins,
					     u32 ins_bytes)
{
	u32 to_move;
	NTFS_DE *e = hdr_first_de(hdr);
	u32 used = le32_to_cpu(hdr->used);

	if (!e)
		return NULL;

	/* Now we just make room for the inserted entries and jam it in. */
	to_move = used - le32_to_cpu(hdr->de_off);
	memmove(Add2Ptr(e, ins_bytes), e, to_move);
	memcpy(e, ins, ins_bytes);
	hdr->used = cpu_to_le32(used + ins_bytes);

	return e;
}

void fnd_clear(struct ntfs_fnd *fnd)
{
	int i;

	for (i = 0; i < fnd->level; i++) {
		struct indx_node *n = fnd->nodes[i];

		if (!n)
			continue;

		put_indx_node(n);
		fnd->nodes[i] = NULL;
	}
	fnd->level = 0;
	fnd->root_de = NULL;
}

static int fnd_push(struct ntfs_fnd *fnd, struct indx_node *n, NTFS_DE *e)
{
	int i;

	i = fnd->level;
	if (i < 0 || i >= ARRAY_SIZE(fnd->nodes))
		return -EINVAL;
	fnd->nodes[i] = n;
	fnd->de[i] = e;
	fnd->level += 1;
	return 0;
}

static struct indx_node *fnd_pop(struct ntfs_fnd *fnd)
{
	struct indx_node *n;
	int i = fnd->level;

	i -= 1;
	n = fnd->nodes[i];
	fnd->nodes[i] = NULL;
	fnd->level = i;

	return n;
}

static bool fnd_is_empty(struct ntfs_fnd *fnd)
{
	if (!fnd->level)
		return !fnd->root_de;

	return !fnd->de[fnd->level - 1];
}

struct ntfs_fnd *fnd_get(ntfs_index *indx)
{
	struct ntfs_fnd *fnd = ntfs_alloc(sizeof(struct ntfs_fnd), 1);

	if (!fnd)
		return NULL;

	return fnd;
}

void fnd_put(struct ntfs_fnd *fnd)
{
	if (!fnd)
		return;
	fnd_clear(fnd);
	ntfs_free(fnd);
}

/*
 * hdr_find_e
 *
 * locates an entry the index buffer.
 * If no matching entry is found, it returns the first entry which is greater
 * than the desired entry If the search key is greater than all the entries the
 * buffer, it returns the 'end' entry. This function does a binary search of the
 * current index buffer, for the first entry that is <= to the search value
 * Returns NULL if error
 */
static NTFS_DE *hdr_find_e(const ntfs_index *indx, const INDEX_HDR *hdr,
			   const void *key, size_t key_len, const void *ctx,
			   int *diff)
{
	NTFS_DE *e;
	NTFS_CMP_FUNC cmp = indx->cmp;
	u32 e_size, e_key_len;
	u32 end = le32_to_cpu(hdr->used);
	u32 off = le32_to_cpu(hdr->de_off);

#ifdef NTFS3_INDEX_BINARY_SEARCH
	int max_idx = 0, fnd, min_idx;
	int nslots = 64;
	u16 *offs;

	if (end > 0x10000)
		goto next;

	offs = ntfs_alloc(sizeof(u16) * nslots, 0);
	if (!offs)
		goto next;

	/* use binary search algorithm */
next1:
	if (off + sizeof(NTFS_DE) > end) {
		e = NULL;
		goto out1;
	}
	e = Add2Ptr(hdr, off);
	e_size = le16_to_cpu(e->size);

	if (e_size < sizeof(NTFS_DE) || off + e_size > end) {
		e = NULL;
		goto out1;
	}

	if (max_idx >= nslots) {
		u16 *ptr;
		int new_slots = QuadAlign(2 * nslots);

		ptr = ntfs_alloc(sizeof(u16) * new_slots, 0);
		if (ptr)
			memcpy(ptr, offs, sizeof(u16) * max_idx);
		ntfs_free(offs);
		offs = ptr;
		nslots = new_slots;
		if (!ptr)
			goto next;
	}

	/* Store entry table */
	offs[max_idx] = off;

	if (!de_is_last(e)) {
		off += e_size;
		max_idx += 1;
		goto next1;
	}

	/*
	 * Table of pointers is created
	 * Use binary search to find entry that is <= to the search value
	 */
	fnd = -1;
	min_idx = 0;

	while (min_idx <= max_idx) {
		int mid_idx = min_idx + ((max_idx - min_idx) >> 1);
		int diff2;

		e = Add2Ptr(hdr, offs[mid_idx]);

		e_key_len = le16_to_cpu(e->key_size);

		diff2 = (*cmp)(key, key_len, e + 1, e_key_len, ctx);

		if (!diff2) {
			*diff = 0;
			goto out1;
		}

		if (diff2 < 0) {
			max_idx = mid_idx - 1;
			fnd = mid_idx;
			if (!fnd)
				break;
		} else {
			min_idx = mid_idx + 1;
		}
	}

	if (fnd == -1) {
		e = NULL;
		goto out1;
	}

	*diff = -1;
	e = Add2Ptr(hdr, offs[fnd]);

out1:
	ntfs_free(offs);

	return e;
#endif

next:
	/*
	 * Entries index are sorted
	 * Enumerate all entries until we find entry that is <= to the search value
	 */
	if (off + sizeof(NTFS_DE) > end)
		return NULL;

	e = Add2Ptr(hdr, off);
	e_size = le16_to_cpu(e->size);

	if (e_size < sizeof(NTFS_DE) || off + e_size > end)
		return NULL;

	off += e_size;

	e_key_len = le16_to_cpu(e->key_size);

	*diff = (*cmp)(key, key_len, e + 1, e_key_len, ctx);
	if (!*diff)
		return e;

	if (*diff <= 0)
		return e;

	if (de_is_last(e)) {
		*diff = 1;
		return e;
	}
	goto next;
}

/*
 * hdr_insert_de
 *
 * inserts an index entry into the buffer.
 * 'before' should be a pointer previously returned from hdr_find_e
 */
static NTFS_DE *hdr_insert_de(const ntfs_index *indx, INDEX_HDR *hdr,
			      const NTFS_DE *de, NTFS_DE *before,
			      const void *ctx)
{
	int diff;
	size_t off = PtrOffset(hdr, before);
	u32 used = le32_to_cpu(hdr->used);
	u32 total = le32_to_cpu(hdr->total);
	u16 de_size = le16_to_cpu(de->size);

	/* First, check to see if there's enough room */
	if (used + de_size > total)
		return NULL;

	/* We know there's enough space, so we know we'll succeed. */
	if (before) {
		/* Check that before is inside Index */
		if (off >= used || off < le32_to_cpu(hdr->de_off) ||
		    off + le16_to_cpu(before->size) > total) {
			return NULL;
		}
		goto ok;
	}
	/* No insert point is applied. Get it manually */
	before = hdr_find_e(indx, hdr, de + 1, le16_to_cpu(de->key_size), ctx,
			    &diff);
	if (!before)
		return NULL;
	off = PtrOffset(hdr, before);

ok:
	/* Now we just make room for the entry and jam it in. */
	memmove(Add2Ptr(before, de_size), before, used - off);

	hdr->used = cpu_to_le32(used + de_size);
	memcpy(before, de, de_size);

	return before;
}

/*
 * hdr_delete_de
 *
 * removes an entry from the index buffer
 */
static inline NTFS_DE *hdr_delete_de(INDEX_HDR *hdr, NTFS_DE *re)
{
	u32 used = le32_to_cpu(hdr->used);
	u16 esize = le16_to_cpu(re->size);
	u32 off = PtrOffset(hdr, re);
	int bytes = used - (off + esize);

	if (off >= used || esize < sizeof(NTFS_DE) || bytes < sizeof(NTFS_DE))
		return NULL;

	hdr->used = cpu_to_le32(used - esize);
	memmove(re, Add2Ptr(re, esize), bytes);

	return re;
}

void indx_clear(ntfs_index *indx)
{
	run_close(&indx->alloc_run);
	run_close(&indx->bitmap_run);
}

int indx_init(ntfs_index *indx, ntfs_sb_info *sbi, const ATTRIB *attr,
	      enum index_mutex_classed type)
{
	u32 t32;
	const INDEX_ROOT *root = resident_data(attr);

	/* Check root fields */
	if (!root->index_block_clst)
		return -EINVAL;

	indx->type = type;
	indx->idx2vbn_bits = __ffs(root->index_block_clst);

	t32 = le32_to_cpu(root->index_block_size);
	indx->index_bits = blksize_bits(t32);

	/* Check index record size */
	if (t32 < sbi->cluster_size) {
		/* index record is smaller than a cluster, use 512 blocks */
		if (t32 != root->index_block_clst * SECTOR_SIZE)
			return -EINVAL;

		/* Check alignment to a cluster */
		if ((sbi->cluster_size >> SECTOR_SHIFT) &
		    (root->index_block_clst - 1)) {
			return -EINVAL;
		}

		indx->vbn2vbo_bits = SECTOR_SHIFT;
	} else {
		/* index record must be a multiple of cluster size */
		if (t32 != root->index_block_clst << sbi->cluster_bits)
			return -EINVAL;

		indx->vbn2vbo_bits = sbi->cluster_bits;
	}

	indx->cmp = get_cmp_func(root);

	return indx->cmp ? 0 : -EINVAL;
}

static struct indx_node *indx_new(ntfs_index *indx, ntfs_inode *ni, CLST vbn,
				  const __le64 *sub_vbn)
{
	int err;
	NTFS_DE *e;
	struct indx_node *r;
	INDEX_HDR *hdr;
	INDEX_BUFFER *index;
	u64 vbo = (u64)vbn << indx->vbn2vbo_bits;
	u32 bytes = 1u << indx->index_bits;
	u16 fn;
	u32 eo;

	r = ntfs_alloc(sizeof(struct indx_node), 1);
	if (!r)
		return ERR_PTR(-ENOMEM);

	index = ntfs_alloc(bytes, 1);
	if (!index) {
		ntfs_free(r);
		return ERR_PTR(-ENOMEM);
	}

	err = ntfs_get_bh(ni->mi.sbi, &indx->alloc_run, vbo, bytes, &r->nb);

	if (err) {
		ntfs_free(index);
		ntfs_free(r);
		return ERR_PTR(err);
	}

	/* Create header */
	index->rhdr.sign = NTFS_INDX_SIGNATURE;
	index->rhdr.fix_off = cpu_to_le16(sizeof(INDEX_BUFFER)); // 0x28
	fn = (bytes >> SECTOR_SHIFT) + 1; // 9
	index->rhdr.fix_num = cpu_to_le16(fn);
	index->vbn = cpu_to_le64(vbn);
	hdr = &index->ihdr;
	eo = QuadAlign(sizeof(INDEX_BUFFER) + fn * sizeof(short));
	hdr->de_off = cpu_to_le32(eo);

	e = Add2Ptr(hdr, eo);

	if (sub_vbn) {
		e->flags = NTFS_IE_LAST | NTFS_IE_HAS_SUBNODES;
		e->size = cpu_to_le16(sizeof(NTFS_DE) + sizeof(u64));
		hdr->used = cpu_to_le32(eo + sizeof(NTFS_DE) + sizeof(u64));
		de_set_vbn_le(e, *sub_vbn);
		hdr->flags = 1;
	} else {
		e->size = cpu_to_le16(sizeof(NTFS_DE));
		hdr->used = cpu_to_le32(eo + sizeof(NTFS_DE));
		e->flags = NTFS_IE_LAST;
	}

	hdr->total = cpu_to_le32(bytes - offsetof(INDEX_BUFFER, ihdr));

	r->index = index;
	return r;
}

INDEX_ROOT *indx_get_root(ntfs_index *indx, ntfs_inode *ni, ATTRIB **attr,
			  mft_inode **mi)
{
	ATTR_LIST_ENTRY *le = NULL;
	ATTRIB *a;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	a = ni_find_attr(ni, NULL, &le, ATTR_ROOT, in->name, in->name_len, NULL,
			 mi);
	if (!a)
		return NULL;

	if (attr)
		*attr = a;

	return resident_data_ex(a, sizeof(INDEX_ROOT));
}

static int indx_write(ntfs_index *indx, ntfs_inode *ni, struct indx_node *node,
		      int sync)
{
	int err;
	INDEX_BUFFER *ib = node->index;

	err = ntfs_write_bh_ex(ni->mi.sbi, &ib->rhdr, &node->nb, sync);

	return err;
}

int indx_read(ntfs_index *indx, ntfs_inode *ni, CLST vbn,
	      struct indx_node **node)
{
	int err;
	INDEX_BUFFER *ib;
	u64 vbo = (u64)vbn << indx->vbn2vbo_bits;
	u32 bytes = 1u << indx->index_bits;
	struct indx_node *in = *node;
	const struct INDEX_NAMES *name;

	if (!in) {
		in = ntfs_alloc(sizeof(struct indx_node), 1);
		if (!in)
			return -ENOMEM;
	} else {
		nb_put(&in->nb);
	}

	ib = in->index;
	if (!ib) {
		ib = ntfs_alloc(bytes, 0);
		if (!ib) {
			err = -ENOMEM;
			goto out;
		}
	}

	err = ntfs_read_bh_ex(ni->mi.sbi, &indx->alloc_run, vbo, &ib->rhdr,
			      bytes, &in->nb);

	if (!err)
		goto ok;

	if (err == 1)
		goto ok;

	if (err != -ENOENT)
		goto out;

	name = &s_index_names[indx->type];
	err = attr_load_runs_vcn(ni, ATTR_ALLOC, name->name, name->name_len,
				 &indx->alloc_run,
				 vbo >> ni->mi.sbi->cluster_bits);
	if (err)
		goto out;

	err = ntfs_read_bh_ex(ni->mi.sbi, &indx->alloc_run, vbo, &ib->rhdr,
			      bytes, &in->nb);
	if (err == 1)
		goto ok;

	if (err)
		goto out;

ok:
	if (err == 1)
		ntfs_write_bh_ex(ni->mi.sbi, &ib->rhdr, &in->nb, 0);
	in->index = ib;
	*node = in;

out:
	if (ib != in->index)
		ntfs_free(ib);

	if (*node != in) {
		nb_put(&in->nb);
		ntfs_free(in);
	}

	return err;
}

/*
 * indx_find
 *
 * scans NTFS directory for given entry
 */
int indx_find(ntfs_index *indx, ntfs_inode *ni, const INDEX_ROOT *root,
	      const void *key, size_t key_len, const void *ctx, int *diff,
	      NTFS_DE **entry, struct ntfs_fnd *fnd)
{
	int err;
	NTFS_DE *e;
	const INDEX_HDR *hdr;
	struct indx_node *node;

	if (!root)
		root = indx_get_root(&ni->dir, ni, NULL, NULL);

	if (!root) {
		err = -EINVAL;
		goto out;
	}

	hdr = &root->ihdr;

	/* Check cache */
	e = fnd->level ? fnd->de[fnd->level - 1] : fnd->root_de;
	if (e && !de_is_last(e) &&
	    !(*indx->cmp)(key, key_len, e + 1, le16_to_cpu(e->key_size), ctx)) {
		*entry = e;
		*diff = 0;
		return 0;
	}

	/* Soft finder reset */
	fnd_clear(fnd);

	/* Lookup entry that is <= to the search value */
	e = hdr_find_e(indx, hdr, key, key_len, ctx, diff);
	if (!e)
		return -EINVAL;

	if (fnd)
		fnd->root_de = e;

	err = 0;
	node = NULL;
next:
	if (*diff >= 0 || !de_has_vcn_ex(e)) {
		*entry = e;
		goto out;
	}

	/* Read next level. */
	err = indx_read(indx, ni, de_get_vbn(e), &node);
	if (err)
		goto out;

	/* Lookup entry that is <= to the search value */
	e = hdr_find_e(indx, &node->index->ihdr, key, key_len, ctx, diff);
	if (!e) {
		err = -EINVAL;
		put_indx_node(node);
		goto out;
	}

	fnd_push(fnd, node, e);
	node = NULL;
	goto next;

out:
	return err;
}

int indx_find_sort(ntfs_index *indx, ntfs_inode *ni, const INDEX_ROOT *root,
		   NTFS_DE **entry, struct ntfs_fnd *fnd)
{
	int err;
	struct indx_node *n = NULL;
	NTFS_DE *e;
	size_t iter = 0;
	int level = fnd->level;

	if (!*entry) {
		/* Start find */
		e = hdr_first_de(&root->ihdr);
		if (!e)
			return 0;
		fnd_clear(fnd);
		fnd->root_de = e;
	} else if (!level) {
		if (de_is_last(fnd->root_de)) {
			*entry = NULL;
			return 0;
		}

		e = hdr_next_de(&root->ihdr, fnd->root_de);
		if (!e)
			return -EINVAL;
		fnd->root_de = e;
	} else {
		n = fnd->nodes[level - 1];
		e = fnd->de[level - 1];

		if (de_is_last(e))
			goto PopLevel;

		e = hdr_next_de(&n->index->ihdr, e);
		if (!e)
			return -EINVAL;

		fnd->de[level - 1] = e;
	}

	/* Just to avoid tree cycle */
next_iter:
	if (iter++ >= 1000)
		return -EINVAL;

	while (de_has_vcn_ex(e)) {
		if (le16_to_cpu(e->size) < sizeof(NTFS_DE) + sizeof(u64)) {
			if (n) {
				fnd_pop(fnd);
				ntfs_free(n);
			}
			return -EINVAL;
		}

		/* Read next level */
		err = indx_read(indx, ni, de_get_vbn(e), &n);

		/* Try next level */
		e = hdr_first_de(&n->index->ihdr);
		if (!e) {
			ntfs_free(n);
			return -EINVAL;
		}

		fnd_push(fnd, n, e);
	}

	if (le16_to_cpu(e->size) > sizeof(NTFS_DE)) {
		*entry = e;
		return 0;
	}

PopLevel:
	if (!de_is_last(e))
		goto next_iter;

	/* Pop one level */
	if (n) {
		fnd_pop(fnd);
		ntfs_free(n);
	}

	level = fnd->level;

	if (level) {
		n = fnd->nodes[level - 1];
		e = fnd->de[level - 1];
	} else if (fnd->root_de) {
		n = NULL;
		e = fnd->root_de;
		fnd->root_de = NULL;
	} else {
		*entry = NULL;
		return 0;
	}

	if (le16_to_cpu(e->size) > sizeof(NTFS_DE)) {
		*entry = e;
		if (!fnd->root_de)
			fnd->root_de = e;
		return 0;
	}
	goto PopLevel;
}

int indx_find_raw(ntfs_index *indx, ntfs_inode *ni, const INDEX_ROOT *root,
		  NTFS_DE **entry, size_t *off, struct ntfs_fnd *fnd)
{
	int err;
	struct indx_node *n = NULL;
	NTFS_DE *e = NULL;
	NTFS_DE *e2;
	size_t bit;
	CLST next_used_vbn;
	CLST next_vbn;
	u32 record_size = ni->mi.sbi->record_size;

	/* Use non sorted algorithm */
	if (!*entry) {
		/* This is the first call */
		e = hdr_first_de(&root->ihdr);
		if (!e)
			return 0;
		fnd_clear(fnd);
		fnd->root_de = e;

		if (!*off)
			goto enum_hdr;

		/* The first call with setup of initial element */
		if (*off < record_size) {
			/* Start enumeration from root */
			*off = 0;
			goto enum_hdr;
		}
		next_vbn = (((*off - record_size) >> indx->index_bits))
			   << indx->idx2vbn_bits;
		goto Next;
	}

	if (!fnd->root_de)
		return -EINVAL;

enum_hdr:
	/* Check if current entry can be used */
	if (e && le16_to_cpu(e->size) > sizeof(NTFS_DE))
		goto ok;

	if (!fnd->level) {
		/* Continue to enumerate root */
		if (!de_is_last(fnd->root_de)) {
			e = hdr_next_de(&root->ihdr, fnd->root_de);
			if (!e)
				return -EINVAL;
			fnd->root_de = e;
			goto enum_hdr;
		}

		/* Start to enumerate indexes from 0 */
		next_vbn = 0;
		goto Next;
	}

	/* Continue to enumerate indexes */
	e2 = fnd->de[fnd->level - 1];

	n = fnd->nodes[fnd->level - 1];

	if (!de_is_last(e2)) {
		e = hdr_next_de(&n->index->ihdr, e2);
		if (!e)
			return -EINVAL;
		fnd->de[fnd->level - 1] = e;
		goto enum_hdr;
	}

	/* Continue with next index */
	next_vbn = le64_to_cpu(n->index->vbn) + root->index_block_clst;

Next:
	/* Release current index */
	if (n) {
		fnd_pop(fnd);
		put_indx_node(n);
		n = NULL;
	}

	/* Skip all free indexes */
	bit = next_vbn >> indx->idx2vbn_bits;
	err = indx_used_bit(indx, ni, &bit);
	if (err == -ENOENT || bit == MINUS_ONE_T) {
		/* No used indexes */
		*entry = NULL;
		return 0;
	}

	next_used_vbn = bit << indx->idx2vbn_bits;

	/* Read buffer into memory */
	err = indx_read(indx, ni, next_used_vbn, &n);
	if (err)
		return err;

	e = hdr_first_de(&n->index->ihdr);
	fnd_push(fnd, n, e);
	if (!e)
		return -EINVAL;

	goto enum_hdr;

ok:
	/* return offset to restore enumerator if necessary */
	if (!n) {
		/* 'e' points in root */
		*off = PtrOffset(&root->ihdr, e);
	} else {
		/* 'e' points in index */
		*off = (le64_to_cpu(n->index->vbn) << indx->vbn2vbo_bits) +
		       record_size + PtrOffset(&n->index->ihdr, e);
	}

	*entry = e;
	return 0;
}

/*
 * indx_create_allocate
 *
 * create "Allocation + Bitmap" attributes
 */
static int indx_create_allocate(ntfs_index *indx, ntfs_inode *ni, CLST *vbn)
{
	int err = -ENOMEM;
	ntfs_sb_info *sbi = ni->mi.sbi;

	ATTRIB *bitmap;
	ATTRIB *alloc;
	u32 alloc_size = ntfs_up_cluster(sbi, 1u << indx->index_bits);
	CLST len = alloc_size >> sbi->cluster_bits;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];
	CLST alen;
	struct runs_tree run;

	run_init(&run);

	err = attr_allocate_clusters(sbi, &run, 0, 0, len, NULL, 0, &alen, 0,
				     NULL);
	if (err)
		goto out;

	err = ni_insert_nonresident(ni, ATTR_ALLOC, in->name, in->name_len,
				    &run, 0, len, 0, &alloc, NULL);
	if (err)
		goto out1;

	err = ni_insert_resident(ni, QuadAlign(1), ATTR_BITMAP, in->name,
				 in->name_len, &bitmap, NULL);
	if (err)
		goto out2;

	memcpy(&indx->alloc_run, &run, sizeof(run));

	*vbn = 0;

	if (in->name == I30_NAME)
		ni->vfs_inode.i_size = alloc_size;

	return 0;

out2:
	mi_remove_attr(&ni->mi, alloc);

out1:
	run_deallocate(sbi, &run, false);

out:
	return err;
}

/*
 * indx_add_allocate
 *
 * add clusters to index
 */
static int indx_add_allocate(ntfs_index *indx, ntfs_inode *ni, CLST *vbn)
{
	int err;
	size_t bit;
	u64 data_size, alloc_size;
	u64 bpb, vbpb;
	ATTRIB *bmp, *alloc;
	mft_inode *mi;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	err = indx_find_free(indx, ni, &bit, &bmp);
	if (err)
		goto out1;

	if (bit != MINUS_ONE_T) {
		bmp = NULL;
	} else {
		if (bmp->non_res) {
			bpb = le64_to_cpu(bmp->nres.data_size);
			vbpb = le64_to_cpu(bmp->nres.valid_size);
		} else {
			bpb = vbpb = le32_to_cpu(bmp->res.data_size);
		}

		/* Increase bitmap */
		err = attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
				    &indx->bitmap_run, QuadAlign(bpb + 8), NULL,
				    true, NULL);
		if (err)
			goto out1;

		bit = bpb << 3;
	}

	alloc = ni_find_attr(ni, NULL, NULL, ATTR_ALLOC, in->name, in->name_len,
			     NULL, &mi);
	if (!alloc) {
		if (bmp)
			goto out2;
		goto out1;
	}

	data_size = (u64)(bit + 1) << indx->index_bits;
	alloc_size = ntfs_up_cluster(ni->mi.sbi, data_size);

	if (alloc_size > le64_to_cpu(alloc->nres.alloc_size)) {
		/* Increase allocation */
		err = attr_set_size(ni, ATTR_ALLOC, in->name, in->name_len,
				    &indx->alloc_run, alloc_size, &alloc_size,
				    true, NULL);
		if (err) {
			if (bmp)
				goto out2;
			goto out1;
		}

		if (in->name == I30_NAME)
			ni->vfs_inode.i_size = alloc_size;
	} else if (data_size > le64_to_cpu(alloc->nres.data_size)) {
		alloc->nres.data_size = alloc->nres.valid_size =
			cpu_to_le64(data_size);
		mi->dirty = true;
	}

	*vbn = bit << indx->idx2vbn_bits;

	return 0;

out2:
	/* Ops (no space?) */
	attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
		      &indx->bitmap_run, bpb, &vbpb, false, NULL);

out1:
	return err;
}

/*
 * indx_insert_into_root
 *
 * attempts to insert an entry into the index root
 * If necessary, it will twiddle the index b-tree.
 */
static int indx_insert_into_root(ntfs_index *indx, ntfs_inode *ni,
				 const NTFS_DE *new_de, NTFS_DE *root_de,
				 const void *ctx, struct ntfs_fnd *fnd)
{
	int err = 0;
	NTFS_DE *e, *e0, *re;
	mft_inode *mi;
	ATTRIB *attr;
	MFT_REC *rec;
	INDEX_HDR *hdr;
	struct indx_node *n;
	CLST new_vbn;
	__le64 *sub_vbn, t_vbn;
	u16 new_de_size;
	u32 hdr_used, hdr_total, asize, tail, used, aoff, to_move;
	u32 root_size, new_root_size;
	ntfs_sb_info *sbi;
	char *next;
	int ds_root;
	INDEX_ROOT *root, *a_root = NULL;

	/* Get the record this root placed in */
	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root)
		goto out;

	/*
	 * Try easy case:
	 * hdr_insert_de will succeed if there's room the root for the new entry.
	 */
	hdr = &root->ihdr;
	sbi = ni->mi.sbi;
	rec = mi->mrec;
	aoff = PtrOffset(rec, attr);
	used = le32_to_cpu(rec->used);
	new_de_size = le16_to_cpu(new_de->size);
	hdr_used = le32_to_cpu(hdr->used);
	hdr_total = le32_to_cpu(hdr->total);
	asize = le32_to_cpu(attr->size);
	next = Add2Ptr(attr, asize);
	tail = used - aoff - asize;
	root_size = le32_to_cpu(attr->res.data_size);

	ds_root = new_de_size + hdr_used - hdr_total;

	if (used + ds_root < sbi->max_bytes_per_attr) {
		/* make a room for new elements */
		memmove(next + ds_root, next, used - aoff - asize);
		hdr->total = cpu_to_le32(hdr_total + ds_root);
		e = hdr_insert_de(indx, hdr, new_de, root_de, ctx);
		WARN_ON(!e);
		fnd_clear(fnd);
		fnd->root_de = e;
		attr->size = cpu_to_le32(asize + ds_root);
		attr->res.data_size = cpu_to_le32(root_size + ds_root);
		rec->used = cpu_to_le32(used + ds_root);

		return 0;
	}

	/* Make a copy of root attribute to restore if error */
	a_root = ntfs_memdup(attr, asize);
	if (!a_root) {
		err = -ENOMEM;
		goto out;
	}

	/* copy all the non-end entries from the index root to the new buffer.*/
	to_move = 0;
	e0 = hdr_first_de(hdr);

	/* Calculate the size to copy */
	for (e = e0;; e = hdr_next_de(hdr, e)) {
		if (!e) {
			err = -EINVAL;
			goto out;
		}

		if (de_is_last(e))
			break;
		to_move += le16_to_cpu(e->size);
	}

	n = NULL;
	if (!to_move)
		re = NULL;
	else {
		re = ntfs_memdup(e0, to_move);
		if (!re) {
			err = -ENOMEM;
			goto out;
		}
	}

	sub_vbn = NULL;
	if (de_has_vcn(e)) {
		t_vbn = de_get_vbn_le(e);
		sub_vbn = &t_vbn;
	}

	new_root_size = sizeof(INDEX_ROOT) + sizeof(NTFS_DE) + sizeof(u64);
	ds_root = new_root_size - root_size;

	if (ds_root > 0 && used + ds_root > sbi->max_bytes_per_attr) {
		/* make root external */
		err = -EOPNOTSUPP;
		goto out;
	}

	if (ds_root) {
		memmove(next + ds_root, next, tail);
		used += ds_root;
		asize += ds_root;
		rec->used = cpu_to_le32(used);
		attr->size = cpu_to_le32(asize);
		attr->res.data_size = cpu_to_le32(new_root_size);
		mi->dirty = true;
	}

	/* Fill first entry (vcn will be set later) */
	e = (NTFS_DE *)(root + 1);
	memset(e, 0, sizeof(NTFS_DE));
	e->size = cpu_to_le16(sizeof(NTFS_DE) + sizeof(u64));
	e->flags = NTFS_IE_HAS_SUBNODES | NTFS_IE_LAST;

	hdr->flags = 1;
	hdr->used = hdr->total =
		cpu_to_le32(new_root_size - offsetof(INDEX_ROOT, ihdr));

	fnd->root_de = hdr_first_de(hdr);

	/* Create alloc and bitmap attributes (if not) */
	if (run_is_empty(&indx->alloc_run)) {
		err = indx_create_allocate(indx, ni, &new_vbn);
		if (err) {
			/* restore root after 'indx_create_allocate' */
			memmove(next - ds_root, next, tail);
			used -= ds_root;
			rec->used = cpu_to_le32(used);
			memcpy(attr, a_root, asize);
			goto out1;
		}
	} else {
		err = indx_add_allocate(indx, ni, &new_vbn);
		if (err)
			goto out1;
	}

	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		err = -EINVAL;
		goto out1;
	}

	e = (NTFS_DE *)(root + 1);
	*(__le64 *)(e + 1) = cpu_to_le64(new_vbn);

	/* now we can create/format the new buffer and copy the entries into */
	n = indx_new(indx, ni, new_vbn, sub_vbn);
	if (IS_ERR(n)) {
		err = PTR_ERR(n);
		goto out1;
	}

	hdr = &n->index->ihdr;
	hdr_used = le32_to_cpu(hdr->used);
	hdr_total = le32_to_cpu(hdr->total);

	/* Copy root entries into new buffer */
	hdr_insert_head(hdr, re, to_move);

	/* Update bitmap attribute */
	indx_mark_used(indx, ni, new_vbn >> indx->idx2vbn_bits);

	/* Check if we can insert new entry new index buffer */
	if (hdr_used + new_de_size > hdr_total) {
		/*
		 * This occurs if mft record is the same or bigger than index
		 * buffer. Move all root new index and have no space to add
		 * new entry classic case when mft record is 1K and index
		 * buffer 4K the problem should not occurs
		 */
		ntfs_trace(sbi->sb,
			   "Failed: root + new entry > index. Reinsert");
		ntfs_free(re);
		indx_write(indx, ni, n, 0);

		put_indx_node(n);
		fnd_clear(fnd);
		err = indx_insert_entry(indx, ni, new_de, ctx, fnd);
		goto out;
	}

	/*
	 * Now root is a parent for new index buffer
	 * Insert NewEntry a new buffer
	 */
	e = hdr_insert_de(indx, hdr, new_de, NULL, ctx);
	if (!e) {
		err = -EINVAL;
		goto out1;
	}
	fnd_push(fnd, n, e);

	/* Just write updates index into disk */
	indx_write(indx, ni, n, 0);

	n = NULL;

out1:
	ntfs_free(re);
	if (n)
		put_indx_node(n);

out:
	ntfs_free(a_root);
	return err;
}

/*
 * indx_insert_into_buffer
 *
 * attempts to insert an entry into an Index Allocation Buffer.
 * If necessary, it will split the buffer.
 */
static int indx_insert_into_buffer(ntfs_index *indx, ntfs_inode *ni,
				   INDEX_ROOT *root, const NTFS_DE *new_de,
				   const void *ctx, int level,
				   struct ntfs_fnd *fnd)
{
	int err;
	const NTFS_DE *sp;
	NTFS_DE *e, *de_t, *up_e = NULL;
	struct indx_node *n2 = NULL;
	struct indx_node *n1 = fnd->nodes[level];
	INDEX_HDR *hdr1 = &n1->index->ihdr;
	INDEX_HDR *hdr2;
	u32 to_copy, used;
	CLST new_vbn;
	__le64 t_vbn, *sub_vbn;
	u16 sp_size;

	/* Try the most easy case */
	e = fnd->level - 1 == level ? fnd->de[level] : NULL;
	e = hdr_insert_de(indx, hdr1, new_de, e, ctx);
	fnd->de[level] = e;
	if (e) {
		/* Just write updated index into disk */
		indx_write(indx, ni, n1, 0);
		return 0;
	}

	/*
	 * No space to insert into buffer. Split it.
	 * To split we:
	 *  - Save split point ('cause index buffers will be changed)
	 * - Allocate NewBuffer and copy all entries <= sp into new buffer
	 * - Remove all entries (sp including) from TargetBuffer
	 * - Insert NewEntry into left or right buffer (depending on sp <=>
	 *     NewEntry)
	 * - Insert sp into parent buffer (or root)
	 * - Make sp a parent for new buffer
	 */
	sp = hdr_find_split(hdr1);
	if (!sp)
		return -EINVAL;

	sp_size = le16_to_cpu(sp->size);
	up_e = ntfs_alloc(sp_size + sizeof(u64), 0);
	if (!up_e)
		return -ENOMEM;
	memcpy(up_e, sp, sp_size);

	if (!hdr1->flags) {
		up_e->flags |= NTFS_IE_HAS_SUBNODES;
		up_e->size = cpu_to_le16(sp_size + sizeof(u64));
		sub_vbn = NULL;
	} else {
		t_vbn = de_get_vbn_le(up_e);
		sub_vbn = &t_vbn;
	}

	/* Allocate on disk a new index allocation buffer. */
	err = indx_add_allocate(indx, ni, &new_vbn);
	if (err)
		goto out;

	/* Allocate and format memory a new index buffer */
	n2 = indx_new(indx, ni, new_vbn, sub_vbn);
	if (IS_ERR(n2)) {
		err = PTR_ERR(n2);
		goto out;
	}

	hdr2 = &n2->index->ihdr;

	/* Make sp a parent for new buffer */
	de_set_vbn(up_e, new_vbn);

	/* copy all the entries <= sp into the new buffer. */
	de_t = hdr_first_de(hdr1);
	to_copy = PtrOffset(de_t, sp);
	hdr_insert_head(hdr2, de_t, to_copy);

	/* remove all entries (sp including) from hdr1 */
	used = le32_to_cpu(hdr1->used) - to_copy - sp_size;
	memmove(de_t, Add2Ptr(sp, sp_size), used - le32_to_cpu(hdr1->de_off));
	hdr1->used = cpu_to_le32(used);

	/* Insert new entry into left or right buffer (depending on sp <=> new_de) */
	hdr_insert_de(indx,
		      (*indx->cmp)(new_de + 1, le16_to_cpu(new_de->key_size),
				   up_e + 1, le16_to_cpu(up_e->key_size),
				   ctx) < 0 ?
			      hdr2 :
			      hdr1,
		      new_de, NULL, ctx);

	indx_mark_used(indx, ni, new_vbn >> indx->idx2vbn_bits);

	indx_write(indx, ni, n1, 0);
	indx_write(indx, ni, n2, 0);

	put_indx_node(n2);

	/*
	 * we've finished splitting everybody, so we are ready to
	 * insert the promoted entry into the parent.
	 */
	if (!level) {
		/* Insert in root */
		err = indx_insert_into_root(indx, ni, up_e, NULL, ctx, fnd);
		if (err)
			goto out;
	} else {
		/*
		 * The target buffer's parent is another index buffer
		 * TODO: Remove recursion
		 */
		err = indx_insert_into_buffer(indx, ni, root, up_e, ctx,
					      level - 1, fnd);
		if (err)
			goto out;
	}

out:
	ntfs_free(up_e);

	return err;
}

/*
 * indx_insert_entry
 *
 * inserts new entry into index
 */
int indx_insert_entry(ntfs_index *indx, ntfs_inode *ni, const NTFS_DE *new_de,
		      const void *ctx, struct ntfs_fnd *fnd)
{
	int err;
	int diff;
	NTFS_DE *e;
	struct ntfs_fnd *fnd_a = NULL;
	INDEX_ROOT *root;

	if (!fnd) {
		fnd_a = fnd_get(indx);
		if (!fnd_a) {
			err = -ENOMEM;
			goto out1;
		}
		fnd = fnd_a;
	}

	root = indx_get_root(indx, ni, NULL, NULL);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	if (!fnd_is_empty(fnd))
		goto insert_step;

	/* Find the spot the tree where we want to insert the new entry. */
	err = indx_find(indx, ni, root, new_de + 1,
			le16_to_cpu(new_de->key_size), ctx, &diff, &e, fnd);
	if (err)
		goto out;

	if (!diff) {
		err = -EEXIST;
		goto out;
	}

insert_step:
	if (!fnd->level) {
		/* The root is also a leaf, so we'll insert the new entry into it. */
		err = indx_insert_into_root(indx, ni, new_de, fnd->root_de, ctx,
					    fnd);
		if (err)
			goto out;
	} else {
		/* found a leaf buffer, so we'll insert the new entry into it.*/
		err = indx_insert_into_buffer(indx, ni, root, new_de, ctx,
					      fnd->level - 1, fnd);
		if (err)
			goto out;
	}

out:
	indx->changed = true;
	fnd_put(fnd_a);
out1:

	return err;
}

/*
 * indx_find_buffer
 *
 * locates a buffer the tree.
 */
static struct indx_node *indx_find_buffer(ntfs_index *indx, ntfs_inode *ni,
					  const INDEX_ROOT *root, __le64 vbn,
					  struct indx_node *n)
{
	int err;
	const NTFS_DE *e;
	struct indx_node *r;
	const INDEX_HDR *hdr = n ? &n->index->ihdr : &root->ihdr;

	/* Step 1: Scan one level */
	for (e = hdr_first_de(hdr);; e = hdr_next_de(hdr, e)) {
		if (!e)
			return ERR_PTR(-EINVAL);

		if (de_has_vcn(e) && vbn == de_get_vbn_le(e))
			return n;

		if (de_is_last(e))
			break;
	}

	/* Step2: Do recursion */
	e = Add2Ptr(hdr, le32_to_cpu(hdr->de_off));
	for (;;) {
		if (de_has_vcn_ex(e)) {
			err = indx_read(indx, ni, de_get_vbn(e), &n);
			if (err)
				return ERR_PTR(err);

			r = indx_find_buffer(indx, ni, root, vbn, n);
			if (r)
				return r;
		}

		if (de_is_last(e))
			break;

		e = Add2Ptr(e, le16_to_cpu(e->size));
	}

	return NULL;
}

/*
 * indx_shrink
 *
 * deallocates unused tail indexes
 */
static int indx_shrink(ntfs_index *indx, ntfs_inode *ni, size_t bit)
{
	int err = 0;
	u64 bpb, new_alloc;
	size_t nbits;
	ATTRIB *b;
	ATTR_LIST_ENTRY *le = NULL;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 NULL, NULL);

	if (!b)
		return -ENOENT;

	if (!b->non_res) {
		unsigned long pos;
		const unsigned long *bm = resident_data(b);

		nbits = le32_to_cpu(b->res.data_size) * 8;

		if (bit >= nbits)
			return 0;

		pos = find_next_bit(bm, nbits, bit);
		if (pos < nbits)
			return 0;
	} else {
		size_t used = MINUS_ONE_T;

		nbits = le64_to_cpu(b->nres.data_size) * 8;

		if (bit >= nbits)
			return 0;

		err = scan_nres_bitmap(ni->mi.sbi, b, &indx->bitmap_run, bit,
				       &scan_for_used, &used);
		if (err)
			return err;

		if (used != MINUS_ONE_T)
			return 0;
	}

	new_alloc = (u64)bit << indx->index_bits;

	err = attr_set_size(ni, ATTR_ALLOC, in->name, in->name_len,
			    &indx->alloc_run, new_alloc, &new_alloc, false,
			    NULL);
	if (err)
		return err;

	if (in->name == I30_NAME)
		ni->vfs_inode.i_size = new_alloc;

	bpb = bitmap_size(bit);
	if (bpb * 8 == nbits)
		return 0;

	err = attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
			    &indx->bitmap_run, bpb, &bpb, false, NULL);

	return err;
}

static int indx_free_children(ntfs_index *indx, ntfs_inode *ni,
			      const NTFS_DE *e, bool trim)
{
	int err;
	struct indx_node *n;
	INDEX_HDR *hdr;
	CLST vbn = de_get_vbn(e);
	size_t i;

	err = indx_read(indx, ni, vbn, &n);
	if (err)
		return err;

	hdr = &n->index->ihdr;
	/* First, recurse into the children, if any.*/
	if (!hdr_has_subnode(hdr))
		goto putnode;

	for (e = hdr_first_de(hdr); e; e = hdr_next_de(hdr, e)) {
		indx_free_children(indx, ni, e, false);
		if (de_is_last(e))
			break;
	}

putnode:
	put_indx_node(n);

	i = vbn >> indx->idx2vbn_bits;
	/* We've gotten rid of the children; add this buffer to the free list. */
	indx_mark_free(indx, ni, i);

	if (!trim)
		return 0;

	/*
	 * If there are no used indexes after current free index
	 * then we can truncate allocation and bitmap
	 * Use bitmap to estimate the case
	 */
	indx_shrink(indx, ni, i + 1);
	return 0;
}

/*
 * indx_get_entry_to_replace
 *
 * finds a replacement entry for a deleted entry
 * always returns a node entry:
 * NTFS_IE_HAS_SUBNODES is set the flags and the size includes the sub_vcn
 */
static int indx_get_entry_to_replace(ntfs_index *indx, ntfs_inode *ni,
				     const NTFS_DE *de_next,
				     NTFS_DE **de_to_replace,
				     struct ntfs_fnd *fnd)
{
	int err;
	int level = -1;
	CLST vbn;
	NTFS_DE *e, *te, *re;
	struct indx_node *n;
	INDEX_BUFFER *ib;

	*de_to_replace = NULL;

	/* Find first leaf entry down from de_next */
	vbn = de_get_vbn(de_next);
	for (;;) {
		n = NULL;
		err = indx_read(indx, ni, vbn, &n);
		if (err)
			goto out;

		e = hdr_first_de(&n->index->ihdr);
		fnd_push(fnd, n, e);

		if (!de_is_last(e)) {
			/*
			 * This buffer is non-empty, so its first entry could be used as the
			 * replacement entry.
			 */
			level = fnd->level - 1;
		}

		if (!de_has_vcn(e))
			break;

		/* This buffer is a node. Continue to go down */
		vbn = de_get_vbn(e);
	}

	if (level == -1)
		goto out;

	n = fnd->nodes[level];
	te = hdr_first_de(&n->index->ihdr);
	/* Copy the candidate entry into the replacement entry buffer. */
	re = ntfs_alloc(le16_to_cpu(te->size) + sizeof(u64), 0);
	if (!re) {
		err = -ENOMEM;
		goto out;
	}

	*de_to_replace = re;
	memcpy(re, te, le16_to_cpu(te->size));

	if (!de_has_vcn(re)) {
		/*
		 * The replacement entry we found doesn't have a sub_vcn. increase its size
		 * to hold one.
		 */
		le16_add_cpu(&re->size, sizeof(u64));
		re->flags |= NTFS_IE_HAS_SUBNODES;
	} else {
		/*
		 * The replacement entry we found was a node entry, which means that all
		 * its child buffers are empty. Return them to the free pool.
		 */
		indx_free_children(indx, ni, te, true);
	}

	/*
	 * Expunge the replacement entry from its former location,
	 * and then write that buffer.
	 */
	ib = n->index;
	e = hdr_delete_de(&ib->ihdr, te);

	fnd->de[level] = e;
	indx_write(indx, ni, n, 0);

	/* Check to see if this action created an empty leaf. */
	if (ib_is_leaf(ib) && ib_is_empty(ib))
		return 0;

out:
	fnd_clear(fnd);

	return err;
}

/*
 * indx_delete_entry
 *
 * deletes an entry from the index.
 */
int indx_delete_entry(ntfs_index *indx, ntfs_inode *ni, const void *key,
		      u32 key_len, const void *ctx)
{
	int err, diff;
	INDEX_ROOT *root;
	INDEX_HDR *hdr;
	struct ntfs_fnd *fnd, *fnd2;
	INDEX_BUFFER *ib;
	NTFS_DE *e, *re, *next, *prev, *me;
	struct indx_node *n, *n2d = NULL;
	__le64 sub_vbn;
	int level, level2;
	ATTRIB *attr;
	mft_inode *mi;
	u32 e_size, root_size, new_root_size;
	size_t trim_bit;
	const struct INDEX_NAMES *in;

	fnd = fnd_get(indx);
	if (!fnd) {
		err = -ENOMEM;
		goto out2;
	}

	fnd2 = fnd_get(NULL);
	if (!fnd2) {
		err = -ENOMEM;
		goto out1;
	}

	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	/* Locate the entry to remove. */
	err = indx_find(indx, ni, root, key, key_len, ctx, &diff, &e, fnd);
	if (err)
		goto out;

	if (!e || diff) {
		err = -ENOENT;
		goto out;
	}

	level = fnd->level;

	if (level) {
		n = fnd->nodes[level - 1];
		e = fnd->de[level - 1];
		ib = n->index;
		hdr = &ib->ihdr;
	} else {
		hdr = &root->ihdr;
		e = fnd->root_de;
		n = NULL;
	}

	e_size = le16_to_cpu(e->size);

	if (!de_has_vcn_ex(e)) {
		/* The entry to delete is a leaf, so we can just rip it out */
		hdr_delete_de(hdr, e);

		if (level) {
			indx_write(indx, ni, n, 0);

			/*
			 * Check to see if removing that entry made
			 * the leaf empty.
			 */
			if (ib_is_leaf(ib) && ib_is_empty(ib)) {
				fnd_pop(fnd);
				fnd_push(fnd2, n, e);
			}
			goto delete_branch;
		}

		hdr->total = hdr->used;

		/* Shrink resident root attribute */
		mi_resize_attr(mi, attr, 0 - e_size);
		goto out;
	}

	/*
	 * The entry we wish to delete is a node buffer, so we
	 * have to find a replacement for it.
	 */
	next = de_get_next(e);

	err = indx_get_entry_to_replace(indx, ni, next, &re, fnd2);
	if (err)
		goto out;

	if (re) {
		de_set_vbn_le(re, de_get_vbn_le(e));
		hdr_delete_de(hdr, e);

		err = level ? indx_insert_into_buffer(indx, ni, root, re, ctx,
						      fnd->level - 1, fnd) :
			      indx_insert_into_root(indx, ni, re, e, ctx, fnd);
		ntfs_free(re);

		if (err)
			goto out;
	} else {
		/*
		 * There is no replacement for the current entry.
		 * This means that the subtree rooted at its node is empty,
		 * and can be deleted, which turn means that the node can
		 * just inherit the deleted entry sub_vcn
		 */
		indx_free_children(indx, ni, next, true);

		de_set_vbn_le(next, de_get_vbn_le(e));
		hdr_delete_de(hdr, e);
		if (level)
			indx_write(indx, ni, n, 0);
		else {
			hdr->total = hdr->used;

			/* Shrink resident root attribute */
			mi_resize_attr(mi, attr, 0 - e_size);
		}
	}

delete_branch:

	/* Delete a branch of tree */
	if (!fnd2 || !fnd2->level)
		goto out;

	/* Reinit root 'cause it can be changed */
	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	n2d = NULL;
	sub_vbn = fnd2->nodes[0]->index->vbn;
	level2 = 0;
	level = fnd->level;

	hdr = level ? &fnd->nodes[level - 1]->index->ihdr : &root->ihdr;

	/* Scan current level */
	for (e = hdr_first_de(hdr);; e = hdr_next_de(hdr, e)) {
		if (!e) {
			err = -EINVAL;
			goto out;
		}

		if (de_has_vcn(e) && sub_vbn == de_get_vbn_le(e))
			break;

		if (de_is_last(e)) {
			e = NULL;
			break;
		}
	}

	if (!e) {
		/* Do slow search from root */
		struct indx_node *in;

		fnd_clear(fnd);

		in = indx_find_buffer(indx, ni, root, sub_vbn, NULL);
		if (IS_ERR(in)) {
			err = PTR_ERR(in);
			goto out;
		}

		if (in)
			fnd_push(fnd, in, NULL);
	}

	/* Merge fnd2 -> fnd */
	for (level = 0; level < fnd2->level; level++) {
		fnd_push(fnd, fnd2->nodes[level], fnd2->de[level]);
		fnd2->nodes[level] = NULL;
	}
	fnd2->level = 0;

	hdr = NULL;
	for (level = fnd->level; level; level--) {
		struct indx_node *in = fnd->nodes[level - 1];

		ib = in->index;
		if (ib_is_empty(ib)) {
			sub_vbn = ib->vbn;
		} else {
			hdr = &ib->ihdr;
			n2d = in;
			level2 = level;
			break;
		}
	}

	if (!hdr)
		hdr = &root->ihdr;

	e = hdr_first_de(hdr);
	if (!e) {
		err = -EINVAL;
		goto out;
	}

	if (hdr == &root->ihdr && de_is_last(e))
		goto collapse_tree;

	prev = NULL;
	while (!de_is_last(e)) {
		if (de_has_vcn(e) && sub_vbn == de_get_vbn_le(e))
			break;
		prev = e;
		e = hdr_next_de(hdr, e);
		if (!e) {
			err = -EINVAL;
			goto out;
		}
	}

	if (sub_vbn != de_get_vbn_le(e)) {
		/*
		 * Didn't find the parent entry, although this buffer is the parent trail.
		 * Something is corrupt.
		 */
		err = -EINVAL;
		goto out;
	}

	if (de_is_last(e)) {
		/*
		 * Since we can't remove the end entry, we'll remove its
		 * predecessor instead. This means we have to transfer the
		 * predecessor's sub_vcn to the end entry.
		 * Note: that this index block is not empty, so the
		 * predecessor must exist
		 */
		if (!prev) {
			err = -EINVAL;
			goto out;
		}

		if (de_has_vcn(prev)) {
			de_set_vbn_le(e, de_get_vbn_le(prev));
		} else if (de_has_vcn(e)) {
			le16_sub_cpu(&e->size, sizeof(u64));
			e->flags &= ~NTFS_IE_HAS_SUBNODES;
			le32_sub_cpu(&hdr->used, sizeof(u64));
		}
		e = prev;
	}

	/*
	 * Copy the current entry into a temporary buffer (stripping off its
	 * down-pointer, if any) and delete it from the current buffer or root,
	 * as appropriate.
	 */
	e_size = le16_to_cpu(e->size);
	me = ntfs_memdup(e, e_size);
	if (!me) {
		err = -ENOMEM;
		goto out;
	}

	if (de_has_vcn(me)) {
		me->flags &= ~NTFS_IE_HAS_SUBNODES;
		le16_sub_cpu(&me->size, sizeof(u64));
	}

	hdr_delete_de(hdr, e);

	if (hdr == &root->ihdr) {
		level = 0;
		hdr->total = hdr->used;

		/* Shrink resident root attribute */
		mi_resize_attr(mi, attr, 0 - e_size);
	} else {
		indx_write(indx, ni, n2d, 0);
		level = level2;
	}

	/* Mark unused buffers as free */
	trim_bit = -1;
	for (; level < fnd->level; level++) {
		ib = fnd->nodes[level]->index;
		if (ib_is_empty(ib)) {
			size_t k = le64_to_cpu(ib->vbn) >> indx->idx2vbn_bits;

			indx_mark_free(indx, ni, k);
			if (k < trim_bit)
				trim_bit = k;
		}
	}

	fnd_clear(fnd);
	/*fnd->root_de = NULL;*/

	/*
	 * Re-insert the entry into the tree.
	 * Find the spot the tree where we want to insert the new entry.
	 */
	err = indx_insert_entry(indx, ni, me, ctx, fnd);
	ntfs_free(me);
	if (err)
		goto out;

	if (trim_bit != -1)
		indx_shrink(indx, ni, trim_bit);
	goto out;

collapse_tree:

	/*
	 * This tree needs to be collapsed down to an empty root.
	 * Recreate the index root as an empty leaf and free all the bits the
	 * index allocation bitmap.
	 */
	fnd_clear(fnd);
	fnd_clear(fnd2);

	in = &s_index_names[indx->type];

	err = attr_set_size(ni, ATTR_ALLOC, in->name, in->name_len,
			    &indx->alloc_run, 0, NULL, false, NULL);
	err = ni_remove_attr(ni, ATTR_ALLOC, in->name, in->name_len, false,
			     NULL);
	run_close(&indx->alloc_run);

	err = attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
			    &indx->bitmap_run, 0, NULL, false, NULL);
	err = ni_remove_attr(ni, ATTR_BITMAP, in->name, in->name_len, false,
			     NULL);
	run_close(&indx->bitmap_run);

	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	root_size = le32_to_cpu(attr->res.data_size);
	new_root_size = sizeof(INDEX_ROOT) + sizeof(NTFS_DE);

	if (new_root_size != root_size &&
	    !mi_resize_attr(mi, attr, new_root_size - root_size)) {
		err = -EINVAL;
		goto out;
	}

	/* Fill first entry */
	e = (NTFS_DE *)(root + 1);
	e->ref.low = 0;
	e->ref.high = 0;
	e->ref.seq = 0;
	e->size = cpu_to_le16(sizeof(NTFS_DE));
	e->flags = NTFS_IE_LAST; // 0x02
	e->key_size = 0;
	e->Reserved = 0;

	hdr = &root->ihdr;
	hdr->flags = 0;
	hdr->used = hdr->total =
		cpu_to_le32(new_root_size - offsetof(INDEX_ROOT, ihdr));
	mi->dirty = true;

	if (in->name == I30_NAME)
		ni->vfs_inode.i_size = 0;

out:
	fnd_put(fnd2);
out1:
	indx->changed = true;
	fnd_put(fnd);

out2:
	return err;
}

int indx_update_dup(ntfs_inode *ni, ntfs_sb_info *sbi,
		    const ATTR_FILE_NAME *fname, const NTFS_DUP_INFO *dup,
		    int sync)
{
	int err, diff;
	NTFS_DE *e = NULL;
	ATTR_FILE_NAME *e_fname;
	struct ntfs_fnd *fnd;
	INDEX_ROOT *root;
	mft_inode *mi;
	ntfs_index *indx = &ni->dir;

	fnd = fnd_get(indx);
	if (!fnd) {
		err = -ENOMEM;
		goto out1;
	}

	root = indx_get_root(indx, ni, NULL, &mi);

	if (!root) {
		err = -EINVAL;
		goto out;
	}

	/* Find entries tree and on disk */
	err = indx_find(indx, ni, root, fname, fname_full_size(fname), sbi,
			&diff, &e, fnd);
	if (err)
		goto out;

	if (!e) {
		err = -EINVAL;
		goto out;
	}

	if (diff) {
		err = -EINVAL;
		goto out;
	}

	e_fname = (ATTR_FILE_NAME *)(e + 1);

	if (!memcmp(&e_fname->dup, dup, sizeof(*dup)))
		goto out;

	memcpy(&e_fname->dup, dup, sizeof(*dup));

	if (fnd->level) {
		err = indx_write(indx, ni, fnd->nodes[fnd->level - 1], sync);
	} else if (sync) {
		err = mi_write(mi, 1);
	} else {
		mi->dirty = true;
		mark_inode_dirty(&ni->vfs_inode);
	}

out:
	fnd_put(fnd);

out1:
	return err;
}
