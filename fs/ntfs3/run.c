// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ntfs3/run.c
 *
 * Copyright (C) 2019-2020 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/nls.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

struct ntfs_run {
	CLST vcn; /* virtual cluster number */
	CLST len; /* length in clusters */
	CLST lcn; /* logical cluster number */
};

/*
 * run_lookup
 *
 * Lookup the index of a MCB entry that is first <= vcn.
 * case of success it will return non-zero value and set
 * 'index' parameter to index of entry been found.
 * case of entry missing from list 'index' will be set to
 * point to insertion position for the entry question.
 */
bool run_lookup(const struct runs_tree *run, CLST vcn, size_t *index)
{
	size_t min_idx, max_idx, mid_idx;
	struct ntfs_run *r;

	if (!run->count) {
		*index = 0;
		return false;
	}

	min_idx = 0;
	max_idx = run->count - 1;

	/* Check boundary cases specially, 'cause they cover the often requests */
	r = run->runs_;
	if (vcn < r->vcn) {
		*index = 0;
		return false;
	}

	if (vcn < r->vcn + r->len) {
		*index = 0;
		return true;
	}

	r += max_idx;
	if (vcn >= r->vcn + r->len) {
		*index = run->count;
		return false;
	}

	if (vcn >= r->vcn) {
		*index = max_idx;
		return true;
	}

	do {
		mid_idx = min_idx + ((max_idx - min_idx) >> 1);
		r = run->runs_ + mid_idx;

		if (vcn < r->vcn) {
			max_idx = mid_idx - 1;
			if (!mid_idx)
				break;
		} else if (vcn >= r->vcn + r->len) {
			min_idx = mid_idx + 1;
		} else {
			*index = mid_idx;
			return true;
		}
	} while (min_idx <= max_idx);

	*index = max_idx + 1;
	return false;
}

/*
 * run_consolidate
 *
 * consolidate runs starting from a given one.
 */
static void run_consolidate(struct runs_tree *run, size_t index)
{
	size_t i;
	struct ntfs_run *r = run->runs_ + index;

	while (index + 1 < run->count) {
		/*
		 * I should merge current run with next
		 * if start of the next run lies inside one being tested.
		 */
		struct ntfs_run *n = r + 1;
		CLST end = r->vcn + r->len;
		CLST dl;

		/* Stop if runs are not aligned one to another. */
		if (n->vcn > end)
			break;

		dl = end - n->vcn;

		/*
		 * If range at index overlaps with next one
		 * then I will either adjust it's start position
		 * or (if completely matches) dust remove one from the list.
		 */
		if (dl > 0) {
			if (n->len <= dl)
				goto remove_next_range;

			n->len -= dl;
			n->vcn += dl;
			if (n->lcn != SPARSE_LCN)
				n->lcn += dl;
		}

		/*
		 * Stop if sparse mode does not match
		 * both current and next runs.
		 */
		if ((n->lcn == SPARSE_LCN) != (r->lcn == SPARSE_LCN)) {
			index += 1;
			r = n;
			continue;
		}

		/*
		 * Check if volume block
		 * of a next run lcn does not match
		 * last volume block of the current run.
		 */
		if (n->lcn != SPARSE_LCN && n->lcn != r->lcn + r->len)
			break;

		/*
		 * Next and current are siblings.
		 * Eat/join.
		 */
		r->len += n->len - dl;

remove_next_range:
		i = run->count - (index + 1);
		if (i > 1)
			memmove(n, n + 1, sizeof(*n) * (i - 1));

		run->count -= 1;
	}
}

bool run_is_mapped_full(const struct runs_tree *run, CLST svcn, CLST evcn)
{
	size_t index;

	if (!run_lookup(run, svcn, &index))
		return false;

	do {
		const struct ntfs_run *m = run->runs_ + index;
		CLST end = m->vcn + m->len;

		if (end > evcn)
			return true;
	} while (++index < run->count);

	return false;
}

