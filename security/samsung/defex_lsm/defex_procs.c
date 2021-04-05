/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <asm/barrier.h>
#include <linux/compiler.h>
#include <linux/const.h>
#ifdef DEFEX_DSMS_ENABLE
#include <linux/dsms.h>
#endif
#include <linux/file.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/namei.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#ifdef DEFEX_DSMS_ENABLE
#include <linux/string.h>
#endif
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include "include/defex_caches.h"
#include "include/defex_catch_list.h"
#include "include/defex_config.h"
#include "include/defex_internal.h"
#include "include/defex_rules.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#endif

static const char unknown_file[] = "<unknown filename>";
#define SAFE_STR_FREE(ptr)	do { if (ptr && (void*)ptr != (void*)unknown_file) kfree(ptr); } while(0)

#ifdef DEFEX_DEPENDING_ON_OEMUNLOCK
bool boot_state_unlocked __ro_after_init;
static int __init verifiedboot_state_setup(char *str)
{
	static const char unlocked[] = "orange";

	boot_state_unlocked = !strncmp(str, unlocked, sizeof(unlocked));

	if(boot_state_unlocked)
		pr_crit("Device is unlocked and DEFEX will be disabled.");

	return 0;
}
__setup("androidboot.verifiedbootstate=", verifiedboot_state_setup);
#endif /* DEFEX_DEPENDING_ON_OEMUNLOCK */

#ifdef DEFEX_DSMS_ENABLE

#	define PED_VIOLATION "DFX1"
#	define SAFEPLACE_VIOLATION "DFX2"
#	define INTEGRITY_VIOLATION "DFX3"
#	define MESSAGE_BUFFER_SIZE 200
#	define STORED_CREDS_SIZE 100

static void defex_report_violation(const char *violation, uint64_t counter,
	int syscall, struct task_struct *p, struct file *f, uid_t stored_uid, uid_t stored_fsuid, uid_t stored_egid, int case_num)
{
	int usermode_result;
	char message[MESSAGE_BUFFER_SIZE + 1];

	const uid_t uid = uid_get_value(p->cred->uid);
	const uid_t euid = uid_get_value(p->cred->euid);
	const uid_t fsuid = uid_get_value(p->cred->fsuid);
	const uid_t egid = uid_get_value(p->cred->egid);
	const char *process_name = p->comm;
	const char *prt_process_name = NULL;
	const char *prt_program_path = NULL;
	const char *program_path = defex_get_filename(p);
	const pid_t pid = p->pid, tgid = p->tgid;

	char *file_path = NULL, *buff = NULL;
	const struct path *dpath = NULL;
	struct task_struct *parent = NULL;

	char stored_creds[STORED_CREDS_SIZE + 1];

	read_lock(&tasklist_lock);
	parent = p->parent;
	if (!parent) {
		read_unlock(&tasklist_lock);
		goto out;
	}

	get_task_struct(parent);
	read_unlock(&tasklist_lock);

	prt_process_name = parent->comm;
	prt_program_path = defex_get_filename(parent);


	if (f != NULL) {
		buff = kzalloc(PATH_MAX, GFP_KERNEL);
		if (buff != NULL) {
			dpath = &(f->f_path);
			file_path = d_path(dpath, buff, PATH_MAX);
		}
	}
	else {
		snprintf(stored_creds, sizeof(stored_creds), "[%ld, %ld, %ld]", (long)stored_uid, (long)stored_fsuid, (long)stored_egid);
		stored_creds[sizeof(stored_creds) - 1] = 0;
	}
#ifdef DEFEX_DEPENDING_ON_OEMUNLOCK
	snprintf(message, sizeof(message), "%d, sc=%d, tsk=%s(%s %u %u), %s(%s), [%ld %ld %ld %ld], %s%s, %d",
		boot_state_unlocked, syscall, process_name, (program_path ? program_path : ""),pid, tgid, prt_process_name,
		(prt_program_path ? prt_program_path : ""), (long)uid, (long)euid, (long)fsuid, (long)egid,
		(file_path ? "file=" : "stored "), (file_path ? file_path : stored_creds), case_num);
#else
	snprintf(message, sizeof(message), "sc=%d, tsk=%s(%s %u %u), %s(%s), [%ld %ld %ld %ld], %s%s, %d",
		syscall, process_name, (program_path ? program_path : ""),pid, tgid, prt_process_name,
		(prt_program_path ? prt_program_path : ""), (long)uid, (long)euid, (long)fsuid, (long)egid,
		(file_path ? "file=" : "stored "), (file_path ? file_path : stored_creds), case_num);
#endif
	message[sizeof(message) - 1] = 0;

#ifdef DEFEX_DEBUG_ENABLE
	printk(KERN_ERR "DEFEX Violation : feature=%s value=%ld, detail=[%s]",
		violation, (long)counter, message);
#endif /* DEFEX_DEBUG_ENABLE */
	usermode_result = dsms_send_message(violation, message, counter);
#ifdef DEFEX_DEBUG_ENABLE
	printk(KERN_ERR "DEFEX Result : %d\n", usermode_result);
#endif /* DEFEX_DEBUG_ENABLE */

	SAFE_STR_FREE(program_path);
	SAFE_STR_FREE(prt_program_path);
	kfree(buff);
out:
	if (parent)
		put_task_struct(parent);
}
#endif /* DEFEX_DSMS_ENABLE */

