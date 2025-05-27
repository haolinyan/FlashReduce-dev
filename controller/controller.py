from flashreduce_pb2_grpc import SyncServicer, add_SyncServicer_to_server
import flashreduce_pb2
from grpc import server
from concurrent import futures
import threading
import logging

class Controller(SyncServicer):
    def __init__(self, ip='[::]', port=50099):
        self.log = logging.getLogger(__name__)
        self.ip = ip
        self.port = port
        self._server = server(futures.ThreadPoolExecutor(max_workers=10))
        add_SyncServicer_to_server(self, self._server)
        self._server.add_insecure_port('{}:{}'.format(self.ip, self.port))
        self.lock = threading.RLock()
        # Barrier state
        self._barrier_op_id = 0
        self._barrier_ctrs = {self._barrier_op_id: 0}
        self._barrier_events = {self._barrier_op_id: threading.Event()}
        # Broadcast state
        self._bcast_values = []
        self._bcast_bitmap = []
        self._bcast_events = []

    def run(self):
        self._server.start()
        self.log.info("gRPC server started on {}:{}".format(self.ip, self.port))
        self._server.wait_for_termination()

    def stop(self):
        self._server.stop(0)
        self.log.info("gRPC server exited")

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

        return flashreduce_pb2.BarrierResponse()
    
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

        return flashreduce_pb2.BroadcastResponse(value=self._bcast_values[idx])

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    grpc_server = Controller()
    try:
        grpc_server.run()
    except KeyboardInterrupt:
        grpc_server.stop()