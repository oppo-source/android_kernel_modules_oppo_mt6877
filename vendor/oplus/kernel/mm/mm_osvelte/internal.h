/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */
#ifndef _OSVELTE_INTERNAL_H
#define _OSVELTE_INTERNAL_H

#include <asm/ioctls.h>
#include "common.h"

#include "mm-utils.h"

/* experimental feature */
#define OSVELTE_FEATURE_USE_HASHLIST 1

#define OSVELTE_MAJOR		(0)
#define OSVELTE_MINOR		(2)
#define OSVELTE_PATCH_NUM	(7)
#define OSVELTE_VERSION (OSVELTE_MAJOR << 16 | OSVELTE_MINOR)

#define CMD_COMMON_MIN		CMD_OSVELTE_SET_SCENE
#define CMD_COMMON_MAX		CMD_OSVELTE_CLEAR_SCENE
#define CMD_COMMON_INVALID	0xFFFFFFFE

#define OSVELTE_STATIC_ASSERT(c)				\
{								\
	enum { OSVELTE_static_assert = 1 / (int)(!!(c)) };	\
}

#define OSVELTE_TAG "osvelte"
#define osvelte_loge(f, ...)					\
	mm_loge_tag(OSVELTE_TAG, f, ##__VA_ARGS__)

#define osvelte_logi(f, ...)					\
	mm_logi_tag(OSVELTE_TAG, f, ##__VA_ARGS__)

#define osvelte_logd(f, ...)					\
	mm_logd_tag(OSVELTE_TAG, f, ##__VA_ARGS__)
#endif /* _OSVELTE_INTERNAL_H */
