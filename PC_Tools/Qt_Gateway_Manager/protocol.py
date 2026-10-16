"""Standard-library client for the production Modbus TCP / IGW3 protocol."""
import socket, struct, zlib, secrets, json, hashlib, time
from pathlib import Path
BOARD=0x743A0001
APP_BASE=0x08020000
CAPACITY=0x1E0000
HEADER=struct.Struct('>IBBHIIII')
CONFIG=struct.Struct('>4B6HI6iI')
class ProtocolError(RuntimeError): pass
def read_exact(sock,n):
    data=bytearray()
    while len(data)<n:
        part=sock.recv(n-len(data))
        if not part:raise ProtocolError('Connection closed before a complete response')
        data.extend(part)
    return bytes(data)
def crc(data):return zlib.crc32(data)&0xFFFFFFFF
def version_string(value):return f'{value>>16}.{(value>>8)&255}.{value&255}'
def version_value(text):
    if '.' not in str(text):return int(str(text),0)
    parts=[int(v) for v in str(text).split('.')]
    if len(parts)!=3 or any(v<0 or v>255 for v in parts):raise ValueError('Version must be x.y.z, each 0..255')
    return (parts[0]<<16)|(parts[1]<<8)|parts[2]
def validate_image(data):
    if not 1044<=len(data)<=CAPACITY:raise ProtocolError('Application size is outside the Flash partition')
    sp,pc=struct.unpack_from('<II',data)
    ram=any(start<sp<=end for start,end in ((0x20000000,0x20020000),(0x24000000,0x24080000),(0x30000000,0x30048000),(0x38000000,0x38010000)))
    if not ram or sp&7 or not pc&1 or not APP_BASE<=(pc&~1)<APP_BASE+len(data):raise ProtocolError('Not an application linked at 0x08020000')
    if data[0x400:0x408]!=b'IGWID3\0\0':raise ProtocolError('Missing compiled IGW3 identity')
    board,version,base=struct.unpack_from('<3I',data,0x408)
    if board!=BOARD or base!=APP_BASE:raise ProtocolError('Application was built for a different board or base address')
    return version
