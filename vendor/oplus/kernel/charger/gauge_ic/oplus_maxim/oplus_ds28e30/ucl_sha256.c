// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

#include <linux/slab.h> /* kfree() */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include "ucl_hash.h"
#ifdef HASH_SHA256

#include "deep_cover_coproc.h"
#include "ucl_config.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_types.h"
#include "sha256.h"

extern int sha_debug;

static u32 _wsb_b2w(u8 *src)
{
	return ((u32)src[3] | ((u32)src[2] << BYTE_LENGTH_8) |
		((u32)src[1] << BYTE_LENGTH_16) | ((u32)src[0] << BYTE_LENGTH_24));
}

static void _wsb_w2b(u8 *dst, u32 src)
{
	dst[3] = src & BYTE_VALUE_FF;
	src >>= BYTE_VALUE_8;
	dst[2] = src & BYTE_VALUE_FF;
	src >>= BYTE_VALUE_8;
	dst[1] = src & BYTE_VALUE_FF;
	src >>= BYTE_VALUE_8;
	dst[0] = src & BYTE_VALUE_FF;
}

void swapcpy_b2w(u32 *dst, const u8 *src, u32 word_len)
{
	int i;

	for (i = 0; i < (int)word_len; i++) {
		dst[i] = _wsb_b2w((u8 *)src);
		src += BYTE_LENGTH_4;
	}
}


void swapcpy_w2b(u8 *dst, const u32 *src, u32 word_len)
{
	int i;

	for (i = 0; i < (int)word_len; i++) {
		_wsb_w2b(dst, src[i]);
		dst += BYTE_LENGTH_4;
	}
}

void swapcpy_b2b(u8 *dst, u8 *src, u32 word_len)
{
	u8 tmp;
	int i;

	for (i = 0; i < (int)word_len; i++) {
		tmp = src[0];
		dst[0] = src[3];
		dst[3] = tmp;
		tmp = src[1];
		dst[1] = src[2];
		dst[2] = tmp;
		dst += BYTE_LENGTH_4;
		src += BYTE_LENGTH_4;
	}
}

int ucl_sha256_init(ucl_sha256_ctx_t *ctx)
{
	u32 temp_ctx_state[BYTE_LENGTH_8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	ctx->state[0] = temp_ctx_state[0];
	ctx->state[1] = temp_ctx_state[1];
	ctx->state[2] = temp_ctx_state[2];
	ctx->state[3] = temp_ctx_state[3];
	ctx->state[4] = temp_ctx_state[4];
	ctx->state[5] = temp_ctx_state[5];
	ctx->state[6] = temp_ctx_state[6];
	ctx->state[7] = temp_ctx_state[7];
	ctx->count[0] = ZERO_VALUE;
	ctx->count[1] = ZERO_VALUE;

	return UCL_OK;
}

int ucl_sha256_core(ucl_sha256_ctx_t *ctx, u8 *data, u32 data_len)
{
	u32 indexh, part_len, i;
	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	if ((data == NULL) || (data_len == ZERO_VALUE))
		return UCL_NOP;
	/** Compute number of bytes mod 64 */
	indexh = (u32)((ctx->count[1] >> BYTE_LENGTH_3) & BYTE_VALUE_3F);

	/* Update number of bits */
	if ((ctx->count[1] += ((u32)data_len << BYTE_LENGTH_3)) < ((u32)data_len << BYTE_LENGTH_3))
		ctx->count[0]++;
	ctx->count[0] += ((u32)data_len >> BYTE_LENGTH_29);
	part_len = BYTE_LENGTH_64 - indexh;

	/* Process 512-bits block as many times as possible. */

	if (data_len >= part_len) {
		memcpy(&ctx->buffer[indexh], data, part_len);
		swapcpy_b2b(ctx->buffer, ctx->buffer, BYTE_LENGTH_16);
		sha256_stone(ctx->state, (u32 *)ctx->buffer);
		for (i = part_len; i + BYTE_LENGTH_63 < data_len; i += BYTE_LENGTH_64) {
			swapcpy_b2b(ctx->buffer, &data[i], BYTE_LENGTH_16);
			sha256_stone(ctx->state, (u32 *) ctx->buffer);
		}
		indexh = ZERO_VALUE;
	} else {
		i = ZERO_VALUE;
	}

	/* Buffer remaining data */
	memcpy((void *)&ctx->buffer[indexh], &data[i], data_len - i);

	return UCL_OK;
}


int ucl_sha256_finish(u8 *hash, ucl_sha256_ctx_t *ctx)
{
	u8 bits[BYTE_LENGTH_8];
	u32 indexh, pad_len;
	u8 padding[BYTE_LENGTH_64];
	padding[0] = BYTE_VALUE_80;
	memset((void *)padding + 1, ZERO_VALUE, BYTE_LENGTH_63);

	if (hash == NULL)
		return UCL_INVALID_OUTPUT;

	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	/* Save number of bits */
	swapcpy_w2b(bits, ctx->count, BYTE_LENGTH_2);
	/* Pad out to 56 mod 64. */
	indexh = (u32)((ctx->count[1] >> 3) & BYTE_VALUE_3F);
	pad_len = (indexh < BYTE_LENGTH_56) ? (BYTE_LENGTH_56 - indexh) : (BYTE_LENGTH_120 - indexh);
	ucl_sha256_core(ctx, padding, pad_len);
	/* Append length (before padding) */
	ucl_sha256_core(ctx, bits, BYTE_LENGTH_8);
	/* Store state in digest */
	swapcpy_w2b(hash, ctx->state, BYTE_LENGTH_8);
	/* Zeroize sensitive information. */
	memset(ctx, ZERO_VALUE, sizeof(*ctx));

	return UCL_OK;
}

int ucl_sha256(u8 *hash, u8 *message, u32 byte_length)
{
	ucl_sha256_ctx_t ctx;

	if (hash == NULL)
		return UCL_INVALID_OUTPUT;

	ucl_sha256_init(&ctx);
	ucl_sha256_core(&ctx, message, byte_length);
	ucl_sha256_finish(hash, &ctx);

	return UCL_OK;
}
#endif /* HASH_SHA256 */
