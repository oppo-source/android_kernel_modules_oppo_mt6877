#include "io-priority.h"

bool oplus_ioprio_stat_enabled = false;
bool oplus_ioprio_feature_enabled = false;

#define IOPRIO_PROC_DIR_NAME "oplus_io_priority"
static struct proc_dir_entry *ioprio_proc_dir;

#define IOPRIO_GRANUL_COUNT (9) /* 4 16 32 64 128 256 512 1024 KB */
static const int ioprio_granuls [IOPRIO_GRANUL_COUNT] = {
	/* 0, 8, 32, 64, 128, 256, 512, 1024, 2048 */
	0, 3, 5, 6, 7, 8, 9, 10, 11
};

enum ioprio_io_type {
	IOPRIO_TYPE_NONE,
	IOPRIO_TYPE_READ,
	IOPRIO_TYPE_WRITE,
	MAX_IOPRIO_TYPE,
};

struct ioprio_dist_stat {
	unsigned int total_cnt[MAX_IOPRIO_TYPE];
	unsigned int granul_dist_cnt[MAX_IOPRIO_TYPE][IOPRIO_GRANUL_COUNT + 1]; /* last is total. */
	unsigned long long granul_total_sectors[MAX_IOPRIO_TYPE][IOPRIO_GRANUL_COUNT + 1];
};

struct ioprio_sd_info {
	spinlock_t lock;
	struct ioprio_dist_stat lowpri_stat;
	struct ioprio_dist_stat rt_stat; /* bio->bi_ioprio already is IOPRIO_CLASS_RT */
	struct ioprio_dist_stat hipri_stat; /* marked by vendor hook */
	struct ioprio_dist_stat ux_stat; /* marked by vendor hook */
};

#define OPLUS_DEFAULT_UX_READ_THRESHOLD (1 << (20 - SECTOR_SHIFT)) /* 1MB */
#define OPLUS_DEFAULT_UX_WRITE_THRESHOLD (512 << (10 - SECTOR_SHIFT)) /* 512KB */
struct ioprio_ux_policy {
	int max_ux_read_sectors;
	int max_ux_write_sectors;
};

static const char *ioprio_type_name[] = {
	[IOPRIO_TYPE_NONE]	= "NON-RW",
	[IOPRIO_TYPE_READ]	= "READ",
	[IOPRIO_TYPE_WRITE]	= "WRITE",
};

struct ioprio_sd_info global_ioprio_sd_info;
struct ioprio_ux_policy global_ioprio_ux_policy;

static inline int bio_to_granul_index(struct bio *bio)
{
	unsigned int sectors = bio_sectors(bio);
	int index;

	if (sectors == 0)
		return 0;

	index = ilog2(sectors);
	/* (0..8) sectors */
	if (index < ioprio_granuls[1])
		return 0;
	/* [8..32) sectors */
	if (index < ioprio_granuls[2])
		return 1;
	/* [32..2048) sectors */
	if (index < ioprio_granuls[IOPRIO_GRANUL_COUNT - 1])
		return index - 3;
	/* [2048..) sectors */
	return IOPRIO_GRANUL_COUNT - 1;
}

static inline int bio_to_ioprio_type(struct bio *bio)
{
	unsigned int sectors = bio_sectors(bio);

	if (0 == sectors)
		return IOPRIO_TYPE_NONE;
	/* bio already splitted, means child bio has already set ioprio and
	 * child has the same ioprio with parent.*/
	if (bio_flagged(bio, BIO_CHAIN))
		return IOPRIO_TYPE_NONE;
	if (bio_op(bio) != REQ_OP_READ && bio_op(bio) != REQ_OP_WRITE)
		return IOPRIO_TYPE_NONE;

	if (bio_data_dir(bio) == WRITE)
		return IOPRIO_TYPE_WRITE;
	else
		return IOPRIO_TYPE_READ;
}

static void __update_ioprio_dist_stat(struct ioprio_dist_stat *stat, struct bio *bio)
{
	int type = bio_to_ioprio_type(bio);
	int index = bio_to_granul_index(bio);

	stat->total_cnt[type]++;
	if (type == IOPRIO_TYPE_NONE)
		return;
	stat->granul_dist_cnt[type][index]++;
	stat->granul_total_sectors[type][index] += bio_sectors(bio);
	stat->granul_dist_cnt[type][IOPRIO_GRANUL_COUNT]++;
	stat->granul_total_sectors[type][IOPRIO_GRANUL_COUNT] += bio_sectors(bio);
}

