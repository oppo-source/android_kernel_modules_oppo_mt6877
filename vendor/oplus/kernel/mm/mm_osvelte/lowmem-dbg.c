// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/rcupdate.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/ksm.h>
#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/proc_fs.h>
#include <linux/vmstat.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <generated/utsrelease.h>
#include <trace/events/vmscan.h>
#include <trace/hooks/signal.h>

#include "../../../mm/slab.h"

#include "common.h"
#include "internal.h"
#include "memstat.h"
#include "sys-memstat.h"
#include "lowmem-dbg.h"
#include "mm-utils.h"

static struct list_head *debug_slab_caches;
static struct mutex *debug_slab_mutex;

static void lowmem_dbg_dump(struct work_struct *work);

static DECLARE_WORK(lowmem_dbg_work, lowmem_dbg_dump);

struct files_acct {
	size_t sz;
	size_t watermark;
	bool show;
	int (*iter_fd)(const void *p, struct file *file, unsigned fd);
};

struct lowmem_dbg_cfg {
	u32 interval;
	u32 lmkd_interval;
	u64 last_jiffies;
	u64 watermark_low;
	u64 watermark_slab;
	u64 watermark_dmabuf;
	u64 watermark_gpu;
	u64 watermark_other;
	struct kobject *kobj;
};
static struct lowmem_dbg_cfg g_cfg;

struct task_struct *find_lock_task_mm_dup(struct task_struct *p)
{
	struct task_struct *t;

	rcu_read_lock();

	for_each_thread(p, t) {
		task_lock(t);
		if (likely(t->mm))
			goto found;
		task_unlock(t);
	}
	t = NULL;
found:
	rcu_read_unlock();

	return t;
}

static bool frozen(struct task_struct *p)
{
	return READ_ONCE(p->__state) & TASK_FROZEN;
}

#define MEDIA_UID (1013)
static int dump_procs(bool verbose)
{
	struct task_struct *p;
	struct task_struct *tsk;
	char task_state = ' ';
	char frozen_mark = ' ';
	/* unsigned long tsk_nr_ptes = 0; */
	pid_t ppid = 0;
	struct task_struct *bad_process = NULL;
	unsigned long bp_anon, bp_swap;

	osvelte_info("======= %s\n", __func__);
	osvelte_info("comm             32   uid s f   pid  ppid   oom       vss    anon    file   shmem    swap\n");
	rcu_read_lock();
	for_each_process(p) {
		unsigned long anon, swap;
		uid_t uid;

		tsk = find_lock_task_mm_dup(p);
		if (!tsk)
			continue;

		/* tsk_nr_ptes = mm_pgtables_bytes(tsk->mm); */
		task_state = task_state_to_char(tsk);
		/* check whether we have freezed a task. */
		frozen_mark = frozen(tsk) ? '*' : ' ';
		ppid = task_pid_nr(rcu_dereference(tsk->real_parent));
		anon = get_mm_counter(tsk->mm, MM_ANONPAGES);
		swap = get_mm_counter(tsk->mm, MM_SWAPENTS);
		uid = from_kuid(&init_user_ns, task_uid(tsk));

		osvelte_info("%-16s %2d %5d %c %c %5d %5d %5d %9lu %7lu %7lu %7lu %7lu\n",
			     tsk->comm, test_ti_thread_flag(task_thread_info(tsk), TIF_32BIT) != 0,
			     uid,
			     task_state, frozen_mark,
			     tsk->pid, ppid, tsk->signal->oom_score_adj,
			     tsk->mm->total_vm,
			     anon,
			     get_mm_counter(tsk->mm, MM_FILEPAGES),
			     get_mm_counter(tsk->mm, MM_SHMEMPAGES),
			     swap);

		if (uid == MEDIA_UID && !bad_process && common_is_bad_process(tsk, anon + swap)) {
			bad_process = tsk;
			get_task_struct(bad_process);
			bp_anon = anon;
			bp_swap = swap;
		}
		task_unlock(tsk);
	}
	rcu_read_unlock();

	if (bad_process) {
		pr_err("killed BAD process %d (%s) anon: %lu swap: %lu\n",
		       bad_process->tgid, bad_process->comm, K(bp_anon), K(bp_swap));
		do_send_sig_info(SIGABRT, SEND_SIG_PRIV, bad_process, PIDTYPE_TGID);
		put_task_struct(bad_process);
	}
	return 0;
}

