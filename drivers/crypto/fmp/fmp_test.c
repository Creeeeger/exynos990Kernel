/*
 * Exynos FMP cipher driver
 *
 * Copyright (C) 2016 Samsung Electronics Co., Ltd.
 * Authors: Boojin Kim <boojin.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/crypto.h>
#include <linux/buffer_head.h>
#include <linux/genhd.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/blk_types.h>
#include <crypto/fmp.h>
#include <linux/mm_types.h>
#include <linux/file.h>
#include <linux/fs_struct.h>
#include <linux/magic.h>
#include <linux/pid.h>
#include <linux/sched/task.h>
#include "fmp_test.h"

#define FIPS_BLOCK_NAME	"/dev/block/by-name/fmp_selftest"

/* Older Exynos partition tables have no fmp_selftest partition. Allocate
 * one block owned by an unlinked ext4 file instead of overwriting a guessed
 * raw sector. Its inode stays open until every hardware KAT I/O has finished.
 */
static int fmp_test_alloc_scratch(struct fmp_test_data *data)
{
	struct pid *pid = find_get_pid(1);
	struct task_struct *init = get_pid_task(pid, PIDTYPE_PID);
	struct inode *inode;
	struct path root;
	void *zero;
	loff_t pos = 0;
	ssize_t written;
	int ret;

	put_pid(pid);
	if (!init)
		return -ESRCH;
	task_lock(init);
	get_fs_root(init->fs, &root);
	task_unlock(init);
	put_task_struct(init);
	data->scratch_file = file_open_root(root.dentry, root.mnt,
		"metadata/vold", O_TMPFILE | O_EXCL | O_RDWR, 0600);
	path_put(&root);
	if (IS_ERR(data->scratch_file)) {
		ret = PTR_ERR(data->scratch_file);
		data->scratch_file = NULL;
		return ret;
	}
	inode = file_inode(data->scratch_file);
	if (inode->i_sb->s_magic != EXT4_SUPER_MAGIC || !inode->i_sb->s_bdev ||
	    inode->i_blkbits != 12 || IS_ENCRYPTED(inode)) {
		ret = -EOPNOTSUPP;
		goto err_file;
	}
	zero = kzalloc(FMP_BLK_SIZE, GFP_KERNEL);
	if (!zero) {
		ret = -ENOMEM;
		goto err_file;
	}
	written = kernel_write(data->scratch_file, zero, FMP_BLK_SIZE, &pos);
	kfree(zero);
	if (written != FMP_BLK_SIZE) {
		ret = written < 0 ? written : -EIO;
		goto err_file;
	}
	ret = vfs_fsync(data->scratch_file, 0);
	if (ret)
		goto err_file;
	data->sector = bmap(inode, 0);
	if (!data->sector) {
		ret = -EIO;
		goto err_file;
	}
	data->bdev = blkdev_get_by_dev(inode->i_sb->s_bdev->bd_dev,
				      FMODE_READ | FMODE_WRITE, NULL);
	if (IS_ERR(data->bdev)) {
		ret = PTR_ERR(data->bdev);
		data->bdev = NULL;
		goto err_file;
	}
	return 0;
err_file:
	fput(data->scratch_file);
	data->scratch_file = NULL;
	return ret;
}

/* test block device init for fmp test */
struct fmp_test_data *fmp_test_init(struct exynos_fmp *fmp)
{
	struct fmp_test_data *data;
	struct device *dev;
	struct inode *inode;
	int ret;

	fmode_t fmode = FMODE_WRITE | FMODE_READ;

	if (!fmp) {
		pr_err("%s: Invalid exynos fmp struct\n", __func__);
		goto err;
	}

	dev = fmp->dev;
	data = kzalloc(sizeof(struct fmp_test_data), GFP_KERNEL);
	if (!data)
		goto err;
	data->bdev = blkdev_get_by_path(FIPS_BLOCK_NAME, fmode, NULL);
	if (IS_ERR(data->bdev)) {
		ret = PTR_ERR(data->bdev);
		data->bdev = NULL;
		if (ret != -ENOENT && ret != -ENOTDIR)
			goto err_data;
		ret = fmp_test_alloc_scratch(data);
		if (ret) {
			dev_err(dev, "%s: cannot allocate hardware-test scratch block: %d\n",
				__func__, ret);
			goto err_data;
		}
		dev_info(dev, "%s: using an allocated ext4 scratch block\n", __func__);
		return data;
	}
	data->test_block_offset = 1;

	inode = data->bdev->bd_inode;
	if (i_size_read(inode) < FMP_BLK_SIZE) {
		blkdev_put(data->bdev, fmode);
		goto err_data;
	}
	/* __getblk uses FMP_BLK_SIZE units, not the pseudo filesystem's size. */
	data->sector = (i_size_read(inode) / FMP_BLK_SIZE) - 1;

	return data;
err_data:
	kzfree(data);
err:
	return NULL;
}

