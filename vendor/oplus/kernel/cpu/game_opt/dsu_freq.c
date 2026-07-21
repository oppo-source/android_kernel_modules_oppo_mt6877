#include "dsu_freq.h"
#include "game_ctrl.h"

#define DSU_FREQ_LEN 256

#if IS_ENABLED(CONFIG_OPLUS_DSU_OPT)
extern u32 get_dsu_freq(void);
#endif

static int dsu_freq_show(struct seq_file *m, void *v)
{
    char page[DSU_FREQ_LEN];
    ssize_t len = 0;
    u32 dsu_freq = 0;

#if IS_ENABLED(CONFIG_OPLUS_DSU_OPT)
    dsu_freq = get_dsu_freq();
#endif
    systrace_c_printk("dsu_freq", dsu_freq);
    len += snprintf(page + len, DSU_FREQ_LEN - len, "%u\n", dsu_freq);
    if (len > 0) {
        seq_puts(m, page);
    }
    return 0;
}

static int dsu_freq_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, dsu_freq_show, inode);
}

static const struct proc_ops dsu_freq_proc_ops = {
    .proc_open		= dsu_freq_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

void dsu_freq_init(void)
{
    proc_create_data("dsu_freq", 0664, game_opt_dir, &dsu_freq_proc_ops, NULL);
}
