/****************************************************************************
 *
 * Copyright (c) 2014 - 2019 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/
#include <linux/mutex.h>
#include <linux/wakelock.h>

#include "scsc_wlbtd.h"

/* 
 * The value for maximum timeout is set to 18 seconds as most of customer
 * platform operate timeout of 20 seconds for watchdog timer. So please do
 * not change this or atleast don't change it without proper discussion. 
 */
#define MAX_TIMEOUT		18000 /* in milisecounds */
#define WRITE_FILE_TIMEOUT	1000 /* in milisecounds */

/* completion to indicate when moredump is done */
static DECLARE_COMPLETION(event_done);
static DECLARE_COMPLETION(fw_panic_done);
static DECLARE_COMPLETION(write_file_done);
static DEFINE_MUTEX(write_file_lock);

static DEFINE_MUTEX(build_type_lock);
static char *build_type;

static struct wake_lock wlbtd_wakelock;

/* module parameter controlling recovery handling */
extern int disable_recovery_handling;

const char *response_code_to_str(enum scsc_wlbtd_response_codes response_code)
{
	switch (response_code) {
	case SCSC_WLBTD_ERR_PARSE_FAILED:
		return "SCSC_WLBTD_ERR_PARSE_FAILED";
	case SCSC_WLBTD_FW_PANIC_TAR_GENERATED:
		return "SCSC_WLBTD_FW_PANIC_TAR_GENERATED";
	case SCSC_WLBTD_FW_PANIC_ERR_SCRIPT_FILE_NOT_FOUND:
		return "SCSC_WLBTD_FW_PANIC_ERR_SCRIPT_FILE_NOT_FOUND";
	case SCSC_WLBTD_FW_PANIC_ERR_NO_DEV:
		return "SCSC_WLBTD_FW_PANIC_ERR_NO_DEV";
	case SCSC_WLBTD_FW_PANIC_ERR_MMAP:
		return "SCSC_WLBTD_FW_PANIC_ERR_MMAP";
	case SCSC_WLBTD_FW_PANIC_ERR_SABLE_FILE:
		return "SCSC_WLBTD_FW_PANIC_ERR_SABLE_FILE";
	case SCSC_WLBTD_FW_PANIC_ERR_TAR:
		return "SCSC_WLBTD_FW_PANIC_ERR_TAR";
	case SCSC_WLBTD_OTHER_SBL_GENERATED:
		return "SCSC_WLBTD_OTHER_SBL_GENERATED";
	case SCSC_WLBTD_OTHER_TAR_GENERATED:
		return "SCSC_WLBTD_OTHER_TAR_GENERATED";
	case SCSC_WLBTD_OTHER_ERR_SCRIPT_FILE_NOT_FOUND:
		return "SCSC_WLBTD_OTHER_ERR_SCRIPT_FILE_NOT_FOUND";
	case SCSC_WLBTD_OTHER_ERR_NO_DEV:
		return "SCSC_WLBTD_OTHER_ERR_NO_DEV";
	case SCSC_WLBTD_OTHER_ERR_MMAP:
		return "SCSC_WLBTD_OTHER_ERR_MMAP";
	case SCSC_WLBTD_OTHER_ERR_SABLE_FILE:
		return "SCSC_WLBTD_OTHER_ERR_SABLE_FILE";
	case SCSC_WLBTD_OTHER_ERR_TAR:
		return "SCSC_WLBTD_OTHER_ERR_TAR";
	case SCSC_WLBTD_OTHER_IGNORE_TRIGGER:
		return "SCSC_WLBTD_OTHER_IGNORE_TRIGGER";
	default:
		SCSC_TAG_ERR(WLBTD, "UNKNOWN response_code %d", response_code);
		return "UNKNOWN response_code";
	}
}

/**
 * This callback runs whenever the socket receives messages.
 */
static int msg_from_wlbtd_cb(struct sk_buff *skb, struct genl_info *info)
{
	int status = 0;

	if (info->attrs[1])
		SCSC_TAG_INFO(WLBTD, "ATTR_STR: %s\n",
				(char *)nla_data(info->attrs[1]));

	if (info->attrs[2]) {
		status = *((__u32 *)nla_data(info->attrs[2]));
		if (status)
			SCSC_TAG_ERR(WLBTD, "ATTR_INT: %u\n", status);
	}

	if (!completion_done(&event_done))
		complete(&event_done);

	return 0;
}