static int iter_ashmem(const void *p, struct file *file, unsigned fd)
{
	struct files_acct *acct = (struct files_acct*)p;
	struct ashmem_area *asma;

	if (!file || !file->private_data || !is_ashmem_file(file))
		return 0;

	asma = file->private_data;
	if (!asma->size || !asma->file)
		return 0;

	if (acct->show) {
		osvelte_info("inode: %8ld sz: %8zu name: %s",
			     file_inode(asma->file)->i_ino, asma->size / SZ_1K,
			     asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0' ?
			     asma->name + ASHMEM_NAME_PREFIX_LEN : ASHMEM_NAME_DEF);
		return 0;
	}

	acct->sz += asma->size;
	return 0;
}

static int iter_dmabuf(const void *p, struct file *file, unsigned fd)
{
	struct files_acct *acct = (struct files_acct*)p;
	struct dma_buf *dmabuf;

	if (!file || !file->private_data || !is_dma_buf_file(file))
		return 0;

	dmabuf = file->private_data;
	if (!dmabuf->size)
		return 0;

	acct->sz += dmabuf->size;
	if (acct->show) {
		spin_lock(&dmabuf->name_lock);
		osvelte_info("inode: %8ld sz: %8zu name: %s",
			     file_inode(dmabuf->file)->i_ino, dmabuf->size / SZ_1K,
			     dmabuf->name ?: "");
		spin_unlock(&dmabuf->name_lock);
		return 0;
	}
	return 0;
}

int dump_procs_fd_info(struct files_acct *acct, const char *title, bool verbose)
{
	struct task_struct *tsk = NULL;

	osvelte_info("======= %s\n", title);
	osvelte_info("%-16s %-5s size\n", "comm", "pid");

	rcu_read_lock();
	for_each_process(tsk) {
		if (tsk->flags & PF_KTHREAD)
			continue;

		acct->sz = 0;
		acct->show = verbose;

		task_lock(tsk);
		iterate_fd(tsk->files, 0, acct->iter_fd, (void *)acct);
		task_unlock(tsk);

		if (!verbose && PAGES(acct->sz) > acct->watermark) {
			acct->sz = 0;
			acct->show = true;
			/* iter fd twice & show buf info */
			task_lock(tsk);
			iterate_fd(tsk->files, 0, acct->iter_fd, (void *)acct);
			task_unlock(tsk);
		}

		if (acct->sz)
			osvelte_info("%-16s %5d %zu\n", tsk->comm, tsk->pid,
				     acct->sz / SZ_1K);
	}
	rcu_read_unlock();
	return 0;
}

