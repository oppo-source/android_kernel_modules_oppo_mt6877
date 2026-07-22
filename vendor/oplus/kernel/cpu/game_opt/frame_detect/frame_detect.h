#ifndef __FRAME_DETECT_H__
#define __FRAME_DETECT_H__

#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>

#define MAX_BUFFER_COUNT	(1 << 3)
#define MAX_BUFFER_MASK		((1 << 3) - 1)

enum frame_detect_flag
{
	FRAME_DETECT_ENABLE,
	INPUT_OPT_ENABLE,
};

#define FDF_FRAME_DETECT_ENABLE		BIT(FRAME_DETECT_ENABLE)
#define FDF_INPUT_OPT_ENABLE		(BIT(FRAME_DETECT_ENABLE) | BIT(INPUT_OPT_ENABLE))

enum frame_detect_cmd_id
{
	FRAME_DETECT_FIRST_ID, /* reserved word */
	FRAME_DETECT_FRAME_START,
	FRAME_DETECT_BUFFER_DEQUEUE_BEGIN,
	FRAME_DETECT_BUFFER_DEQUEUE_END,
	FRAME_DETECT_BUFFER_QUEUE_BEGIN,
	FRAME_DETECT_BUFFER_QUEUE_END,
	FRAME_DETECT_TASK_INFO,
	FRAME_DETECT_FRAME_TIME,
	FRAME_DETECT_BUFFER_TIME_L2Q,
	FRAME_DETECT_MAX_ID,
};

enum frame_detect_task_info
{
	TASK_INFO_SF_APP,
	TASK_INFO_LOGIC_THREAD,
	TASK_INFO_COUNT,
};

struct frame_detect_info
{
	union
	{
		struct
		{
			uint64_t buffer_id;
			union
			{
				uint64_t frame_start;
				uint64_t std_frame_time;
				uint64_t buf_dequeue_begin;
				uint64_t buf_dequeue_end;
				uint64_t buf_queue_begin;
				uint64_t buf_queue_end;
				uint64_t buf_time_l2q;
			};
		};

		struct
		{
			pid_t pid;
			int type;
		};
	};
};

#define FD_MAGIC 0xE1
#define CMD_ID_FRAME_START \
	_IOWR(FD_MAGIC, FRAME_DETECT_FRAME_START, struct frame_detect_info)
#define CMD_ID_BUFFER_DEQUEUE_BEGIN \
	_IOWR(FD_MAGIC, FRAME_DETECT_BUFFER_DEQUEUE_BEGIN, struct frame_detect_info)
#define CMD_ID_BUFFER_DEQUEUE_END \
	_IOWR(FD_MAGIC, FRAME_DETECT_BUFFER_DEQUEUE_END, struct frame_detect_info)
#define CMD_ID_BUFFER_QUEUE_BEGIN \
	_IOWR(FD_MAGIC, FRAME_DETECT_BUFFER_QUEUE_BEGIN, struct frame_detect_info)
#define CMD_ID_BUFFER_QUEUE_END \
	_IOWR(FD_MAGIC, FRAME_DETECT_BUFFER_QUEUE_END, struct frame_detect_info)
#define CMD_ID_TASK_INFO \
	_IOWR(FD_MAGIC, FRAME_DETECT_TASK_INFO, struct frame_detect_info)
#define CMD_ID_FRAME_TIME \
	_IOWR(FD_MAGIC, FRAME_DETECT_FRAME_TIME, struct frame_detect_info)
#define CMD_ID_BUFFER_TIME_L2Q \
	_IOWR(FD_MAGIC, FRAME_DETECT_BUFFER_TIME_L2Q, struct frame_detect_info)

void set_frame_detect_task(enum frame_detect_task_info type, pid_t pid);

int frame_detect_init(void);
void frame_detect_exit(void);

#endif // __FRAME_DETECT_H__
