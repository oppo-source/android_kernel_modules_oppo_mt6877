#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/icmp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/version.h>
#include <net/dst.h>
#include <net/genetlink.h>
#include <net/inet_connection_sock.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/tcp_states.h>
#include <net/udp.h>
#include <linux/netfilter_ipv6.h>
#include <linux/crc32.h>
#include <linux/jiffies.h>


#define LOG_TAG "oplus_tx_full"

static int s_debug = 0;

#define LOGK(flag, fmt, args...)     \
    do {                             \
        if (flag || s_debug) {       \
            printk("[%s]:" fmt "\n", LOG_TAG, ##args);\
        }                                             \
    } while (0)


#define UPLOAD_ONE_MAX_LEN  (256)
#define NLA_DATA(na)	((char *)((char*)(na) + NLA_HDRLEN))

static u32 s_one_upload_size = UPLOAD_ONE_MAX_LEN;
static u32 s_user_pid = 0;
static u32 s_count[4] = {0};
static u32 s_index = 0;
static u32 s_queue = 0;
static u32 s_simulate = 0;
static u32 s_enable_report = 0;
static u32 s_queue_size = 60;

static spinlock_t s_tx_full_lock;

static struct genl_family oplus_tx_full_genl_family;

enum tx_full_msg_type_et {
	OPLUS_TX_FULL_MSG_UNSPEC,
	OPLUS_TX_FULL_MSG_REPORT,
	OPLUS_TX_FULL_MSG_STATE,
	__OPLUS_TX_FULL_MSG_MAX,
};
#define OPLUS_TX_FULL_MSG_MAX (__OPLUS_TX_FULL_MSG_MAX - 1)
enum tx_full_cmd_type_et {
	OPLUS_TX_FULL_CMD_UNSPEC,
	OPLUS_TX_FULL_CMD_CHANGE,
	__OPLUS_TX_FULL_CMD_MAX,
};
#define OPLUS_TX_FULL_CMD_MAX (__OPLUS_TX_FULL_CMD_MAX - 1)

#define OPLUS_TX_FULL_FAMILY_NAME "oplus_tx_full"
#define OPLUS_TX_FULL_FAMILY_VERSION 1

typedef struct {
	long time;
	int count;
	int queue;
	int index;
} tx_full_info;

typedef struct {
	tx_full_info data[60];
	int front;
	int rear;
	int size;
} tx_full_queue;

typedef struct {
	tx_full_queue* queues;
	int size;
} tx_full_map;

static tx_full_map* map_data = NULL;

static void init_queue(tx_full_queue* q) {
	q->front = 0;
	q->rear = -1;
	q->size = 0;

}

static void enqueue(tx_full_queue* q, tx_full_info txi) {
	if (q->size < s_queue_size) {
		q->rear = (q->rear + 1) % s_queue_size;
		q->data[q->rear] = txi;
		q->size++;
	}

}

static void dequeue(tx_full_queue* q) {
	if (q->size > 0) {
		q->front = (q->front + 1) % s_queue_size;
		q->size--;
	}
}

static int find_queue_index(tx_full_map* map, int queue, int index) {
	for (int i = 0; i < map->size; i++) {
		if (map->queues[i].data[0].queue == queue && map->queues[i].data[0].index == index) {
			return i;
		}
	}
	return -1;
}

static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data, int len)
{
	int ret;

	/* add a netlink attribute to a socket buffer */
	if ((ret = nla_put(skb, type, len, data)) != 0) {
		return ret;
	}

	return 0;
}

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);

	if (skb == NULL) {
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &oplus_tx_full_genl_family, 0, cmd);
	LOGK(0, "genl_msg_prepare_usr_msg_1,skb_len=%u,pid=%u,cmd=%u,id=%u\n",
	skb->len, (unsigned int)pid, cmd, oplus_tx_full_genl_family.id);
	*skbp = skb;
	return 0;
}

