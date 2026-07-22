#ifndef _OSVELTE_MM_UTILS_H
#define _OSVELTE_MM_UTILS_H

#define MM_LOG_LVL 1
#define MM_LOG_TAG "mm"
enum {
	MM_LOG_VERBOSE = 0,
	MM_LOG_INFO,
	MM_LOG_DEBUG,
	MM_LOG_ERR,
};

static inline char mm_loglvl_to_char(int l)
{
	switch (l) {
	case MM_LOG_VERBOSE:
		return 'V';
	case MM_LOG_INFO:
		return 'I';
	case MM_LOG_DEBUG:
		return 'D';
	case MM_LOG_ERR:
		return 'E';
	}
	return '?';
}

#define mm_log(l, t, f, ...) do {					\
	if (l >= MM_LOG_LVL) 						\
		printk(KERN_ERR "%s %5d %5d %c %-16s: %s:%d "f,		\
		       t, current->tgid, current->pid,			\
		       mm_loglvl_to_char(l), current->comm, __func__,	\
		       __LINE__,  ##__VA_ARGS__);			\
} while (0)

#define mm_loge(f, ...)							\
	mm_log(MM_LOG_ERR, MM_LOG_TAG, f, ##__VA_ARGS__)
#define mm_logi(f, ...)							\
	mm_log(MM_LOG_INFO, MM_LOG_TAG, f, ##__VA_ARGS__)
#define mm_logd(f, ...)							\
	mm_log(MM_LOG_DEBUG, MM_LOG_TAG, f, ##__VA_ARGS__)

#define mm_loge_tag(t, f, ...)						\
	mm_log(MM_LOG_ERR, t, f, ##__VA_ARGS__)
#define mm_logi_tag(t, f, ...)						\
	mm_log(MM_LOG_INFO, t, f, ##__VA_ARGS__)
#define mm_logd_tag(t, f, ...)						\
	mm_log(MM_LOG_DEBUG, t, f, ##__VA_ARGS__)

#endif /* _OSVELTE_MM_UTILS_H */