bool run_lookup_entry(const struct runs_tree *run, CLST vcn, CLST *lcn,
		      CLST *len, size_t *index)
{
	size_t idx;
	CLST gap;
	struct ntfs_run *r;

	/* Fail immediately if nrun was not touched yet. */
	if (!run->runs_)
		return false;

	if (!run_lookup(run, vcn, &idx))
		return false;

	r = run->runs_ + idx;

	if (vcn >= r->vcn + r->len)
		return false;

	gap = vcn - r->vcn;
	if (r->len <= gap)
		return false;

	*lcn = r->lcn == SPARSE_LCN ? SPARSE_LCN : (r->lcn + gap);

	if (len)
		*len = r->len - gap;
	if (index)
		*index = idx;

	return true;
}

/*
 * run_truncate_head
 *
 * decommit the range before vcn
 */
void run_truncate_head(struct runs_tree *run, CLST vcn)
{
	size_t index;
	struct ntfs_run *r;

	if (run_lookup(run, vcn, &index)) {
		r = run->runs_ + index;

		if (vcn > r->vcn) {
			CLST dlen = vcn - r->vcn;

			r->vcn = vcn;
			r->len -= dlen;
			if (r->lcn != SPARSE_LCN)
				r->lcn += dlen;
		}

		if (!index)
			return;
	}
	r = run->runs_;
	memmove(r, r + index, sizeof(*r) * (run->count - index));

	run->count -= index;

	if (!run->count) {
		ntfs_free(run->runs_);
		run->runs_ = NULL;
		run->allocated = 0;
	}
}

/*
 * run_truncate
 *
 * decommit the range after vcn
 */
void run_truncate(struct runs_tree *run, CLST vcn)
{
	size_t index;

	/*
	 * If I hit the range then
	 * I have to truncate one.
	 * If range to be truncated is becoming empty
	 * then it will entirely be removed.
	 */
	if (run_lookup(run, vcn, &index)) {
		struct ntfs_run *r = run->runs_ + index;

		r->len = vcn - r->vcn;

		if (r->len > 0)
			index += 1;
	}

	/*
	 * At this point 'index' is set to
	 * position that should be thrown away (including index itself)
	 * Simple one - just set the limit.
	 */
	run->count = index;

	/* Do not reallocate array 'runs'. Only free if possible */
	if (!index) {
		ntfs_free(run->runs_);
		run->runs_ = NULL;
		run->allocated = 0;
	}
}

/*
 * run_add_entry
 *
 * sets location to known state.
 * run to be added may overlap with existing location.
 * returns false if of memory
 */
