// SPDX-License-Identifier: GPL-2.0
/*
 * Compatibility helpers for Samsung SDP and DDAR on the modern fscrypt split.
 */

#include <crypto/algapi.h>
#include <crypto/skcipher.h>
#include <keys/user-type.h>
#include <linux/key.h>
#include <linux/scatterlist.h>

#include "../fscrypt_private.h"

static int derive_key_aes(const u8 *master_key,
			  const u8 nonce[FS_KEY_DERIVATION_NONCE_SIZE],
			  u8 *derived_key, unsigned int derived_keysize)
{
	struct crypto_skcipher *tfm;
	struct skcipher_request *req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	struct scatterlist src_sg, dst_sg;
	int err;

	tfm = crypto_alloc_skcipher("ecb(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	crypto_skcipher_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		err = -ENOMEM;
		goto out;
	}

	skcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			crypto_req_done, &wait);
	err = crypto_skcipher_setkey(tfm, nonce,
				     FS_KEY_DERIVATION_NONCE_SIZE);
	if (err)
		goto out;

	sg_init_one(&src_sg, master_key, derived_keysize);
	sg_init_one(&dst_sg, derived_key, derived_keysize);
	skcipher_request_set_crypt(req, &src_sg, &dst_sg, derived_keysize,
				   NULL);
	err = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);
out:
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return err;
}

static struct key *
find_and_lock_process_key(const char *prefix,
			  const u8 descriptor[FSCRYPT_KEY_DESCRIPTOR_SIZE],
			  unsigned int min_keysize,
			  const struct fscrypt_key **payload_ret)
{
	const struct user_key_payload *ukp;
	const struct fscrypt_key *payload;
	char *description;
	struct key *key;

	description = kasprintf(GFP_NOFS, "%s%*phN", prefix,
				FSCRYPT_KEY_DESCRIPTOR_SIZE, descriptor);
	if (!description)
		return ERR_PTR(-ENOMEM);

	key = request_key(&key_type_logon, description, NULL);
	kfree(description);
	if (IS_ERR(key))
		return key;

	down_read(&key->sem);
	ukp = user_key_payload_locked(key);
	if (!ukp)
		goto invalid;

	payload = (const struct fscrypt_key *)ukp->data;
	if (ukp->datalen != sizeof(*payload) ||
	    payload->size < min_keysize || payload->size > FSCRYPT_MAX_KEY_SIZE)
		goto invalid;

	*payload_ret = payload;
	return key;

invalid:
	up_read(&key->sem);
	key_put(key);
	return ERR_PTR(-ENOKEY);
}

static int get_v1_context(struct inode *inode, struct fscrypt_context_v1 *ctx)
{
	int err;

	memset(ctx, 0, sizeof(*ctx));
	err = inode->i_sb->s_cop->get_context(inode, ctx, sizeof(*ctx));
	if (err == offsetof(struct fscrypt_context_v1, knox_flags))
		err = sizeof(*ctx);
	if (err < 0)
		return err;
	if (err != sizeof(*ctx) || ctx->version != FSCRYPT_CONTEXT_V1)
		return -EINVAL;
	return 0;
}

static int get_master_key(struct inode *inode,
			  const struct fscrypt_context_v1 *ctx,
			  unsigned int min_keysize,
			  struct fscrypt_key *master_key)
{
	const struct fscrypt_key *payload;
	struct key *key;

	key = find_and_lock_process_key(FSCRYPT_KEY_DESC_PREFIX,
					ctx->master_key_descriptor,
					min_keysize, &payload);
	if (key == ERR_PTR(-ENOKEY) && inode->i_sb->s_cop->key_prefix)
		key = find_and_lock_process_key(inode->i_sb->s_cop->key_prefix,
						ctx->master_key_descriptor,
						min_keysize, &payload);
	if (IS_ERR(key))
		return PTR_ERR(key);

	memcpy(master_key, payload, sizeof(*master_key));
	up_read(&key->sem);
	key_put(key);
	return 0;
}

int fscrypt_get_encryption_key(struct inode *inode, struct fscrypt_key *key)
{
	struct fscrypt_context_v1 ctx;
	struct fscrypt_key master_key;
	struct fscrypt_info *ci = inode->i_crypt_info;
	int err;

	if (!ci || !ci->ci_mode)
		return -EINVAL;

	err = get_v1_context(inode, &ctx);
	if (err)
		return err;
	err = get_master_key(inode, &ctx, ci->ci_mode->keysize, &master_key);
	if (err)
		return err;

	if (ctx.flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) {
		memcpy(key->raw, master_key.raw, ci->ci_mode->keysize);
		err = 0;
	} else {
		err = derive_key_aes(master_key.raw, ctx.nonce, key->raw,
				     ci->ci_mode->keysize);
	}
	if (!err)
		key->size = ci->ci_mode->keysize;
	memzero_explicit(&master_key, sizeof(master_key));
	return err;
}
EXPORT_SYMBOL(fscrypt_get_encryption_key);

int fscrypt_get_encryption_key_classified(struct inode *inode,
					  struct fscrypt_key *key)
{
	struct fscrypt_info *ci = inode->i_crypt_info;
	int err = -EINVAL;

	if (!ci || !ci->ci_mode || !ci->ci_sdp_info)
		return -EINVAL;

	if (fscrypt_sdp_is_uninitialized(ci))
		err = fscrypt_sdp_derive_uninitialized_dek(ci, key->raw,
							   ci->ci_mode->keysize);
	else if (fscrypt_sdp_is_sensitive(ci))
		err = fscrypt_sdp_derive_dek(ci, key->raw,
					     ci->ci_mode->keysize);
	else if (fscrypt_sdp_is_native(ci))
		err = fscrypt_sdp_derive_fek(inode, ci, key->raw,
					     ci->ci_mode->keysize);

	if (!err)
		key->size = ci->ci_mode->keysize;
	return err;
}
EXPORT_SYMBOL(fscrypt_get_encryption_key_classified);

int fscrypt_get_encryption_kek(struct inode *inode,
			      struct fscrypt_info *ci,
			      struct fscrypt_key *kek)
{
	struct fscrypt_context_v1 ctx;
	int err;

	if (!ci || !ci->ci_mode)
		return -EINVAL;

	err = get_v1_context(inode, &ctx);
	if (err)
		return err;
	return get_master_key(inode, &ctx, ci->ci_mode->keysize, kek);
}
EXPORT_SYMBOL(fscrypt_get_encryption_kek);
