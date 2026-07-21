// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2025 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "mglru_opt: " fmt

#include <linux/module.h>
#include <trace/hooks/vmscan.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/madvise.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/vmstat.h>
#include <linux/oom.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <uapi/linux/sched/types.h>
#include <linux/cpufreq.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/pgtable.h>
#include <linux/mman.h>
#include <linux/userfaultfd_k.h>
#include <linux/page_size_compat.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/pagewalk.h>
#include <linux/hugetlb.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <linux/percpu.h>
#include <linux/local_lock.h>
#include <linux/pagevec.h>
#include <linux/jump_label.h>

#include "mglru_opt.h"
#include "../../mm/internal.h"
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_OSVELTE)
#include "../mm_osvelte/mm-config.h"
#endif /* CONFIG_OPLUS_FEATURE_MM_OSVELTE */

/*
  FIXME:
  Temporarily modified to differentiate between platforms,
  Because some kernels do not have the respin vendor hook yet.
*/
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MGLRU_OPT)

static atomic_t mglru_opt_enable;

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
typedef void (*folio_activate_t)(struct folio *folio);
typedef void (*deactivate_file_folio_t)(struct folio *folio);

static kallsyms_lookup_name_t kallsyms_lookup_name_dup = NULL;
static folio_activate_t folio_activate_dup = NULL;
static deactivate_file_folio_t deactivate_file_folio_dup = NULL;
static struct cpu_fbatches *p_cpu_fbatches = NULL;
static struct static_key *p_lru_gen_caps;

/* Protecting only lru_rotate.fbatch which requires disabling interrupts */
struct lru_rotate {
	local_lock_t lock;
	struct folio_batch fbatch;
};
/*
 * The following folio batches are grouped together because they are protected
 * by disabling preemption (and interrupts remain enabled).
 */
struct cpu_fbatches {
	local_lock_t lock;
	struct folio_batch lru_add;
	struct folio_batch lru_deactivate_file;
	struct folio_batch lru_deactivate;
	struct folio_batch lru_lazyfree;
#ifdef CONFIG_SMP
	struct folio_batch activate;
#endif
};

int mglru_opt_enabled(void)
{
	return likely(atomic_read(&mglru_opt_enable));
}

static inline bool lru_gen_enabled_dup(void)
{
	return static_key_enabled(p_lru_gen_caps);
}

static void __lru_cache_activate_folio(struct folio *folio)
{
	struct folio_batch *fbatch;
	int i;
	local_lock(&p_cpu_fbatches->lock);
	fbatch = this_cpu_ptr(&p_cpu_fbatches->lru_add);

	/*
	 * Search backwards on the optimistic assumption that the folio being
	 * activated has just been added to this batch. Note that only
	 * the local batch is examined as a !LRU folio could be in the
	 * process of being released, reclaimed, migrated or on a remote
	 * batch that is currently being drained. Furthermore, marking
	 * a remote batch's folio active potentially hits a race where
	 * a folio is marked active just after it is added to the inactive
	 * list causing accounting errors and BUG_ON checks to trigger.
	 */
	for (i = folio_batch_count(fbatch) - 1; i >= 0; i--) {
		struct folio *batch_folio = fbatch->folios[i];

		if (batch_folio == folio) {
			folio_set_active(folio);
			break;
		}
	}

	local_unlock(&p_cpu_fbatches->lock);
}

static void __nocfi folio_add_lru_folio_activate(void *data,
	struct folio *folio, bool *bypass)
{
	if (lru_gen_enabled_dup())
		*bypass = true;
}

static void __nocfi filemap_fault_folio_activate(void *data,
	struct folio *folio)
{
	if (lru_gen_enabled_dup() &&
	    lru_gen_in_fault() &&
	    !(current->flags & PF_MEMALLOC) &&
	    !folio_test_active(folio) &&
	    !folio_test_unevictable(folio)) {
		if (folio_test_lru(folio))
			folio_activate_dup(folio);
		else /* still in lru cache */
			__lru_cache_activate_folio(folio);
	}
}

static void __nocfi hook_folio_remove_rmap_ptes(void *data,
	struct folio *folio)
{
	/* move unmapped file folios to the tail of min gen */
	if (lru_gen_enabled_dup() && !folio_test_anon(folio) && !folio_mapped(folio))
		deactivate_file_folio_dup(folio);
}

static int register_mglru_opt_vendor_hooks(void)
{
	int ret = 0;

	ret = register_trace_android_vh_folio_add_lru_folio_activate(folio_add_lru_folio_activate, NULL);
	if (ret != 0) {
		pr_err("register_ttrace_android_vh_folio_add_lru_folio_activate failed! ret=%d\n",
				ret);
		goto out;
	}

	ret = register_trace_android_vh_filemap_fault_pre_folio_locked(filemap_fault_folio_activate, NULL);
	if (ret != 0) {
		pr_err("trace_android_vh_filemap_fault_pre_folio_locked failed! ret=%d\n",
				ret);
		goto out;
	}

	ret = register_trace_android_vh_filemap_folio_mapped(filemap_fault_folio_activate, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_filemap_folio_mapped failed! ret=%d\n",
				ret);
		goto out;
	}

	ret = register_trace_android_vh_folio_remove_rmap_ptes(hook_folio_remove_rmap_ptes, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_folio_remove_rmap_ptes failed! ret=%d\n",
				ret);
		goto out;
	}

