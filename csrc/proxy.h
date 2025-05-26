#pragma once
#include "get_clock.h"
#include <pthread.h>
#include <cstdint>
#include <infiniband/verbs.h>
struct RDMAEndpoint {
    struct ibv_cq* cq;
    int available_wqes;
    struct ibv_qp* qp;
    struct ibv_recv_wr recv_wr;
    struct ibv_sge sge;
    struct ibv_send_wr send_wr;
};

enum ProxyOpState {
    ProxyOpNone,
    ProxyOpReady,
    ProxyOpProgress,
};
struct ProxyArgs;
typedef void (*proxyProgressFunc_t)(struct ProxyArgs*);
struct ProxyArgs {
    bool idle;
    enum ProxyOpState state;
    proxyProgressFunc_t progress;
    ProxyArgs** proxyTail;
    struct ProxyArgs* next;
    struct ProxyArgs* nextPeer;
    struct RDMAEndpoint endpoint;
    int iterations;
    cycles_t startTick;
    cycles_t endTick;
    int first_completion;
    int count;
};
struct ProxyHandler {
    pthread_t proxyThread;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    ProxyArgs* ops;
    bool stop;
    uint32_t* abortFlag;
    struct ProxyArgs* pool;
    struct ProxyPool* pools;
};

void ProxyCreate(struct ProxyHandler* handler);
struct ProxyArgs* allocateArgs(struct ProxyHandler* handler);
void ProxyArgsAppend(struct ProxyHandler* handler, struct ProxyArgs* args);
void ProxyStart(struct ProxyHandler* handler);
void ProxyDestroy(struct ProxyHandler* handler);
void ProxyWaitAllOpFinished(ProxyHandler* handler);