#ifdef DEFEX_SAFEPLACE_ENABLE
static long kill_process(struct task_struct *p)
{
	read_lock(&tasklist_lock);
	force_sig(SIGKILL, p);
	read_unlock(&tasklist_lock);
	return 0;
}
#endif /* DEFEX_SAFEPLACE_ENABLE */

#ifdef DEFEX_PED_ENABLE
static long kill_process_group(struct task_struct *p, int tgid, int pid)
{
	read_lock(&tasklist_lock);
	for_each_process(p) {
		if (p->tgid == tgid)
			send_sig(SIGKILL, p, 0);
	}
	send_sig(SIGKILL, current, 0);
	read_unlock(&tasklist_lock);
	return 0;
}
#endif /* DEFEX_PED_ENABLE */

struct file *defex_get_source_file(struct task_struct *p)
{
	struct file *file_addr = NULL;
	struct mm_struct *proc_mm;

#ifdef DEFEX_CACHES_ENABLE
	bool self;
	file_addr = defex_file_cache_find(p->pid);

	if (!file_addr) {
		proc_mm = get_task_mm(p);
		if (!proc_mm)
			return NULL;
		file_addr = get_mm_exe_file(proc_mm);
		mmput(proc_mm);
		if (!file_addr)
			return NULL;
		defex_file_cache_add(p->pid, file_addr);
	} else {
		self = (p == current);
		proc_mm = (self)?p->mm:get_task_mm(p);
		if (!proc_mm)
			return NULL;
		if (self)
			down_read(&proc_mm->mmap_sem);
		if (file_addr != proc_mm->exe_file) {
			file_addr = proc_mm->exe_file;
			if (!file_addr)
				goto clean_mm;
			get_file(file_addr);
			defex_file_cache_update(file_addr);
		}
clean_mm:
		if (self)
			up_read(&proc_mm->mmap_sem);
		else
			mmput(proc_mm);
	}
#else

	proc_mm = get_task_mm(p);
	if (!proc_mm)
		return NULL;
	file_addr = get_mm_exe_file(proc_mm);
	mmput(proc_mm);
#endif /* DEFEX_CACHES_ENABLE */
	return file_addr;
}

char *defex_get_filename(struct task_struct *p)
{
	struct file *exe_file = NULL;
	const struct path *dpath = NULL;
	char *path = NULL, *buff = NULL;
	char *filename = NULL;

	exe_file = defex_get_source_file(p);
	if (!exe_file)
		goto out_filename;

	dpath = &exe_file->f_path;

	buff = kzalloc(PATH_MAX, GFP_ATOMIC);
	if (buff)
		path = d_path(dpath, buff, PATH_MAX);

#ifndef DEFEX_CACHES_ENABLE
	fput(exe_file);
#endif /* DEFEX_CACHES_ENABLE */

out_filename:
	if (path && !IS_ERR(path))
		filename = kstrdup(path, GFP_ATOMIC);

	if (!filename)
		filename = (char *)unknown_file;

	if (buff)
		kfree(buff);
	return filename;
}

