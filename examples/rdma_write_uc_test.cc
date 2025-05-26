/*
摸底UC，RC情况下的RDMA通信模式
1. UC情况下的消息切分方式以及报文内容
2. UC情况下丢包对端侧接收报文的影响，例如重复发送报文
*/
#include "rdma_utils.h"
#include "socket_endpoint.h"
#include <infiniband/verbs.h>
#include <glog/logging.h>
#include <ostream>
#include "get_clock.h"

void send_benchmark(struct ibv_send_wr *send_wr, struct ibv_qp *qp, struct ibv_cq *cq, int iterations, int available_wqes)
{
    struct ibv_wc wcs[32];
    int i = 0;
    while (i < iterations)
    {
        if (available_wqes == 0)
        {
            int num_completions = ibv_poll_cq(cq, 32, wcs);
            available_wqes += num_completions;
            continue;
        }
        struct ibv_send_wr *bad_wr = nullptr;
        send_wr->wr_id = i;
        int ret = ibv_post_send(qp, send_wr, &bad_wr);
        available_wqes--;
        i += 1;
    }
}

void recv_benchmark(struct ibv_recv_wr *recv_wr, struct ibv_qp *qp, struct ibv_cq *cq, int iterations)
{
    int i = 0;
    struct ibv_wc wcs[32];
    cycles_t c1,c2;
    int first_completion = 1;
    int count = 0;
    while (i < iterations)
    {
        int num_completions = ibv_poll_cq(cq, 32, wcs);
        for (int k = 0; k < num_completions; ++k)
        {
            recv_wr->wr_id = wcs[k].wr_id;
            struct ibv_recv_wr *bad_recv_wr;
            ibv_post_recv(qp, recv_wr, &bad_recv_wr);
        }
        i += num_completions;
        count += num_completions;
        if (count > 0 && first_completion)
        {
            c1 = get_cycles();
            first_completion = 0;
            count = 0;
        }
    }
    c2 = get_cycles();
    
    double mhz;
	mhz = get_cpu_mhz(0);
    double nanosec = 1e3 * (c2 - c1) / mhz;
    LOG(INFO) << ", Rx (Gbps): " << (count * 65536 * 8) / nanosec << " Gbps";
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
        struct ibv_recv_wr recv_wr;
        std::memset(&recv_wr, 0, sizeof(recv_wr));
        recv_wr.wr_id = 0;
        recv_wr.next = nullptr;
        recv_wr.sg_list = nullptr;
        recv_wr.num_sge = 0;
        struct ibv_recv_wr *bad_recv_wr;
        ret = ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
        CHECK(ret == 0) << "Failed to post receive WR";
        sock.syncReady();
        LOG(INFO) << "Step 9: Post receive WR to queue pair and then wait for completion";
        struct ibv_wc wc;
        while (ret = ibv_poll_cq(cq, 1, &wc) == 0)
        {
        };
        CHECK_EQ(wc.status, IBV_WC_SUCCESS) << "Failed to poll CQ for receive completion";
        LOG(INFO) << "Step 10: Receive completion received, status: " << ibv_wc_status_str(wc.status)
                  << ", QP number: " << wc.qp_num
                  << ", WR ID: " << wc.wr_id
                  << ", byte length: " << wc.byte_len;
        for (int i = 0; i < kSendQueueDepth; i++)
        {
            recv_wr.wr_id = i;
            struct ibv_recv_wr *bad_recv_wr = nullptr;
            ret = ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
        }
        sock.syncReady();
        recv_benchmark(&recv_wr, qp, cq, 10);
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
        struct ibv_send_wr send_wr;
        std::memset(&send_wr, 0, sizeof(send_wr));
        struct ibv_sge sge;
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
        struct ibv_send_wr *bad_wr = nullptr;
        ret = ibv_post_send(qp, &send_wr, &bad_wr);
        if (ret > 0)
        {
            perror("ibv_post_send failed with error: ");
        }
        CHECK(!bad_wr) << "Error posting send WR at WR " << bad_wr << " id 0x"
                       << std::hex << bad_wr->wr_id << " QP 0x" << qp->qp_num
                       << std::dec << ", opcode: " << send_wr.opcode;
        LOG(INFO) << "Step 9: Post send WR to queue pair with RDMA write operation";
        struct ibv_wc wc;
        while (ret = ibv_poll_cq(cq, 1, &wc) == 0)
        {
        };
        CHECK_EQ(wc.status, IBV_WC_SUCCESS) << "Failed to poll CQ for send completion";
        LOG(INFO) << "Step 10: Send completion received, status: " << ibv_wc_status_str(wc.status)
                  << ", QP number: " << wc.qp_num
                  << ", WR ID: " << wc.wr_id
                  << ", byte length: " << wc.byte_len;
        sock.syncReady();
        send_benchmark(&send_wr, qp, cq, 10, kSendQueueDepth);
    }

    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    free(buffer);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    return 0;
}