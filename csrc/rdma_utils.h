#pragma once
#include <infiniband/verbs.h>
#include <glog/logging.h>
#define IB_PORT 1
#define GID_INDEX 3 // RoCEv2 GID index

struct QpInfo
{
    uint32_t rkey;
    void *raddr;
    uint32_t qp_num;
    uint32_t psn;
    ibv_gid gid;
    uint16_t lid;
};

void init_ibv_device(struct ibv_device **device, struct ibv_context **context, const char *device_name);

int modify_qp_to_init(struct ibv_qp *qp);

int modify_qp_to_rts(
    struct ibv_qp *qp,
    const QpInfo &neighbor_qp_info, ibv_mtu mtu, int reliable);