#ifdef CONFIG_SLUB_DEBUG
static int dump_slab_info(bool verbose)
{
	if(!debug_slab_caches)
		return 0;

	if(!debug_slab_mutex)
		return 0;

	osvelte_info("======= %s\n", __func__);
	if (likely(!verbose)) {
		unsigned long slab_pages = 0;
		struct kmem_cache *cachep = NULL;
		struct kmem_cache *max_cachep = NULL;
		struct kmem_cache *prev_max_cachep = NULL;

		mutex_lock(debug_slab_mutex);
		list_for_each_entry(cachep, debug_slab_caches, list) {
			struct slabinfo sinfo;
			unsigned long scratch;

			memset(&sinfo, 0, sizeof(sinfo));
			get_slabinfo(cachep, &sinfo);
			scratch = sinfo.num_slabs << sinfo.cache_order;

			if (slab_pages < scratch) {
				slab_pages = scratch;
				prev_max_cachep = max_cachep;
				max_cachep = cachep;
			}
		}

		if (max_cachep || prev_max_cachep)
			osvelte_info("name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab> :"
				     " tunables <limit> <batchcount> <sharedfactor> : slabdata <active_slabs> <num_slabs> <sharedavail>\n");

		if (max_cachep) {
			struct slabinfo sinfo;

			memset(&sinfo, 0, sizeof(sinfo));
			get_slabinfo(max_cachep, &sinfo);

			osvelte_info("%-17s %6lu %6lu %6u %4u %4d : tunables %4u %4u %4u : slabdata %6lu %6lu %6lu\n",
				     max_cachep->name, sinfo.active_objs,
				     sinfo.num_objs, max_cachep->size,
				     sinfo.objects_per_slab,
				     (1 << sinfo.cache_order),
				     sinfo.limit, sinfo.batchcount, sinfo.shared,
				     sinfo.active_slabs, sinfo.num_slabs,
				     sinfo.shared_avail);
		}

		if (prev_max_cachep) {
			struct slabinfo sinfo;

			memset(&sinfo, 0, sizeof(sinfo));
			get_slabinfo(prev_max_cachep, &sinfo);

			osvelte_info("%-17s %6lu %6lu %6u %4u %4d : tunables %4u %4u %4u : slabdata %6lu %6lu %6lu\n",
				     prev_max_cachep->name, sinfo.active_objs,
				     sinfo.num_objs, prev_max_cachep->size,
				     sinfo.objects_per_slab,
				     (1 << sinfo.cache_order),
				     sinfo.limit, sinfo.batchcount, sinfo.shared,
				     sinfo.active_slabs, sinfo.num_slabs,
				     sinfo.shared_avail);
		}
		mutex_unlock(debug_slab_mutex);

	} else {
		struct kmem_cache *cachep = NULL;

		osvelte_info("# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab> :"
			     " tunables <limit> <batchcount> <sharedfactor> : slabdata <active_slabs> <num_slabs> <sharedavail>\n");

		mutex_lock(debug_slab_mutex);
		list_for_each_entry(cachep, debug_slab_caches, list) {
			struct slabinfo sinfo;

			memset(&sinfo, 0, sizeof(sinfo));
			get_slabinfo(cachep, &sinfo);

			osvelte_info("%-17s %6lu %6lu %6u %4u %4d : tunables %4u %4u %4u : slabdata %6lu %6lu %6lu\n",
				     cachep->name, sinfo.active_objs,
				     sinfo.num_objs, cachep->size,
				     sinfo.objects_per_slab,
				     (1 << sinfo.cache_order),
				     sinfo.limit, sinfo.batchcount, sinfo.shared,
				     sinfo.active_slabs, sinfo.num_slabs,
				     sinfo.shared_avail);
		}
		mutex_unlock(debug_slab_mutex);
	}
	return 0;
}
#else /* CONFIG_SLUB_DEBUG */
static inline int dump_slab_info(bool verbose)
{
	return 0;
}
#endif /* CONFIG_SLUB_DEBUG */

#define for_each_populated_zone_dup(zone)			\
	for (zone = (first_online_pgdat_dup())->node_zones;	\
	     zone;						\
	     zone = next_zone_dup(zone))			\
		if (!populated_zone(zone))			\
			; /* do nothing */			\
		else

static struct pglist_data *first_online_pgdat_dup(void)
{
	return NODE_DATA(first_online_node);
}

static struct pglist_data *next_online_pgdat_dup(struct pglist_data *pgdat)
{
	int nid = next_online_node(pgdat->node_id);

	if (nid == MAX_NUMNODES)
		return NULL;
	return NODE_DATA(nid);
}

struct zone *next_zone_dup(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else {
		pgdat = next_online_pgdat_dup(pgdat);
		if (pgdat)
			zone = pgdat->node_zones;
		else
			zone = NULL;
	}
	return zone;
}

