#include "grpc_client.h"
#include "rdma_utils.h"

gRPCClient::gRPCClient(std::string ip, std::string port)
{
    std::string controller_socket = ip + ":" + port;
    stub_ = Sync::NewStub(grpc::CreateChannel(controller_socket, grpc::InsecureChannelCredentials()));
    session_stub_ = Session::NewStub(grpc::CreateChannel(controller_socket, grpc::InsecureChannelCredentials()));
    LOG(INFO) << "gRPCClient initialized with address: " << controller_socket;
}

void gRPCClient::RdmaSession(uint32_t session_id, 
                            uint32_t rank, 
                            uint32_t num_workers, 
                            uint32_t root, 
                            uint64_t mac,
                            uint32_t ipv4,
                            std::vector<QpInfo> &local_qp_infos, 
                            std::vector<QpInfo> &remote_qp_infos) {
    RdmaSessionRequest request;
    request.set_session_id(session_id);
    request.set_rank(rank);
    request.set_num_workers(num_workers);
    request.set_root(root);
    request.set_mac(mac);
    request.set_ipv4(ipv4);
    struct QpInfo local_qp_info = local_qp_infos[0];
    request.set_rkey(local_qp_info.rkey);
    request.set_raddr((uint64_t)local_qp_info.raddr);
    request.set_qpn(local_qp_info.qp_num);
    request.set_psn(local_qp_info.psn);
    request.set_gid_subnet(local_qp_info.gid.global.subnet_prefix);
    request.set_gid_iface(local_qp_info.gid.global.interface_id);
    request.set_lid((uint32_t)local_qp_info.lid);
    ClientContext context;
    RdmaSessionResponse response;
    Status status = session_stub_->RdmaSession(&context, request, &response);
    CHECK(status.ok()) << "RdmaSession failed: " << status.error_code()
                       << ": " << status.error_message();
    struct QpInfo remote_qp_info;
    remote_qp_info.rkey = response.rkey();
    remote_qp_info.raddr = reinterpret_cast<void *>(response.raddr());
    remote_qp_info.qp_num = response.qpn();
    remote_qp_info.psn = response.psn();
    remote_qp_info.gid.global.subnet_prefix = response.gid_subnet();
    remote_qp_info.gid.global.interface_id = response.gid_iface();
    remote_qp_info.lid = (uint16_t)response.lid();
    remote_qp_infos.push_back(remote_qp_info);
}


bool gRPCClient::Barrier(uint32_t num_workers)
{
    BarrierRequest request;
    request.set_num_workers(num_workers);

    BarrierResponse response;
    ClientContext context;

    Status status = stub_->Barrier(&context, request, &response);

    CHECK(status.ok()) << "Barrier failed: " << status.error_code()
                       << ": " << status.error_message();

    LOG(INFO) << "Barrier successful";
    return true;
}

uint64_t gRPCClient::Broadcast(uint64_t value, uint32_t rank, uint32_t num_workers, uint32_t root)
{
    CHECK_GT(num_workers, 0) << "Number of workers must be greater than 0";
    CHECK_LT(rank, num_workers) << "Rank must be less than number of workers";
    CHECK_LT(root, num_workers) << "Root rank must be less than number of workers";

    BroadcastRequest request;
    request.set_value(value);
    request.set_rank(rank);
    request.set_num_workers(num_workers);
    request.set_root(root);

    BroadcastResponse response;
    ClientContext context;

    Status status = stub_->Broadcast(&context, request, &response);

    CHECK(status.ok()) << "Broadcast failed: " << status.error_code()
                       << ": " << status.error_message();

    LOG(INFO) << "Broadcast successful from rank " << rank
              << " with value " << value;
    return response.value();
}