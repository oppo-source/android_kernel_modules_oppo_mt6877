// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

#include "ucl_config.h"
#include "ucl_types.h"
#include "ucl_sys.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_rng.h"
#include "ucl_hash.h"
#include "deep_cover_coproc.h"
#ifdef HASH_SHA256
#include "ucl_sha256.h"
#endif


/* this is not secure for ECDSA signatures,  as being pseudo random number generator */
/* this is for test and demo only */
int ucl_rng_read(u8 *rand, u32 rand_byte_len)
{
	int msgi, j;
	static u8 pseudo[BYTE_LENGTH_16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x00, 0x11, 0x22,
				0x33, 0x44, 0x55, 0x00, 0x11, 0x22, 0x33, 0x44};
	u8 output[BYTE_LENGTH_32], input[BYTE_LENGTH_16];
	u8 block_size;
	block_size = BYTE_LENGTH_16;

	for (msgi = 0;msgi < (int)rand_byte_len;) {
		for (j = 0; j < block_size; j++)
			input[j] = pseudo[j];
		ucl_sha256(output, input, block_size);
		for (j = 0; j < block_size; j++)
			pseudo[j] = output[j];
		for (j = 0; j < block_size; j++) {
			if(msgi < (int)rand_byte_len) {
				rand[msgi] = output[j];
				msgi++;
			}
		}
	}
	return (rand_byte_len);
}
