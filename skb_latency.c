#include <error.h>
#include <unistd.h>
#include <bpf/bpf.h>

#include "skb_latency.skel.h"

static int oob_tolerance = 100;

static void dump_map(struct bpf_map *evt, struct bpf_map *oob, const char *name)
{
	int key = 0;
	int val = 0;

	printf("%s ", name);

	bpf_map__lookup_elem(oob, &key, sizeof(key), &val, sizeof(val), 0);
	if (val > oob_tolerance)
		fprintf(stderr, "%s oob:%d\n", name, val);

	for (int i = 0; i < TRACK_US; i++) {
		int key = i;
		int val = 0;

		bpf_map__lookup_elem(evt, &key, sizeof(key), &val, sizeof(val), 0);

		printf("%d ", val);
	}
	printf("\n");
}

#define DUMP_MAP(dir, name) \
	dump_map(obj->maps.evt_##name, obj->maps.oob_##name, #dir":"#name);

int main(int argc, char *argv[])
{
	struct skb_latency *obj;
	int scale = 1000;
	int delay = 2;
	bool gro = true;
	bool tx = true;
	bool rx = true;
	int opt;

	while ((opt = getopt(argc, argv, "d:Go:rs:t")) != -1) {
		switch (opt) {
		case 'd':
			delay = atoi(optarg);
			break;
		case 'G':
			gro = false;
			break;
		case 'o':
			oob_tolerance = atoi(optarg);
			break;
		case 'r':
			tx = false;
			break;
		case 's':
			scale = atoi(optarg);
			break;
		case 't':
			rx = false;
			break;
		default:
			fprintf(stderr, "unknown flag '%c'\n", opt);
			exit(EXIT_FAILURE);
		}
	}

	obj = skb_latency__open_and_load();
	if (!obj)
		error(1, libbpf_get_error(obj), "open_and_load");

	obj->bss->scale = scale;

	if (skb_latency__attach(obj) < 0)
		error(1, libbpf_get_error(obj), "attach");

	sleep(delay);

	if (obj->bss->missed_skb)
		fprintf(stderr, "missed %d skbs\n", obj->bss->missed_skb);

	if (rx) {
		DUMP_MAP(rx, napi_gro_receive);
		if (gro) {
			DUMP_MAP(rx, ipv6_gro_receive);
			DUMP_MAP(rx, tcp6_gro_receive);
			DUMP_MAP(rx, tcp_gro_pull_header);
			DUMP_MAP(rx, tcp_gro_complete);
		}
		DUMP_MAP(rx, netif_receive_skb);
		DUMP_MAP(rx, ip6_rcv_core);
		DUMP_MAP(rx, tcp_v6_do_rcv);
		DUMP_MAP(rx, tcp_data_queue);
		DUMP_MAP(rx, tcp_queue_rcv);
		DUMP_MAP(rx, tcp_event_data_recv);
	}


	if (tx) {
		DUMP_MAP(tx, __tcp_transmit_skb);
		DUMP_MAP(tx, ip6_xmit);
		/*
		DUMP_MAP(tx, ip6_output);
		DUMP_MAP(tx, ip6_finish_output);
		DUMP_MAP(tx, ip6_finish_output2);
		DUMP_MAP(tx, __dev_queue_xmit);
		*/
		DUMP_MAP(tx, net_dev_queue);
		DUMP_MAP(tx, net_dev_start_xmit);
		DUMP_MAP(tx, mlx5e_xmit);
	}

	skb_latency__detach(obj);
	skb_latency__destroy(obj);

	return 0;
}
