// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2014, Linaro Limited
 * Copyright (c) 2018, Renesas Electronics Corporation
 */

#include <crypto/crypto.h>
#include <initcall.h>
#include <kernel/panic.h>
#include <kernel/tee_time.h>
#include <rng_support.h>
#include <stdlib.h>
#include <string_ext.h>
#include <string.h>
#include <tee/tee_cryp_utl.h>
#include <trace.h>
#include <utee_defines.h>

TEE_Result tee_hash_get_digest_size(uint32_t algo, size_t *size)
{
	switch (algo) {
	case TEE_ALG_MD5:
	case TEE_ALG_HMAC_MD5:
		*size = TEE_MD5_HASH_SIZE;
		break;
	case TEE_ALG_SHA1:
	case TEE_ALG_HMAC_SHA1:
	case TEE_ALG_DSA_SHA1:
		*size = TEE_SHA1_HASH_SIZE;
		break;
	case TEE_ALG_SHA224:
	case TEE_ALG_HMAC_SHA224:
	case TEE_ALG_DSA_SHA224:
		*size = TEE_SHA224_HASH_SIZE;
		break;
	case TEE_ALG_SHA256:
	case TEE_ALG_HMAC_SHA256:
	case TEE_ALG_DSA_SHA256:
		*size = TEE_SHA256_HASH_SIZE;
		break;
	case TEE_ALG_SHA384:
	case TEE_ALG_HMAC_SHA384:
		*size = TEE_SHA384_HASH_SIZE;
		break;
	case TEE_ALG_SHA512:
	case TEE_ALG_HMAC_SHA512:
		*size = TEE_SHA512_HASH_SIZE;
		break;
	case TEE_ALG_SM3:
	case TEE_ALG_HMAC_SM3:
		*size = TEE_SM3_HASH_SIZE;
		break;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}

	return TEE_SUCCESS;
}

TEE_Result tee_hash_createdigest(uint32_t algo, const uint8_t *data,
				 size_t datalen, uint8_t *digest,
				 size_t digestlen)
{
	TEE_Result res;
	void *ctx = NULL;

	res = crypto_hash_alloc_ctx(&ctx, algo);
	if (res)
		return res;

	res = crypto_hash_init(ctx);
	if (res)
		goto out;

	if (datalen != 0) {
		res = crypto_hash_update(ctx, data, datalen);
		if (res)
			goto out;
	}

	res = crypto_hash_final(ctx, digest, digestlen);
out:
	crypto_hash_free_ctx(ctx);

	return res;
}

