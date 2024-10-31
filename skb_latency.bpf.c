// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define TRACK_SKBS 4096

#define DEFINE_MAP(name) \
	struct { \
		__uint(type, BPF_MAP_TYPE_ARRAY); \
		__type(key, int); \
		__type(value, int); \
		__uint(max_entries, TRACK_US); \
	} evt_##name SEC(".maps"); \
	struct { \
		__uint(type, BPF_MAP_TYPE_ARRAY); \
		__type(key, int); \
		__type(value, int); \
		__uint(max_entries, 1); \
	} oob_##name SEC(".maps")

#define BEGIN_TRACKING_FEXIT(name, args...) \
	SEC("fexit/"#name) \
	int BPF_PROG(name, args) \
	{ \
		__u64 now = sched_clock(); \
		begin_tracking(ret, now); \
		return 0; \
	}

#define END_TRACKING_TRACEPOINT(name, dir, arg, field) \
	SEC("tracepoint/"dir"/"#name) \
	int name(struct arg *ctx) \
	{ \
		end_tracking(ctx->field); \
		return 0; \
	}

#define RECORD_FENTRY(name, args...) \
	DEFINE_MAP(name); \
	SEC("fentry/"#name) \
	int BPF_PROG(name, args) \
	{ \
		__u64 now = sched_clock(); \
		record_event(skb, &evt_##name, &oob_##name, now); \
		return 0; \
	}

#define RECORD_FEXIT(name, args...) \
	DEFINE_MAP(name); \
	SEC("fexit/"#name) \
	int BPF_PROG(name, args) \
	{ \
		__u64 now = sched_clock(); \
		record_event(skb, &evt_##name, &oob_##name, now); \
		return 0; \
	}

#define RECORD_TRACEPOINT(name, dir, arg, field) \
	DEFINE_MAP(name); \
	SEC("tracepoint/"dir"/"#name) \
	int name(arg *ctx) \
	{ \
		__u64 now = sched_clock(); \
		record_event(ctx->field, &evt_##name, &oob_##name, now); \
		return 0; \
	}

int missed_skb;
int scale;

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, TRACK_SKBS);
	__type(key, __u64);
	__type(value, __u64);
} birth_map SEC(".maps");

static __u64 sched_clock(void)
{
	/*return bpf_ktime_get_ns();*/
	return bpf_ktime_get_boot_ns();
}

static void begin_tracking(void *skb, __u64 now)
{
	__u64 key = (__u64)skb;
	__u64 val = now;

	if (bpf_map_update_elem(&birth_map, &key, &val, BPF_ANY) < 0)
		missed_skb++;
}

static void begin_tracking_clone(void *old_skb, void *new_skb, __u64 now)
{
	__u64 key = (__u64)old_skb;
	__u64 *birth_val;
	__u64 val = now;

	/* reuse old birth time if any */
	birth_val = bpf_map_lookup_elem(&birth_map, &key);
	if (birth_val)
		val = *birth_val;

	key = (__u64)new_skb;
	if (bpf_map_update_elem(&birth_map, &key, &val, BPF_ANY) < 0)
		missed_skb++;
}

static void end_tracking(void *skb)
{
	__u64 key = (__u64)skb;

	bpf_map_delete_elem(&birth_map, &key);
}

static void record_event(void *skb, void *map, void *oob, __u64 now)
{
	__u64 key = (__u64)skb;
	__u64 *begin = NULL;

	begin = bpf_map_lookup_elem(&birth_map, &key);
	if (!begin)
		return;

	int bucket = -1;
	int ts = (now - *begin) / scale;

	/* Resolution:
	 *  <100us         - 1us     [0  .. 99]
	 *   100us..1000   - 10us    [100..189]
	 * >=1000us        - 100us   [190..   ]
	 */

	if (ts < 100)
		bucket = ts;
	else if (ts < 1000)
		bucket = 100 + (ts - 100) / 10;
	else
		bucket = 190 + (ts - 1000) + ts / 100;

	int *bucket_val = bpf_map_lookup_elem(map, &bucket);
	if (!bucket_val) {
		int oob_key = 0;
		int *oob_val = bpf_map_lookup_elem(oob, &oob_key);
		if (oob_val)
			*oob_val += 1;
		return;
	}

	__sync_add_and_fetch(bucket_val, 1);
}

/* /sys/kernel/tracing/events/skb/consume_skb/format */
struct consume_skb_args {
	unsigned long long pad;
	void *skbaddr;
};
END_TRACKING_TRACEPOINT(consume_skb, "skb", consume_skb_args, skbaddr);

/* /sys/kernel/tracing/events/skb/kfree_skb/format */
struct kfree_skb_args {
	unsigned long long pad;
	void *skbaddr;
};
END_TRACKING_TRACEPOINT(kfree_skb, "skb", consume_skb_args, skbaddr);

/* RX */

BEGIN_TRACKING_FEXIT(napi_alloc_skb,
		     struct napi_struct *napi, unsigned int length,
		     struct sk_buff *ret);

BEGIN_TRACKING_FEXIT(napi_build_skb,
		     struct napi_struct *napi, unsigned int length,
		     struct sk_buff *ret);

/* /sys/kernel/tracing/events/net/netif_receive_skb/format */
struct netif_receive_skb_args {
	unsigned long long pad;
	void *skbaddr;
};
RECORD_TRACEPOINT(netif_receive_skb, "net", struct netif_receive_skb_args, skbaddr);

RECORD_FENTRY(napi_gro_receive,
	      struct napi_struct *napi, struct sk_buff *skb);

RECORD_FENTRY(ipv6_gro_receive,
	      struct list_head *head, struct sk_buff *skb);

RECORD_FENTRY(tcp6_gro_receive,
	      struct list_head *head, struct sk_buff *skb);

RECORD_FENTRY(tcp_gro_pull_header, struct sk_buff *skb);

RECORD_FENTRY(tcp_gro_complete, struct sk_buff *skb);

RECORD_FENTRY(ip6_rcv_core,
	      struct sk_buff *skb, struct net_device *dev, struct net *net);

RECORD_FENTRY(tcp_v6_do_rcv,
	      struct sock *sk, struct sk_buff *skb);

RECORD_FENTRY(tcp_data_queue,
	      struct sock *sk, struct sk_buff *skb);

RECORD_FENTRY(tcp_queue_rcv,
	      struct sock *sk, struct sk_buff *skb, bool *fragstolen);

RECORD_FENTRY(tcp_event_data_recv,
	      struct sock *sk, struct sk_buff *skb);

/* TX */

SEC("fexit/skb_clone")
int BPF_PROG(skb_clone, struct sk_buff *skb, gfp_t gfp_mask, struct sk_buff *ret)
{
	__u64 now = sched_clock();

	begin_tracking_clone(skb, ret, now);

	return 0;
}

BEGIN_TRACKING_FEXIT(tcp_stream_alloc_skb,
		     struct sock *sk, gfp_t gfp, bool force_schedule,
		     struct sk_buff *ret);

RECORD_FENTRY(__tcp_transmit_skb,
	      struct sock *sk, struct sk_buff *skb,
	      int clone_it, gfp_t gfp_mask, u32 rcv_nxt);

RECORD_FENTRY(ip6_xmit,
	      const struct sock *sk, struct sk_buff *skb, struct flowi6 *fl6,
	      __u32 mark, struct ipv6_txoptions *opt, int tclass, u32 priority);

/*
RECORD_FENTRY(ip6_output,
	      struct net *net, struct sock *sk, struct sk_buff *skb);

RECORD_FENTRY(ip6_finish_output,
	      struct net *net, struct sock *sk, struct sk_buff *skb);

RECORD_FENTRY(ip6_finish_output2,
	      struct net *net, struct sock *sk, struct sk_buff *skb);

RECORD_FENTRY(__dev_queue_xmit,
	      struct sk_buff *skb, struct net_device *sb_dev);
*/

/* /sys/kernel/tracing/events/net/net_dev_queue/format */
struct net_dev_queue_arg {
	unsigned long long pad;
	void *skbaddr;
};
RECORD_TRACEPOINT(net_dev_queue, "net", struct net_dev_queue_arg, skbaddr);

/* /sys/kernel/tracing/events/net/net_dev_start_xmit/format */
struct net_dev_start_xmit_args {
	unsigned long long pad;
	unsigned long long pad2;
	void *skbaddr;
};
RECORD_TRACEPOINT(net_dev_start_xmit, "net", struct net_dev_start_xmit_args, skbaddr);

RECORD_FEXIT(mlx5e_xmit,
	     struct sk_buff *skb, struct net_device *dev, netdev_tx_t ret);

char _license[] SEC("license") = "GPL";
