// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

#ifndef _ECC_GENERATE_KEY_H
#define _ECC_GENERATE_KEY_H

#include "ecdsa_generic_api.h"
#include "ucl_defs.h"
#include "ucl_sha256.h"
#include "ucl_sys.h"
#include "ucl_rng.h"

#define uchar unsigned char

#ifndef GEN_ECC_KEY
extern int deep_cover_generate_public_key(uchar *private_key, uchar *pubkey_x, uchar *pubkey_y);
#endif /* GEN_ECC_KEY */
#endif /* _ECC_GENERATE_KEY_H */
