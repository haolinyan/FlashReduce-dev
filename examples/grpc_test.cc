/*
LD_LIBRARY_PATH=/usr/local/grpc/lib ./build/examples/grpc_test 
*/
#include "grpc_client.h"
#include "rdma_utils.h"

int main(int argc, char** argv) {
    uint32_t rank = atoi(argv[1]);
    // 创建与gRPC服务器通信的通道
    std::string ip = "localhost";
    std::string port = "8934";
    gRPCClient client(ip, port);
    // 设置工作节点数量
    uint32_t num_workers = 2;
    // 执行Barrier操作
    bool result = client.Barrier(num_workers);
    // 执行broadcast操作
    uint64_t value = 42;
    uint32_t root = 0;
    uint64_t broadcasted_value = client.Broadcast(value, rank, num_workers, root);

    ibv_device *device = nullptr;
    ibv_context *context = nullptr;
    init_ibv_device(&device, &context, "mlx5_7");
    struct ibv_port_attr port_attr;
    std::memset(&port_attr, 0, sizeof(port_attr));
    CHECK(ibv_query_port(context, IB_PORT, &port_attr) == 0) << "Failed to query port attributes";
    ibv_gid gid;
    CHECK(ibv_query_gid(context, IB_PORT, GID_INDEX, &gid) == 0) << "Failed to query GID";
    std::vector<QpInfo> local_qp_infos, remote_qp_infos;
    struct QpInfo qp_info;
    qp_info.rkey = 102 + rank;
    qp_info.raddr = (void*)(0x1000 + rank * 0x100);
    qp_info.qp_num = 200 + rank;
    qp_info.psn = 300 + rank;
    qp_info.lid = 400 + rank;
    qp_info.gid = gid;
    LOG(INFO) << "Local QP Info: rkey=" << qp_info.rkey
              << ", raddr=" << qp_info.raddr
              << ", qp_num=" << qp_info.qp_num
              << ", psn=" << qp_info.psn
              << ", gid=" << qp_info.gid.global.subnet_prefix << ":" << qp_info.gid.global.interface_id
              << ", lid=" << qp_info.lid;
    local_qp_infos.push_back(qp_info);
    client.RdmaSession(1, rank, num_workers, rank, 0, 0, local_qp_infos, remote_qp_infos);
    LOG(INFO) << "Remote QP Info: rkey=" << remote_qp_infos[0].rkey
              << ", raddr=" << remote_qp_infos[0].raddr
              << ", qp_num=" << remote_qp_infos[0].qp_num
              << ", psn=" << remote_qp_infos[0].psn
              << ", gid=" << remote_qp_infos[0].gid.global.subnet_prefix << ":" << remote_qp_infos[0].gid.global.interface_id
              << ", lid=" << remote_qp_infos[0].lid;
    ibv_close_device(context);
    return 0;
}