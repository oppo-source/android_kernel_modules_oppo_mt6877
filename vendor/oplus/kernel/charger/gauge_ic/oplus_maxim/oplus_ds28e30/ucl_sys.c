// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

#include "ucl_config.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_types.h"

#include "ucl_hash.h"
#ifdef HASH_SHA256
#include "ucl_sha256.h"
#endif
#ifdef HASH_SIA256
#include "ucl_sia256.h"
#endif

__API__ int hash_size[MAX_HASH_FUNCTIONS];

int __API__ ucl_init(void)
{
#ifdef HASH_SHA256
	hash_size[UCL_SHA256] = UCL_SHA256_HASHSIZE;
#endif
#ifdef HASH_SIA256
	hash_size[UCL_SIA256] = UCL_SIA256_HASHSIZE;
#endif
	return(UCL_OK);
}