#ifdef DEFEX_PED_ENABLE
static int task_defex_is_secured(struct task_struct *p)
{
	struct file *exe_file = NULL;
	const struct path *dpath = NULL;
	int is_secured = 1;

	exe_file = defex_get_source_file(p);
	if (!exe_file)
		goto skip_secured;

	dpath = &exe_file->f_path;
	if (!dpath->dentry || !dpath->dentry->d_inode)
		goto out_secured;

	is_secured = !rules_lookup(dpath, feature_ped_exception, exe_file);

out_secured:
#ifndef DEFEX_CACHES_ENABLE
	fput(exe_file);
#endif /* DEFEX_CACHES_ENABLE */
skip_secured:
	return is_secured;
}
#endif /* DEFEX_PED_ENABLE */

#ifdef DEFEX_PED_ENABLE
static int at_same_group(unsigned int uid1, unsigned int uid2)
{
	static const unsigned int lod_base = 0x61A8;

	/* allow the weaken privilege */
	if (uid1 >= 10000 && uid2 < 10000) return 1;
	/* allow traverse in the same class */
	if ((uid1 / 1000) == (uid2 / 1000)) return 1;
	/* allow traverse to isolated ranges */
	if (uid1 >= 90000) return 1;
	/* allow LoD process */
	return ((uid1 >> 16) == lod_base) && ((uid2 >> 16) == lod_base);
}

static int at_same_group_gid(unsigned int gid1, unsigned int gid2)
{
	static const unsigned int lod_base = 0x61A8, inet = 3003;

	/* allow the weaken privilege */
	if (gid1 >= 10000 && gid2 < 10000) return 1;
	/* allow traverse in the same class */
	if ((gid1 / 1000) == (gid2 / 1000)) return 1;
	/* allow traverse to isolated ranges */
	if (gid1 >= 90000) return 1;
	/* allow LoD process */
	return (((gid1 >> 16) == lod_base) || (gid1 == inet)) && ((gid2 >> 16) == lod_base);
}

#ifdef DEFEX_LP_ENABLE
/* Lower Permission feature decision function */
static int lower_adb_permission(struct task_struct *p, unsigned short cred_flags)
{
	char *parent_file;
	struct task_struct *parent = NULL;
#ifndef DEFEX_PERMISSIVE_LP
	struct cred *shellcred;
#endif /* DEFEX_PERMISSIVE_LP */
	unsigned long parent_length;
	unsigned int adbd_length;
	int ret = 0;
	static const char adbd_str[] = "/system/bin/adbd";

	read_lock(&tasklist_lock);
	parent = p->parent;
	if (!parent) {
		read_unlock(&tasklist_lock);
		goto out;
	}

	get_task_struct(parent);
	read_unlock(&tasklist_lock);

	if (p->pid == 1 || parent->pid == 1)
		goto out;

	parent_file = defex_get_filename(parent);
	parent_length = strlen(parent_file);
	adbd_length = (sizeof(adbd_str) - 1);

#ifndef DEFEX_PERMISSIVE_LP
	if (!strncmp(parent_file, adbd_str, (parent_length < adbd_length) ? parent_length : adbd_length)) {
		shellcred = prepare_creds();
		pr_crit("[DEFEX] adb with root");
		if (!shellcred) {
			pr_crit("[DEFEX] prepare_creds fail");
			ret = 0;
			goto out;
		}

		shellcred->uid.val = 2000;
		shellcred->suid.val = 2000;
		shellcred->euid.val = 2000;
		shellcred->fsuid.val = 2000;
		shellcred->gid.val = 2000;
		shellcred->sgid.val = 2000;
		shellcred->egid.val = 2000;
		shellcred->fsgid.val = 2000;
		commit_creds(shellcred);

		set_task_creds(REF_PID(p), 2000, 2000, 2000, cred_flags);

		ret = 1;
	}
#endif /* DEFEX_PERMISSIVE_LP */

	SAFE_STR_FREE(parent_file);
out:
	if (parent)
		put_task_struct(parent);
	return ret;
}
#endif /* DEFEX_LP_ENABLE */

