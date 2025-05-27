#include "proxy.h"
#include <stdio.h>
#include <unistd.h>
#include "rdma_utils.h"
#include "socket_endpoint.h"
#include <infiniband/verbs.h>
#include <ostream>
#include <cstring>
#include "get_clock.h"

void send_benchmark(ProxyArgs *args)
{
    if (args->state == ProxyOpReady)
    {
        args->state = ProxyOpProgress;
    }
    
    if (args->state == ProxyOpProgress && args->iterations < 10)
    {
        args->idle = 0;
        args->iterations += 1;
        if (args->endpoint.available_wqes == 0)
        {
            struct ibv_wc wcs[32];
            int num_completions = ibv_poll_cq(args->endpoint.cq, 32, wcs);
            args->endpoint.available_wqes += num_completions;
            return;
        }
        struct ibv_send_wr *bad_wr = nullptr;
        args->endpoint.send_wr.wr_id = args->iterations;
        int ret = ibv_post_send(args->endpoint.qp, &args->endpoint.send_wr, &bad_wr);
        args->endpoint.available_wqes--;
    }
    if (args->iterations == 10) {
        args->state = ProxyOpNone;
        args->idle = 0;
    }    
}

void recv_benchmark(ProxyArgs *args)
{
    if (args->state == ProxyOpReady)
    {
        args->state = ProxyOpProgress;
        args->first_completion = 1;
        args->count = 0;
    }
    
    if (args->state == ProxyOpProgress && args->iterations < 10)
    {
        args->idle = 0;
        struct ibv_wc wcs[32];
        int num_completions = ibv_poll_cq(args->endpoint.cq, 32, wcs);
        if (num_completions) args->iterations += 1;
        args->count += num_completions;
        for (int k = 0; k < num_completions; ++k)
        {
            args->endpoint.recv_wr.wr_id = wcs[k].wr_id;
            struct ibv_recv_wr *bad_recv_wr = nullptr;
            ibv_post_recv(args->endpoint.qp, &args->endpoint.recv_wr, &bad_recv_wr);
        }
        if (args->count > 0 && args->first_completion)
        {
            args->startTick = get_cycles();
            args->first_completion = 0;
            args->count = 0;
        }
    }
    if (args->iterations == 10) {
        args->state = ProxyOpNone;
        args->idle = 0;
        args->endTick = get_cycles();
        double mhz = get_cpu_mhz(0);
        double nanosec = 1e3 * (args->endTick - args->startTick) / mhz;
        LOG(INFO) << ", Rx (Gbps): " << (args->count * 65536 * 8) / nanosec << " Gbps";
    }    
}