static void dump_each_zone(void)
{
	unsigned long free_pcp = 0;
	int cpu;
	struct zone *zone;

	for_each_populated_zone_dup(zone) {
		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->per_cpu_pageset, cpu)->count;
	}

	osvelte_info("active_anon:%lu inactive_anon:%lu isolated_anon:%lu"
		     " active_file:%lu inactive_file:%lu isolated_file:%lu"
		     " unevictable:%lu dirty:%lu writeback:%lu"
		     " slab_reclaimable:%lu slab_unreclaimable:%lu"
		     " mapped:%lu shmem:%lu pagetables:%lu bounce:%lu"
		     " free:%lu free_pcp:%lu free_cma:%lu\n",
		     global_node_page_state(NR_ACTIVE_ANON),
		     global_node_page_state(NR_INACTIVE_ANON),
		     global_node_page_state(NR_ISOLATED_ANON),
		     global_node_page_state(NR_ACTIVE_FILE),
		     global_node_page_state(NR_INACTIVE_FILE),
		     global_node_page_state(NR_ISOLATED_FILE),
		     global_node_page_state(NR_UNEVICTABLE),
		     global_node_page_state(NR_FILE_DIRTY),
		     global_node_page_state(NR_WRITEBACK),
		     global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B),
		     global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B),
		     global_node_page_state(NR_FILE_MAPPED),
		     global_node_page_state(NR_SHMEM),
		     global_node_page_state(NR_PAGETABLE),
		     global_zone_page_state(NR_BOUNCE),
		     global_zone_page_state(NR_FREE_PAGES),
		     free_pcp,
		     global_zone_page_state(NR_FREE_CMA_PAGES));

	for_each_populated_zone_dup(zone) {
		free_pcp = 0;
		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->per_cpu_pageset, cpu)->count;

		if (IS_ENABLED(CONFIG_NUMA))
			osvelte_info("Node %d \n", zone_to_nid(zone));
		osvelte_info("%s"
			     " free:%lukB"
			     " min:%lukB"
			     " low:%lukB"
			     " high:%lukB"
			     " reserved_highatomic:%luKB"
			     " active_anon:%lukB"
			     " inactive_anon:%lukB"
			     " active_file:%lukB"
			     " inactive_file:%lukB"
			     " unevictable:%lukB"
			     " writepending:%lukB"
			     " present:%lukB"
			     " managed:%lukB"
			     " mlocked:%lukB"
			     " bounce:%lukB"
			     " free_pcp:%lukB"
			     " local_pcp:%ukB"
			     " free_cma:%lukB"
			     "\n",
			     zone->name, K(zone_page_state(zone, NR_FREE_PAGES)),
			     K(min_wmark_pages(zone)),
			     K(low_wmark_pages(zone)),
			     K(high_wmark_pages(zone)),
			     K(zone->nr_reserved_highatomic),
			     K(zone_page_state(zone, NR_ZONE_ACTIVE_ANON)),
			     K(zone_page_state(zone, NR_ZONE_INACTIVE_ANON)),
			     K(zone_page_state(zone, NR_ZONE_ACTIVE_FILE)),
			     K(zone_page_state(zone, NR_ZONE_INACTIVE_FILE)),
			     K(zone_page_state(zone, NR_ZONE_UNEVICTABLE)),
			     K(zone_page_state(zone, NR_ZONE_WRITE_PENDING)),
			     K(zone->present_pages),
			     K(zone_managed_pages(zone)),
			     K(zone_page_state(zone, NR_MLOCK)),
			     K(zone_page_state(zone, NR_BOUNCE)),
			     K(free_pcp),
			     K(this_cpu_read(zone->per_cpu_pageset->count)),
			     K(zone_page_state(zone, NR_FREE_CMA_PAGES)));
	}
}