out:
	return ret;
}

static void unregister_mglru_opt_vendor_hooks(void)
{
	unregister_trace_android_vh_folio_remove_rmap_ptes(hook_folio_remove_rmap_ptes, NULL);
	unregister_trace_android_vh_filemap_folio_mapped(filemap_fault_folio_activate, NULL);
	unregister_trace_android_vh_filemap_fault_pre_folio_locked(filemap_fault_folio_activate, NULL);
	unregister_trace_android_vh_folio_add_lru_folio_activate(folio_add_lru_folio_activate, NULL);
}

static int mglru_proc_stat_show(struct seq_file *s, void *v)
{
	int i;
	unsigned long events[NR_DEBUG_EVENT_ITEMS];

	all_debug_events(events);

	seq_printf(s, "mglru_opt_enabled %d\n", mglru_opt_enabled() && lru_gen_enabled_dup());
	seq_printf(s, "lru_gen_enabled %d\n", lru_gen_enabled_dup());

	for (i = 0; i < NR_DEBUG_EVENT_ITEMS; i++)
		seq_printf(s, "%s %lu\n", debug_event_text[i], events[i]);

	return 0;
}

static void *get_symbol_address(const char *symbol_name) {
	struct kprobe kp = {
		.symbol_name = symbol_name
	};
	int ret;

	ret = register_kprobe(&kp);
	if (ret) {
		pr_err("get %s addr from kprobe failed! ret=%d\n", symbol_name, ret);
		return NULL;
	}

	void *addr = (void *)kp.addr;

	unregister_kprobe(&kp);
	return addr;
}

#define GET_SYMBOL(symbol) \
	do { \
	symbol##_dup = (symbol##_t)get_symbol_address(#symbol); \
	if (!symbol##_dup) { \
		pr_err("Failed to get %s address!\n", #symbol); \
		return -1; \
	} \
	pr_debug("Successfully get %s addr: 0x%px\n", #symbol, symbol##_dup); \
	} while (0)

static int get_all_symbol(void)
{
	p_lru_gen_caps = (struct static_key *)kallsyms_lookup_name_dup("lru_gen_caps");
	if (!p_lru_gen_caps) {
		pr_info("fail get lru_gen_caps\n");
		return -1;
	}
	pr_debug("suceesfully get lru_gen_caps addr:0x%px,lru_gen_enabled:%d\n", p_lru_gen_caps, lru_gen_enabled_dup());

	p_cpu_fbatches = (struct cpu_fbatches *)kallsyms_lookup_name_dup("cpu_fbatches");
	if (!p_cpu_fbatches) {
		pr_info("fail get p_cpu_fbatches\n");
		return -1;
	}
	pr_debug("suceesfully get cpu_fbatches addr:0x%px\n", p_cpu_fbatches);

	GET_SYMBOL(folio_activate);
	GET_SYMBOL(deactivate_file_folio);

	return 0;
}

static int __init mglru_opt_init(void)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_OSVELTE)
	struct config_oplus_bsp_mglru_opt *config;

	/* check cmdline if rus disble */
	if (oplus_test_mm_feature_disable(COMFD1_MGLRU_OPT)) {
		pr_info("mglru_opt diabled by cmdline\n");
		return 0;
	}

	config = oplus_read_mm_config(module_name_mglru_opt);
	if (!config || !config->enable) {
		pr_info("%s is disabled in config\n", module_name_mglru_opt);
		return 0;
	}
#endif /* CONFIG_OPLUS_FEATURE_MM_OSVELTE */

	GET_SYMBOL(kallsyms_lookup_name);

	/*Create debug entry*/
	struct proc_dir_entry *root_dir_entry = proc_mkdir("oplus_mem", NULL);
	proc_create_single((root_dir_entry ?
				"mglru_opt_debug" : "oplus_mem/mglru_opt_debug"),
				0400, root_dir_entry, mglru_proc_stat_show);

	ret = get_all_symbol();
	if(ret != 0)
		return ret;

	ret = register_mglru_opt_vendor_hooks();
	if (ret != 0)
		return ret;

	atomic_set(&mglru_opt_enable, true);
	pr_info("mglru_opt_init succeed!\n");
	return 0;
}

static void __exit mglru_opt_exit(void)
{
	unregister_mglru_opt_vendor_hooks();
	atomic_set(&mglru_opt_enable, false);
	pr_info("mglru_opt_exit succeed!\n");
}
#else
static int __init mglru_opt_init(void)
{
	pr_info("mglru_opt_init not support!\n");
	return 0;
}
static void __exit mglru_opt_exit(void)
{
}
#endif

module_init(mglru_opt_init);
module_exit(mglru_opt_exit);

MODULE_LICENSE("GPL v2");
