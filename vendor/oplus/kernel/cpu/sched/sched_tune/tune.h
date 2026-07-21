/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef __TUNE_H__
#define __TUNE_H__

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "schedtune: " fmt

/*
 * Maximum number of boost groups to support
 * When per-task boosting is used we still allow only limited number of
 * boost groups for two main reasons:
 * 1. on a real system we usually have only few classes of workloads which
 *    make sense to boost with different values (e.g. background vs foreground
 *    tasks, interactive vs low-priority tasks)
 * 2. a limited number allows for a simpler and more memory/time efficient
 *    implementation especially for the computation of the per-CPU boost
 *    value
 */
#define BOOSTGROUPS_COUNT               (30)

/* We hold schedtune boost in effect for at least this long */
#define SCHEDTUNE_BOOST_HOLD_NS         50000000ULL

#define QOS_SCHED_TUNE_DEFAULT (-101)
#define QOS_SCHED_TUNE_RESET (0)

/* SchdTune tunables for a group of tasks */
struct schedtune {

	/* Boost group allocated ID */
	int idx;

	/* Boost value for tasks on that SchedTune CGroup */
	int boost;
};


/* SchedTune boost groups
 * Keep track of all the boost groups which impact on CPU, for example when a
 * CPU has two RUNNABLE tasks belonging to two different boost groups and thus
 * likely with different boost values.
 * Since on each system we expect only a limited number of boost groups, here
 * we use a simple array to keep track of the metrics required to compute the
 * maximum per-CPU boosting value.
 */
struct boost_groups {
	/* Maximum boost value for all RUNNABLE tasks on a CPU */
	int boost_max;
	u64 boost_ts;
	struct {
		/* True when this boost group maps an actual cgroup */
		bool valid;
		/* The boost for tasks on that boost group */
		int boost;
		/* Count of RUNNABLE tasks on that boost group */
		unsigned tasks;
		/* Timestamp of boost activation */
		u64 ts;
	} group[BOOSTGROUPS_COUNT];
	/* CPU's boost group locking */
	raw_spinlock_t lock;
};

int schedtune_boost_write(struct cgroup_subsys_state *css, struct cftype *cft, s64 boost);
s64 schedtune_boost_read(struct cgroup_subsys_state *css, struct cftype *cft);
noinline unsigned long  stune_util(int cpu, unsigned long other_util,
		 unsigned long util);

struct schedtune *task_schedtune(struct task_struct *tsk);
unsigned long schedtune_task_util(struct task_struct *p);
noinline unsigned long  stune_util(int cpu, unsigned long other_util,
		 unsigned long util);
int schedtune_task_boost(struct task_struct *p);

#endif /* __TUNE_H__ */

