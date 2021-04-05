/*
 * sdp_name.c
 *
 */
#include <linux/crypto.h>
#include <sdp/fs_handler.h>
#if defined(CONFIG_EXT4CRYPT_SDP) || defined(CONFIG_DDAR)
#include "../fscrypt_knox_private.h"
#endif

#ifdef CONFIG_EXT4CRYPT_SDP
#include "../ext4.h"
#include "../ext4_crypto.h"
#include "fscrypto_sdp_dek_private.h"
#include "fscrypto_sdp_name.h"
#endif

struct fscrypt_sdp_renament {
	struct inode *dir;
	struct dentry *dentry;
	struct inode *inode;
};

static int fscrypt_sdp_get_storage_type(struct dentry *target_dentry)
{
	if (!target_dentry)
		return FSCRYPT_SDP_NAME_FUNC_ERROR;
	else {
		int p_type = FSCRYPT_SDP_NAME_FUNC_ERROR;
		struct dentry *t_dentry = target_dentry;

		while (strcasecmp(t_dentry->d_name.name, "/")) {// "/" means "/data"
			if (!strcasecmp(t_dentry->d_name.name, "user")) {
				p_type = FSCRYPT_STORAGE_TYPE_DATA_CE;
			} else if (!strcasecmp(t_dentry->d_name.name, "media")) {
				p_type = FSCRYPT_STORAGE_TYPE_MEDIA_CE;
			} else if (!strcasecmp(t_dentry->d_name.name, "system_ce")) {
				p_type = FSCRYPT_STORAGE_TYPE_SYSTEM_CE;
			} else if (!strcasecmp(t_dentry->d_name.name, "misc_ce")) {
				p_type = FSCRYPT_STORAGE_TYPE_MISC_CE;
			} else if (!strcasecmp(t_dentry->d_name.name, "user_de")) {
				p_type = FSCRYPT_STORAGE_TYPE_DATA_DE;
			} else if (!strcasecmp(t_dentry->d_name.name, "system_de")) {
				p_type = FSCRYPT_STORAGE_TYPE_SYSTEM_DE;
			} else if (!strcasecmp(t_dentry->d_name.name, "misc_de")) {
				p_type = FSCRYPT_STORAGE_TYPE_MISC_DE;
			} else if (!strcasecmp(t_dentry->d_name.name, "enc_user")) {
				p_type = FSCRYPT_STORAGE_TYPE_SDP_ENC_USER;
			} else if (!strcasecmp(t_dentry->d_name.name, "knox")) {
				p_type = FSCRYPT_STORAGE_TYPE_SDP_ENC_EMULATED;
			} else {
				p_type = FSCRYPT_SDP_NAME_FUNC_ERROR;
			}
			t_dentry = t_dentry->d_parent;
		}

		return p_type;
	}
}

void fscrypt_sdp_check_chamber_event(struct inode *old_dir, struct dentry *old_dentry,
					struct inode *new_dir, struct dentry *new_dentry)
{
	struct fscrypt_sdp_renament old = {
		.dir = old_dir,
		.dentry = old_dentry,
		.inode = d_inode(old_dentry),
	};
	struct fscrypt_sdp_renament new = {
		.dir = new_dir,
		.dentry = new_dentry,
		.inode = d_inode(new_dentry),
	};

	/* EXT4CRYPT-dedicated */
	if (EXT4_I(old_dir)->i_crypt_info &&
				EXT4_I(new_dir)->i_crypt_info) {
		sdp_fs_command_t *cmd = NULL;
		int rename_event = 0x00;

		if (FSCRYPT_IS_SENSITIVE_DENTRY(old.dentry->d_parent) &&
				!FSCRYPT_IS_SENSITIVE_DENTRY(new.dentry->d_parent)) {
			rename_event |= FSCRYPT_EVT_RENAME_OUT_OF_CHAMBER;
		}

		if (!FSCRYPT_IS_SENSITIVE_DENTRY(old.dentry->d_parent) &&
				FSCRYPT_IS_SENSITIVE_DENTRY(new.dentry->d_parent)) {
			rename_event |= FSCRYPT_EVT_RENAME_TO_CHAMBER;
		}

		if ((rename_event & FSCRYPT_EVT_RENAME_TO_CHAMBER) &&
				!FSCRYPT_IS_SENSITIVE_DENTRY(old.dentry)) {//Protected dir to Sensitive area
			cmd = sdp_fs_command_alloc(FSOP_SDP_SET_SENSITIVE, current->pid,
								fscrypt_sdp_get_engine_id(new.dir),
								fscrypt_sdp_get_storage_type(new.dentry->d_parent),
								old.inode->i_ino,
								GFP_NOFS);
		} else if (rename_event & FSCRYPT_EVT_RENAME_OUT_OF_CHAMBER) {//Sensitive dir to Protected area
			cmd = sdp_fs_command_alloc(FSOP_SDP_SET_PROTECTED, current->pid,
								fscrypt_sdp_get_engine_id(old.dir),
								fscrypt_sdp_get_storage_type(new.dentry->d_parent),
								old.inode->i_ino,
								GFP_NOFS);
		}

		if (cmd != NULL) {
			sdp_fs_request(cmd, NULL);
			sdp_fs_command_free(cmd);
		}
	}
}

int fscrypt_sdp_check_rename_pre(struct dentry *old_dentry)
{
	/* EXT4CRYPT-dedicated */
	if (EXT4_I(old_dentry->d_inode)->i_crypt_info &&
			EXT4_I(old_dentry->d_inode)->i_crypt_info->ci_sdp_info &&
			FSCRYPT_IS_CHAMBER_DENTRY(old_dentry)) {
		printk_once(KERN_WARNING
				"Renaming Chamber directory, I/O error\n");
		return -EIO;
	}

	return 0;
}

void fscrypt_sdp_check_rename_post(struct inode *old_dir, struct dentry *old_dentry,
					struct inode *new_dir, struct dentry *new_dentry)
{
//	if (IS_ENCRYPTED(d_inode(old_dentry))) {
		fscrypt_sdp_check_chamber_event(old_dir, old_dentry, new_dir, new_dentry);
//	}
}

int fscrypt_sdp_check_rmdir(struct dentry *dentry)
{
	/* EXT4CRYPT-dedicated */
	if (EXT4_I(dentry->d_inode)->i_crypt_info &&
			EXT4_I(dentry->d_inode)->i_crypt_info->ci_sdp_info &&
			FSCRYPT_IS_CHAMBER_DENTRY(dentry)) {
		printk_once(KERN_WARNING
				"You're removing Chamber directory, I/O error\n");
		return -EIO;
	}

	return 0;
}
