/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSU_SELINUX_H
#define _LINUX_KSU_SELINUX_H

#include <linux/fs.h>

typedef ssize_t (*ksu_selinux_write_fn)(struct file *, const char __user *,
					 size_t, loff_t *);

ssize_t ksu_selinux_hide_transaction_write(struct file *file,
		const char __user *buf, size_t count, loff_t *pos,
		ksu_selinux_write_fn original_write);
ssize_t ksu_selinux_hide_proc_attr_write(struct file *file,
		const char __user *buf, size_t count, loff_t *pos,
		ksu_selinux_write_fn original_write);
bool ksu_selinux_hide_status_open(struct file *file);

#endif
