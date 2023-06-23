// #include "http.c"
#include "../../headers/bpf.h"
#include "../../headers/common.h"
#include "../../headers/l7_req.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define PROTOCOL_UNKNOWN    0
#define PROTOCOL_HTTP	    1

#define METHOD_UNKNOWN      0
#define METHOD_GET          1


#define MAX_PAYLOAD_SIZE 512

char __license[] SEC("license") = "Dual MIT/GPL";

struct l7_event {
    __u64 fd;
    __u32 pid;
    __u32 status;
    __u64 duration;
    __u8 protocol;
    __u8 method;
    __u16 padding;
    unsigned char payload[MAX_PAYLOAD_SIZE];
};

struct l7_request {
    __u64 write_time_ns;  
    __u8 protocol;
    __u8 method;
    unsigned char payload[MAX_PAYLOAD_SIZE];
};

struct socket_key {
    __u64 fd;
    __u32 pid;
};

// Instead of allocating on bpf stack, we allocate on a per-CPU array map
struct {
     __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
     __type(key, __u32);
     __type(value, struct l7_event);
     __uint(max_entries, 1);
} l7_event_heap SEC(".maps");

struct {
     __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
     __type(key, __u32);
     __type(value, struct l7_request);
     __uint(max_entries, 1);
} l7_request_heap SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 32768);
    __type(key, struct socket_key);
    __type(value, struct l7_request);
} active_l7_requests SEC(".maps");


// send l7 events to userspace
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} l7_events SEC(".maps");


// when given with __type macro below
// type *btf.Pointer not supported
struct read_args {
    __u64 fd;
    char* buf;
    __u64 size;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u64);
    __uint(value_size, sizeof(struct read_args));
    __uint(max_entries, 10240);
} active_reads SEC(".maps");


// After socket creation and connection establishment, the kernel will call the
// write function of the socket's protocol handler to send data to the remote
// peer. The kernel will call the read function of the socket's protocol handler
// to receive data from the remote peer.

// Flow:
// 1. sys_enter_write
    // -- TODO: check if write was successful (return value), sys_exit_write ?
// 2. sys_enter_read
// 3. sys_exit_read


SEC("tracepoint/syscalls/sys_enter_write")
int sys_enter_write(struct trace_event_raw_sys_enter_write* ctx) {
    __u64 id = bpf_get_current_pid_tgid();

    int zero = 0;
    struct l7_request *req = bpf_map_lookup_elem(&l7_request_heap, &zero);

    if (!req) {
        char msg[] = "Err: Could not get request from l7_request_heap";
        bpf_trace_printk(msg, sizeof(msg));
        return 0;
    }

    req->protocol = PROTOCOL_UNKNOWN;
    req->write_time_ns = bpf_ktime_get_ns();

    struct socket_key k = {};
    k.pid = id >> 32;
    k.fd = ctx->fd;

    if(ctx->buf){
        char buf_prefix[16];
        long r = bpf_probe_read(&buf_prefix, sizeof(buf_prefix), (void *)(ctx->buf)) ;
        
        if (r < 0) {
            char msg[] = "could not read into buf_prefix - %ld";
            bpf_trace_printk(msg, sizeof(msg), r);
            return 0;
        }

        // TODO: get all types of http requests
        if (!(buf_prefix[0] == 'G' && buf_prefix[1] == 'E' && buf_prefix[2] == 'T')) {
            return 0; // TODO: only allow GET requests for now
        }else{
            // char msg[] = "GET request";
            // bpf_trace_printk(msg, sizeof(msg));
        }
    }else{
        char msgCtx[] = "write buffer is null";
        bpf_trace_printk(msgCtx, sizeof(msgCtx));
        return 0;
    }
    // buffer starts with GET
    req->protocol = PROTOCOL_HTTP;
    req->method = METHOD_GET;
    
    // copy request payload
    bpf_probe_read(&req->payload, sizeof(req->payload), (const void *)ctx->buf);
    
    long res = bpf_map_update_elem(&active_l7_requests, &k, req, BPF_ANY);
    if(res < 0)
    {
		char msg[] = "Error writing to active_l7_requests - %ld";
		bpf_trace_printk(msg, sizeof(msg), res);
    }

    return 0;
}


SEC("tracepoint/syscalls/sys_enter_read")
int sys_enter_read(struct trace_event_raw_sys_enter_read* ctx) {
    __u64 id = bpf_get_current_pid_tgid();
    
    struct socket_key k = {};
    k.pid = id >> 32;
    k.fd = ctx->fd;

    // assume process is reading from the same socket it wrote to
    void* active_req = bpf_map_lookup_elem(&active_l7_requests, &k);
    if(!active_req) // if not found
    {
        return 0;
    }
    
    struct read_args args = {};
    args.fd = ctx->fd;
    args.buf = ctx->buf;
    args.size = ctx->count;

    bpf_map_update_elem(&active_reads, &id, &args, BPF_ANY);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_read")
int sys_exit_read(struct trace_event_raw_sys_exit_read* ctx) {
    if (ctx->ret <= 0) { // read failed
        return 0; // TODO: failure handling, timeout etc.
        // TODO: delete from active_reads ??
        // TODO: delete from active_l7_requests ??
    }

    __u64 id = bpf_get_current_pid_tgid();
    struct read_args *read_info = bpf_map_lookup_elem(&active_reads, &id);
    if (!read_info) {
        return 0;
    }
    
    struct socket_key k = {};
    k.pid = id >> 32;
    k.fd = read_info->fd; 


    struct l7_request *active_req = bpf_map_lookup_elem(&active_l7_requests, &k);
    if (!active_req) {
        return 0;
    }

    bpf_map_delete_elem(&active_reads, &id);
    bpf_map_delete_elem(&active_l7_requests, &k);

    // Instead of allocating on bpf stack, use cpu map
    int zero = 0;
    struct l7_event *e = bpf_map_lookup_elem(&l7_event_heap, &zero);
    if (!e) {
        return 0;
    }

    e->fd = k.fd;
    e->pid = k.pid;
    

    e->method = active_req->method;


    // copy req payload
    bpf_probe_read(e->payload, MAX_PAYLOAD_SIZE, active_req->payload);

    e->protocol = active_req->protocol;
    e->duration = bpf_ktime_get_ns() - active_req->write_time_ns;
    

    // TODO: get status from buffer
    // char *buf = args->buf;
    // __u64 size = args->size;   

    // if (e->protocol == PROTOCOL_HTTP) {
    //     e->status = parse_http_status(buf);
    // } 
       
    bpf_perf_event_output(ctx, &l7_events, BPF_F_CURRENT_CPU, e, sizeof(*e));
    return 0;
}