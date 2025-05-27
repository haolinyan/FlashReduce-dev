/*
LD_LIBRARY_PATH=/usr/local/grpc/lib ./build/examples/grpc_barrier_test 
*/
#include "grpc_client.h"


int main(int argc, char** argv) {
    // 创建与gRPC服务器通信的通道
    std::string ip = "localhost";
    std::string port = "50099";
    gRPCClient client(ip, port);
    // 设置工作节点数量
    uint32_t num_workers = 2;
    // 执行Barrier操作
    bool result = client.Barrier(num_workers);
    return 0;
}