#ifndef __IO_PRIORITY_HEADER__
#define __IO_PRIORITY_HEADER__
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/jiffies.h>
#include <linux/sched/signal.h>
#include <trace/hooks/blk.h>
#include <trace/hooks/sd.h>
#include <uapi/linux/ioprio.h>
#include <linux/blk-mq.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/log2.h>
#include <linux/printk.h>
#include <linux/cgroup.h>
#include <soc/oplus/boot/oplus_project.h>
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#include <../kernel/oplus_cpu/sched/sched_assist/sa_fair.h>
#include <../kernel/oplus_cpu/sched/sched_assist/sa_group.h>
#endif

#define IO_PRIO_INFO(fmt, arg...) \
    printk("[IO_PRIO_INFO] [%-16s] %20s:%-4d "fmt, current->comm, __func__, __LINE__, ##arg)

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
extern bool test_task_ux(struct task_struct *task);
#else
static inline bool test_task_ux(struct task_struct *task)
{
	return false;
}
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
static inline int test_task_top_app(struct task_struct *task)
{
	return ta_task(task);
}

static inline int test_task_fg(struct task_struct *task)
{
	return fg_task(task);
}
#else
static inline int test_task_top_app(struct task_struct *task)
{
	return 0;
}

static inline int test_task_fg(struct task_struct *task)
{
	return 0;
}
#endif

int deadline_init(void);
void deadline_exit(void);

#endif /* __IO_PRIORITY_HEADER__ */
