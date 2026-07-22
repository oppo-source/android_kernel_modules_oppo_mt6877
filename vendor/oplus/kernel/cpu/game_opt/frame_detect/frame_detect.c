/** Copyright (C), 2025-2029, OPLUS Mobile Comm Corp., Ltd.
* Description: frame detect for game
* Author: zhoutianyao
* Create: 2025-1-15
* Notes: NA
*/

#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/rwlock.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/delay.h>

#include "game_ctrl.h"
#include "frame_detect/frame_detect.h"
#include "oem_data/gts_common.h"

#define DECLARE_DEBUG_TRACE(name, proto, data)				\
	static void __maybe_unused debug_##name(proto) {		\
		if (unlikely(g_debug_enable)) {	\
			name(data);										\
		}													\
	}
#include "debug_common.h"
#undef DECLARE_DEBUG_TRACE

/************************** definition ************************/


/************************** record info ************************/

void init_fss(void)
{
}

void init_fds(void)
{
}

void init_fos(void)
{
}

/************************** frame start checking ************************/


/************************** setup timer ************************/


/************************** vendor hooks ************************/

void ttwu_frame_detect_hook(struct task_struct *task __maybe_unused)
{
}

static void register_frame_detect_vendor_hooks(void)
{
}

static void unregister_frame_detect_vendor_hooks(void)
{
}

/************************** proc ops ************************/


/************************** public function ************************/

void set_frame_detect_task(enum frame_detect_task_info type, pid_t pid)
{
	debug_trace_pr_val_str("task_info_type", (int)type);
	debug_trace_pr_val_str("task_info_pid", (int)pid);
}

int frame_detect_init(void)
{
	init_fss();
	init_fds();
	init_fos();
	register_frame_detect_vendor_hooks();
	return 0;
}

void frame_detect_exit(void)
{
	unregister_frame_detect_vendor_hooks();
}