/* Cred. violation feature decision function */
#define AID_MEDIA_RW	1023
#define SECUREFD_MEDIA_RW	0xE4E5BF
#ifndef DEFEX_DSMS_ENABLE
static int task_defex_check_creds(struct task_struct *p)
#else
static int task_defex_check_creds(struct task_struct *p, int syscall)
#endif /* DEFEX_DSMS_ENABLE */
{
	char *path = NULL;
	int check_deeper, case_num;
	unsigned int cur_uid, cur_euid, cur_fsuid, cur_egid;
	unsigned int ref_uid, ref_fsuid, ref_egid;
	struct task_struct *parent;
	unsigned short cred_flags;
#ifndef DEFEX_PED_BASED_ON_TGID_ENABLE
	unsigned int g_uid, g_fsuid, g_egid;
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
	static const unsigned int dead_uid = 0xDEADBEAF;

	if (!is_task_creds_ready() || !p->cred)
		goto out;

	get_task_creds(REF_PID(p), &ref_uid, &ref_fsuid, &ref_egid, &cred_flags);
#ifndef DEFEX_PED_BASED_ON_TGID_ENABLE
	if (p->tgid != p->pid) {
		get_task_creds(p->tgid, &g_uid, &g_fsuid, &g_egid, &cred_flags);
	} else {
		g_uid = ref_uid;
		g_fsuid = ref_fsuid;
		g_egid = ref_egid;
	}
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */

	cur_uid = uid_get_value(p->cred->uid);
	cur_euid = uid_get_value(p->cred->euid);
	cur_fsuid = uid_get_value(p->cred->fsuid);
	cur_egid = uid_get_value(p->cred->egid);

	if (!ref_uid) {
#ifdef DEFEX_PED_BASED_ON_TGID_ENABLE
		if (p->tgid != p->pid && p->tgid != 1) {
			path = defex_get_filename(p);
			pr_crit("defex[6]: cred wasn't stored [task=%s, filename=%s, uid=%d, tgid=%u, pid=%u, ppid=%u]\n",
				p->comm, (path ? path : ""), cur_uid, p->tgid, p->pid, p->real_parent->pid);
			pr_crit("defex[6]: stored [euid=%d fsuid=%d egid=%d] current [uid=%d euid=%d fsuid=%d egid=%d]\n",
				ref_uid, ref_fsuid, ref_egid, cur_uid, cur_euid, cur_fsuid, cur_egid);
			goto exit;
		}
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
		read_lock(&tasklist_lock);
		parent = p->parent;
		if (parent)
			get_task_struct(parent);
		read_unlock(&tasklist_lock);
		if (parent) {
			if (CHECK_ROOT_CREDS(parent))
				cred_flags |= CRED_FLAGS_PROOT;

			put_task_struct(parent);
		}

		if (CHECK_ROOT_CREDS(p)) {
#ifdef DEFEX_LP_ENABLE
			if (!lower_adb_permission(p, cred_flags))
#endif /* DEFEX_LP_ENABLE */
			{
				set_task_creds(REF_PID(p), 1, 1, 1, cred_flags);
			}
		}
		else
			set_task_creds(REF_PID(p), cur_euid, cur_fsuid, cur_egid, cred_flags);
	} else if (ref_uid == 1) {
		if (!CHECK_ROOT_CREDS(p))
			set_task_creds(REF_PID(p), cur_euid, cur_fsuid, cur_egid, cred_flags);
	} else if (ref_uid == dead_uid
#ifndef DEFEX_PED_BASED_ON_TGID_ENABLE
		|| g_uid == dead_uid
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
			 ) {
		path = defex_get_filename(p);
		pr_crit("defex[5]: process wasn't killed [task=%s, filename=%s, uid=%d, tgid=%u, pid=%u, ppid=%u]\n",
			p->comm, (path ? path : ""), cur_uid, p->tgid, p->pid, p->real_parent->pid);
		pr_crit("defex[5]: stored [euid=%d fsuid=%d egid=%d] current [uid=%d euid=%d fsuid=%d egid=%d]\n",
			ref_uid, ref_fsuid, ref_egid, cur_uid, cur_euid, cur_fsuid, cur_egid);
		goto exit;
	} else {
		check_deeper = 0;
		/* temporary allow fsuid changes to "media_rw" */
		if ( (cur_uid != ref_uid) ||
				(cur_euid != ref_uid) ||
	 			(cur_egid != ref_egid) ||
	  			!((cur_fsuid == ref_fsuid) ||
	  			 (cur_fsuid == ref_uid) ||
	  			 (cur_fsuid == AID_MEDIA_RW) ||
	  			 (cur_fsuid == SECUREFD_MEDIA_RW)) ) {
			check_deeper = 1;
			if (CHECK_ROOT_CREDS(p))
				set_task_creds(REF_PID(p), 1, 1, 1, cred_flags);
			else
				set_task_creds(REF_PID(p), cur_euid, cur_fsuid, cur_egid, cred_flags);
		}
		if (check_deeper &&
				(!at_same_group(cur_uid, ref_uid) ||
				!at_same_group(cur_euid, ref_uid) ||
				!at_same_group_gid(cur_egid, ref_egid) ||
				!at_same_group(cur_fsuid, ref_fsuid)) &&
				task_defex_is_secured(p)) {
#ifdef DEFEX_PED_BASED_ON_TGID_ENABLE
			case_num = ((p->tgid == p->pid) ? 1 : 2);
#else
			case_num = 1;
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
			goto trigger_violation;
		}

#ifndef DEFEX_PED_BASED_ON_TGID_ENABLE
		if (p->tgid != p->pid) {
			if ((g_uid > 1) &&
					(!at_same_group(cur_uid, g_uid) ||
					!at_same_group(cur_euid, g_uid) ||
					!at_same_group_gid(cur_egid, g_egid)) &&
					task_defex_is_secured(p)) {
				case_num = 2;
				goto trigger_violation;
			}
		}
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
	}

	if (CHECK_ROOT_CREDS(p) && !(cred_flags & CRED_FLAGS_PROOT) && task_defex_is_secured(p)) {
		if ((p->tgid != p->pid)
#ifndef DEFEX_PED_BASED_ON_TGID_ENABLE
			&& (g_uid > 1)
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
			) {
			case_num = 3;
			goto trigger_violation;
		}
		case_num = 4;
		goto trigger_violation;
	}

out:
	return DEFEX_ALLOW;

trigger_violation:
	set_task_creds(REF_PID(p), dead_uid, dead_uid, dead_uid, cred_flags);
#ifndef DEFEX_PED_BASED_ON_TGID_ENABLE
	if (p->tgid != p->pid)
		set_task_creds(p->tgid, dead_uid, dead_uid, dead_uid, cred_flags);
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
	path = defex_get_filename(p);
	pr_crit("defex[%d]: credential violation [task=%s, filename=%s, uid=%d, tgid=%u, pid=%u, ppid=%u]\n",
		case_num, p->comm, (path ? path : ""), cur_uid, p->tgid, p->pid, p->real_parent->pid);
	pr_crit("defex[%d]: stored [euid=%d fsuid=%d egid=%d] current [uid=%d euid=%d fsuid=%d egid=%d]\n",
		case_num, ref_uid, ref_fsuid, ref_egid, cur_uid, cur_euid, cur_fsuid, cur_egid);

#ifdef DEFEX_DSMS_ENABLE
	defex_report_violation(PED_VIOLATION, 0, syscall, p, NULL, ref_uid, ref_fsuid, ref_egid, case_num);
#endif /* DEFEX_DSMS_ENABLE */

exit:
	SAFE_STR_FREE(path);
	return -DEFEX_DENY;
}
#endif /* DEFEX_PED_ENABLE */