bool run_add_entry(struct runs_tree *run, CLST vcn, CLST lcn, CLST len)
{
	size_t used, index;
	struct ntfs_run *r;
	bool inrange;
	CLST tail_vcn = 0, tail_len = 0, tail_lcn = 0;
	bool should_add_tail = false;

	/*
	 * Lookup the insertion point.
	 *
	 * Execute bsearch for the entry containing
	 * start position question.
	 */
	inrange = run_lookup(run, vcn, &index);

	/*
	 * Shortcut here would be case of
	 * range not been found but one been added
	 * continues previous run.
	 * this case I can directly make use of
	 * existing range as my start point.
	 */
	if (!inrange && index > 0) {
		struct ntfs_run *t = run->runs_ + index - 1;

		if (t->vcn + t->len == vcn &&
		    (t->lcn == SPARSE_LCN) == (lcn == SPARSE_LCN) &&
		    (lcn == SPARSE_LCN || lcn == t->lcn + t->len)) {
			inrange = true;
			index -= 1;
		}
	}

	/*
	 * At this point 'index' either points to the range
	 * containing start position or to the insertion position
	 * for a new range.
	 * So first let's check if range I'm probing is here already.
	 */
	if (!inrange)
		goto requires_new_range;

	r = run->runs_ + index;

	/*
	 * If one of ranges was not allocated
	 * then I have to split location I just matched.
	 * and insert current one
	 * a common case this requires tail to be reinserted
	 * a recursive call.
	 */
	if (((lcn == SPARSE_LCN) != (r->lcn == SPARSE_LCN)) ||
	    (lcn != SPARSE_LCN && lcn != r->lcn + (vcn - r->vcn))) {
		CLST to_eat = vcn - r->vcn;
		CLST Tovcn = to_eat + len;

		should_add_tail = Tovcn < r->len;

		if (should_add_tail) {
			if (r->lcn == SPARSE_LCN)
				tail_lcn = (CLST)SPARSE_LCN;
			else
				tail_lcn = r->lcn + Tovcn;
			tail_vcn = r->vcn + Tovcn;
			tail_len = r->len - Tovcn;
		}

		if (to_eat > 0) {
			r->len = to_eat;
			inrange = false;
			index += 1;
			goto requires_new_range;
		}

		/* lcn should match one I'm going to add. */
		r->lcn = lcn;
	}

	/*
	 * If existing range fits then I'm done.
	 * Otherwise extend found one and fall back to range jocode.
	 */
	if (r->vcn + r->len < vcn + len)
		r->len += len - ((r->vcn + r->len) - vcn);
	goto consolidate;

requires_new_range:
	/*
	 * Range was not found.
	 * Insert at position.
	 * 'index' points to the insertion point.
	 * Here is also the place to check
	 * if allocated size exceeded specified limit.
	 * If it does, third part of allocation will be thrown away.
	 */
	used = run->count * sizeof(struct ntfs_run);

	/*
	 * Check allocated space.
	 * If one is not enough to get one more entry
	 * then it will be reallocated
	 */
	if (run->allocated < used + sizeof(struct ntfs_run)) {
		size_t bytes;
		struct ntfs_run *new_ptr;

		/* Use power of 2 for 'bytes'*/
		if (!used)
			bytes = 64;
		else if (used <= 16 * PAGE_SIZE) {
			if (is_power_of2(run->allocated))
				bytes = run->allocated << 1;
			else
				bytes = (size_t)1 << (2 + blksize_bits(used));
		} else {
			bytes = run->allocated + (16 * PAGE_SIZE);
		}

		new_ptr = ntfs_alloc(bytes, 0);
		if (!new_ptr)
			return false;

		r = new_ptr + index;
		memcpy(new_ptr, run->runs_, index * sizeof(struct ntfs_run));
		memcpy(r + 1, run->runs_ + index,
		       sizeof(struct ntfs_run) * (run->count - index));

		ntfs_free(run->runs_);
		run->runs_ = new_ptr;
		run->allocated = bytes;

	} else {
		size_t i = run->count - index;

		r = run->runs_ + index;

		/* memmove appears to be a bottle neck here... */
		if (i > 0)
			memmove(r + 1, r, sizeof(struct ntfs_run) * i);
	}

	r->vcn = vcn;
	r->lcn = lcn;
	r->len = len;
	run->count += 1;

consolidate:

	/*
	 * And normalize it starting from insertion point.
	 * It's possible that no insertion needed case if
	 * start point lies withthe range of an entry
	 * that 'index' points to.
	 */
	if (inrange && index > 0)
		index -= 1;
	run_consolidate(run, index);
	run_consolidate(run, index + 1);

	/*
	 * a special case
	 * I have to add extra range a tail.
	 */
	if (should_add_tail &&
	    !run_add_entry(run, tail_vcn, tail_lcn, tail_len))
		return false;

	return true;
}

/*
 * run_get_entry
 *
 * returns index-th mapped region
 */
bool run_get_entry(const struct runs_tree *run, size_t index, CLST *vcn,
		   CLST *lcn, CLST *len)
{
	const struct ntfs_run *r;

	if (index >= run->count)
		return false;

	r = run->runs_ + index;

	if (!r->len)
		return false;

	if (vcn)
		*vcn = r->vcn;
	if (lcn)
		*lcn = r->lcn;
	if (len)
		*len = r->len;
	return true;
}

/*
 * run_packed_size
 *
 * calculates the size of packed int64
 */