static int send_netlink_data(int type, char *data, int len) {
	int ret = 0;
	void * head;
	struct sk_buff *skbuff;
	size_t size;

	/* allocate new buffer cache */
	size = nla_total_size(len);
	ret = genl_msg_prepare_usr_msg(OPLUS_TX_FULL_MSG_REPORT, size, s_user_pid, &skbuff);
	if (ret) {
		return ret;
	}

	ret = genl_msg_mk_usr_msg(skbuff, type, data, len);
	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	/* send data */
	ret = genlmsg_unicast(&init_net, skbuff, s_user_pid);
	if(ret < 0) {
		LOGK(1,"genlmsg_unicast return error, ret = %d\n", ret);
		return -1;
	}
	return 0;
}

static void process_tx_full(int count, int queue, int index) {
	if (!map_data) {
		LOGK(1, "Invalid TxFullInfoMap pointer");
		return;
	}

	int queue_index = find_queue_index(map_data, queue, index);
	if (queue_index == -1) {
		map_data->queues = krealloc(map_data->queues, (map_data->size + 1) * sizeof(tx_full_queue), GFP_KERNEL);
		if (!map_data->queues) {
			LOGK(1, "Memory allocation failed");
			return;
		}
		LOGK(1, "Memory allocation");
		queue_index = map_data->size++;
		init_queue(&map_data->queues[queue_index]);
	}

	tx_full_queue* q = &map_data->queues[queue_index];
	long time = jiffies_to_msecs(jiffies) / 1000;
	LOGK(0, "time:%ld", time);
	tx_full_info txi = {time, count, queue, index};
	enqueue(q, txi);

	if (q->size == s_queue_size) {
		long t0 = q->data[q->front].time;
		long t1 = q->data[q->rear].time;
		LOGK(0, "front:%d,t0:%ld,rear:%d,t1:%ld", q->front, t0, q->rear, t1);
		if ((t1 - t0 < 61) && (t1 - t0 > 0)) {
			u32 cur_copy_len = 0;
			u32 max_upload_size = s_one_upload_size;
			char *data = NULL;
			LOGK(0, "process_tx_full upload");
			data = kmalloc(max_upload_size, GFP_ATOMIC);
			if (data == NULL) {
				LOGK(1, "malloc %u failed!", max_upload_size);
				return;
			}
			memset(data, 0, max_upload_size);
			memcpy(data, &count, sizeof(u32));
			cur_copy_len += sizeof(u32);
			if (cur_copy_len != 0) {
				int ret = send_netlink_data(OPLUS_TX_FULL_MSG_REPORT, data, cur_copy_len);
				LOGK(0, "send_netlink_data size %u return %d", cur_copy_len, ret);
			}
			kfree(data);
			init_queue(q);
		} else {
			dequeue(q);
			LOGK(0, "invalid tx full");
		}
	}
}

int send_all_stats(u32 count, u32 queue, u32 index) {
	if (!s_enable_report) {
		LOGK(0, "send_all_stats flag %u", s_enable_report);
		return 0;
	}
	if (queue < 0 || queue > 3) {
		LOGK(1, "invalid queue %u!", queue);
		return 0;
	}
	process_tx_full(count, queue, index);
	s_count[queue] = count + 1;
	s_queue = queue;
	s_index = index;

	return 0;
}

static void enable_tx_full_report(struct nlattr *nla) {
	u32 *state = (u32 *)NLA_DATA(nla);
	s_enable_report = state[0];
	LOGK(0, "set reprot state %d", s_enable_report);
}