static void __lowmem_dbg_dump(struct lowmem_dbg_cfg *cfg)
{
	unsigned long tot, free, available, cma_free;
	unsigned long slab_reclaimable, slab_unreclaimable;
	unsigned long anon, active_anon, inactive_anon;
	unsigned long file, active_file, inactive_file, shmem;
	unsigned long vmalloc, pgtbl, kernel_stack, kernel_misc_reclaimable;
	unsigned long dmabuf, dmabuf_pool, gpu, unaccounted;
	unsigned long zram_origin, zram_comp, zram_memused, zram_samepages;
	struct sysinfo si;
	struct files_acct files_acct;

	if (unlikely(!cfg))
		return;

	tot = sys_totalram();
	free = sys_freeram();
	cma_free = sys_free_cma();
	available = si_mem_available();

	slab_reclaimable = sys_slab_reclaimable();
	slab_unreclaimable = sys_slab_unreclaimable();

	si_swapinfo(&si);

	anon = sys_anon();
	active_anon = sys_active_anon();
	inactive_anon = sys_inactive_anon();
	shmem = sys_sharedram();

	file = sys_file();
	active_file = sys_active_file();
	inactive_file = sys_inactive_file();

	vmalloc = sys_vmalloc();
	pgtbl = sys_page_tables();
	kernel_stack = sys_kernel_stack();
	kernel_misc_reclaimable = sys_kernel_misc_reclaimable();

	dmabuf = read_mtrack_mem_usage(MTRACK_DMABUF, MTRACK_DMABUF_SYSTEM_HEAP);
	dmabuf_pool = read_mtrack_mem_usage(MTRACK_DMABUF, MTRACK_DMABUF_POOL);
	gpu = read_mtrack_mem_usage(MTRACK_GPU, MTRACK_GPU_TOTAL);

	zram_origin = read_mtrack_mem_usage(MTRACK_ZRAM, MTRACK_ZRAM_ORIG);
	zram_comp = read_mtrack_mem_usage(MTRACK_ZRAM, MTRACK_ZRAM_COMPR_BYTES);
	zram_memused = read_mtrack_mem_usage(MTRACK_ZRAM, MTRACK_ZRAM_MEMUSED);
	zram_samepages = read_mtrack_mem_usage(MTRACK_ZRAM, MTRACK_ZRAM_SAMEPAGES);

	unaccounted = tot - free - slab_reclaimable - slab_unreclaimable -
		vmalloc - anon - file - pgtbl - kernel_stack - dmabuf -
		gpu - kernel_misc_reclaimable - zram_memused;

	osvelte_info("lowmem_dbg start osvelte v%d.%d.%d based on %s<<<<<<<\n",
		     OSVELTE_MAJOR, OSVELTE_MINOR, OSVELTE_PATCH_NUM, UTS_RELEASE);
	dump_each_zone();

	osvelte_info("total: %lu free: %lu available: %lu\n",
		     K(tot), K(free), K(available));
	osvelte_info("cma_free: %lu (free - cma_free + file): %lu\n",
		     K(cma_free), K(free - cma_free + active_file + inactive_file));
	osvelte_info("swap_total: %lu swap_free: %lu\n",
		     K(si.totalswap), K(si.freeswap));
	osvelte_info("slab_reclaimable: %lu slab_unreclaimable: %lu\n",
		     K(slab_reclaimable), K(slab_unreclaimable));
	osvelte_info("anon: %lu active_anon: %lu inactive_anon: %lu\n",
		     K(anon), K(active_anon), K(inactive_anon));
	osvelte_info("file: %lu active_file: %lu inactive_file: %lu shmem: %lu\n",
		     K(file), K(active_file), K(inactive_file), K(shmem));
	osvelte_info("vmalloc: %lu page_tables: %lu kernel_stack: %lu kernel_misc_reclaimable: %lu\n",
		     K(vmalloc), K(pgtbl), K(kernel_stack), K(kernel_misc_reclaimable));
	osvelte_info("zram_orig: %lu zram_comp: %lu zram_memused: %lu zram_samepages: %lu\n",
		     K(zram_origin), zram_comp >> 10, K(zram_memused), K(zram_samepages));
	osvelte_info("dmabuf: %lu dmabuf_pool: %lu gpu: %lu unaccounted: %lu\n",
		     K(dmabuf), K(dmabuf_pool), K(gpu), K(unaccounted));

	dump_procs(true);
	dump_slab_info(slab_unreclaimable > cfg->watermark_slab);

	/* dump dmaabuf */
	files_acct.iter_fd = iter_dmabuf;
	files_acct.watermark = tot;
	dump_procs_fd_info(&files_acct, "dump_procs_dmabuf_info", dmabuf > cfg->watermark_dmabuf);

	/* dump ashmem */
	files_acct.iter_fd = iter_ashmem;
	files_acct.watermark = cfg->watermark_other;
	dump_procs_fd_info(&files_acct, "dump_procs_ashmem_info", false);

	dump_mtrack_usage_stat(MTRACK_GPU, false);
	osvelte_info("lowmem_dbg end >>>>>>>\n");
}

