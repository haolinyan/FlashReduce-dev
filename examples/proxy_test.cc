#include "proxy.h"
#include <stdio.h>
#include <unistd.h>
#include <glog/logging.h>
// 示例任务执行函数
void exampleTask(ProxyArgs *args)
{
    printf("Executing example task...\n");
    printf("Example task done.\n");
    args->state = ProxyOpNone;
    args->idle = 0;
}

int main(int argc, char **argv)
{
    FLAGS_alsologtostderr = 1;
    google::InitGoogleLogging(argv[0]);
    ProxyHandler handler;
    // 初始化 abortFlag
    uint32_t abort = 0;
    handler.abortFlag = &abort;
    // 创建代理执行线程
    ProxyCreate(&handler);

    // 分配任务结构体
    ProxyArgs *args = allocateArgs(&handler);

    // 填充任务结构体
    args->state = ProxyOpReady;
    args->progress = exampleTask;
    // 假设这里有一个 channel 的 proxyTail 指针
    ProxyArgs *channelProxyTail = nullptr;
    args->proxyTail = &channelProxyTail;

    // 将任务添加到任务队列
    ProxyArgsAppend(&handler, args);

    // 唤醒代理执行线程
    ProxyStart(&handler);

    // 等待一段时间，让任务有机会执行
    // 这里可以使用更合适的同步机制
    sleep(2);

    ProxyDestroy(&handler);

    return 0;
}