int main(int argc, char **argv)
{
    FLAGS_alsologtostderr = 1;
    google::InitGoogleLogging(argv[0]);
    int server = atoi(argv[1]);
    int msg_numel = 16384;
    ibv_mtu mtu = IBV_MTU_256; // Use 1024 MTU for this example
    const int kCompletionQueueDepth = 1024;
    const int kSendQueueDepth = 256;
    const int kReceiveQueueDepth = 512;
    const int kScatterGatherElementCount = 1;
    const int kMaxInlineData = 16;
    ProxyHandler handler;
    uint32_t abort = 0;
    handler.abortFlag = &abort;
    ProxyCreate(&handler);
    ProxyArgs *args = allocateArgs(&handler);
    args->state = ProxyOpReady;
    ProxyArgs *channelProxyTail = nullptr;
     args->proxyTail = &channelProxyTail;
    ibv_device *device = nullptr;
    ibv_context *context = nullptr;
    init_ibv_device(&device, &context, "mlx5_0");
    struct ibv_port_attr port_attr;
    std::memset(&port_attr, 0, sizeof(port_attr));
    CHECK(ibv_query_port(context, IB_PORT, &port_attr) == 0) << "Failed to query port attributes";
    ibv_gid gid;
    CHECK(ibv_query_gid(context, IB_PORT, GID_INDEX, &gid) == 0) << "Failed to query GID";
    LOG(INFO) << "Step 1: Initialize RDMA device " << ibv_get_device_name(device)
              << ", open device context, query port attributes such as GID: "
              << std::hex << gid.global.subnet_prefix << gid.global.interface_id << std::dec;

    struct ibv_pd *pd = ibv_alloc_pd(context);
    CHECK(pd) << "Failed to allocate protection domain";
    LOG(INFO) << "Step 2: Allocate protection domain";

    int buffer_size = msg_numel * sizeof(float);
    void *buffer = malloc(buffer_size);
    CHECK(buffer) << "Failed to allocate buffer of size " << buffer_size;
    struct ibv_mr *mr = ibv_reg_mr(pd, buffer, buffer_size, (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_ZERO_BASED));
    CHECK(mr) << "Failed to register memory region";
    LOG(INFO) << "Step 3: Allocate and register memory region of size " << buffer_size
              << " with local write and remote write access";

    struct ibv_cq *cq = ibv_create_cq(context, kCompletionQueueDepth, nullptr, nullptr, 0);
    CHECK(cq) << "Failed to create completion queue";
    LOG(INFO) << "Step 4: Create completion queue with depth " << kCompletionQueueDepth;

    struct ibv_qp_init_attr init_attributes;
    std::memset(&init_attributes, 0, sizeof(init_attributes));
    init_attributes.send_cq = cq;
    init_attributes.recv_cq = cq;
    init_attributes.qp_type = IBV_QPT_UC;
    init_attributes.cap.max_send_wr = kSendQueueDepth;
    init_attributes.cap.max_recv_wr = kReceiveQueueDepth;
    init_attributes.cap.max_send_sge = kScatterGatherElementCount;
    init_attributes.cap.max_recv_sge = kScatterGatherElementCount;
    init_attributes.cap.max_inline_data = kMaxInlineData;
    struct ibv_qp *qp = ibv_create_qp(pd, &init_attributes);
    CHECK(qp) << "Failed to create queue pair";
    LOG(INFO) << "Step 5: Create queue pair with send/receive CQ, type UC, and specified capabilities";

    args->endpoint.cq = cq;
    args->endpoint.qp = qp;
    args->iterations = 0;

    int ret = modify_qp_to_init(qp);
    CHECK(ret == 0) << "Failed to modify QP to INIT state";
    LOG(INFO) << "Step 6: Modify QP to INIT state with port number, PKey index, and access flags";
    // 交换gid, rkey, qp_num, psn
    struct QpInfo qp_info, neighbor_qp_info;
    qp_info.rkey = mr->rkey;
    qp_info.raddr = buffer;
    qp_info.qp_num = qp->qp_num;
    qp_info.psn = 0;
    qp_info.gid = gid;
    qp_info.lid = port_attr.lid;
    if (server)
    {
        SocketEndpoint sock(12345);
        sock.syncData(sizeof(QpInfo), &qp_info, &neighbor_qp_info);
        LOG(INFO) << "Step 7: Server side, exchange QP info with client";
        LOG(INFO) << "        Server QP Info: rkey=" << qp_info.rkey
                  << ", qp_num=" << qp_info.qp_num
                  << ", psn=" << qp_info.psn
                  << ", gid=" << std::hex << qp_info.gid.global.subnet_prefix
                  << qp_info.gid.global.interface_id << std::dec
                  << ", lid=" << qp_info.lid;
        LOG(INFO) << "        Neighbor QP Info: rkey=" << neighbor_qp_info.rkey
                  << ", qp_num=" << neighbor_qp_info.qp_num
                  << ", psn=" << neighbor_qp_info.psn
                  << ", gid=" << std::hex << neighbor_qp_info.gid.global.subnet_prefix
                  << neighbor_qp_info.gid.global.interface_id << std::dec
                  << ", lid=" << neighbor_qp_info.lid;
        ret = modify_qp_to_rts(qp, neighbor_qp_info, mtu, 0);
        CHECK(ret >= 0) << "Failed to modify QP to RTS state";
        LOG(INFO) << "Step 8: Modify QP to RTS state with initial PSN and other parameters";
        std::memset(&args->endpoint.recv_wr, 0, sizeof(args->endpoint.recv_wr));
        struct ibv_recv_wr &recv_wr = args->endpoint.recv_wr;
        recv_wr.wr_id = 0;
        recv_wr.next = nullptr;
        recv_wr.sg_list = nullptr;
        recv_wr.num_sge = 0;
        for (int i = 0; i < kSendQueueDepth; i++)
        {
            recv_wr.wr_id = i;
            struct ibv_recv_wr *bad_recv_wr = nullptr;
            ret = ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
        }
        args->endpoint.available_wqes = kReceiveQueueDepth;
        args->progress = recv_benchmark;
        sock.syncReady();
        ProxyArgsAppend(&handler, args);
    }
    else
    {
        SocketEndpoint sock("localhost", 12345);
        sock.syncData(sizeof(QpInfo), &qp_info, &neighbor_qp_info);
        LOG(INFO) << "Step 7: Client side, exchange QP info with server";
        LOG(INFO) << "        Client QP Info: rkey=" << qp_info.rkey
                  << ", qp_num=" << qp_info.qp_num
                  << ", psn=" << qp_info.psn
                  << ", gid=" << std::hex << qp_info.gid.global.subnet_prefix
                  << qp_info.gid.global.interface_id << std::dec
                  << ", lid=" << qp_info.lid;
        LOG(INFO) << "        Neighbor QP Info: rkey=" << neighbor_qp_info.rkey
                  << ", qp_num=" << neighbor_qp_info.qp_num
                  << ", psn=" << neighbor_qp_info.psn
                  << ", gid=" << std::hex << neighbor_qp_info.gid.global.subnet_prefix
                  << neighbor_qp_info.gid.global.interface_id << std::dec
                  << ", lid=" << neighbor_qp_info.lid;
        ret = modify_qp_to_rts(qp, neighbor_qp_info, mtu, 0);
        CHECK(ret >= 0) << "Failed to modify QP to RTS state";
        LOG(INFO) << "Step 8: Modify QP to RTS state with initial PSN and other parameters";
        sock.syncReady();
        std::memset(&args->endpoint.send_wr, 0, sizeof(args->endpoint.send_wr));
        struct ibv_send_wr &send_wr = args->endpoint.send_wr;
        struct ibv_sge &sge = args->endpoint.sge;
        std::memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)buffer;
        sge.length = buffer_size;
        sge.lkey = mr->lkey;
        send_wr.wr_id = 0;
        send_wr.next = nullptr;
        send_wr.sg_list = &sge;
        send_wr.num_sge = 1;
        send_wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM; // Use RDMA write with immediate
        send_wr.send_flags = IBV_SEND_SIGNALED;
        send_wr.wr.rdma.remote_addr = (uint64_t)neighbor_qp_info.raddr; // Remote address to write to
        send_wr.wr.rdma.rkey = neighbor_qp_info.rkey;                   // Remote key for the memory region
        send_wr.imm_data = 0;
        args->endpoint.available_wqes = kSendQueueDepth;
        args->progress = send_benchmark;
        sock.syncReady();
        ProxyArgsAppend(&handler, args);
    }
    ProxyStart(&handler);
    ProxyWaitAllOpFinished(&handler);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    free(buffer);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    ProxyDestroy(&handler);
    return 0;
}