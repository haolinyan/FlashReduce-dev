import logging
import ipaddress
import threading

import switchml_pb2
import switchml_pb2_grpc

from concurrent import futures
from grpc import server
from grpc_reflection.v1alpha import reflection  # 导入服务反射模块

from common import PacketSize


class GRPCServer(switchml_pb2_grpc.SessionServicer,
                 switchml_pb2_grpc.SyncServicer):

    def __init__(self, ip='[::]', port=50099, folded_pipe=False):
        self.log = logging.getLogger(__name__)
        self.ip = ip
        self.port = port
        self.folded_pipe = folded_pipe

        # gRPC server setup
        self._server = server(futures.ThreadPoolExecutor(max_workers=10))
        switchml_pb2_grpc.add_SessionServicer_to_server(self, self._server)
        switchml_pb2_grpc.add_SyncServicer_to_server(self, self._server)
        self._server.add_insecure_port('{}:{}'.format(self.ip, self.port))

        # 启用服务反射
        SERVICE_NAMES = (
            switchml_pb2.DESCRIPTOR.services_by_name['Session'].full_name,
            switchml_pb2.DESCRIPTOR.services_by_name['Sync'].full_name,
            reflection.SERVICE_NAME,
        )
        reflection.enable_server_reflection(SERVICE_NAMES, self._server)
        self.log.info("Enabling reflection for services: %s", SERVICE_NAMES)
        # Thread synchronization
        self.lock = threading.RLock()

        # Barrier state
        self._barrier_op_id = 0
        self._barrier_ctrs = {self._barrier_op_id: 0}
        self._barrier_events = {self._barrier_op_id: threading.Event()}

        # Broadcast state
        self._bcast_values = []
        self._bcast_bitmap = []
        self._bcast_events = []

        # Controller
        self.ctrl = None

    def reset(self):
        with self.lock:
            self._barrier_op_id = 0
            self._barrier_ctrs = {self._barrier_op_id: 0}
            self._barrier_events = {self._barrier_op_id: threading.Event()}
            self._bcast_values = []
            self._bcast_bitmap = []
            self._bcast_events = []

    def run(self, controller):
        self.ctrl = controller
        self._server.start()
        self.log.info("gRPC server started on {}:{}".format(self.ip, self.port))  # 添加此行
        self._server.wait_for_termination()

    def stop(self):
        self._server.stop(0)

    def Barrier(self, request, context):
        current_op_id = None
        event = None
        with self.lock:
            current_op_id = self._barrier_op_id
            self._barrier_ctrs[current_op_id] += 1

            if self._barrier_ctrs[current_op_id] < request.num_workers:
                event = self._barrier_events[current_op_id]
            else:
                event = self._barrier_events[current_op_id]
                event.set()
                self._barrier_op_id += 1
                self._barrier_ctrs[self._barrier_op_id] = 0
                self._barrier_events[self._barrier_op_id] = threading.Event()

        if event and self._barrier_ctrs[current_op_id] < request.num_workers:
            event.wait()
            with self.lock:
                self._barrier_ctrs[current_op_id] -= 1
                if self._barrier_ctrs[current_op_id] == 0:
                    del self._barrier_ctrs[current_op_id]
                    del self._barrier_events[current_op_id]

        return switchml_pb2.BarrierResponse()

    def Broadcast(self, request, context):
        idx = -1
        with self.lock:
            # Cleanup completed operations
            for i in reversed(range(len(self._bcast_bitmap))):
                if all(self._bcast_bitmap[i]):
                    del self._bcast_bitmap[i]
                    del self._bcast_values[i]
                    del self._bcast_events[i]

            # Find existing or create new operation
            idx = -1
            for i in range(len(self._bcast_bitmap)):
                if not self._bcast_bitmap[i][request.rank]:
                    idx = i
                    break
            if idx == -1:
                idx = len(self._bcast_bitmap)
                self._bcast_bitmap.append([False] * request.num_workers)
                self._bcast_values.append(None)
                self._bcast_events.append(threading.Event())

            # Handle request
            if request.rank == request.root:
                self._bcast_values[idx] = request.value
                self._bcast_events[idx].set()
                self._bcast_bitmap[idx][request.rank] = True
            else:
                if self._bcast_values[idx] is None:
                    current_event = self._bcast_events[idx]
                    current_idx = idx
                    self.lock.release()
                    try:
                        current_event.wait()
                    finally:
                        self.lock.acquire()
                    self._bcast_bitmap[current_idx][request.rank] = True
                else:
                    self._bcast_bitmap[idx][request.rank] = True

        return switchml_pb2.BroadcastResponse(value=self._bcast_values[idx])

    def RdmaSession(self, request, context):
        mac_hex = '{:012X}'.format(request.mac)
        mac_str = ':'.join(mac_hex[i:i+2] for i in range(0, 12, 2))
        ipv4_str = str(ipaddress.ip_address(request.ipv4))

        self.log.debug(
            '# RDMA:\n Session ID: {}\n Rank: {}\n Num workers: {}\n MAC: {}\n'
            ' IP: {}\n Rkey: {}\n Pkt size: {}B\n Msg size: {}B\n QPs: {}\n'
            ' PSNs: {}\n'.format(
                request.session_id, request.rank, request.num_workers, mac_str,
                ipv4_str, request.rkey,
                str(PacketSize(request.packet_size)).split('.')[1][4:],
                request.message_size, request.qpns, request.psns))

        if not self.folded_pipe and PacketSize(
                request.packet_size) == PacketSize.MTU_1024:
            self.log.error(
                "Processing 1024B per packet requires a folded pipeline. Using 256B payload.")
            request.packet_size = int(PacketSize.MTU_256)

        if not self.ctrl:
            return switchml_pb2.RdmaSessionResponse(
                session_id=request.session_id,
                mac=request.mac,
                ipv4=request.ipv4,
                rkey=request.rkey,
                qpns=request.qpns,
                psns=request.psns)

        if request.rank == 0:
            self.ctrl.clear_rdma_workers(request.session_id)

        success, error_msg = self.ctrl.add_rdma_worker(
            request.session_id, request.rank, request.num_workers, mac_str,
            ipv4_str, request.rkey, request.packet_size, request.message_size,
            zip(request.qpns, request.psns))
        if not success:
            self.log.error(error_msg)
            return switchml_pb2.RdmaSessionResponse(session_id=0,
                                                    mac=0,
                                                    ipv4=0,
                                                    rkey=0,
                                                    qpns=[],
                                                    psns=[])

        switch_mac, switch_ipv4 = self.ctrl.get_switch_mac_and_ip()
        switch_mac = int(switch_mac.replace(':', ''), 16)
        switch_ipv4 = int(ipaddress.ip_address(switch_ipv4))
        switch_qpns = [
            0x800000 | (request.rank << 16) | i
            for i, _ in enumerate(request.qpns)
        ]
        switch_psns = list(range(len(request.qpns)))

        return switchml_pb2.RdmaSessionResponse(
            session_id=request.session_id,
            mac=switch_mac,
            ipv4=switch_ipv4,
            rkey=request.rkey,
            qpns=switch_qpns,
            psns=switch_psns)

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    grpc_server = GRPCServer()
    try:
        grpc_server.run(None)
    except KeyboardInterrupt:
        grpc_server.stop()