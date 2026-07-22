#include "frk_stability.h"
#include "frk_netlink.h"
#include "binder_watcher.h"

static bool is_system_server(struct task_struct *t)
{
	if (!t)
		return false;

  	if (task_uid(t).val == SYSTEM_UID) {
  		if (!strcmp(t->comm, "system_server"))
  			return true;
  	}
  	return false;
}
static bool is_surface_flinger(struct task_struct *t)
{
	if (!t)
		return false;

  	if (task_uid(t).val == SYSTEM_UID) {
  		if (!strcmp(t->comm, "surfaceflinger"))
  			return true;
  	}
  	return false;
}
static struct binder_buffer *binder_buffer_next(struct binder_buffer *buffer)
{
	return list_entry(buffer->entry.next, struct binder_buffer, entry);
}


static size_t binder_alloc_buffer_size(struct binder_alloc *alloc,
				       struct binder_buffer *buffer)
{
	if (list_is_last(&buffer->entry, &alloc->buffers))
		return alloc->buffer + alloc->buffer_size - buffer->user_data;
	return binder_buffer_next(buffer)->user_data - buffer->user_data;
}

void pending_transaction_watcher_do_work(struct work_struct *work)
{
	struct pending_transaction_watcher_args *args= container_of(work, struct pending_transaction_watcher_args, pending_transaction_watcher_work);
	if (args) {
		int pending_transaction_array[2] = { 0 };
		//binder_watcher_debug("system thread counts: %d, %d:%s is creating way too much threads,thread counts: %d \n",args->nr_threads,args->tsk->tgid, args->tsk->comm,args->current_signal_nr_threads);
		binder_watcher_debug("pending transaction binder node id: %d,elapsed time is: %d \n",args->binder_node_id, args->elapsed_time);
		//binder_watcher_debug("pending transaction binder node id: %d,elapsed time is: %d \n",args->binder_node_id, args->elapsed_time);
		pending_transaction_array[0] = args->binder_node_id; //binder node id which has pending transaction
		pending_transaction_array[1] = args->elapsed_time; 
		send_to_frk(PENDING_TRANSACTION_WATCHER_EVENT,ARRAY_SIZE(pending_transaction_array),pending_transaction_array);
		kmem_cache_free(pending_transaction_watcher_struct_cachep,args);
	}

}

void pending_transaction_watcher(void *data, struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync)
{
	static int last_node_id = -1;
	struct binder_node *node = t->buffer->target_node;
	bool oneway = !!(t->flags & TF_ONE_WAY);
	struct binder_alloc *alloc = NULL;

	if (!oneway || !pending_async || proc == NULL)
		goto watcher_done;

        alloc = &proc->alloc;


	if (alloc == NULL || alloc->free_async_space > alloc->buffer_size / 3) {
		//This should not happen, if it does, do nothing
		//if there is still 2/3 free async space left, do nothing
	        goto watcher_done;
	}

	if(is_system_server(proc->tsk)) {
		//this transaction is aiming at system_server,check the first pending transaction
		//get the first transaction work from proc->async_todo 
		struct binder_work *w;

		w = list_first_entry_or_null(&node->async_todo, struct binder_work, entry);
		if (w) {
			struct binder_transaction *t;

			if (w->type == BINDER_WORK_TRANSACTION) {
				t = container_of(w, struct binder_transaction, work);
				ktime_t current_time = ktime_get();
				if (ktime_ms_delta(current_time, t->start_time) > 50000) {
					int pending_count = 0;
					struct binder_work *w1;
					list_for_each_entry(w1, &node->async_todo, entry) {
						if (w1->type != BINDER_WORK_TRANSACTION)
							continue;
						pending_count++;
					}
					if (pending_count > 200) {
						if (last_node_id > 0 && last_node_id == node->debug_id) {
							//binder_watcher_debug("pending transaction binder node id: %d,last node id : %d \n",node->debug_id, last_node_id);
							binder_watcher_debug("pending transaction binder node id: %d,last node id : %d \n",node->debug_id, last_node_id);
							goto watcher_done;
						}
						last_node_id = node->debug_id;
						pending_transaction_work_args = kmem_cache_alloc(pending_transaction_watcher_struct_cachep, GFP_ATOMIC);
						if (!pending_transaction_work_args)
							return;
						pending_transaction_work_args->binder_node_id = node->debug_id;
						pending_transaction_work_args->elapsed_time  = ktime_ms_delta(current_time, t->start_time) / 1000; //unit:sencond
						INIT_WORK(&pending_transaction_work_args->pending_transaction_watcher_work, pending_transaction_watcher_do_work);
						schedule_work(&pending_transaction_work_args->pending_transaction_watcher_work);
					}
				}
			}
		}
	}
watcher_done:
	return;
}

