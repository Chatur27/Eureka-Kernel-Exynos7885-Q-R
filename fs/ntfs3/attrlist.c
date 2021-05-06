// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ntfs3/attrib.c
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

/* Returns true if le is valid */
static inline bool al_is_valid_le(const ntfs_inode *ni, ATTR_LIST_ENTRY *le)
{
	if (!le || !ni->attr_list.le || !ni->attr_list.size)
		return false;

	return PtrOffset(ni->attr_list.le, le) + le16_to_cpu(le->size) <=
	       ni->attr_list.size;
}

void al_destroy(ntfs_inode *ni)
{
	run_close(&ni->attr_list.run);
	ntfs_free(ni->attr_list.le);
	ni->attr_list.le = NULL;
	ni->attr_list.size = 0;
	ni->attr_list.dirty = false;
}

/*
 * ntfs_load_attr_list
 *
 * This method makes sure that the ATTRIB list, if present,
 * has been properly set up.
 */
int ntfs_load_attr_list(ntfs_inode *ni, ATTRIB *attr)
{
	int err;
	size_t lsize;
	void *le = NULL;

	if (ni->attr_list.size)
		return 0;

	if (!attr->non_res) {
		lsize = le32_to_cpu(attr->res.data_size);
		le = ntfs_alloc(al_aligned(lsize), 0);
		if (!le) {
			err = -ENOMEM;
			goto out;
		}
		memcpy(le, resident_data(attr), lsize);
	} else if (attr->nres.svcn) {
		err = -EINVAL;
		goto out;
	} else {
		u16 run_off = le16_to_cpu(attr->nres.run_off);

		lsize = le64_to_cpu(attr->nres.data_size);

		run_init(&ni->attr_list.run);

		err = run_unpack_ex(&ni->attr_list.run, ni->mi.sbi, ni->mi.rno,
				    0, le64_to_cpu(attr->nres.evcn),
				    Add2Ptr(attr, run_off),
				    le32_to_cpu(attr->size) - run_off);
		if (err < 0)
			goto out;

		le = ntfs_alloc(al_aligned(lsize), 0);
		if (!le) {
			err = -ENOMEM;
			goto out;
		}

		err = ntfs_read_run_nb(ni->mi.sbi, &ni->attr_list.run, 0, le,
				       lsize, NULL);
		if (err)
			goto out;
	}

	ni->attr_list.size = lsize;
	ni->attr_list.le = le;

	return 0;

out:
	ni->attr_list.le = le;
	al_destroy(ni);

	return err;
}

/*
 * al_enumerate
 *
 * Returns the next list le
 * if le is NULL then returns the first le
 */
ATTR_LIST_ENTRY *al_enumerate(ntfs_inode *ni, ATTR_LIST_ENTRY *le)
{
	size_t off;
	u16 sz;

	if (!le) {
		le = ni->attr_list.le;
	} else {
		sz = le16_to_cpu(le->size);
		if (sz < sizeof(ATTR_LIST_ENTRY)) {
			/* Impossible 'cause we should not return such le */
			return NULL;
		}
		le = Add2Ptr(le, sz);
	}

	/* Check boundary */
	off = PtrOffset(ni->attr_list.le, le);
	if (off + sizeof(ATTR_LIST_ENTRY) > ni->attr_list.size) {
		// The regular end of list
		return NULL;
	}

	sz = le16_to_cpu(le->size);

	/* Check le for errors */
	if (sz < sizeof(ATTR_LIST_ENTRY) || off + sz > ni->attr_list.size ||
	    sz < le->name_off + le->name_len * sizeof(short)) {
		return NULL;
	}

	return le;
}

/*
 * al_find_le
 *
 * finds the first le in the list which matches type, name and vcn
 * Returns NULL if not found
 */
ATTR_LIST_ENTRY *al_find_le(ntfs_inode *ni, ATTR_LIST_ENTRY *le,
			    const ATTRIB *attr)
{
	CLST svcn = attr_svcn(attr);

	return al_find_ex(ni, le, attr->type, attr_name(attr), attr->name_len,
			  &svcn);
}