int fmp_cipher_run(struct exynos_fmp *fmp, struct fmp_test_data *fdata,
		uint8_t *data, uint32_t len, bool bypass, uint32_t write,
		void *priv, struct fmp_crypto_info *ci)
{
	int ret = 0;
	struct device *dev;
	static struct buffer_head *bh;
	u32 org_algo_mode;

	if (!fmp || !fdata || !ci) {
		pr_err("%s: Invalid fmp struct(fmp, fdata, ci)\n", __func__);
		return -EINVAL;
	}
	dev = fmp->dev;

	bh = __getblk(fdata->bdev, fdata->sector, FMP_BLK_SIZE);
	if (!bh) {
		dev_err(dev, "%s: Fail to get block from bdev\n", __func__);
		return -ENODEV;
	}

	/* set algo_mode for test */
	org_algo_mode = ci->algo_mode;
	if (bypass)
		ci->algo_mode = EXYNOS_FMP_BYPASS_MODE;
	ci->algo_mode |= EXYNOS_FMP_ALGO_MODE_TEST;

	/* priv is diskc for crypto test. */
	if (!priv) {
		/* ci is fmp_test_data->ci */
		fmp->test_data = fdata;
		ci->ctx = fmp;
		ci->use_diskc = 0;
		ci->enc_mode = EXYNOS_FMP_FILE_ENC;
		bh->b_private = fmp;
		fmp->bh = bh;
		fmp->fips_run++;
	} else {
		/* ci is crypto_tfm_ctx(tfm) */
		bh->b_private = priv;
	}

	if (write == WRITE_MODE) {
		memcpy(bh->b_data, data, len);
		set_buffer_dirty(bh);
		ret = __sync_dirty_buffer(bh, REQ_SYNC | REQ_FUA);
		if (ret) {
			dev_err(dev, "%s: IO error syncing for write mode\n",
				__func__);
			ret = -EIO;
			goto out;
		}
		memset(bh->b_data, 0, FMP_BLK_SIZE);
	} else {
		get_bh(bh);
		lock_buffer(bh);
		fmp_set_bh_compl_handler(bh);
		submit_bh(REQ_OP_READ, REQ_SYNC | REQ_PRIO, bh);
		wait_on_buffer(bh);
		if (unlikely(!buffer_uptodate(bh))) {
			ret = -EIO;
			goto out;
		}
		memcpy(data, bh->b_data, len);
	}
out:
	if (ci)
		ci->algo_mode = org_algo_mode;
	put_bh(bh);

	fmp->bh = NULL;
	return ret;
}

int fmp_test_crypt(struct exynos_fmp *fmp, struct fmp_test_data *fdata,
		uint8_t *src, uint8_t *dst, uint32_t len, uint32_t enc,
		void *priv, struct fmp_crypto_info *ci)
{
	int ret = 0;

	if (!fdata) {
		pr_err("%s: Invalid exynos fmp struct\n", __func__);
		return -1;
	}

	if (enc == ENCRYPT) {
		ret = fmp_cipher_run(fmp, fdata, src, len, 0,
				WRITE_MODE, priv, ci);
		if (ret) {
			pr_err("Fail to run fmp cipher ret(%d)\n",
				ret);
			goto err;
		}
		ret = fmp_cipher_run(fmp, fdata, dst, len, 1,
				READ_MODE, priv, ci);
		if (ret) {
			pr_err("Fail to run fmp cipher ret(%d)\n",
				ret);
			goto err;
		}
	} else if (enc == DECRYPT) {
		ret = fmp_cipher_run(fmp, fdata, src, len, 1,
				WRITE_MODE, priv, ci);
		if (ret) {
			pr_err("Fail to run fmp cipher ret(%d)\n",
				ret);
			goto err;
		}
		ret = fmp_cipher_run(fmp, fdata, dst, len, 0,
				READ_MODE, priv, ci);
		if (ret) {
			pr_err("Fail to run fmp cipher ret(%d)\n",
				ret);
			goto err;
		}
	} else {
		pr_err("%s: Invalid enc %d mode\n", __func__, enc);
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}

/* test block device release for fmp test */
void fmp_test_exit(struct fmp_test_data *fdata)
{
	fmode_t fmode = FMODE_WRITE | FMODE_READ;
	struct buffer_head *bh;

	if (!fdata) {
		pr_err("%s: Invalid exynos fmp struct\n", __func__);
		return;
	}
	if (fdata->scratch_file && fdata->bdev) {
		bh = __find_get_block(fdata->bdev, fdata->sector, FMP_BLK_SIZE);
		if (bh) {
			lock_buffer(bh);
			bh->b_private = NULL;
			clear_buffer_uptodate(bh);
			unlock_buffer(bh);
			brelse(bh);
		}
	}
	if (fdata->bdev)
		blkdev_put(fdata->bdev, fmode);
	if (fdata->scratch_file)
		fput(fdata->scratch_file);
	kzfree(fdata);
}