void binder_buffer_watcher(void *ignore, size_t size, size_t *free_async_space, 
							int is_async,  bool *should_fail)
{
	struct task_struct *task = NULL;
	struct binder_alloc *alloc = NULL;
	alloc = container_of(free_async_space, struct binder_alloc, free_async_space);
	if (alloc == NULL) {
	        //This should not happen, if it does, do nothing
	        goto watcher_done;
	}

	//first, check if it is async binder transaction
	if (is_async) {
		if (alloc->free_async_space > alloc->buffer_size / 3)
			//if there is still 2/3 free async space left, do nothing
			goto watcher_done;

	 	//if this async transaction is from surfaceflinger backgroud thread or main thread, do nothing.
  		if (is_surface_flinger(current->group_leader)) {
			//binder_watcher_debug("%d:%s is calling alloc binder buffer  %d\n",current->tgid, current->comm,alloc->pid);
  			goto watcher_done;
		}

		// first check this target allocation is aiming at system_server
		task = get_pid_task(find_vpid(alloc->pid), PIDTYPE_PID);
		if(is_system_server(task)) {
			//now system_server async space is lower than 40%, try to calculate the binder buffer usage of this pid
			struct rb_node *n;
			struct binder_buffer *buffer;
			size_t total_alloc_size = 0;
			size_t num_buffers = 0;

			for (n = rb_first(&alloc->allocated_buffers); n != NULL;
				 n = rb_next(n)) {
				buffer = rb_entry(n, struct binder_buffer, rb_node);
				if (buffer->pid != current->tgid)
					continue;
				if (!buffer->async_transaction)
					continue;
				total_alloc_size += binder_alloc_buffer_size(alloc, buffer)
					+ sizeof(struct binder_buffer);
				num_buffers++;
			}
		
			/*
			 * Warn if this pid has more than 50 transactions, or more than 50% of
			 * async space (which is 25% of total buffer size). Oneway spam is only
			 * detected when the threshold is exceeded.
			 */
			if (total_alloc_size > alloc->buffer_size / 4) {
				binder_watcher_debug("%d: pid %d spamming oneway? %zd buffers allocated for a total size of %zd\n",
		        			      alloc->pid, current->tgid, num_buffers, total_alloc_size);
				binder_watcher_debug("%d: pid %d spamming oneway? should fail\n",
		        			      alloc->pid, current->tgid);
				*should_fail = true;
			}

		}
	} else {
                //this is a sync transaction
		if (size < (SZ_1M + 2048))
			goto watcher_done;
		if (__kuid_val(current_real_cred()->euid) < 10000)
			goto watcher_done;
		//if this async transaction is from surfaceflinger backgroud thread or main thread, do nothing.
		if (is_surface_flinger(current->group_leader)) {
			//binder_watcher_debug("%d:%s is calling alloc binder buffer  %d\n",current->tgid, current->comm,alloc->pid);
			goto watcher_done;
		}
		// first check this target allocation is aiming at system_server
		task = get_pid_task(find_vpid(alloc->pid), PIDTYPE_PID);
		if(is_system_server(task)) {
	        	//binder_watcher_debug("#3333 %d: pid %d uid %d try to allocate  a total size of %zd\n",
		        // 			      alloc->pid, current->tgid, __kuid_val(current_real_cred()->euid),size);
			binder_watcher_debug("%d: pid %d uid %d try to allocate  a total size of %zd,should fail!\n",
		         			      alloc->pid, current->tgid, __kuid_val(current_real_cred()->euid),size);
			*should_fail = true;
		}
        }
	if (task) {
		put_task_struct(task);
	}
watcher_done:
	return;
}
