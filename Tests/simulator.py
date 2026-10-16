"""TCP adapter around the native firmware DLL. No HAL or physical peripherals."""
import ctypes,threading,socketserver,socket,struct,time,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def build_bridge():
    sys.path.insert(0,str(ROOT/'Tools'))
    from host_build import build_native,SOURCES
    target=ROOT/'build/Tests/gateway_sim.dll';target.parent.mkdir(parents=True,exist_ok=True)
    inputs=[ROOT/p for p in SOURCES]+[ROOT/'Tests/native/sim_bridge.c']
    if not target.exists() or any(p.stat().st_mtime>target.stat().st_mtime for p in inputs):
        build_native(['Tests/native/sim_bridge.c'],target,shared=True)
    return target
def read_exact(conn,n):
    data=bytearray()
    while len(data)<n:
        part=conn.recv(n-len(data))
        if not part:return None
        data.extend(part)
    return bytes(data)
class Simulator:
    def __init__(self,modbus_port=0,ota_port=0,fragment=False):
        self.dll=ctypes.CDLL(str(build_bridge()));self.dll.sim_modbus.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p];self.dll.sim_modbus.restype=ctypes.c_size_t;self.dll.sim_frame.argtypes=self.dll.sim_modbus.argtypes;self.dll.sim_frame.restype=ctypes.c_size_t
        self.dll.sim_tick.argtypes=[ctypes.c_uint32];self.dll.sim_init();self.lock=threading.RLock();self.fragment=fragment;self.closed=threading.Event();self.connections=[]
        outer=self
        class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                outer.connections.append(self.request);self.request.settimeout(2)
                try:
                    while not outer.closed.is_set():
                        ota=self.server.ota;header=read_exact(self.request,24 if ota else 7)
                        if header is None:break
                        length=struct.unpack_from('>I',header,16)[0] if ota else struct.unpack_from('>H',header,4)[0]-1
                        if length<0 or length>(1024 if ota else 253):break
                        payload=read_exact(self.request,length)
                        if payload is None:break
                        packet=header+payload;out=ctypes.create_string_buffer(1200)
                        with outer.lock:n=(outer.dll.sim_frame if ota else outer.dll.sim_modbus)(packet,len(packet),out)
                        if not n:break
                        wire=out.raw[:n]
                        if outer.fragment:
                            for start in range(0,len(wire),3):self.request.sendall(wire[start:start+3])
                        else:self.request.sendall(wire)
                except (OSError,TimeoutError):pass
                finally:
                    if self.server.ota:
                        with outer.lock:outer.dll.sim_drop_download()
                    if self.request in outer.connections:outer.connections.remove(self.request)
        self.mb=Server(('127.0.0.1',modbus_port),Handler);self.mb.ota=False
        self.ot=Server(('127.0.0.1',ota_port),Handler);self.ot.ota=True
        self.modbus_port=self.mb.server_address[1];self.ota_port=self.ot.server_address[1]
    def start(self):
        for server in (self.mb,self.ot):threading.Thread(target=server.serve_forever,daemon=True).start()
        def tick():
            last=time.monotonic()
            while not self.closed.wait(.05):
                now=time.monotonic()
                with self.lock:self.dll.sim_tick(round((now-last)*1000))
                last=now
        threading.Thread(target=tick,daemon=True).start()
    def close(self):
        self.closed.set()
        for conn in list(self.connections):
            try:conn.shutdown(socket.SHUT_RDWR);conn.close()
            except OSError:pass
        for server in (self.mb,self.ot):server.shutdown();server.server_close()
if __name__=='__main__':
    sim=Simulator(1502,15001);sim.start();print('C firmware simulator: 127.0.0.1 Modbus=1502 OTA=15001',flush=True)
    try:
        while True:time.sleep(.5)
    except KeyboardInterrupt:sim.close()
