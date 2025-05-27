#pragma once
#include <grpcpp/grpcpp.h>
#include "flashreduce.pb.h"
#include "flashreduce.grpc.pb.h"
#include <glog/logging.h>
#include <memory>
#include <string>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using flashreduce_proto::BarrierRequest;
using flashreduce_proto::BarrierResponse;
using flashreduce_proto::BroadcastRequest;
using flashreduce_proto::BroadcastResponse;
using flashreduce_proto::Sync;


class gRPCClient {
public:
    gRPCClient(std::string ip, std::string port) {
        std::string controller_socket = ip + ":" + port;
        stub_ = Sync::NewStub(grpc::CreateChannel(controller_socket, grpc::InsecureChannelCredentials()));
        LOG(INFO) << "gRPCClient initialized with address: " << controller_socket;
    }

    bool Barrier(uint32_t num_workers) {
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

    uint64_t Broadcast(uint64_t value, uint32_t rank, uint32_t num_workers, uint32_t root) {
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

private:
    std::unique_ptr<Sync::Stub> stub_;
};    