static inline int run_packed_size(const s64 *n)
{
#ifdef __BIG_ENDIAN
	const u8 *p = (const u8 *)n + sizeof(*n) - 1;

	if (*n >= 0) {
		if (p[-7] || p[-6] || p[-5] || p[-4])
			p -= 4;
		if (p[-3] || p[-2])
			p -= 2;
		if (p[-1])
			p -= 1;
		if (p[0] & 0x80)
			p -= 1;
	} else {
		if (p[-7] != 0xff || p[-6] != 0xff || p[-5] != 0xff ||
		    p[-4] != 0xff)
			p -= 4;
		if (p[-3] != 0xff || p[-2] != 0xff)
			p -= 2;
		if (p[-1] != 0xff)
			p -= 1;
		if (!(p[0] & 0x80))
			p -= 1;
	}
	return (const u8 *)n + sizeof(*n) - p;
#else
	const u8 *p = (const u8 *)n;

	if (*n >= 0) {
		if (p[7] || p[6] || p[5] || p[4])
			p += 4;
		if (p[3] || p[2])
			p += 2;
		if (p[1])
			p += 1;
		if (p[0] & 0x80)
			p += 1;
	} else {
		if (p[7] != 0xff || p[6] != 0xff || p[5] != 0xff ||
		    p[4] != 0xff)
			p += 4;
		if (p[3] != 0xff || p[2] != 0xff)
			p += 2;
		if (p[1] != 0xff)
			p += 1;
		if (!(p[0] & 0x80))
			p += 1;
	}

	return 1 + p - (const u8 *)n;
#endif
}

/*
 * run_pack
 *
 * packs runs into buffer
 * packed_vcns - how much runs we have packed
 * packed_size - how much bytes we have used run_buf
 */
int run_pack(const struct runs_tree *run, CLST svcn, CLST len, u8 *run_buf,
	     u32 run_buf_size, CLST *packed_vcns)
{
	CLST next_vcn, vcn, lcn;
	CLST prev_lcn = 0;
	CLST evcn1 = svcn + len;
	int packed_size = 0;
	size_t i;
	bool ok;
	s64 dlcn, len64;
	int offset_size, size_size, t;
	const u8 *p;

	next_vcn = vcn = svcn;

	*packed_vcns = 0;

	if (!len)
		goto out;

	ok = run_lookup_entry(run, vcn, &lcn, &len, &i);

	if (!ok)
		goto error;

	if (next_vcn != vcn)
		goto error;

next_entry:
	/* offset of current fragment relatively to previous fragment */
	dlcn = 0;
	next_vcn = vcn + len;

	if (next_vcn > evcn1)
		len = evcn1 - vcn;

	/*
	 * mirror of len, but signed, because run_packed_size()
	 * works with signed int only
	 */
	len64 = len;

	/* how much bytes is packed len64 */
	size_size = run_packed_size(&len64);

	/* offset_size - how much bytes is packed dlcn */
	if (lcn == SPARSE_LCN)
		offset_size = 0;
	else {
		/* NOTE: lcn can be less than prev_lcn! */
		dlcn = (s64)lcn - prev_lcn;
		offset_size = run_packed_size(&dlcn);
		prev_lcn = lcn;
	}

	t = run_buf_size - packed_size - 2 - offset_size;
	if (t <= 0)
		goto out;

	/* can we store this entire run */
	if (t < size_size)
		goto out;

	if (!run_buf)
		goto skip_pack;

	p = (u8 *)&len64;

	/* pack run header */
	run_buf[0] = ((u8)(size_size | (offset_size << 4)));
	run_buf += 1;

	/* Pack the length of run */
	switch (size_size) {
#ifdef __BIG_ENDIAN
	case 8:
		run_buf[7] = p[0];
		fallthrough;
	case 7:
		run_buf[6] = p[1];
		fallthrough;
	case 6:
		run_buf[5] = p[2];
		fallthrough;
	case 5:
		run_buf[4] = p[3];
		fallthrough;
	case 4:
		run_buf[3] = p[4];
		fallthrough;
	case 3:
		run_buf[2] = p[5];
		fallthrough;
	case 2:
		run_buf[1] = p[6];
		fallthrough;
	case 1:
		run_buf[0] = p[7];
#else
	case 8:
		run_buf[7] = p[7];
		fallthrough;
	case 7:
		run_buf[6] = p[6];
		fallthrough;
	case 6:
		run_buf[5] = p[5];
		fallthrough;
	case 5:
		run_buf[4] = p[4];
		fallthrough;
	case 4:
		run_buf[3] = p[3];
		fallthrough;
	case 3:
		run_buf[2] = p[2];
		fallthrough;
	case 2:
		run_buf[1] = p[1];
		fallthrough;
	case 1:
		run_buf[0] = p[0];
#endif
	}

	run_buf += size_size;
	p = (u8 *)&dlcn;

	/* Pack the offset from previous lcn */
	switch (offset_size) {
#ifdef __BIG_ENDIAN
	case 8:
		run_buf[7] = p[0];
		fallthrough;
	case 7:
		run_buf[6] = p[1];
		fallthrough;
	case 6:
		run_buf[5] = p[2];
		fallthrough;
	case 5:
		run_buf[4] = p[3];
		fallthrough;
	case 4:
		run_buf[3] = p[4];
		fallthrough;
	case 3:
		run_buf[2] = p[5];
		fallthrough;
	case 2:
		run_buf[1] = p[6];
		fallthrough;
	case 1:
		run_buf[0] = p[7];
#else
	case 8:
		run_buf[7] = p[7];
		fallthrough;
	case 7:
		run_buf[6] = p[6];
		fallthrough;
	case 6:
		run_buf[5] = p[5];
		fallthrough;
	case 5:
		run_buf[4] = p[4];
		fallthrough;
	case 4:
		run_buf[3] = p[3];
		fallthrough;
	case 3:
		run_buf[2] = p[2];
		fallthrough;
	case 2:
		run_buf[1] = p[1];
		fallthrough;
	case 1:
		run_buf[0] = p[0];
#endif
	}

	run_buf += offset_size;

skip_pack:

	packed_size += 1 + offset_size + size_size;
	*packed_vcns += len;

	if (packed_size + 1 >= run_buf_size || next_vcn >= evcn1)
		goto out;

	ok = run_get_entry(run, ++i, &vcn, &lcn, &len);
	if (!ok)
		goto error;

	if (next_vcn != vcn)
		goto error;

	goto next_entry;

out:
	/* Store last zero */
	if (run_buf)
		run_buf[0] = 0;

	return packed_size + 1;

error:
	return -EOPNOTSUPP;
}