/*
 * al_find_ex
 *
 * finds the first le in the list which matches type, name and vcn
 * Returns NULL if not found
 */
ATTR_LIST_ENTRY *al_find_ex(ntfs_inode *ni, ATTR_LIST_ENTRY *le, ATTR_TYPE type,
			    const __le16 *name, u8 name_len, const CLST *vcn)
{
	ATTR_LIST_ENTRY *ret = NULL;
	u32 type_in = le32_to_cpu(type);

	while ((le = al_enumerate(ni, le))) {
		u64 le_vcn;
		int diff;

		/* List entries are sorted by type, name and vcn */
		diff = le32_to_cpu(le->type) - type_in;
		if (diff < 0)
			continue;

		if (diff > 0)
			return ret;

		if (le->name_len != name_len)
			continue;

		if (name_len &&
		    memcmp(le_name(le), name, name_len * sizeof(short)))
			continue;

		if (!vcn)
			return le;

		le_vcn = le64_to_cpu(le->vcn);
		if (*vcn == le_vcn)
			return le;

		if (*vcn < le_vcn)
			return ret;

		ret = le;
	}

	return ret;
}

/*
 * al_find_le_to_insert
 *
 * finds the first list entry which matches type, name and vcn
 * Returns NULL if not found
 */
static ATTR_LIST_ENTRY *al_find_le_to_insert(ntfs_inode *ni, ATTR_TYPE type,
					     const __le16 *name, u8 name_len,
					     const CLST *vcn)
{
	ATTR_LIST_ENTRY *le = NULL, *prev;
	u32 type_in = le32_to_cpu(type);
	int diff;

	/* List entries are sorted by type, name, vcn */
next:
	le = al_enumerate(ni, prev = le);
	if (!le)
		goto out;
	diff = le32_to_cpu(le->type) - type_in;
	if (diff < 0)
		goto next;
	if (diff > 0)
		goto out;

	if (ntfs_cmp_names(name, name_len, le_name(le), le->name_len, NULL) > 0)
		goto next;

	if (!vcn || *vcn > le64_to_cpu(le->vcn))
		goto next;

out:
	if (!le)
		le = prev ? Add2Ptr(prev, le16_to_cpu(prev->size)) :
			    ni->attr_list.le;

	return le;
}

/*
 * al_add_le
 *
 * adds an "attribute list entry" to the list.
 */
int al_add_le(ntfs_inode *ni, ATTR_TYPE type, const __le16 *name, u8 name_len,
	      CLST svcn, __le16 id, const MFT_REF *ref,
	      ATTR_LIST_ENTRY **new_le)
{
	int err;
	ATTRIB *attr;
	ATTR_LIST_ENTRY *le;
	size_t off;
	u16 sz;
	size_t asize, new_asize;
	u64 new_size;
	typeof(ni->attr_list) *al = &ni->attr_list;

	/*
	 * Compute the size of the new le and the new length of the
	 * list with al le added.
	 */
	sz = le_size(name_len);
	new_size = al->size + sz;
	asize = al_aligned(al->size);
	new_asize = al_aligned(new_size);

	/* Scan forward to the point at which the new le should be inserted. */
	le = al_find_le_to_insert(ni, type, name, name_len, &svcn);
	off = PtrOffset(al->le, le);

	if (new_size > asize) {
		void *ptr = ntfs_alloc(new_asize, 0);

		if (!ptr)
			return -ENOMEM;

		memcpy(ptr, al->le, off);
		memcpy(Add2Ptr(ptr, off + sz), le, al->size - off);
		le = Add2Ptr(ptr, off);
		ntfs_free(al->le);
		al->le = ptr;
	} else {
		memmove(Add2Ptr(le, sz), le, al->size - off);
	}

	al->size = new_size;

	le->type = type;
	le->size = cpu_to_le16(sz);
	le->name_len = name_len;
	le->name_off = offsetof(ATTR_LIST_ENTRY, name);
	le->vcn = cpu_to_le64(svcn);
	le->ref = *ref;
	le->id = id;
	memcpy(le->name, name, sizeof(short) * name_len);

	al->dirty = true;

	err = attr_set_size(ni, ATTR_LIST, NULL, 0, &al->run, new_size,
			    &new_size, true, &attr);
	if (err)
		return err;

	if (attr && attr->non_res) {
		err = ntfs_sb_write_run(ni->mi.sbi, &al->run, 0, al->le,
					al->size);
		if (err)
			return err;
	}

	al->dirty = false;
	*new_le = le;

	return 0;
}

