#include <linux/types.h>
#include <trace/hooks/binder.h>
#include <drivers/android/binder_alloc.h>
#include <drivers/android/binder_internal.h>

struct pending_transaction_watcher_args {
	int binder_node_id;
	int elapsed_time;
	struct work_struct pending_transaction_watcher_work;
};
extern struct pending_transaction_watcher_args  *pending_transaction_work_args;
extern struct  kmem_cache *pending_transaction_watcher_struct_cachep;
void binder_buffer_watcher(void *ignore, size_t size, size_t *free_async_space, 
							int is_async,  bool *should_fail);
void pending_transaction_watcher(void *data, struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync);