static int msg_from_wlbtd_sable_cb(struct sk_buff *skb, struct genl_info *info)
{
	enum scsc_wlbtd_response_codes status = 0;
	const char *data = (const char *)nla_data(info->attrs[1]);

	if (info->attrs[1])
		SCSC_TAG_INFO(WLBTD, "%s\n", data);

	if (info->attrs[2]) {
		status = (enum scsc_wlbtd_response_codes) nla_get_u16(info->attrs[2]);
		if (status < SCSC_WLBTD_LAST_RESPONSE_CODE)
			SCSC_TAG_ERR(WLBTD, "%s\n", response_code_to_str(status));
		else
			SCSC_TAG_INFO(WLBTD, "Received invalid status value");
	}
	/* completion cases :
	 * 1) FW_PANIC_TAR_GENERATED
	 *    for trigger scsc_log_fw_panic only one response from wlbtd when
	 *    tar done
	 *    ---> complete fw_panic_done
	 * 2) for all other triggers, we get 2 responses
	 *	a) OTHER_SBL_GENERATED
	 *	   Once .sbl is written
	 *    ---> complete event_done
	 *	b) OTHER_TAR_GENERATED
	 *	   2nd time when sable tar is done
	 *	   IGNORE this response and Don't complete
	 * 3) OTHER_IGNORE_TRIGGER
	 *    When we get rapid requests for SABLE generation,
	 *    to serialise while processing current request,
	 *    we ignore requests other than "fw_panic" in wlbtd and
	 *    send a msg "ignoring" back to kernel.
	 *    ---> complete event_done
	 * 4) FW_PANIC_ERR_* and OTHER_ERR_*
	 *    when something failed, file not found, mmap failed, etc.
	 *    ---> complete the completion with waiter(s) based on if it was
	 *    a fw_panic trigger or other trigger
	 * 5) ERR_PARSE_FAILED
	 *    When msg parsing fails, wlbtd doesn't know the trigger type
	 *    ---> complete the completion with waiter(s)
	 */

	switch (status) {
	case SCSC_WLBTD_ERR_PARSE_FAILED:
		if (!completion_done(&fw_panic_done)) {
			SCSC_TAG_INFO(WLBTD, "completing fw_panic_done\n");
			complete(&fw_panic_done);
		}
		if (!completion_done(&event_done)) {
			SCSC_TAG_INFO(WLBTD, "completing event_done\n");
			complete(&event_done);
		}
		break;
	case SCSC_WLBTD_FW_PANIC_TAR_GENERATED:
	case SCSC_WLBTD_FW_PANIC_ERR_TAR:
	case SCSC_WLBTD_FW_PANIC_ERR_SCRIPT_FILE_NOT_FOUND:
	case SCSC_WLBTD_FW_PANIC_ERR_NO_DEV:
	case SCSC_WLBTD_FW_PANIC_ERR_MMAP:
	case SCSC_WLBTD_FW_PANIC_ERR_SABLE_FILE:
		if (!completion_done(&fw_panic_done)) {
			SCSC_TAG_INFO(WLBTD, "completing fw_panic_done\n");
			complete(&fw_panic_done);
		}
		break;
	case SCSC_WLBTD_OTHER_TAR_GENERATED:
		/* ignore */
		break;
	case SCSC_WLBTD_OTHER_SBL_GENERATED:
	case SCSC_WLBTD_OTHER_ERR_TAR:
	case SCSC_WLBTD_OTHER_ERR_SCRIPT_FILE_NOT_FOUND:
	case SCSC_WLBTD_OTHER_ERR_NO_DEV:
	case SCSC_WLBTD_OTHER_ERR_MMAP:
	case SCSC_WLBTD_OTHER_ERR_SABLE_FILE:
	case SCSC_WLBTD_OTHER_IGNORE_TRIGGER:
		if (!completion_done(&event_done)) {
			SCSC_TAG_INFO(WLBTD, "completing event_done\n");
			complete(&event_done);
		}
		break;
	default:
		SCSC_TAG_ERR(WLBTD, "UNKNOWN reponse from WLBTD\n");
	}

	return 0;
}

