#include "proxy.h"
#include "common.h"
#define PROXYARGS_ALLOCATE_SIZE 32

/**
 * @struct ProxyPool
 * @ingroup ProxyModule
 * @brief 用于管理 ProxyArgs 对象池的结构体。
 */
struct ProxyPool {
    /** 指向下一个 ProxyPool 的指针 */
    struct ProxyPool* next;
    /** 存储 ProxyArgs 对象的数组 */
    struct ProxyArgs elems[PROXYARGS_ALLOCATE_SIZE];
};

/**
 * @brief 代理执行线程的主函数。
 * @ingroup ProxyModule
 *
 * 该函数会不断轮询任务队列，执行队列中的任务。当队列为空时，线程会休眠，直到被唤醒。
 * 若所有任务都处于等待状态且超过一定轮询次数，线程会主动放弃 CPU 使用权。
 *
 * @param handler_ 指向 ProxyHandler 结构体的指针。
 * @return 始终返回 NULL。
 */
static void* persistentThread(void* handler_) {
    ProxyHandler* handler = (ProxyHandler*)handler_;
    int idle = 1;
    int idleSpin = 0;
    ProxyArgs* op = NULL;
    while (1) {
        do {
            if (*handler->abortFlag)
                return NULL;
            if (op == NULL) {
                pthread_mutex_lock(&handler->mutex);
                op = handler->ops;
                if (op == NULL) {
                    if (handler->stop) {
                        pthread_mutex_unlock(&handler->mutex);
                        return NULL;
                    }
                    pthread_cond_wait(&handler->cond, &handler->mutex);
                }
                pthread_mutex_unlock(&handler->mutex);
            }
        } while (op == NULL);
        op->idle = 0;
        if (op->state != ProxyOpNone) {
            op->progress(op);
        }
        idle &= op->idle;
        pthread_mutex_lock(&handler->mutex);
        if (!idle)
            idleSpin = 0;
        ProxyArgs* next = op->next;
        if (next->state == ProxyOpNone) {
            ProxyArgs* freeOp = next;
            if (next->nextPeer) {
                next = next->nextPeer;
                if (op != freeOp) {
                    next->next = freeOp->next;
                    op->next = next;
                } else {
                    next->next = next;
                }
            } else {
                *(next->proxyTail) = NULL;
                if (op != freeOp) {
                    next = next->next;
                    op->next = next;
                } else {
                    next = NULL;
                }
            }
            if (freeOp == handler->ops)
                handler->ops = next;
            freeOp->next = handler->pool;
            handler->pool = freeOp;
        }
        op = next;
        if (op == handler->ops) {
            if (idle == 1) {
                if (++idleSpin == 10) {
                    sched_yield();
                    idleSpin = 0;
                }
            }
            idle = 1;
        }
        pthread_mutex_unlock(&handler->mutex);
    }
}

/**
 * @brief 创建代理执行线程。
 * @ingroup ProxyModule
 *
 * 该函数用于初始化 ProxyHandler 结构体，并创建一个新的线程来执行
 * persistentThread 函数。
 *
 * @param handler 指向 ProxyHandler 结构体的指针。
 */
void ProxyCreate(ProxyHandler* handler) {
    if (!handler->proxyThread) {
        handler->stop = false;
        handler->ops = NULL;
        handler->mutex = PTHREAD_MUTEX_INITIALIZER;
        handler->cond = PTHREAD_COND_INITIALIZER;
        pthread_create(&handler->proxyThread, NULL, persistentThread, handler);
    }
    LOG(INFO) << "Proxy thread created.";
}

/**
 * @brief 将任务添加到任务队列。
 * @ingroup ProxyModule
 *
 * 该函数将一个 ProxyArgs 结构体表示的任务添加到任务队列中。
 *
 * @param handler 指向 ProxyHandler 结构体的指针。
 * @param args 指向 ProxyArgs 结构体的指针，表示要添加的任务。
 */
void ProxyArgsAppend(ProxyHandler* handler, ProxyArgs* args) {
    pthread_mutex_lock(&handler->mutex);
    if (*args->proxyTail == NULL) {
        if (handler->ops == NULL) {
            args->next = args;
            handler->ops = args;
        } else {
            args->next = handler->ops->next;
            handler->ops->next = args;
        }
        *args->proxyTail = args;
    } else {
        (*args->proxyTail)->nextPeer = args;
        *args->proxyTail = args;
    }
    pthread_mutex_unlock(&handler->mutex);
}

/**
 * @brief 分配一个 ProxyArgs 结构体。
 * @ingroup ProxyModule
 *
 * 该函数从对象池中分配一个 ProxyArgs
 * 结构体，如果对象池为空，则创建一个新的对象池。
 *
 * @param handler 指向 ProxyHandler 结构体的指针。
 * @return 指向分配的 ProxyArgs 结构体的指针。
 */
ProxyArgs* allocateArgs(ProxyHandler* handler) {
    ProxyArgs* elem;
    pthread_mutex_lock(&handler->mutex);
    if (handler->pool == NULL) {
        struct ProxyPool* newPool;
        CALLOC(newPool, ProxyPool, 1);
        ProxyArgs* newElems = newPool->elems;
        for (int i = 0; i < PROXYARGS_ALLOCATE_SIZE; i++) {
            if (i + 1 < PROXYARGS_ALLOCATE_SIZE)
                newElems[i].next = newElems + i + 1;
        }
        handler->pool = newElems;
        newPool->next = handler->pools;
        handler->pools = newPool;
    }
    elem = handler->pool;
    handler->pool = handler->pool->next;
    pthread_mutex_unlock(&handler->mutex);
    elem->next = elem->nextPeer = NULL;
    return elem;
}

/**
 * @brief 唤醒代理执行线程。
 * @ingroup ProxyModule
 *
 * 该函数用于唤醒处于休眠状态的代理执行线程，使其开始处理任务队列中的任务。
 *
 * @param handler 指向 ProxyHandler 结构体的指针。
 */
void ProxyStart(ProxyHandler* handler) {
    pthread_mutex_lock(&handler->mutex);
    if (handler->ops != NULL) {
        handler->stop = false;
        pthread_cond_signal(&handler->cond);
    }
    pthread_mutex_unlock(&handler->mutex);
}

/**
 * @brief 销毁代理执行线程。
 * @ingroup ProxyModule
 *
 * 该函数用于停止代理执行线程，并释放相关资源。
 *
 * @param handler 指向 ProxyHandler 结构体的指针。
 */
void ProxyDestroy(ProxyHandler* handler) {
    pthread_mutex_lock(&handler->mutex);
    handler->stop = true;
    pthread_cond_signal(&handler->cond);
    pthread_mutex_unlock(&handler->mutex);
    if (handler->proxyThread)
        pthread_join(handler->proxyThread, NULL);
    pthread_mutex_lock(&handler->mutex);
    while (handler->pools != NULL) {
        struct ProxyPool* next = handler->pools->next;
        free(handler->pools);
        handler->pools = next;
    }
    pthread_mutex_unlock(&handler->mutex);
    LOG(INFO) << "Proxy thread destroyed.";
}

void ProxyWaitAllOpFinished(ProxyHandler* handler) {
    while (handler->ops != NULL) {
        sched_yield();
    }
}