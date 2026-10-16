"""Real TCP sockets, fragmented replies, with production C protocol/OTA modules."""
import sys,socket,struct,tempfile,time,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'PC_Tools/Qt_Gateway_Manager'))
from protocol import ModbusClient,MaintenanceClient,package_image,encode_frame,ProtocolError,receive_frame
from simulator import Simulator
def check(value,message):
    if not value:raise AssertionError(message)
def main():
    s=Simulator(fragment=True);s.start();checks=0
    try:
        m=ModbusClient('127.0.0.1',s.modbus_port)
        check(m.snapshot()['version']==0x30000,'factory version');checks+=1
        m.run(100);check(m.snapshot()['enabled'],'motor command');checks+=1
        m.move(200,True,51200);check(m.snapshot()['position']==-51200,'absolute position');checks+=1
        m.heartbeat()
        # Deterministic simulated time proves read-only polling does not extend the lease.
        with s.lock:s.dll.sim_tick(2100)
        check(not m.snapshot()['enabled'],'lease timeout');checks+=1
        m.close()
        client=MaintenanceClient('127.0.0.1',s.ota_port)
        config=client.get_config();config[4]=2;config[6]=7;client.set_config(config)
        check(client.get_config()[4:7]==[2,3,7],'configuration round trip');checks+=1
        client.close()
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'Gateway.bin';image=bytearray(struct.pack('<II',0x24080000,0x08020101)+bytes(range(256))*16)
            image[0x400:0x414]=b'IGWID3\0\0'+struct.pack('<3I',0x743A0001,0x30001,0x08020000);binary.write_bytes(image)
            package_image(binary,'3.0.1')
            client=MaintenanceClient('127.0.0.1',s.ota_port);progress=[];client.upload(binary,progress=progress.append)
            check(progress[-1]==100,'upload complete');check(client.status()['version']==0x30001,'boot confirmation');checks+=2;client.close()
            image[0x40C:0x410]=struct.pack('<I',0x30002);binary.write_bytes(image);package_image(binary,'3.0.2')
            with s.lock:s.dll.sim_fail_trial(1)
            client=MaintenanceClient('127.0.0.1',s.ota_port);client.upload(binary)
            check(client.status()['version']==0x30001,'new firmware failure rollback');checks+=1;client.close()
            # Bad CRC is never passed to the native OTA state machine.
            conn=socket.create_connection(('127.0.0.1',s.ota_port),1);bad=bytearray(encode_frame(4));bad[20]^=1;conn.sendall(bad);check(conn.recv(24)==b'','bad frame closed');checks+=1;conn.close()
            # Interrupted download retains the confirmed version and permits a fresh session.
            client=MaintenanceClient('127.0.0.1',s.ota_port);client.call(1,struct.pack('>4I',0x743A0001,0x30003,4104,0));client.close();time.sleep(.1)
            client=MaintenanceClient('127.0.0.1',s.ota_port);status=client.status();check(status['version']==0x30001 and status['pending']==0xFFFFFFFF,'interrupted download');checks+=1;client.close()
        print(f'PASS {checks} TCP integration checks (fragmented responses, production C DLL)')
        (ROOT/'build/Tests/transport-report.json').write_text(json.dumps({'passed':True,'checks':checks,'scope':'native C DLL over loopback sockets; simulated motor/sensor/Flash'},indent=2))
    finally:s.close()
if __name__=='__main__':main()