static int msg_from_wlbtd_build_type_cb(struct sk_buff *skb, struct genl_info *info)
{
	if (!info->attrs[1]) {
		SCSC_TAG_WARNING(WLBTD, "info->attrs[1] = NULL\n");
		return -1;
	}

	if (!nla_len(info->attrs[1])) {
		SCSC_TAG_WARNING(WLBTD, "nla_len = 0\n");
		return -1;
	}

	mutex_lock(&build_type_lock);
	if (build_type) {
		SCSC_TAG_WARNING(WLBTD, "ro.build.type = %s\n", build_type);
		mutex_unlock(&build_type_lock);
		return 0;
	}
	/* nla_len includes trailing zero. Tested.*/
	build_type = kmalloc(info->attrs[1]->nla_len, GFP_KERNEL);
	if (!build_type) {
		SCSC_TAG_WARNING(WLBTD, "kmalloc failed: build_type = NULL\n");
		mutex_unlock(&build_type_lock);
		return -1;
	}
	memcpy(build_type, (char *)nla_data(info->attrs[1]), info->attrs[1]->nla_len);
	SCSC_TAG_WARNING(WLBTD, "ro.build.type = %s\n", build_type);
	mutex_unlock(&build_type_lock);
	return 0;

}

static int msg_from_wlbtd_write_file_cb(struct sk_buff *skb, struct genl_info *info)
{
	if (info->attrs[3])
		SCSC_TAG_INFO(WLBTD, "%s\n", (char *)nla_data(info->attrs[3]));

	complete(&write_file_done);
	return 0;
}

/**
 * Here you can define some constraints for the attributes so Linux will
 * validate them for you.
 */
static struct nla_policy policies[] = {
	[ATTR_STR] = { .type = NLA_STRING, },
	[ATTR_INT] = { .type = NLA_U32, },
};

static struct nla_policy policy_sable[] = {
	[ATTR_INT] = { .type = NLA_U16, },
	[ATTR_INT8] = { .type = NLA_U8, },
};

static struct nla_policy policies_build_type[] = {
	[ATTR_STR] = { .type = NLA_STRING, },
};

static struct nla_policy policy_write_file[] = {
	[ATTR_PATH] = { .type = NLA_STRING, },
	[ATTR_CONTENT] = { .type = NLA_STRING, },
};

/**
 * Actual message type definition.
 */
struct genl_ops scsc_ops[] = {
	{
		.cmd = EVENT_SCSC,
		.flags = 0,
		.policy = policies,
		.doit = msg_from_wlbtd_cb,
		.dumpit = NULL,
	},
	{
		.cmd = EVENT_SYSTEM_PROPERTY,
		.flags = 0,
		.policy = policies_build_type,
		.doit = msg_from_wlbtd_build_type_cb,
		.dumpit = NULL,
	},
	{
		.cmd = EVENT_SABLE,
		.flags = 0,
		.policy = policy_sable,
		.doit = msg_from_wlbtd_sable_cb,
		.dumpit = NULL,
	},
	{
		.cmd = EVENT_WRITE_FILE,
		.flags = 0,
		.policy = policy_write_file,
		.doit = msg_from_wlbtd_write_file_cb,
		.dumpit = NULL,
	},
};

/* The netlink family */
static struct genl_family scsc_nlfamily = {
	.id = GENL_ID_GENERATE, /* Don't bother with a hardcoded ID */
	.name = "scsc_mdp_family",     /* Have users key off the name instead */
	.hdrsize = 0,           /* No private header */
	.version = 1,
	.maxattr = __ATTR_MAX,
};