static void update_ioprio_dist_stat(struct bio *bio)
{
	unsigned long flag;
	bool is_ux = test_task_ux(current) || test_task_fg(current) || test_task_top_app(current);

	if (!oplus_ioprio_stat_enabled)
		return;
	spin_lock_irqsave(&global_ioprio_sd_info.lock, flag);
	if (bio->bi_opf & REQ_BACKGROUND)
		__update_ioprio_dist_stat(&global_ioprio_sd_info.lowpri_stat, bio);
	if (IOPRIO_PRIO_CLASS(bio->bi_ioprio) == IOPRIO_CLASS_RT)
		__update_ioprio_dist_stat(&global_ioprio_sd_info.rt_stat, bio);
	/* REQ_HIPRIO and ux thread rw will be marked as ux I/O */
	if (bio->bi_opf & (REQ_META | REQ_PRIO))
		__update_ioprio_dist_stat(&global_ioprio_sd_info.hipri_stat, bio);
	else if (is_ux)
		__update_ioprio_dist_stat(&global_ioprio_sd_info.ux_stat, bio);
	spin_unlock_irqrestore(&global_ioprio_sd_info.lock, flag);
}

static void __ioprio_print_stat(struct ioprio_dist_stat *stat, struct seq_file *seq_filp)
{
	int i = 0, j = 0;
	unsigned long long avg;

	for (i = 0; i < MAX_IOPRIO_TYPE; i++) {
		seq_printf(seq_filp, "IOPRIO TYPE: %s\n", ioprio_type_name[i]);
		seq_printf(seq_filp, "dist: ");
		for (j = 0; j < IOPRIO_GRANUL_COUNT; j++)
			seq_printf(seq_filp, "%d\t", 0 == j ? 0 : 1 << ioprio_granuls[j]);
		seq_printf(seq_filp, "total\n");
		seq_printf(seq_filp, " cnt: ");
		for (j = 0; j <= IOPRIO_GRANUL_COUNT; j++)
			seq_printf(seq_filp, "%u\t", stat->granul_dist_cnt[i][j]);
		seq_printf(seq_filp, "\n");
		seq_printf(seq_filp, " avg: ");
		for (j = 0; j <= IOPRIO_GRANUL_COUNT; j++) {
			avg = (0 == stat->granul_dist_cnt[i][j]) ? 0 :
				stat->granul_total_sectors[i][j] / stat->granul_dist_cnt[i][j];
			seq_printf(seq_filp, "%llu\t", avg);
		}
		seq_printf(seq_filp, "\n");
	}
}

#define IOPRIO_PRINT_SIGN "==================================="
static void ioprio_print_stat(struct seq_file *seq_filp)
{
	seq_printf(seq_filp, IOPRIO_PRINT_SIGN"LOWPRI STAT"IOPRIO_PRINT_SIGN"\n");
	__ioprio_print_stat(&global_ioprio_sd_info.lowpri_stat, seq_filp);
	seq_printf(seq_filp, IOPRIO_PRINT_SIGN"RT STAT"IOPRIO_PRINT_SIGN"\n");
	__ioprio_print_stat(&global_ioprio_sd_info.rt_stat, seq_filp);
	seq_printf(seq_filp, IOPRIO_PRINT_SIGN"HIPRI STAT"IOPRIO_PRINT_SIGN"\n");
	__ioprio_print_stat(&global_ioprio_sd_info.hipri_stat, seq_filp);
	seq_printf(seq_filp, IOPRIO_PRINT_SIGN"UX STAT"IOPRIO_PRINT_SIGN"\n");
	__ioprio_print_stat(&global_ioprio_sd_info.ux_stat, seq_filp);
}

static int ioprio_ux_status_show(struct seq_file *seq_filp, void *data)
{
	ioprio_print_stat(seq_filp);
	return 0;
}

int ioprio_ux_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ioprio_ux_status_show, file);
}

struct proc_ops ioprio_ux_status_proc_fops = {
	.proc_open = ioprio_ux_status_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

struct {
	const char *name;
	umode_t mode;
	struct proc_dir_entry *node;
	struct proc_ops *proc_fops;
} ioprio_procfs_nodes[] = {
	{"ioprio_ux_status",	S_IRUGO, NULL, &ioprio_ux_status_proc_fops},
	{NULL,	0,	NULL, NULL}
};

void ioprio_set_bio_by_ux(struct bio *bio)
{
	enum ioprio_io_type type = bio_to_ioprio_type(bio);
	unsigned int sectors = bio_sectors(bio);
	bool is_ux = test_task_ux(current) || test_task_fg(current) || test_task_top_app(current);
	bool is_hipri = !!(bio->bi_opf & (REQ_META | REQ_PRIO)); /* REQ_SYNC maybe wdio and gc. */
	bool is_rt = (IOPRIO_PRIO_CLASS(get_current_ioprio()) == IOPRIO_CLASS_RT);

	if (type == IOPRIO_TYPE_NONE)
		return;
	if (!is_ux && !is_hipri && !is_rt)
		return;
	if (IOPRIO_PRIO_CLASS(bio->bi_ioprio) == IOPRIO_CLASS_RT)
		return;

	/* ckpt write and metadata must be high priority I/O. */
	switch (type) {
	case IOPRIO_TYPE_READ:
		if (is_hipri || sectors <= global_ioprio_ux_policy.max_ux_read_sectors)
			bio->bi_ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 4);
		break;
	case IOPRIO_TYPE_WRITE:
		if (is_hipri || sectors <= global_ioprio_ux_policy.max_ux_write_sectors)
			bio->bi_ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 4);
		break;
	default:
		IO_PRIO_INFO("Unexpected type: %d\n", type);
	}
}

