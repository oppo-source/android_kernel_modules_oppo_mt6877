/** Copyright (C), 2025-2029, OPLUS Mobile Comm Corp., Ltd.
* Description: oem data ops for game
* Author: zhoutianyao
* Create: 2025-1-15
* Notes: NA
*/

#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/minmax.h>
#include <linux/align.h>
#include <asm/cache.h>
#include <linux/topology.h>
#include <linux/vmalloc.h>
#include <asm/barrier.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <trace/events/sched.h>
#include <trace/hooks/sched.h>

#include "oem_data/gts_common.h"
#include "oem_data/game_oem_data.h"

/************************** gts cachep ************************/

struct kmem_cache *game_task_struct_cachep;
EXPORT_SYMBOL(game_task_struct_cachep);

static inline struct game_task_struct *alloc_game_task_struct_node(int node __maybe_unused)
{
	return kmem_cache_alloc(game_task_struct_cachep, GFP_ATOMIC);
}

static inline void free_game_task_struct(struct game_task_struct *gts)
{
	if (!gts) {
		return;
	}

	kmem_cache_free(game_task_struct_cachep, gts);
}

static int numa_node_of_task_struct(struct task_struct *tsk)
{
	return NUMA_NO_NODE;
}

/************************** vendor hooks ************************/

static void android_vh_dup_task_struct_hook(void *unused,
				struct task_struct *tsk, struct task_struct *orig)
{
	struct game_task_struct *gts = NULL;

	if (!tsk || !orig) {
		return;
	}
	/* The required space has been allocated */
	if (!IS_ERR_OR_NULL((void *)tsk->android_oem_data1[GTS_IDX])) {
		return;
	}

	gts = alloc_game_task_struct_node(numa_node_of_task_struct(orig));
	if (IS_ERR_OR_NULL(gts)) {
		return;
	}

	gts->task = tsk;

	smp_mb();

	WRITE_ONCE(tsk->android_oem_data1[GTS_IDX], (u64) gts);
}

static void android_vh_free_task_hook(void *unused, struct task_struct *tsk)
{
	struct game_task_struct *gts = NULL;

	if (!tsk) {
		return;
	}

	gts = (struct game_task_struct *) READ_ONCE(tsk->android_oem_data1[GTS_IDX]);
	if (IS_ERR_OR_NULL(gts)) {
		return;
	}

	WRITE_ONCE(tsk->android_oem_data1[GTS_IDX], 0);
	barrier();

	smp_mb();

	free_game_task_struct(gts);
}

static void register_game_oem_data_hooks(void)
{
	register_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_hook, NULL);
	register_trace_android_vh_free_task(android_vh_free_task_hook, NULL);
}

static void unregister_game_oem_data_hooks(void)
{
	unregister_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_hook, NULL);
	unregister_trace_android_vh_free_task(android_vh_free_task_hook, NULL);
}

/************************** public function ************************/

int game_oem_data_init(void)
{
	game_task_struct_cachep = kmem_cache_create("game_task_struct",
			sizeof(struct game_task_struct), 0,
			SLAB_PANIC|SLAB_ACCOUNT, init_game_task_struct);
	if (!game_task_struct_cachep) {
		return -ENOMEM;
	}

	register_game_oem_data_hooks();

	return 0;
}

void game_oem_data_exit(void)
{
	unregister_game_oem_data_hooks();
	kmem_cache_destroy(game_task_struct_cachep);
}