int scsc_wlbtd_get_and_print_build_type(void)
{
	struct sk_buff *skb;
	void *msg;
	int rc = 0;

	SCSC_TAG_DEBUG(WLBTD, "start\n");
	wake_lock(&wlbtd_wakelock);

	/* check if the value wasn't cached yet */
	mutex_lock(&build_type_lock);
	if (build_type) {
		SCSC_TAG_WARNING(WLBTD, "ro.build.type = %s\n", build_type);
		SCSC_TAG_DEBUG(WLBTD, "sync end\n");
		mutex_unlock(&build_type_lock);
		goto done;
	}
	mutex_unlock(&build_type_lock);
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		SCSC_TAG_ERR(WLBTD, "Failed to construct message\n");
		goto error;
	}

	SCSC_TAG_INFO(WLBTD, "create message\n");
	msg = genlmsg_put(skb,
			0,           // PID is whatever
			0,           // Sequence number (don't care)
			&scsc_nlfamily,   // Pointer to family struct
			0,           // Flags
			EVENT_SYSTEM_PROPERTY // Generic netlink command
			);
	if (!msg) {
		SCSC_TAG_ERR(WLBTD, "Failed to create message\n");
		goto error;
	}
	rc = nla_put_string(skb, ATTR_STR, "ro.build.type");
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_string failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}
	genlmsg_end(skb, msg);

	SCSC_TAG_INFO(WLBTD, "finalize & send msg\n");
	rc = genlmsg_multicast_allns(&scsc_nlfamily, skb, 0, 0, GFP_KERNEL);

	if (rc) {
		SCSC_TAG_ERR(WLBTD, "failed to send message. rc = %d\n", rc);
		goto error;
	}

	SCSC_TAG_DEBUG(WLBTD, "async end\n");
done:
	wake_unlock(&wlbtd_wakelock);
	return rc;

error:
	if (rc == -ESRCH) {
		/* If no one registered to scsc_mdp_mcgrp (e.g. in case wlbtd
		 * is not running) genlmsg_multicast_allns returns -ESRCH.
		 * Ignore and return.
		 */
		SCSC_TAG_WARNING(WLBTD, "WLBTD not running ?\n");
		wake_unlock(&wlbtd_wakelock);
		return rc;
	}
	/* free skb */
	nlmsg_free(skb);
	wake_unlock(&wlbtd_wakelock);
	return -1;
}

int wlbtd_write_file(const char *file_path, const char *file_content)
{
#ifdef CONFIG_SCSC_WRITE_INFO_FILE_WLBTD
	struct sk_buff *skb;
	void *msg;
	int rc = 0;
	unsigned long completion_jiffies = 0;
	unsigned long max_timeout_jiffies = msecs_to_jiffies(WRITE_FILE_TIMEOUT);

	SCSC_TAG_DEBUG(WLBTD, "start\n");

	mutex_lock(&write_file_lock);
	wake_lock(&wlbtd_wakelock);

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		SCSC_TAG_ERR(WLBTD, "Failed to construct message\n");
		goto error;
	}

	SCSC_TAG_INFO(WLBTD, "create message to write %s\n", file_path);
	msg = genlmsg_put(skb,
			0,		// PID is whatever
			0,		// Sequence number (don't care)
			&scsc_nlfamily,	// Pointer to family struct
			0,		// Flags
			EVENT_WRITE_FILE// Generic netlink command
			);
	if (!msg) {
		SCSC_TAG_ERR(WLBTD, "Failed to create message\n");
		goto error;
	}

	SCSC_TAG_DEBUG(WLBTD, "add values to msg\n");
	rc = nla_put_string(skb, ATTR_PATH, file_path);
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_u32 failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}

	rc = nla_put_string(skb, ATTR_CONTENT, file_content);
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_string failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}

	genlmsg_end(skb, msg);

	SCSC_TAG_INFO(WLBTD, "finalize & send msg\n");
	/* genlmsg_multicast_allns() frees skb */
	rc = genlmsg_multicast_allns(&scsc_nlfamily, skb, 0, 0, GFP_KERNEL);

	if (rc) {
		if (rc == -ESRCH) {
			/* If no one registered to scsc_mcgrp (e.g. in case
			 * wlbtd is not running) genlmsg_multicast_allns
			 * returns -ESRCH. Ignore and return.
			 */
			SCSC_TAG_WARNING(WLBTD, "WLBTD not running ?\n");
			goto done;
		}
		SCSC_TAG_ERR(WLBTD, "Failed to send message. rc = %d\n", rc);
		goto done;
	}

	SCSC_TAG_INFO(WLBTD, "waiting for completion\n");
	/* wait for script to finish */
	completion_jiffies = wait_for_completion_timeout(&write_file_done,
						max_timeout_jiffies);

	if (completion_jiffies == 0)
		SCSC_TAG_ERR(WLBTD, "wait for completion timed out !\n");
	else {
		completion_jiffies = jiffies_to_msecs(max_timeout_jiffies - completion_jiffies);

		SCSC_TAG_INFO(WLBTD, "written %s in %lu ms\n", file_path,
			completion_jiffies ? completion_jiffies : 1);
	}

	/* reinit so completion can be re-used */
	reinit_completion(&write_file_done);

	SCSC_TAG_DEBUG(WLBTD, "end\n");
