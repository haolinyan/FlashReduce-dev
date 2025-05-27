#pragma once
#include <grpcpp/grpcpp.h>
#include "flashreduce.pb.h"
#include "flashreduce.grpc.pb.h"
#include <iostream>
#include <memory>
#include <string>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using flashreduce_proto::BarrierRequest;
using flashreduce_proto::BarrierResponse;
using flashreduce_proto::Sync;


class gRPCClient {
public:
    gRPCClient(std::string ip, std::string port) {
        std::string controller_socket = ip + ":" + port;
        stub_ = Sync::NewStub(grpc::CreateChannel(controller_socket, grpc::InsecureChannelCredentials()));
    }

    bool Barrier(uint32_t num_workers) {
        BarrierRequest request;
        request.set_num_workers(num_workers);

        BarrierResponse response;
        ClientContext context;

        Status status = stub_->Barrier(&context, request, &response);

        if (status.ok()) {
            std::cout << "Barrier successful" << std::endl;
            return true;
        } else {
            std::cout << "Barrier failed: " << status.error_code() << ": " << status.error_message()
                      << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<Sync::Stub> stub_;
};