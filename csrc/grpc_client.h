#pragma once
#include <grpcpp/grpcpp.h>
#include "flashreduce.pb.h"
#include "flashreduce.grpc.pb.h"
#include <glog/logging.h>
#include <memory>
#include <string>

using flashreduce_proto::BarrierRequest;
using flashreduce_proto::BarrierResponse;
using flashreduce_proto::BroadcastRequest;
using flashreduce_proto::BroadcastResponse;
using flashreduce_proto::RdmaSessionRequest;
using flashreduce_proto::RdmaSessionResponse;
using flashreduce_proto::Session;
using flashreduce_proto::Sync;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using flashreduce_proto::Sync;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
struct QpInfo;
class gRPCClient
{
public:
    gRPCClient(std::string ip, std::string port);

    bool Barrier(uint32_t num_workers);

    uint64_t Broadcast(uint64_t value, uint32_t rank, uint32_t num_workers, uint32_t root);

    void RdmaSession(uint32_t session_id, 
                            uint32_t rank, 
                            uint32_t num_workers, 
                            uint32_t root, 
                            uint64_t mac,
                            uint32_t ipv4,
                            std::vector<QpInfo> &local_qp_infos, 
                            std::vector<QpInfo> &remote_qp_infos);

private:
    std::unique_ptr<Sync::Stub> stub_;
    std::unique_ptr<Session::Stub> session_stub_;
};