done:
	wake_unlock(&wlbtd_wakelock);
	mutex_unlock(&write_file_lock);
	return rc;

error:
	/* free skb */
	nlmsg_free(skb);

	wake_unlock(&wlbtd_wakelock);
	mutex_unlock(&write_file_lock);
	return -1;
#else /* CONFIG_SCSC_WRITE_INFO_FILE_WLBTD */
	SCSC_TAG_DEBUG(WLBTD, "not writing %s\n", file_path);
	return 0; /* stub */
#endif
}
EXPORT_SYMBOL(wlbtd_write_file);

int call_wlbtd_sable(u8 trigger_code, u16 reason_code)
{
	struct sk_buff *skb;
	void *msg;
	int rc = 0;
	unsigned long completion_jiffies = 0;
	unsigned long max_timeout_jiffies = msecs_to_jiffies(MAX_TIMEOUT);

	wake_lock(&wlbtd_wakelock);

	SCSC_TAG_INFO(WLBTD, "start:trigger - %s\n",
		scsc_get_trigger_str((int)trigger_code));

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		SCSC_TAG_ERR(WLBTD, "Failed to construct message\n");
		goto error;
	}

	SCSC_TAG_DEBUG(WLBTD, "create message\n");
	msg = genlmsg_put(skb,
			0,		// PID is whatever
			0,		// Sequence number (don't care)
			&scsc_nlfamily,	// Pointer to family struct
			0,		// Flags
			EVENT_SABLE	// Generic netlink command
			);
	if (!msg) {
		SCSC_TAG_ERR(WLBTD, "Failed to create message\n");
		goto error;
	}
	SCSC_TAG_DEBUG(WLBTD, "add values to msg\n");
	rc = nla_put_u16(skb, ATTR_INT, reason_code);
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_u16 failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}

	rc = nla_put_u8(skb, ATTR_INT8, trigger_code);
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_u8 failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}

	genlmsg_end(skb, msg);

	SCSC_TAG_DEBUG(WLBTD, "finalize & send msg\n");
	/* genlmsg_multicast_allns() frees skb */
	rc = genlmsg_multicast_allns(&scsc_nlfamily, skb, 0, 0, GFP_KERNEL);

	if (rc) {
		if (rc == -ESRCH) {
			/* If no one registered to scsc_mcgrp (e.g. in case
			 * wlbtd is not running) genlmsg_multicast_allns
			 * returns -ESRCH. Ignore and return.
			 */
			SCSC_TAG_WARNING(WLBTD, "WLBTD not running ?\n");
			goto done;
		}
		SCSC_TAG_ERR(WLBTD, "Failed to send message. rc = %d\n", rc);
		goto done;
	}

	SCSC_TAG_INFO(WLBTD, "waiting for completion\n");

	/* wait for script to finish */
	if (trigger_code == SCSC_LOG_FW_PANIC)
		completion_jiffies = wait_for_completion_timeout(&fw_panic_done,
						max_timeout_jiffies);
	else
		completion_jiffies = wait_for_completion_timeout(&event_done,
						max_timeout_jiffies);

	if (completion_jiffies) {

		completion_jiffies = max_timeout_jiffies - completion_jiffies;
		SCSC_TAG_INFO(WLBTD, "sable generated in %dms\n",
			(int)jiffies_to_msecs(completion_jiffies) ? : 1);
	} else
		SCSC_TAG_ERR(WLBTD, "wait for completion timed out for %s\n",
				scsc_get_trigger_str((int)trigger_code));

	/* reinit so completion can be re-used */
	if (trigger_code == SCSC_LOG_FW_PANIC)
		reinit_completion(&fw_panic_done);
	else
		reinit_completion(&event_done);

	SCSC_TAG_INFO(WLBTD, "  end:trigger - %s\n",
		scsc_get_trigger_str((int)trigger_code));

done:
	wake_unlock(&wlbtd_wakelock);
	return rc;

