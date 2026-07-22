#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 6, 0)
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/ioprio.h>
#include <linux/blk-mq.h>
#include <trace/hooks/wqlockup.h>
#include <trace/hooks/blk.h>
#include "oplus_wq_dynamic_priority.h"
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>

#define WQ_UX    (1 << 14)
#define VIRTUAL_KWORKER_NORMAL_NICE (-1000)
#define VIRTUAL_KWORKER_KBLOCKD_NICE (-1001)
#define WQ_CMP(str)  (strncmp(wq->name, str, sizeof(str) - 1) == 0)

static struct workqueue_attrs *ux_wq_attrs;
static struct workqueue_attrs *ux_wq_attrs_kblockd;
static struct workqueue_struct *oplus_kblockd_workqueue;
static struct workqueue_struct *oplus_fsverity_read_workqueue;

/* ---------fsverity_enqueue_verify_work--------- */

static int handler_fsverity_work(struct kprobe *p, struct pt_regs *regs)
{
	regs->regs[1] = (u64)oplus_fsverity_read_workqueue;
	return 0;
}

static struct kprobe oplus_fsverity_enqueue_verify_work_kp = {
	.symbol_name = "fsverity_enqueue_verify_work",
	.offset = 0x1c,
	.pre_handler = handler_fsverity_work,

};
static inline int __apply_workqueue_attrs(struct workqueue_struct *wq,
				const struct workqueue_attrs *attrs)
{
	int ret;

	cpus_read_lock();
	ret = apply_workqueue_attrs(wq, attrs);
	cpus_read_unlock();
	return ret;
}

static void android_rvh_alloc_and_link_pwqs_handler(void *unused,
	struct workqueue_struct *wq, int *ret, bool *skip)
{

	if (WQ_CMP("loop") || WQ_CMP("kverityd") || WQ_CMP("oplusfsverity")) {
		*ret = __apply_workqueue_attrs(wq, ux_wq_attrs);
		*skip = true;
	} else if (WQ_CMP("opluskblockd")) {
		*ret = __apply_workqueue_attrs(wq, ux_wq_attrs_kblockd);
		*skip = true;
	}
}

static void android_rvh_alloc_workqueue_handler(void *unused,
	struct workqueue_struct *wq, unsigned int *flags, int *max_active)
{
	if (WQ_CMP("loop") || WQ_CMP("kverityd") || WQ_CMP("opluskblockd") || WQ_CMP("oplusfsverity")) {
		if (!wq->unbound_attrs){
			wq->unbound_attrs = alloc_workqueue_attrs();
			if (!wq->unbound_attrs) {
				pr_err("%s alloc_workqueue_attrs failed: %s", __func__, wq->name);
				return;
			}
		}
		*flags |= (WQ_UNBOUND | WQ_HIGHPRI);
		if (*max_active == 1)
			*flags |= __WQ_ORDERED;
	}
}

static void android_rvh_create_worker_handler(void *unused,
	struct task_struct *task, struct workqueue_attrs *attrs)
{
	if (attrs->nice == VIRTUAL_KWORKER_NORMAL_NICE ||
		attrs->nice == VIRTUAL_KWORKER_KBLOCKD_NICE) {
		oplus_set_ux_state_lock(task, SA_TYPE_LIGHT, -1, true);
		if (task->comm[8] == 'u')
			task->comm[8] = 'X';
	}
}

static void android_vh_f2fs_restore_priority_handler(void *unused, struct task_struct *is_issue_ckpt, int saved_prio)
{
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	if (oplus_get_ux_state(is_issue_ckpt)) {
		oplus_set_ux_state_lock(is_issue_ckpt, 0, -1, true);
		is_issue_ckpt->static_prio = saved_prio;
	}
	return;
#endif
}

static void android_vh_f2fs_improve_priority_handler(void *unused, struct task_struct *is_issue_ckpt, int *saved_prio,
	                                                bool *prio_changed)
{
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	if (oplus_get_ux_state(current)) {
		*saved_prio = is_issue_ckpt->static_prio;
		oplus_set_ux_state_lock(is_issue_ckpt, SA_TYPE_LIGHT, -1, true);
		*prio_changed = true;
	} else {
		*prio_changed = false;
	}
	return;
#endif
}

static void android_vh_blk_mq_kick_requeue_list_handler(void *unused,
	struct request_queue *q, unsigned long delay, bool *skip)
{
	mod_delayed_work_on(WORK_CPU_UNBOUND, oplus_kblockd_workqueue,
		&q->requeue_work, 0);
	*skip = 1;
	return;
}

static void android_vh_blk_mq_delay_run_hw_queue_handler(void *unused,
	int cpu, struct blk_mq_hw_ctx *hctx, unsigned long delay, bool *skip)
{
	mod_delayed_work_on(cpu, oplus_kblockd_workqueue, &hctx->run_work, delay);
	*skip = 1;
	return;
}

struct tracepoints_table {
	const char *name;
	void *func;
	struct tracepoint *tp;
	bool init;
};

