/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSU_DEFEX_H
#define _LINUX_KSU_DEFEX_H

#include <linux/kconfig.h>
#include <linux/types.h>

struct task_struct;

#if IS_BUILTIN(CONFIG_KSU)
bool ksu_defex_is_allowed(struct task_struct *task);
#else
static inline bool ksu_defex_is_allowed(struct task_struct *task)
{
	return false;
}
#endif

#endif