/*
 * run_unpack
 *
 * unpacks packed runs from "run_buf"
 * returns error, if negative, or real used bytes
 */
int run_unpack(struct runs_tree *run, ntfs_sb_info *sbi, CLST ino, CLST svcn,
	       CLST evcn, const u8 *run_buf, u32 run_buf_size)
{
	u64 prev_lcn, vcn;
	const u8 *run_last, *run_0;
	u64 lcn, len;
	/* size_size - how much bytes is packed len */
	u8 size_size;
	/* offset_size - how much bytes is packed dlcn */
	u8 offset_size;
	u8 *p;
	u64 next_vcn;
	s64 dlcn;

	/* Check for empty */
	if (evcn + 1 == svcn)
		return 0;

	if (evcn < svcn)
		return -EINVAL;

	run_0 = run_buf;
	run_last = run_buf + run_buf_size;
	prev_lcn = 0;
	vcn = svcn;

	/* Read all runs the chain */
next_run:
	/* size_size - how much bytes is packed len */
	size_size = *run_buf & 0xF;
	/* offset_size - how much bytes is packed dlcn */
	offset_size = *run_buf++ >> 4;

	/*
	 * Unpack runs.
	 * NOTE: runs are stored little endian order
	 * "len" is unsigned value, "dlcn" is signed
	 */
	if (!size_size)
		goto out;

	/*
	 * Large positive number requires to store 5 bytes
	 * e.g.: 05 FF 7E FF FF 00 00 00
	 */
	len = 0;
	p = (u8 *)&len;

	switch (size_size) {
	default:
error:
		return -EINVAL;

#ifdef __BIG_ENDIAN
	case 8:
		p[0] = run_buf[7];
		fallthrough;
	case 7:
		p[1] = run_buf[6];
		fallthrough;
	case 6:
		p[2] = run_buf[5];
		fallthrough;
	case 5:
		p[3] = run_buf[4];
		fallthrough;
	case 4:
		p[4] = run_buf[3];
		fallthrough;
	case 3:
		p[5] = run_buf[2];
		fallthrough;
	case 2:
		p[6] = run_buf[1];
		fallthrough;
	case 1:
		p[7] = run_buf[0];
#else
	case 8:
		p[7] = run_buf[7];
		fallthrough;
	case 7:
		p[6] = run_buf[6];
		fallthrough;
	case 6:
		p[5] = run_buf[5];
		fallthrough;
	case 5:
		p[4] = run_buf[4];
		fallthrough;
	case 4:
		p[3] = run_buf[3];
		fallthrough;
	case 3:
		p[2] = run_buf[2];
		fallthrough;
	case 2:
		p[1] = run_buf[1];
		fallthrough;
	case 1:
		p[0] = run_buf[0];
#endif
	}

	/* skip size_size */
	run_buf += size_size;

	if (!len)
		goto error;

	if (!offset_size) {
		lcn = SPARSE_LCN;
		goto next_vcn;
	}
	/* Check sign */
	dlcn = (run_buf[offset_size - 1] & 0x80) ? (s64)-1 : 0;
	p = (u8 *)&dlcn;

	switch (offset_size) {
	default:
		goto error;

#ifdef __BIG_ENDIAN
	case 8:
		p[0] = run_buf[7];
		fallthrough;
	case 7:
		p[1] = run_buf[6];
		fallthrough;
	case 6:
		p[2] = run_buf[5];
		fallthrough;
	case 5:
		p[3] = run_buf[4];
		fallthrough;
	case 4:
		p[4] = run_buf[3];
		fallthrough;
	case 3:
		p[5] = run_buf[2];
		fallthrough;
	case 2:
		p[6] = run_buf[1];
		fallthrough;
	case 1:
		p[7] = run_buf[0];
#else
	case 8:
		p[7] = run_buf[7];
		fallthrough;
	case 7:
		p[6] = run_buf[6];
		fallthrough;
	case 6:
		p[5] = run_buf[5];
		fallthrough;
	case 5:
		p[4] = run_buf[4];
		fallthrough;
	case 4:
		p[3] = run_buf[3];
		fallthrough;
	case 3:
		p[2] = run_buf[2];
		fallthrough;
	case 2:
		p[1] = run_buf[1];
		fallthrough;
	case 1:
		p[0] = run_buf[0];
#endif
	}

	/* skip offset_size */
	run_buf += offset_size;
	lcn = prev_lcn + dlcn;
	prev_lcn = lcn;

next_vcn:
	next_vcn = vcn + len;
	/* check boundary */
	if (next_vcn > evcn + 1)
		goto error;

#ifndef NTFS3_64BIT_CLUSTER
	if ((vcn >> 32)
	    /* 0xffffffffffffffff is a valid 'lcn' */
	    || (lcn + 1) > 0x100000000ull || (len >> 32)) {
		goto error;
	}
#endif

	if (!run)
		; /* called from check_attr(fslog.c) to check run */
	else if ((size_t)run == 1) {
		/* called from ni_delete_all to free clusters without storing in run */
		if (lcn != SPARSE_LCN)
			mark_as_free_ex(sbi, lcn, len, true);
	} else if (!run_add_entry(run, vcn, lcn, len))
		return -ENOMEM;

	if (lcn != SPARSE_LCN && lcn + len > sbi->used.bitmap.nbits)
		return -EINVAL;

	vcn = next_vcn;

	if (run_buf < run_last)
		goto next_run;

out:
	/* Check vcn consistency */
	if (vcn == evcn + 1)
		return run_buf - run_0;

	return -EINVAL;
}

