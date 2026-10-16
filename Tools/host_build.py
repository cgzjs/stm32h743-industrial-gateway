"""Host builds use the same protocol and persistence sources as the ARM targets."""
import subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['Middleware/crc/igw_crc.c','System/image_store/image_store.c','Bootloader/boot_policy.c','Application/ota_manager/ota_manager.c','Middleware/firmware_transfer/fw_protocol.c','System/data_model/data_model.c','System/config_store/config_store.c','Middleware/modbus_rtu/modbus_rtu.c','Middleware/modbus_tcp/modbus_tcp.c','Middleware/motor_can_protocol/pd42.c']
INCLUDES=['Include','System/image_store','System/data_model','System/config_store','Bootloader','Application/ota_manager','Middleware/crc','Middleware/firmware_transfer','Middleware/modbus_rtu','Middleware/modbus_tcp','Middleware/motor_can_protocol']
def build_native(extra,target,shared=False):
    zig=ROOT/'Tools/python/ziglang/zig.exe'
    if not zig.exists():subprocess.run([sys.executable,'-m','pip','install','--target',str(ROOT/'Tools/python'),'ziglang==0.13.0'],check=True)
    target=Path(target);target.parent.mkdir(parents=True,exist_ok=True)
    command=[str(zig),'cc','-std=c11','-O1','-Wall','-Wextra','-Wno-unused-parameter']+(['-shared'] if shared else [])+[f'-I{p}' for p in INCLUDES]+SOURCES+extra+['-o',str(target)]
    subprocess.run(command,cwd=ROOT,check=True)
