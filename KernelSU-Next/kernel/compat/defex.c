// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/ksu_defex.h>
#include <linux/sched.h>
#include <linux/uidgid.h>
#include <linux/err.h>

#include "policy/allowlist.h"
#include "selinux/selinux.h"
#include "ksu.h"
#include "compat/kernel_compat.h"

const struct cred *ksu_override_creds(void)
{
#ifdef CONFIG_KDP_CRED
	struct cred *cred = prepare_kernel_cred(NULL);
	const struct cred *old;

	if (!cred)
		return ERR_PTR(-ENOMEM);
	setup_selinux(KERNEL_SU_CONTEXT, cred);
	/* override_creds replaces this local pointer with a per-task KDP copy. */
	old = override_creds(cred);
	put_cred(cred);
	return old;
#else
	return override_creds(ksu_cred);
#endif
}

/*
 * DEFEX must permit transitions explicitly authorized by KernelSU. The
 * allowlist also recognizes the signature-verified manager. Neither a root
 * UID alone nor an executable pathname grants an exception.
 */
bool ksu_defex_is_allowed(struct task_struct *task)
{
	const struct cred *cred;
	bool allowed;
	uid_t uid;

	cred = task == current ? get_current_cred() : get_task_cred(task);
	uid = __kuid_val(cred->uid);
	allowed = is_task_ksu_domain(cred);
	if (!allowed) {
		if (task == current)
			allowed = ksu_is_allow_uid_for_current(uid);
		else if (uid != 2000) /* Shell authorization is task-specific. */
			allowed = ksu_is_allow_uid(uid);
	}
	put_cred(cred);
	return allowed;
}