#ifdef NTFS3_CHECK_FREE_CLST
/*
 * run_unpack_ex
 *
 * unpacks packed runs from "run_buf"
 * checks unpacked runs to be used in bitmap
 * returns error, if negative, or real used bytes
 */
int run_unpack_ex(struct runs_tree *run, ntfs_sb_info *sbi, CLST ino, CLST svcn,
		  CLST evcn, const u8 *run_buf, u32 run_buf_size)
{
	int ret, err;
	CLST next_vcn, vcn, lcn, len;
	size_t index;
	bool ok;
	wnd_bitmap *wnd;

	ret = run_unpack(run, sbi, ino, svcn, evcn, run_buf, run_buf_size);
	if (ret < 0)
		return ret;

	if (!sbi->used.bitmap.sb || !run || (size_t)run == 1)
		return ret;

	if (ino == MFT_REC_BADCLUST)
		return ret;

	next_vcn = vcn = svcn;
	wnd = &sbi->used.bitmap;

	for (ok = run_lookup_entry(run, vcn, &lcn, &len, &index);
	     next_vcn <= evcn;
	     ok = run_get_entry(run, ++index, &vcn, &lcn, &len)) {
		CLST real_free, i;

		if (!ok || next_vcn != vcn)
			return -EINVAL;

		next_vcn = vcn + len;

		if (lcn == SPARSE_LCN)
			continue;

		if (sbi->flags & NTFS_FLAGS_NEED_REPLAY)
			continue;

next:
		down_read_nested(&wnd->rw_lock, BITMAP_MUTEX_CLUSTERS);
		/* Check for free blocks */
		ok = wnd_is_used(wnd, lcn, len);
		up_read(&wnd->rw_lock);
		if (ok)
			continue;

		ntfs_set_state(sbi, NTFS_DIRTY_ERROR);

		if (!down_write_trylock(&wnd->rw_lock))
			continue;

		/* Find first free */
		real_free = len;
		while (real_free && !wnd_is_free(wnd, lcn, 1)) {
			lcn += 1;
			real_free -= 1;
		}

		if (!real_free) {
			up_write(&wnd->rw_lock);
			continue;
		}

		/* Find total free */
		i = 1;
		while (i < real_free && wnd_is_free(wnd, lcn + i, 1))
			i += 1;

		real_free = i;

		err = wnd_set_used(wnd, lcn, real_free);
		up_write(&wnd->rw_lock);

		if (err)
			return err;

		if (len != real_free) {
			len -= real_free + 1;
			lcn += real_free + 1;
			goto next;
		}
	}

	return ret;
}
#endif