static int oplus_tx_full_netlink_rcv_msg(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	LOGK(0, "set s_user_pid=%u type=%u len=%u.", nlhdr->nlmsg_pid, nla->nla_type, nla->nla_len);
	if (s_user_pid == 0) {
		s_user_pid = nlhdr->nlmsg_pid;
		LOGK(1, "update_pid=%u", nlhdr->nlmsg_pid);
	} else if (s_user_pid != nlhdr->nlmsg_pid) {
		LOGK(1, "user pid changed!! %u - %u", s_user_pid, nlhdr->nlmsg_pid);
		s_user_pid = nlhdr->nlmsg_pid;
	}
	LOGK(0, "nla->nla_type %d", nla->nla_type);

	switch (nla->nla_type) {
	case OPLUS_TX_FULL_MSG_REPORT:
		LOGK(0, "send_all_stats return %d", ret);
		break;
	case OPLUS_TX_FULL_MSG_STATE:
		enable_tx_full_report(nla);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int simulation_proc_handler(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos) {
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret) {
		LOGK(1, "simulation_proc_handler return %d", ret);
		return ret;
	}
	LOGK(1, "simulation_proc_handler write %d", write);
	if (write) {
		send_all_stats(s_count[0], s_queue, s_index);
	}

	return 0;
}


static const struct genl_ops oplus_tx_full_genl_ops[] = {
	{
		.cmd = OPLUS_TX_FULL_CMD_CHANGE,
		.flags = 0,
		.doit = oplus_tx_full_netlink_rcv_msg,
		.dumpit = NULL,
	},
};

static struct genl_family oplus_tx_full_genl_family = {
	.id = 0,
	.hdrsize = 0,
	.name = OPLUS_TX_FULL_FAMILY_NAME,
	.version = OPLUS_TX_FULL_FAMILY_VERSION,
	.maxattr = OPLUS_TX_FULL_MSG_MAX,
	.ops = oplus_tx_full_genl_ops,
	.n_ops = ARRAY_SIZE(oplus_tx_full_genl_ops),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	.resv_start_op = OPLUS_TX_FULL_CMD_CHANGE + 1,
#endif
};

static int oplus_tx_full_netlink_init(void)
{
	int ret;
	ret = genl_register_family(&oplus_tx_full_genl_family);
	if (ret) {
		LOGK(1, "genl_register_family:%s failed,ret = %d\n", OPLUS_TX_FULL_FAMILY_NAME, ret);
		return ret;
	} else {
		LOGK(1, "genl_register_family complete, id = %d!\n", oplus_tx_full_genl_family.id);
	}

	return 0;
}


static void oplus_tx_full_netlink_exit(void)
{
	genl_unregister_family(&oplus_tx_full_genl_family);
}

static struct ctl_table oplus_net_hook_sysctl_table[] = {
	{
		.procname   = "debug",
		.data       = &s_debug,
		.maxlen     = sizeof(int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "enable",
		.data       = &s_enable_report,
		.maxlen     = sizeof(int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "count",
		.data       = &s_count[0],
		.maxlen     = sizeof(int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "index",
		.data       = &s_index,
		.maxlen     = sizeof(s_index),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "queue",
		.data       = &s_queue,
		.maxlen     = sizeof(s_queue),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "limit",
		.data       = &s_queue_size,
		.maxlen     = sizeof(s_queue_size),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "simulation",
		.data       = &s_simulate,
		.maxlen     = sizeof(s_simulate),
		.mode       = 0644,
		.proc_handler   = simulation_proc_handler,
	},
	{}
};


static struct ctl_table_header *oplus_tx_full_table_hdr = NULL;

static int oplus_tx_full_sysctl_init(void)
{
	oplus_tx_full_table_hdr = register_net_sysctl(&init_net, "net/oplus_tx_full", oplus_net_hook_sysctl_table);
	return oplus_tx_full_table_hdr == NULL ? -ENOMEM : 0;
}

int oplus_tx_full_init(void)
{
	int ret = 0;

	spin_lock_init(&s_tx_full_lock);

	ret = oplus_tx_full_netlink_init();
	if (ret < 0) {
	    LOGK(1, "init module failed to init netlink, ret =%d", ret);
		return ret;
	} else {
		LOGK(1, "init module init netlink successfully.");
	}

	oplus_tx_full_sysctl_init();
	map_data = kmalloc(sizeof(tx_full_map), GFP_KERNEL);
	if (!map_data) {
		LOGK(1, "Memory allocation failed");
		return -1;
	}
	map_data->queues = NULL;
	map_data->size = 0;

	return ret;
}

void oplus_tx_full_fini(void)
{
	LOGK(1, "oplus_stats_fini.");
	oplus_tx_full_netlink_exit();
	if (oplus_tx_full_table_hdr) {
		unregister_net_sysctl_table(oplus_tx_full_table_hdr);
	}
}

