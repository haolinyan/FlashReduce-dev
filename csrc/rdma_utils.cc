#include "rdma_utils.h"

const int kMinRnrTimer = 0x12;
const int kTimeout = 14;
const int kRetryCount = 7;
const int kRnrRetry = 7;
const int kMaxDestRdAtomic = 0;
const int kMaxRdAtomic = 1;

void init_ibv_device(struct ibv_device **device, struct ibv_context **context, const char *device_name)
{
    ibv_device **devices;
    int num_devices;
    devices = ibv_get_device_list(&num_devices);
    CHECK(devices) << "No RDMA devices found";
    for (int i = 0; i < num_devices; ++i)
    {
        if (strcmp(ibv_get_device_name(devices[i]), device_name) == 0)
        {
            *device = devices[i];
            break;
        }
    }
    *context = ibv_open_device(*device);
    CHECK(*context) << "Failed to open RDMA device: %s", device_name;
    ibv_free_device_list(devices);
}

const char *port_state_to_string(enum ibv_port_state state)
{
    switch (state)
    {
    case IBV_PORT_NOP:
        return "NOP";
    case IBV_PORT_DOWN:
        return "DOWN";
    case IBV_PORT_INIT:
        return "INIT";
    case IBV_PORT_ARMED:
        return "ARMED";
    case IBV_PORT_ACTIVE:
        return "ACTIVE";
    case IBV_PORT_ACTIVE_DEFER:
        return "ACTIVE_DEFER";
    default:
        return "UNKNOWN";
    }
}

const char *mtu_to_string(enum ibv_mtu mtu)
{
    switch (mtu)
    {
    case IBV_MTU_256:
        return "256";
    case IBV_MTU_512:
        return "512";
    case IBV_MTU_1024:
        return "1024";
    case IBV_MTU_2048:
        return "2048";
    case IBV_MTU_4096:
        return "4096";
    default:
        return "UNKNOWN";
    }
}

void print_port_attributes(const struct ibv_port_attr *attr)
{
    if (!attr)
    {
        LOG(ERROR) << "Null ibv_port_attr pointer";
        return;
    }

    std::ostringstream oss;
    oss << "===== IBV Port Attributes =====\n"
        << "State: " << port_state_to_string(attr->state) << "\n"
        << "Max MTU: " << mtu_to_string(attr->max_mtu) << "\n"
        << "Active MTU: " << mtu_to_string(attr->active_mtu) << "\n"
        << "GID Table Length: " << attr->gid_tbl_len << "\n"
        << "Port Capability Flags: 0x" << std::hex << attr->port_cap_flags << std::dec << "\n"
        << "Max Message Size: " << attr->max_msg_sz << "\n"
        << "Bad PKey Counter: " << attr->bad_pkey_cntr << "\n"
        << "QKey Violation Counter: " << attr->qkey_viol_cntr << "\n"
        << "PKey Table Length: " << attr->pkey_tbl_len << "\n"
        << "LID: 0x" << std::hex << attr->lid << std::dec << "\n"
        << "SM LID: 0x" << std::hex << attr->sm_lid << std::dec << "\n"
        << "LMC: 0x" << std::hex << static_cast<int>(attr->lmc) << std::dec << "\n"
        << "Max VL Number: " << static_cast<int>(attr->max_vl_num) << "\n"
        << "SM SL: " << static_cast<int>(attr->sm_sl) << "\n"
        << "Subnet Timeout: " << static_cast<int>(attr->subnet_timeout) << "\n"
        << "Init Type Reply: 0x" << std::hex << static_cast<int>(attr->init_type_reply) << std::dec << "\n"
        << "Active Width: " << static_cast<int>(attr->active_width) << "\n"
        << "Active Speed: " << static_cast<int>(attr->active_speed) << "\n"
        << "Physical State: " << static_cast<int>(attr->phys_state) << "\n"
        << "Link Layer: " << static_cast<int>(attr->link_layer) << "\n"
        << "Flags: 0x" << std::hex << static_cast<int>(attr->flags) << std::dec << "\n"
        << "Port Capability Flags 2: 0x" << std::hex << attr->port_cap_flags2 << std::dec << "\n"
        << "===============================";

    LOG(INFO) << oss.str();
}