/*
 * run_get_highest_vcn
 *
 * returns the highest vcn from a mapping pairs array
 * it used while replaying log file
 */
int run_get_highest_vcn(CLST vcn, const u8 *run_buf, u64 *highest_vcn)
{
	const u8 *run = run_buf;
	u64 vcn64 = vcn;
	u8 size_size;
	u8 offset_size;
	u64 len;
	u8 *p;

next_run:
	size_size = *run & 0xF;
	if (!size_size)
		goto out;

	offset_size = *run++ >> 4;

	len = 0;
	p = (u8 *)&len;

	switch (size_size) {
	default:
error:
		return -EINVAL;

#ifdef __BIG_ENDIAN
	case 8:
		p[0] = run[7];
		fallthrough;
	case 7:
		p[1] = run[6];
		fallthrough;
	case 6:
		p[2] = run[5];
		fallthrough;
	case 5:
		p[3] = run[4];
		fallthrough;
	case 4:
		p[4] = run[3];
		fallthrough;
	case 3:
		p[5] = run[2];
		fallthrough;
	case 2:
		p[6] = run[1];
		fallthrough;
	case 1:
		p[7] = run[0];
#else
	case 8:
		p[7] = run[7];
		fallthrough;
	case 7:
		p[6] = run[6];
		fallthrough;
	case 6:
		p[5] = run[5];
		fallthrough;
	case 5:
		p[4] = run[4];
		fallthrough;
	case 4:
		p[3] = run[3];
		fallthrough;
	case 3:
		p[2] = run[2];
		fallthrough;
	case 2:
		p[1] = run[1];
		fallthrough;
	case 1:
		p[0] = run[0];
#endif
	}

	/* skip size_size */
	run += size_size;

	if (!len)
		goto error;

	run += offset_size;

#ifdef NTFS3_64BIT_CLUSTER
	if ((vcn >> 32) || (len >> 32))
		goto error;
#endif
	vcn64 += len;
	goto next_run;

out:
	*highest_vcn = vcn64 - 1;
	return 0;
}