static void lowmem_dbg_dump(struct work_struct *work)
{
	__lowmem_dbg_dump(&g_cfg);
}

enum dump_mode {
	DM_LMKD,
	DM_LOWMEM,
};

static void try_to_lowmem_dbg_dump(enum dump_mode mode)
{
	long free;
	unsigned long file;
	struct lowmem_dbg_cfg *cfg = &g_cfg;
	static atomic_t atomic_lmk = ATOMIC_INIT(0);
	u64 now = get_jiffies_64();

	if (atomic_inc_return(&atomic_lmk) > 1)
		goto done;

	if (mode == DM_LOWMEM) {
		if (time_before64(now, (cfg->last_jiffies + cfg->interval)))
			goto done;

		free = sys_freeram() - sys_free_cma();
		/* do this really occur ? */
		if (free < 0)
			free = 0;

		file = sys_inactive_file() + sys_active_file();
		if (free + file > cfg->watermark_low)
			goto done;
	} else if (mode == DM_LMKD) {
		if (time_before64(now, (cfg->last_jiffies + cfg->lmkd_interval)))
			goto done;
	}

	cfg->last_jiffies = now;
	schedule_work(&lowmem_dbg_work);
done:
	atomic_dec(&atomic_lmk);
}

void direct_reclaim_vh(void *data, int order, gfp_t gfp_flags)
{
	try_to_lowmem_dbg_dump(DM_LOWMEM);
}

/******************************************************************************
 *                          lmkd utility
 ******************************************************************************/
static void android_vh_do_send_sig_info(void *data, int sig, struct task_struct *killer,
					struct task_struct *dst)
{
	short oom_score_adj = -1001;

	/* lmkd is an RT task, lmkd_reaper is normal taks. */
	if (!rt_task(killer->group_leader) ||
	    strncmp(current->group_leader->comm, "lmkd", 4) != 0)
		return;

	oom_score_adj = dst->signal->oom_score_adj;
	mm_logi_tag("osvelte_lmkd", "kill '%s' (%d), uid %d, oom_score_adj %d\n",
		    dst->comm, dst->tgid, from_kuid(&init_user_ns, task_uid(dst)),
		    oom_score_adj);

	if (oom_score_adj <= 200)
		try_to_lowmem_dbg_dump(DM_LMKD);
}

/******************************************************************************
 *                          sysfs interface
 ******************************************************************************/

static ssize_t dump_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct lowmem_dbg_cfg cfg = {
		.watermark_slab = 0,
		.watermark_dmabuf = 0,
		.watermark_gpu = 0,
		.watermark_other = PAGES(SZ_1G),
	};
	ssize_t size = 0;

	/* debug only. this might concurrent with lowmem_dbg_dump() */
	__lowmem_dbg_dump(&cfg);
	size = sysfs_emit_at(buf, 0, "dump lowmem dbg info to kernel log\n");
	return size;
}

static ssize_t config_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	u64 val;
	char config[32];
	struct lowmem_dbg_cfg *cfg = &g_cfg;

	if (sscanf(buf, "%s %llu", config, &val) != 2)
		return -EINVAL;

	if (strncmp(config, "lmkd_interval", 13) == 0)
		cfg->lmkd_interval = val * HZ;
	else if (strncmp(config, "interval", 13) == 0)
		cfg->lmkd_interval = val * HZ;
	else if (strncmp(config, "wm_low", 6) == 0)
		cfg->watermark_low = val * PAGES(SZ_1M);
	else if (strncmp(config, "wm_dmabuf", 9) == 0)
		cfg->watermark_dmabuf = val * PAGES(SZ_1M);
	else if (strncmp(config, "wm_slab", 7) == 0)
		cfg->watermark_slab = val * PAGES(SZ_1M);
	else
		return -EINVAL;

	osvelte_logi("set %s to %llu\n", config, val);
	return count;
}