static struct tracepoints_table interests[] = {
	{
		.name = "android_rvh_alloc_and_link_pwqs",
		.func = android_rvh_alloc_and_link_pwqs_handler
	},
	{
		.name = "android_rvh_alloc_workqueue",
		.func = android_rvh_alloc_workqueue_handler
	},
	{
		.name = "android_rvh_create_worker",
		.func = android_rvh_create_worker_handler
	},
	{
		.name = "android_vh_blk_mq_delay_run_hw_queue",
		.func = android_vh_blk_mq_delay_run_hw_queue_handler
	},
	{
		.name = "android_vh_blk_mq_kick_requeue_list",
		.func = android_vh_blk_mq_kick_requeue_list_handler
	},
	{
		.name = "android_vh_f2fs_improve_priority",
		.func = android_vh_f2fs_improve_priority_handler
	},
	{
		.name = "android_vh_f2fs_restore_priority",
		.func = android_vh_f2fs_restore_priority_handler
	},
};

#define FOR_EACH_INTEREST(i) \
	for (i = 0; i < sizeof(interests) / sizeof(struct tracepoints_table); \
	i++)

static void lookup_tracepoints(struct tracepoint *tp,
				       void *ignore)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (strcmp(interests[i].name, tp->name) == 0)
			interests[i].tp = tp;
	}
}

static int wq_install_tracepoints(int start, int end)
{
	int i;
	int cnt = sizeof(interests) / sizeof(struct tracepoints_table);

	if (end > cnt) {
		pr_warn("%s: err: tracepoint end > tp cnt\n",
				THIS_MODULE->name);
		end = cnt;
	}

	for (i = start; i <= end; i++) {
		if (interests[i].tp == NULL) {
			pr_err("%s: tracepoint %s not found\n",
				THIS_MODULE->name, interests[i].name);
			return -1;
		}

		if (!interests[i].init) {
			tracepoint_probe_register(interests[i].tp,
						interests[i].func,
						NULL);
			interests[i].init = true;
		}
	}

	return 0;
}

static void wq_uninstall_tracepoints(void)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (interests[i].init) {
			tracepoint_probe_unregister(interests[i].tp,
						    interests[i].func,
						    NULL);
		}
	}
}

static int __init oplus_wq_hook_init(void)
{
	int err = 0;

	/* Install the tracepoints */
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	ux_wq_attrs = alloc_workqueue_attrs();
	if (!ux_wq_attrs) {
		pr_err("%s alloc ux_wq_attrs fail!",__func__);
		err = -ENOMEM;
		goto out;
	} else
		ux_wq_attrs->nice = VIRTUAL_KWORKER_NORMAL_NICE;

	ux_wq_attrs_kblockd = alloc_workqueue_attrs();
	if (!ux_wq_attrs_kblockd) {
		pr_err("%s alloc ux_wq_attrs_kblockd fail!",__func__);
		err = -ENOMEM;
		goto err_free_attrs;
	} else
		ux_wq_attrs_kblockd->nice = VIRTUAL_KWORKER_KBLOCKD_NICE;

	err = wq_install_tracepoints(0, 2);
	if (err)
		goto err_free_kblockd_attrs;

	oplus_kblockd_workqueue =  alloc_workqueue("opluskblockd",
  					    WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UX | WQ_UNBOUND, 0);

	if (!oplus_kblockd_workqueue) {
		err = -ENOMEM;
		pr_err("%s alloc opluskblockd fail!",__func__);
		goto err_free_kblock_wq;
	}

	oplus_fsverity_read_workqueue =  alloc_workqueue("oplusfsverity",
  					    WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UX | WQ_UNBOUND, 0);

	if (!oplus_fsverity_read_workqueue) {
		err = -ENOMEM;
		pr_err("%s alloc oplusfsverity fail!",__func__);
		goto err_free_fsverify_wq;
	}

	err = wq_install_tracepoints(3, 6);
	if (err) {
		goto err_end_wq_uninstall;
	}

	/* kprob register*/
	err = register_kprobe(&oplus_fsverity_enqueue_verify_work_kp);
	if (err < 0) {
		printk(KERN_ERR "register_kprobe fsverity_enqueue_verify_work failed, returned %d\n", err);
		goto err_free_kp_fsverity_enqueue_verify_work_fail;
	}

	return err;

err_free_kp_fsverity_enqueue_verify_work_fail:
	unregister_kprobe(&oplus_fsverity_enqueue_verify_work_kp);
err_end_wq_uninstall:
	wq_uninstall_tracepoints();
err_free_fsverify_wq:
	destroy_workqueue(oplus_fsverity_read_workqueue);
err_free_kblock_wq:
	destroy_workqueue(oplus_kblockd_workqueue);
err_free_kblockd_attrs:
	free_workqueue_attrs(ux_wq_attrs_kblockd);
err_free_attrs:
	free_workqueue_attrs(ux_wq_attrs);
out:
    return err;
}

static void __exit oplus_wq_hook_exit(void)
{
	unregister_kprobe(&oplus_fsverity_enqueue_verify_work_kp);
	destroy_workqueue(oplus_fsverity_read_workqueue);
	destroy_workqueue(oplus_kblockd_workqueue);
	wq_uninstall_tracepoints();
	free_workqueue_attrs(ux_wq_attrs);
	free_workqueue_attrs(ux_wq_attrs_kblockd);
}

module_init(oplus_wq_hook_init);
module_exit(oplus_wq_hook_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lijiang");
MODULE_AUTHOR("Gray Jia");
MODULE_DESCRIPTION("A kernel module using vendorhook to improve IO performance");
#endif
