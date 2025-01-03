LINUX=$(HOME)/src/linux
VMLINUX=$(LINUX)/vmlinux
# TODO: use system libbpf
LIBBPF=$(LINUX)/tools/lib
TOOLS=$(LINUX)/tools/testing/selftests/net/tools/include

#TRACK_US=250
TRACK_US=500

CFLAGS=-DTRACK_US=$(TRACK_US)

all: skb_latency

vmlinux.h:
	bpftool btf dump file $(VMLINUX) format c > $@

skb_latency.bpf.o: skb_latency.bpf.c vmlinux.h
	clang -g -O2 $(CFLAGS) -I$(PWD) -I$(TOOLS) -I$(LIBBPF) --target=bpf -mcpu=v4 -c $< -o $@

skb_latency.skel.h: skb_latency.bpf.o
	bpftool gen skeleton $< name skb_latency > $@

skb_latency: skb_latency.c skb_latency.skel.h
	clang -L$(LIBBPF)/bpf -I$(TOOLS) $(CFLAGS) -lbpf -lelf -lz $< -o $@

clean:
	rm -f skb_latency.bpf.o skb_latency.skel.h skb_latency

run:
	./skb_latency | ./plot