void cb_android_vh_check_set_ioprio(void *ignore, struct bio *bio)
{
	if (!oplus_ioprio_feature_enabled)
		return;
	if (strncmp(bio->bi_bdev->bd_disk->disk_name, "sd", 2))
		return;

	update_ioprio_dist_stat(bio);
	ioprio_set_bio_by_ux(bio);
}


static int register_vendor_hooks(void)
{
	int ret = 0;

	memset(&global_ioprio_sd_info, 0x00, sizeof(struct ioprio_sd_info));
	spin_lock_init(&global_ioprio_sd_info.lock);
	global_ioprio_ux_policy.max_ux_read_sectors = OPLUS_DEFAULT_UX_READ_THRESHOLD;
	global_ioprio_ux_policy.max_ux_write_sectors = OPLUS_DEFAULT_UX_WRITE_THRESHOLD;
	ret = register_trace_android_vh_check_set_ioprio(cb_android_vh_check_set_ioprio, NULL);
	WARN_ON(ret);
	IO_PRIO_INFO("run:%d\n", ret);

	return ret;
}

void unregister_vendor_hooks(void)
{
	unregister_trace_android_vh_check_set_ioprio(cb_android_vh_check_set_ioprio, NULL);
}

static void ioprio_remove_procs(void)
{
	int i = 0;

	for (i = 0; ioprio_procfs_nodes[i].name; i++) {
            remove_proc_entry(ioprio_procfs_nodes[i].name, ioprio_proc_dir);
	}
        remove_proc_entry(IOPRIO_PROC_DIR_NAME, NULL);
}

static int ioprio_create_procs(void)
{
	int ret = 0, i = 0;
	struct proc_dir_entry *node;

	ioprio_proc_dir = proc_mkdir(IOPRIO_PROC_DIR_NAME, NULL);
	if (!ioprio_proc_dir) {
		IO_PRIO_INFO("Can't create procfs node\n");
		ret = -EPERM;
		goto error_dir;
	}
	for (i = 0; ioprio_procfs_nodes[i].name; i++) {
		node = proc_create(ioprio_procfs_nodes[i].name,
			ioprio_procfs_nodes[i].mode,
			ioprio_proc_dir,
			ioprio_procfs_nodes[i].proc_fops);
		if (!node) {
			IO_PRIO_INFO("Can't create %s\n", ioprio_procfs_nodes[i].name);
			ret = -EPERM;
			goto error_entry;
		}
		ioprio_procfs_nodes[i].node = node;
	}

	return 0;

error_entry:
	while (i-- > 0)
            remove_proc_entry(ioprio_procfs_nodes[i].name, ioprio_proc_dir);
        remove_proc_entry(IOPRIO_PROC_DIR_NAME, NULL);
error_dir:
	return ret;
}

static int __init io_priority_init(void)
{
	int ret = 0;

	ret = ioprio_create_procs();
	if (ret) {
		IO_PRIO_INFO("oplus_io_priority init proc error\n");
		goto proc_error;
	}

	ret = register_vendor_hooks();
	if (ret) {
		IO_PRIO_INFO("oplus_io_priority register vendor hooks error\n");
		goto hook_error;
	}

	ret = deadline_init();
	if (ret) {
		IO_PRIO_INFO("add scheduler error\n");
		goto sched_error;
	}

	IO_PRIO_INFO("oplus_io_priority module init done\n");
	return 0;

sched_error:
	unregister_vendor_hooks();
hook_error:
	ioprio_remove_procs();
proc_error:
	return ret;
}

static void __exit io_priority_exit(void)
{
	unregister_vendor_hooks();
	ioprio_remove_procs();
	deadline_exit();
}

module_param(oplus_ioprio_stat_enabled, bool, S_IRUGO | S_IWUSR);
module_param(oplus_ioprio_feature_enabled, bool, S_IRUGO | S_IWUSR);
module_param_named(oplus_max_ux_read_sectors,
		global_ioprio_ux_policy.max_ux_read_sectors, int, S_IRUGO | S_IWUSR);
module_param_named(oplus_max_ux_write_sectors,
		global_ioprio_ux_policy.max_ux_write_sectors, int, S_IRUGO | S_IWUSR);

module_init(io_priority_init);
module_exit(io_priority_exit);
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