/*
 * al_remove_le
 *
 * removes 'le' from attribute list
 */
bool al_remove_le(ntfs_inode *ni, ATTR_LIST_ENTRY *le)
{
	u16 size;
	size_t off;
	typeof(ni->attr_list) *al = &ni->attr_list;

	if (!al_is_valid_le(ni, le))
		return false;

	/* Save on stack the size of le */
	size = le16_to_cpu(le->size);
	off = PtrOffset(al->le, le);

	memmove(le, Add2Ptr(le, size), al->size - (off + size));

	al->size -= size;
	al->dirty = true;

	return true;
}

/*
 * al_delete_le
 *
 * deletes from the list the first le which matches its parameters.
 */
bool al_delete_le(ntfs_inode *ni, ATTR_TYPE type, CLST vcn, const __le16 *name,
		  size_t name_len, const MFT_REF *ref)
{
	u16 size;
	ATTR_LIST_ENTRY *le;
	size_t off;
	typeof(ni->attr_list) *al = &ni->attr_list;

	/* Scan forward to the first le that matches the input */
	le = al_find_ex(ni, NULL, type, name, name_len, &vcn);
	if (!le)
		return false;

	off = PtrOffset(al->le, le);

	if (!ref)
		goto del;

	/*
	 * The caller specified a segment reference, so we have to
	 * scan through the matching entries until we find that segment
	 * reference or we run of matching entries.
	 */
next:
	if (off + sizeof(ATTR_LIST_ENTRY) > al->size)
		goto del;
	if (le->type != type)
		goto del;
	if (le->name_len != name_len)
		goto del;
	if (name_len &&
	    memcmp(name, Add2Ptr(le, le->name_off), name_len * sizeof(short)))
		goto del;
	if (le64_to_cpu(le->vcn) != vcn)
		goto del;
	if (!memcmp(ref, &le->ref, sizeof(*ref)))
		goto del;

	off += le16_to_cpu(le->size);
	le = Add2Ptr(al->le, off);
	goto next;

del:
	/*
	 * If we've gone off the end of the list, or if the type, name,
	 * and vcn don't match, then we don't have any matching records.
	 */
	if (off >= al->size)
		return false;
	if (le->type != type)
		return false;
	if (le->name_len != name_len)
		return false;
	if (name_len &&
	    memcmp(name, Add2Ptr(le, le->name_off), name_len * sizeof(short)))
		return false;
	if (le64_to_cpu(le->vcn) != vcn)
		return false;

	/* Save on stack the size of le */
	size = le16_to_cpu(le->size);
	/* Delete the le. */
	memmove(le, Add2Ptr(le, size), al->size - (off + size));

	al->size -= size;
	al->dirty = true;
	return true;
}

/*
 * al_update
 *
 *
 */
int al_update(ntfs_inode *ni)
{
	int err;
	ntfs_sb_info *sbi = ni->mi.sbi;
	ATTRIB *attr;
	typeof(ni->attr_list) *al = &ni->attr_list;

	if (!al->dirty)
		return 0;

	err = attr_set_size(ni, ATTR_LIST, NULL, 0, &al->run, al->size, NULL,
			    false, &attr);
	if (err)
		goto out;

	if (!attr->non_res)
		memcpy(resident_data(attr), al->le, al->size);
	else {
		err = ntfs_sb_write_run(sbi, &al->run, 0, al->le, al->size);
		if (err)
			goto out;

		attr->nres.valid_size = attr->nres.data_size;
	}

	ni->mi.dirty = true;
	al->dirty = false;

out:
	return err;
}