error:
	/* free skb */
	nlmsg_free(skb);
	wake_unlock(&wlbtd_wakelock);

	return -1;
}
EXPORT_SYMBOL(call_wlbtd_sable);

int call_wlbtd(const char *script_path)
{
	struct sk_buff *skb;
	void *msg;
	int rc = 0;
	unsigned long completion_jiffies = 0;
	unsigned long max_timeout_jiffies = msecs_to_jiffies(MAX_TIMEOUT);

	SCSC_TAG_DEBUG(WLBTD, "start\n");

	wake_lock(&wlbtd_wakelock);

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		SCSC_TAG_ERR(WLBTD, "Failed to construct message\n");
		goto error;
	}

	SCSC_TAG_INFO(WLBTD, "create message\n");
	msg = genlmsg_put(skb,
			0,		// PID is whatever
			0,		// Sequence number (don't care)
			&scsc_nlfamily,	// Pointer to family struct
			0,		// Flags
			EVENT_SCSC	// Generic netlink command
			);
	if (!msg) {
		SCSC_TAG_ERR(WLBTD, "Failed to create message\n");
		goto error;
	}

	SCSC_TAG_DEBUG(WLBTD, "add values to msg\n");
	rc = nla_put_u32(skb, ATTR_INT, 9);
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_u32 failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}

	rc = nla_put_string(skb, ATTR_STR, script_path);
	if (rc) {
		SCSC_TAG_ERR(WLBTD, "nla_put_string failed. rc = %d\n", rc);
		genlmsg_cancel(skb, msg);
		goto error;
	}

	genlmsg_end(skb, msg);

	SCSC_TAG_INFO(WLBTD, "finalize & send msg\n");
	/* genlmsg_multicast_allns() frees skb */
	rc = genlmsg_multicast_allns(&scsc_nlfamily, skb, 0, 0, GFP_KERNEL);

	if (rc) {
		if (rc == -ESRCH) {
			/* If no one registered to scsc_mcgrp (e.g. in case
			 * wlbtd is not running) genlmsg_multicast_allns
			 * returns -ESRCH. Ignore and return.
			 */
			SCSC_TAG_WARNING(WLBTD, "WLBTD not running ?\n");
			goto done;
		}
		SCSC_TAG_ERR(WLBTD, "Failed to send message. rc = %d\n", rc);
		goto done;
	}

	SCSC_TAG_INFO(WLBTD, "waiting for completion\n");

	/* wait for script to finish */
	completion_jiffies = wait_for_completion_timeout(&event_done,
						max_timeout_jiffies);

	if (completion_jiffies) {

		completion_jiffies = max_timeout_jiffies - completion_jiffies;
		SCSC_TAG_INFO(WLBTD, "done in %dms\n",
			(int)jiffies_to_msecs(completion_jiffies) ? : 1);
	} else
		SCSC_TAG_ERR(WLBTD, "wait for completion timed out !\n");

	/* reinit so completion can be re-used */
	reinit_completion(&event_done);

	SCSC_TAG_DEBUG(WLBTD, "end\n");

done:
	wake_unlock(&wlbtd_wakelock);
	return rc;

error:
	/* free skb */
	nlmsg_free(skb);
	wake_unlock(&wlbtd_wakelock);

	return -1;
}
EXPORT_SYMBOL(call_wlbtd);

int scsc_wlbtd_init(void)
{
	int r = 0;

	wake_lock_init(&wlbtd_wakelock, WAKE_LOCK_SUSPEND, "wlbtd_wl");
	init_completion(&event_done);
	init_completion(&fw_panic_done);
	init_completion(&write_file_done);

	/* register the family so that wlbtd can bind */
	r = genl_register_family_with_ops_groups(&scsc_nlfamily, scsc_ops,
			scsc_mcgrp);
	if (r) {
		SCSC_TAG_ERR(WLBTD, "Failed to register family. (%d)\n", r);
		return -1;
	}

	return r;
}

int scsc_wlbtd_deinit(void)
{
	int ret = 0;

	/* unregister family */
	ret = genl_unregister_family(&scsc_nlfamily);
	if (ret) {
		SCSC_TAG_ERR(WLBTD, "genl_unregister_family failed (%d)\n",
				ret);
		return -1;
	}
	kfree(build_type);
	build_type = NULL;
	wake_lock_destroy(&wlbtd_wakelock);

	return ret;
}
