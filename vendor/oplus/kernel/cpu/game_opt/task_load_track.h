#ifndef __TASK_LOAD_TRACK_H__
#define __TASK_LOAD_TRACK_H__

#include <linux/types.h>

#define TLT_INFO_PAGE_SIZE (1 << 5)

enum tlt_flag
{
	TASK_LOAD_TRACK_ENABLE,
};

enum tlt_cmd_id
{
	TLT_FIRST_ID, /* reserved word */
	TLT_STATE_CHANGE,
	TLT_ADD_TASK,
	TLT_REMOVE_TASK,
	TLT_READ_TASK_LOAD,
	TLT_MAX_ID,
};

struct tlt_info
{
	int size;
	uint64_t data[TLT_INFO_PAGE_SIZE * 3];
};

#define TLT_MAGIC 0xE1
#define CMD_ID_TLT_STATE_CHANGE \
	_IOWR(TLT_MAGIC, TLT_STATE_CHANGE, struct tlt_info)
#define CMD_ID_TLT_ADD_TASK \
	_IOWR(TLT_MAGIC, TLT_ADD_TASK, struct tlt_info)
#define CMD_ID_TLT_REMOVE_TASK \
	_IOWR(TLT_MAGIC, TLT_REMOVE_TASK, struct tlt_info)
#define CMD_ID_TLT_READ_TASK_LOAD \
	_IOWR(TLT_MAGIC, TLT_READ_TASK_LOAD, struct tlt_info)

int task_load_track_init(void);
void task_load_track_exit(void);

#endif // __TASK_LOAD_TRACK_H__