static ssize_t stats_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	int size = 0;
	struct lowmem_dbg_cfg *cfg = &g_cfg;

	size += sysfs_emit_at(buf, size, "interval: %u\n",
			      cfg->interval);
	size += sysfs_emit_at(buf, size, "lmkd_interval: %u\n",
			      cfg->lmkd_interval);
	size += sysfs_emit_at(buf, size, "wm_low: %llu\n",
			      cfg->watermark_low);
	size += sysfs_emit_at(buf, size, "wm_slab: %llu\n",
			      cfg->watermark_slab);
	size += sysfs_emit_at(buf, size, "wm_dmabuf: %llu\n",
			      cfg->watermark_dmabuf);
	size += sysfs_emit_at(buf, size, "wm_gpu: %llu\n",
			      cfg->watermark_gpu);
	size += sysfs_emit_at(buf, size, "wm_other: %llu\n",
			      cfg->watermark_other);
	return size;
}

static struct kobj_attribute dump_attr = __ATTR_RO(dump);
static struct kobj_attribute stats_attr = __ATTR_RO(stats);
static struct kobj_attribute config_attr = __ATTR_WO(config);

static struct attribute *attrs[] = {
	&dump_attr.attr,
	&stats_attr.attr,
	&config_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

int osvelte_lowmem_dbg_init(struct kobject *root)
{
	struct lowmem_dbg_cfg *cfg = &g_cfg;
	unsigned long total_ram = sys_totalram();
	int err;

	/* lookup symbols for slab */
	debug_slab_mutex = osvelte_kallsyms_lookup_name("slab_mutex");
	debug_slab_caches = osvelte_kallsyms_lookup_name("slab_caches");

	if (register_trace_mm_vmscan_direct_reclaim_begin(direct_reclaim_vh, NULL)) {
		pr_err("register lowmem-dbg vendor hook failed\n");
		return -EINVAL;
	}
	if (register_trace_android_vh_do_send_sig_info(android_vh_do_send_sig_info, NULL)) {
		pr_err("register android_vh_do_send_sig_info failed\n");
		return -EINVAL;
	}

	cfg->interval = 10 * HZ;
	cfg->lmkd_interval = 2 * HZ;
	if (total_ram >= PAGES(SZ_4G + SZ_8G))
		cfg->watermark_low = PAGES(SZ_1G + SZ_512M);
	else if (total_ram >= PAGES(SZ_4G + SZ_4G))
		cfg->watermark_low = PAGES(SZ_1G + SZ_256M);
	else if (total_ram >= PAGES(SZ_2G + SZ_2G))
		cfg->watermark_low = PAGES(SZ_1G);
	else if (total_ram >= PAGES(SZ_2G + SZ_1G))
		cfg->watermark_low = PAGES(SZ_512M);
	else
		cfg->watermark_low = PAGES(SZ_256M);
	cfg->watermark_slab = PAGES(SZ_1G);

	if (total_ram >= PAGES(SZ_8G))
		cfg->watermark_dmabuf = PAGES(SZ_2G + SZ_512M);
	else
		cfg->watermark_dmabuf = PAGES(SZ_1G + SZ_512M);

	cfg->watermark_gpu = PAGES(SZ_2G + SZ_512M);
	cfg->watermark_other = PAGES(SZ_1G);

	cfg->kobj = kobject_create_and_add("lowmem_dbg", root);
	if (!cfg->kobj) {
		osvelte_loge("failed to create sysfs common_kobj\n");
		/* ignore error here */
		return 0;
	}
	err = sysfs_create_group(cfg->kobj, &attr_group);
	if (err) {
		osvelte_loge("failed to create sysfs common group\n");
		kobject_put(cfg->kobj);
		return -ENOMEM;
	}
	osvelte_logi("+\n");
	return 0;
}

int osvelte_lowmem_dbg_exit(void)
{
	unregister_trace_mm_vmscan_direct_reclaim_begin(direct_reclaim_vh, NULL);
	return 0;
}
