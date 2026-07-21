/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _OPLUS_QOS_SCHED_CGRP_H
#define _OPLUS_QOS_SCHED_CGRP_H

int qs_get_cgrp_qos_level(struct task_struct *task);
int qos_sched_init_cgroup(void);
int qos_sched_deinit_cgroup(void);
#endif /* _OPLUS_QOS_SCHED_CGRP_H */