#ifdef DEFEX_SAFEPLACE_ENABLE
/* Safeplace feature decision function */
static int task_defex_safeplace(struct task_struct *p, struct file *f)
{
	static const char def[] = "";
	int ret = DEFEX_ALLOW, is_violation = 0;
	char *proc_file, *new_file = (char *)def, *buff;
	const struct path *dpath = NULL;

	if (!CHECK_ROOT_CREDS(p))
		goto out;

	if (IS_ERR(f))
		goto out;

	dpath = &f->f_path;
	if (!dpath->dentry || !dpath->dentry->d_inode)
		goto out;

	is_violation = rules_lookup(dpath, feature_safeplace_path, f);
#ifdef DEFEX_INTEGRITY_ENABLE
	if (is_violation != DEFEX_INTEGRITY_FAIL)
#endif /* DEFEX_INTEGRITY_ENABLE */
		is_violation = !is_violation;

	if (is_violation) {
		ret = -DEFEX_DENY;
		proc_file = defex_get_filename(p);
		buff = kzalloc(PATH_MAX, GFP_ATOMIC);
		if (buff)
			new_file = d_path(dpath, buff, PATH_MAX);

#ifdef DEFEX_INTEGRITY_ENABLE
		if (is_violation == DEFEX_INTEGRITY_FAIL) {
			pr_crit("defex: integrity violation [task=%s (%s), child=%s, uid=%d]\n",
				p->comm, (proc_file ? proc_file : ""), new_file, uid_get_value(p->cred->uid));
#ifdef DEFEX_DSMS_ENABLE
			defex_report_violation(INTEGRITY_VIOLATION, 0, __DEFEX_execve, p, f, 0, 0, 0, 0);
#endif /* DEFEX_DSMS_ENABLE */

			/*  Temporary make permissive mode for tereble
			 *  system image is changed as google's and defex might not work
			 */
			ret = DEFEX_ALLOW;
		}
		else
#endif /* DEFEX_INTEGRITY_ENABLE */
		{
			pr_crit("defex: safeplace violation [task=%s (%s), child=%s, uid=%d]\n",
				p->comm, (proc_file ? proc_file : ""), new_file, uid_get_value(p->cred->uid));
#ifdef DEFEX_DSMS_ENABLE
			defex_report_violation(SAFEPLACE_VIOLATION, 0, __DEFEX_execve, p, f, 0, 0, 0, 0);
#endif /* DEFEX_DSMS_ENABLE */
		}

		SAFE_STR_FREE(proc_file);
		kfree(buff);
	}
out:
	return ret;
}
#endif /* DEFEX_SAFEPLACE_ENABLE */