int modify_qp_to_init(struct ibv_qp *qp)
{
    struct ibv_qp_attr attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    attributes.qp_state = IBV_QPS_INIT;
    attributes.port_num = IB_PORT;
    attributes.pkey_index = 0;
    attributes.qp_access_flags = (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    int ret = ibv_modify_qp(qp, &attributes,
                            IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX |
                                IBV_QP_ACCESS_FLAGS);
    return ret;
}

static int modify_qp_to_rtr_uc(struct ibv_qp *qp, const QpInfo &neighbor_qp_info, ibv_mtu mtu)
{
    struct ibv_qp_attr attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    attributes.qp_state = IBV_QPS_RTR;
    attributes.dest_qp_num = neighbor_qp_info.qp_num;
    attributes.rq_psn = neighbor_qp_info.psn;
    attributes.max_dest_rd_atomic = kMaxDestRdAtomic; // used only for RC
    attributes.min_rnr_timer = kMinRnrTimer;          // used only for RC
    attributes.path_mtu = mtu;
    attributes.ah_attr.is_global = 1;
    attributes.ah_attr.dlid = neighbor_qp_info.lid; // not really necessary since using RoCE, not IB, and is_global is set
    attributes.ah_attr.sl = 0;
    attributes.ah_attr.src_path_bits = 0;
    attributes.ah_attr.port_num = IB_PORT;
    attributes.ah_attr.grh.dgid = neighbor_qp_info.gid;
    attributes.ah_attr.grh.sgid_index = GID_INDEX;
    attributes.ah_attr.grh.flow_label = 0;
    attributes.ah_attr.grh.hop_limit = 0xFF;
    attributes.ah_attr.grh.traffic_class = 0;
    int ret = ibv_modify_qp(qp, &attributes,
                            IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                                IBV_QP_RQ_PSN | IBV_QP_AV |
                                0);
    return ret;
}

static int modify_qp_to_rts_uc(struct ibv_qp *qp)
{
    struct ibv_qp_attr attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    attributes.qp_state = IBV_QPS_RTS;
    attributes.sq_psn = 0;
    attributes.timeout = kTimeout;
    attributes.retry_cnt = kRetryCount;
    attributes.rnr_retry = kRnrRetry;
    attributes.max_rd_atomic = kMaxRdAtomic;
    int ret = ibv_modify_qp(qp, &attributes,
                            IBV_QP_STATE | IBV_QP_SQ_PSN);
    return ret;
}

static int modify_qp_to_rts_rc(
    struct ibv_qp *qp,
    const QpInfo &neighbor_qp_info, ibv_mtu mtu)
{
    uint32_t target_qp_num = neighbor_qp_info.qp_num;
    uint16_t target_lid = neighbor_qp_info.lid;
    int ret = 0;
    /* Change QP state to RTR */
    {
        struct ibv_ah_attr ah_attr = {
            .dlid = target_lid,
            .sl = 0,
            .src_path_bits = 0,
            .is_global = 1,
            .port_num = IB_PORT,
        };
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTR,
            .path_mtu = mtu,
            .rq_psn = 0,
            .dest_qp_num = target_qp_num,
            .ah_attr = ah_attr,
            .max_dest_rd_atomic = 1,
            .min_rnr_timer = 0x12,
        };

        qp_attr.ah_attr.grh.sgid_index = GID_INDEX;
        memcpy(&qp_attr.ah_attr.grh.dgid, &neighbor_qp_info.gid, sizeof(ibv_gid));
        qp_attr.ah_attr.grh.hop_limit = 0xFF;
        qp_attr.ah_attr.grh.flow_label = 0;
        qp_attr.ah_attr.grh.traffic_class = 0;

        ret = ibv_modify_qp(
            qp,
            &qp_attr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER);
        CHECK(ret == 0) << "Failed to change qp to rtr, ret = " << ret;
    }
    /* Change QP state to RTS */
    {
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTS,
            .sq_psn = 0,
            .max_rd_atomic = 1,
            .timeout = 14,
            .retry_cnt = 7,
            .rnr_retry = 7,
        };
        ret = ibv_modify_qp(
            qp,
            &qp_attr,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
        CHECK(ret == 0) << "Failed to change qp to rts.";
    }
    return 0;
}

int modify_qp_to_rts(
    struct ibv_qp *qp,
    const QpInfo &neighbor_qp_info, ibv_mtu mtu, int reliable)
{
    if (reliable)
    {
        return modify_qp_to_rts_rc(qp, neighbor_qp_info, mtu);
    }
    else
    {
        int ret = modify_qp_to_rtr_uc(qp, neighbor_qp_info, mtu);
        CHECK(ret == 0) << "Failed to modify QP to RTR, ret = " << ret;
        return modify_qp_to_rts_uc(qp);
    }
}