TEE_Result tee_mac_get_digest_size(uint32_t algo, size_t *size)
{
	switch (algo) {
	case TEE_ALG_HMAC_MD5:
	case TEE_ALG_HMAC_SHA224:
	case TEE_ALG_HMAC_SHA1:
	case TEE_ALG_HMAC_SHA256:
	case TEE_ALG_HMAC_SHA384:
	case TEE_ALG_HMAC_SHA512:
	case TEE_ALG_HMAC_SM3:
		return tee_hash_get_digest_size(algo, size);
	case TEE_ALG_AES_CBC_MAC_NOPAD:
	case TEE_ALG_AES_CBC_MAC_PKCS5:
	case TEE_ALG_AES_CMAC:
	case TEE_ALG_AES_XCBC_MAC:
		*size = TEE_AES_BLOCK_SIZE;
		return TEE_SUCCESS;
	case TEE_ALG_DES_CBC_MAC_NOPAD:
	case TEE_ALG_DES_CBC_MAC_PKCS5:
	case TEE_ALG_DES3_CBC_MAC_NOPAD:
	case TEE_ALG_DES3_CBC_MAC_PKCS5:
		*size = TEE_DES_BLOCK_SIZE;
		return TEE_SUCCESS;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

TEE_Result tee_cipher_get_block_size(uint32_t algo, size_t *size)
{
	switch (algo) {
	case TEE_ALG_AES_CBC_MAC_NOPAD:
	case TEE_ALG_AES_CBC_MAC_PKCS5:
	case TEE_ALG_AES_CMAC:
	case TEE_ALG_AES_XCBC_MAC:
	case TEE_ALG_AES_ECB_NOPAD:
	case TEE_ALG_AES_CBC_NOPAD:
	case TEE_ALG_AES_CTR:
	case TEE_ALG_AES_CTS:
	case TEE_ALG_AES_XTS:
	case TEE_ALG_AES_CCM:
	case TEE_ALG_AES_GCM:
	case TEE_ALG_AES_OFB:
	case TEE_ALG_SM4_ECB_NOPAD:
	case TEE_ALG_SM4_CBC_NOPAD:
	case TEE_ALG_SM4_CTR:
		*size = 16;
		break;

	case TEE_ALG_DES_CBC_MAC_NOPAD:
	case TEE_ALG_DES_CBC_MAC_PKCS5:
	case TEE_ALG_DES_ECB_NOPAD:
	case TEE_ALG_DES_CBC_NOPAD:
	case TEE_ALG_DES3_CBC_MAC_NOPAD:
	case TEE_ALG_DES3_CBC_MAC_PKCS5:
	case TEE_ALG_DES3_ECB_NOPAD:
	case TEE_ALG_DES3_CBC_NOPAD:
		*size = 8;
		break;

	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}

	return TEE_SUCCESS;
}

TEE_Result tee_do_cipher_update(void *ctx, uint32_t algo,
				TEE_OperationMode mode, bool last_block,
				const uint8_t *data, size_t len, uint8_t *dst)
{
	TEE_Result res;
	size_t block_size;

	if (mode != TEE_MODE_ENCRYPT && mode != TEE_MODE_DECRYPT)
		return TEE_ERROR_BAD_PARAMETERS;

	/*
	 * Check that the block contains the correct number of data, apart
	 * for the last block in some XTS / CTR / XTS mode
	 */
	res = tee_cipher_get_block_size(algo, &block_size);
	if (res != TEE_SUCCESS)
		return res;
	if ((len % block_size) != 0) {
		if (!last_block && algo != TEE_ALG_AES_CTR)
			return TEE_ERROR_BAD_PARAMETERS;

		switch (algo) {
		case TEE_ALG_AES_ECB_NOPAD:
		case TEE_ALG_DES_ECB_NOPAD:
		case TEE_ALG_DES3_ECB_NOPAD:
		case TEE_ALG_AES_CBC_NOPAD:
		case TEE_ALG_DES_CBC_NOPAD:
		case TEE_ALG_DES3_CBC_NOPAD:
		case TEE_ALG_SM4_ECB_NOPAD:
		case TEE_ALG_SM4_CBC_NOPAD:
			return TEE_ERROR_BAD_PARAMETERS;

		case TEE_ALG_AES_CTR:
		case TEE_ALG_AES_XTS:
		case TEE_ALG_AES_CTS:
		case TEE_ALG_AES_OFB:
			/*
			 * These modes doesn't require padding for the last
			 * block.
			 *
			 * This isn't entirely true, both XTS and CTS can only
			 * encrypt minimum one block and also they need at least
			 * one complete block in the last update to finish the
			 * encryption. The algorithms are supposed to detect
			 * that, we're only making sure that all data fed up to
			 * that point consists of complete blocks.
			 */
			break;

		default:
			return TEE_ERROR_NOT_SUPPORTED;
		}
	}

	return crypto_cipher_update(ctx, mode, last_block, data, len, dst);
}

/*
 * Override this in your platform code to feed the PRNG platform-specific
 * jitter entropy. This implementation does not efficiently deliver entropy
 * and is here for backwards-compatibility.
 */
__weak void plat_prng_add_jitter_entropy(enum crypto_rng_src sid,
					 unsigned int *pnum)
{
	TEE_Time current;

#ifdef CFG_SECURE_TIME_SOURCE_REE
	if (CRYPTO_RNG_SRC_IS_QUICK(sid))
		return; /* Can't read REE time here */
#endif

	if (tee_time_get_sys_time(&current) == TEE_SUCCESS)
		crypto_rng_add_event(sid, pnum, &current, sizeof(current));
}

__weak void plat_rng_init(void)
{
	TEE_Result res = TEE_SUCCESS;
	TEE_Time t;

#ifndef CFG_SECURE_TIME_SOURCE_REE
	/*
	 * This isn't much of a seed. Ideally we should either get a seed from
	 * a hardware RNG or from a previously saved seed.
	 *
	 * Seeding with hardware RNG is currently up to the platform to
	 * override this function.
	 *
	 * Seeding with a saved seed will require cooperation from normal
	 * world, this is still TODO.
	 */
	res = tee_time_get_sys_time(&t);
#else
	EMSG("Warning: seeding RNG with zeroes");
	memset(&t, 0, sizeof(t));
#endif
	if (!res)
		res = crypto_rng_init(&t, sizeof(t));
	if (res) {
		EMSG("Failed to initialize RNG: %#" PRIx32, res);
		panic();
	}
}

static TEE_Result tee_cryp_init(void)
{
	TEE_Result res = crypto_init();

	if (res) {
		EMSG("Failed to initialize crypto API: %#" PRIx32, res);
		panic();
	}
	plat_rng_init();

	return TEE_SUCCESS;
}
service_init(tee_cryp_init);