#ifdef DEFEX_IMMUTABLE_ENABLE

/* Immutable feature decision function */
static int task_defex_src_exception(struct task_struct *p)
{
	struct file *exe_file = NULL;
	const struct path *dpath = NULL;
	int allow = 1;

	exe_file = defex_get_source_file(p);
	if (!exe_file)
		goto do_skip1;

	dpath = &exe_file->f_path;
	if (!dpath->dentry || !dpath->dentry->d_inode)
		goto do_skip2;

	allow = rules_lookup(dpath, feature_immutable_src_exception, exe_file);

do_skip2:
#ifndef DEFEX_CACHES_ENABLE
	fput(exe_file);
#endif /* DEFEX_CACHES_ENABLE */
do_skip1:
	return allow;
}

/* Immutable feature decision function */
static int task_defex_immutable(struct task_struct *p, struct file *f, int attribute)
{
	static const char def[] = "";
	int ret = DEFEX_ALLOW, is_violation = 0;
	char *proc_file, *new_file = (char *)def, *buff;
	const struct path *dpath = NULL;

	if (IS_ERR(f))
		goto out;

	dpath = &f->f_path;
	if (!dpath->dentry || !dpath->dentry->d_inode)
		goto out;

	is_violation = rules_lookup(dpath, attribute, f);

	if (is_violation) {
		/* Source exception */
		if (attribute == feature_immutable_path_open && task_defex_src_exception(p))
			goto out;
		ret = -DEFEX_DENY;
		proc_file = defex_get_filename(p);
		buff = kzalloc(PATH_MAX, GFP_ATOMIC);
		if (buff)
			new_file = d_path(dpath, buff, PATH_MAX);
		pr_crit("defex: immutable %s violation [task=%s (%s), access to:%s]\n",
			(attribute==feature_immutable_path_open)?"open":"write", p->comm, proc_file, new_file);
		SAFE_STR_FREE(proc_file);
		kfree(buff);
	}
out:
	return ret;
}
#endif /* DEFEX_IMMUTABLE_ENABLE */