def package_image(binary,version):
    path=Path(binary);data=path.read_bytes();embedded=validate_image(data)
    if version_value(version)!=embedded:raise ProtocolError('Manifest version must match the compiled application version')
    manifest={'format':'IGW3-manifest-v1','board':BOARD,'version':version_value(version),'base':APP_BASE,'size':len(data),'crc32':crc(data),'sha256':hashlib.sha256(data).hexdigest(),'binary':path.name}
    path.with_suffix('.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    return manifest
def load_image(binary):
    path=Path(binary);data=path.read_bytes();embedded=validate_image(data)
    manifest=json.loads(path.with_suffix('.json').read_text(encoding='utf-8'))
    if manifest.get('format')!='IGW3-manifest-v1' or manifest.get('board')!=BOARD or manifest.get('version')!=embedded or manifest.get('base')!=APP_BASE or manifest.get('size')!=len(data) or manifest.get('crc32')!=crc(data) or manifest.get('sha256')!=hashlib.sha256(data).hexdigest():raise ProtocolError('Firmware does not match its manifest')
    return data,manifest
class ModbusClient:
    def __init__(self,host,port=502,timeout=1.5):
        self.socket=socket.create_connection((host,port),timeout);self.socket.settimeout(timeout);self.transaction=0
    def close(self):self.socket.close()
    def call(self,pdu):
        self.transaction=(self.transaction+1)&65535
        self.socket.sendall(struct.pack('>HHHB',self.transaction,0,len(pdu)+1,1)+pdu)
        header=read_exact(self.socket,7);tid,pid,length,unit=struct.unpack('>HHHB',header)
        if tid!=self.transaction or pid or unit!=1 or not 2<=length<=254:raise ProtocolError('Invalid Modbus MBAP response')
        reply=read_exact(self.socket,length-1)
        if reply[0]==(pdu[0]|0x80):raise ProtocolError(f'Modbus exception {reply[1]}')
        if reply[0]!=pdu[0]:raise ProtocolError('Modbus function mismatch')
        return reply
    def read(self,address,count):
        reply=self.call(struct.pack('>BHH',3,address,count))
        if len(reply)!=2+2*count or reply[1]!=2*count:raise ProtocolError('Invalid register response length')
        return list(struct.unpack(f'>{count}H',reply[2:]))
    def write(self,address,values):
        pdu=struct.pack('>BHHB',16,address,len(values),2*len(values))+struct.pack(f'>{len(values)}H',*values)
        reply=self.call(pdu)
        if reply!=struct.pack('>BHH',16,address,len(values)):raise ProtocolError('Invalid write acknowledgement')
    def run(self,rpm,reverse=False):self.write(0x20,[rpm,1,int(reverse)])
    def stop(self):self.write(0x21,[0])
    def move(self,rpm,reverse,pulses):
        if not 0<=pulses<=0xFFFFFFFF:raise ValueError('Pulses must be 0..4294967295')
        self.write(0x20,[rpm,1,int(reverse),pulses>>16,pulses&65535,1])
    def heartbeat(self):self.write(0x26,[1])
    def snapshot(self):
        a=self.read(0,4);b=self.read(0x10,6);c=self.read(0x20,3);owner=self.read(0x27,1)[0];v=self.read(0x30,6)
        signed=lambda x:x-65536 if x&32768 else x
        pos=(b[4]<<16)|b[5];pos=pos-0x100000000 if pos&0x80000000 else pos
        return dict(temperature=signed(a[0])/10,humidity=a[1]/10,valid=a[2],alarms=a[3],rpm=signed(b[0]),current=signed(b[1]),state=b[2],fault=b[3],position=pos,target=c[0],enabled=bool(c[1]),reverse=bool(c[2]),owner=owner,version=(v[0]<<16)|v[1],uptime=(v[2]<<16)|v[3],ota_busy=bool(v[4]),progress=v[5]/10)
def encode_frame(operation,session=1,offset=0,payload=b'',status=0):
    if len(payload)>1024:raise ProtocolError('Frame payload exceeds 1024 bytes')
    first=struct.pack('>IBBHIII',0x49475733,1,operation,status,session,offset,len(payload))
    return first+struct.pack('>I',crc(first+payload))+payload
def receive_frame(sock):
    w=read_exact(sock,24);magic,version,op,status,session,offset,length,checksum=HEADER.unpack(w)
    if magic!=0x49475733 or version!=1 or length>1024:raise ProtocolError('Invalid IGW3 header')
    payload=read_exact(sock,length)
    if crc(w[:20]+payload)!=checksum:raise ProtocolError('IGW3 response CRC mismatch')
    return op,status,session,offset,payload
class MaintenanceClient:
    def __init__(self,host,port=5001,timeout=90):
        self.socket=socket.create_connection((host,port),timeout);self.socket.settimeout(timeout);self.session=secrets.randbits(32) or 1
    def close(self):self.socket.close()
    def call(self,operation,payload=b'',offset=0):
        self.socket.sendall(encode_frame(operation,self.session,offset,payload))
        op,status,session,next_offset,data=receive_frame(self.socket)
        if op!=operation or session!=self.session:raise ProtocolError('IGW3 session/operation mismatch')
        if status:raise ProtocolError(f'Gateway rejected operation {operation}: status {status}')
        return next_offset,data
    def status(self):
        _,data=self.call(4)
        if len(data)!=24:raise ProtocolError('Invalid status size')
        return dict(zip(('board','version','active','pending','offset','size'),struct.unpack('>6I',data)))
    def get_config(self):
        _,data=self.call(7)
        if len(data)!=48:raise ProtocolError('Invalid configuration size')
        return list(CONFIG.unpack(data))
    def set_config(self,values):self.call(8,CONFIG.pack(*values))
    def reboot(self):self.call(6)
    def upload(self,binary,progress=lambda percent:None,cancel=lambda:False,reboot=True):
        data,m=load_image(binary)
        try:
            offset,_=self.call(1,struct.pack('>4I',m['board'],m['version'],m['size'],m['crc32']))
            if offset:raise ProtocolError('BEGIN acknowledged a nonzero offset')
            for start in range(0,len(data),1024):
                if cancel():self.call(5);raise ProtocolError('Upload cancelled; confirmed image is preserved')
                block=data[start:start+1024];next_offset,_=self.call(2,block,start)
                if next_offset!=start+len(block):raise ProtocolError('DATA offset acknowledgement mismatch')
                progress(100*next_offset/len(data))
            self.call(3)
            if reboot:self.reboot()
            return m
        except Exception:
            self.close();raise
