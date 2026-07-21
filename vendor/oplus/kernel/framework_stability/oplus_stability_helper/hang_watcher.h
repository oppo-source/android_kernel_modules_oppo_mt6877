// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2030 Oplus. All rights reserved.
 */

#ifndef _HANG_CHECKER_H_
#define _HANG_CHECKER_H_

#include <linux/sched.h>

void check_system_server_hang(int sig, struct task_struct *src, struct task_struct *dst);

#endif /* _HANG_CHECKER_H_ */