/* Main decision function */
int task_defex_enforce(struct task_struct *p, struct file *f, int syscall)
{
	int ret = DEFEX_ALLOW;
	int feature_flag;
	const struct local_syscall_struct *item;

#ifdef DEFEX_DEPENDING_ON_OEMUNLOCK
	if(boot_state_unlocked)
		return ret;
#endif /* DEFEX_DEPENDING_ON_OEMUNLOCK */

	if (!p || p->pid == 1 || !p->mm)
		return ret;

	if (syscall < 0) {
		item = get_local_syscall(-syscall);
		if (!item)
			return ret;
		syscall = item->local_syscall;
	}

	feature_flag = defex_get_features();

	get_task_struct(p);
#ifdef DEFEX_PED_ENABLE
	/* Credential escalation feature */
	if (feature_flag & FEATURE_CHECK_CREDS)	{
#ifndef DEFEX_DSMS_ENABLE
		ret = task_defex_check_creds(p);
#else
		ret = task_defex_check_creds(p, syscall);
#endif /* DEFEX_DSMS_ENABLE */
		if (ret) {
			if (!(feature_flag & FEATURE_CHECK_CREDS_SOFT)) {
				kill_process_group(p, p->tgid, p->pid);
				goto do_deny;
			}
		}
	}
#endif /* DEFEX_PED_ENABLE */

#ifdef DEFEX_SAFEPLACE_ENABLE
	/* Safeplace feature */
	if (feature_flag & FEATURE_SAFEPLACE) {
		if (syscall == __DEFEX_execve) {
			ret = task_defex_safeplace(p, f);
			if (ret == -DEFEX_DENY) {
				if (!(feature_flag & FEATURE_SAFEPLACE_SOFT)) {
					kill_process(p);
					goto do_deny;
				}
			}
		}
	}
#endif /* DEFEX_SAFEPLACE_ENABLE */

#ifdef DEFEX_IMMUTABLE_ENABLE
	/* Immutable feature */
	if (feature_flag & FEATURE_IMMUTABLE) {
		if (syscall == __DEFEX_openat || syscall == __DEFEX_write) {
			ret = task_defex_immutable(p, f,
				(syscall == __DEFEX_openat)?feature_immutable_path_open:feature_immutable_path_write);
			if (ret == -DEFEX_DENY) {
				if (!(feature_flag & FEATURE_IMMUTABLE_SOFT)) {
					goto do_deny;
				}
			}
		}
	}
#endif /* DEFEX_IMMUTABLE_ENABLE */
	put_task_struct(p);
	return DEFEX_ALLOW;

do_deny:
	put_task_struct(p);
	return -DEFEX_DENY;
}

int task_defex_zero_creds(struct task_struct *tsk)
{
#ifdef DEFEX_PED_BASED_ON_TGID_ENABLE
	int is_fork = -1;
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
	get_task_struct(tsk);
	if (tsk->flags & (PF_KTHREAD | PF_WQ_WORKER)) {
		put_task_struct(tsk);
		return 0;
	}
	if (is_task_creds_ready()) {
#ifdef DEFEX_PED_BASED_ON_TGID_ENABLE
		is_fork = ((tsk->flags & PF_FORKNOEXEC) && (!READ_ONCE(tsk->on_rq)));
#ifdef TASK_NEW
		if (!is_fork && (tsk->state & TASK_NEW))
			is_fork = 1;
#endif /* TASK_NEW */
		set_task_creds_tcnt(REF_PID(tsk), is_fork?1:-1);
#else
		delete_task_creds(REF_PID(tsk));
#endif /* DEFEX_PED_BASED_ON_TGID_ENABLE */
	}

#ifdef DEFEX_CACHES_ENABLE
	defex_file_cache_delete(tsk->pid);
#endif /* DEFEX_CACHES_ENABLE */
	put_task_struct(tsk);
	return 0;
}

