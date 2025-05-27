# FlashReduce-dev
## 环境配置
1. grpc
python
pip install grpcio         # gRPC基础运行库
pip install grpcio-tools   # gRPC协议编译工具




2. proto
## 编译client库
```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local/grpc
cmake --build build
```