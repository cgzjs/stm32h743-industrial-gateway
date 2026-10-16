"""Build/run the exact portable firmware C using Zig's native C compiler."""
import json, re, subprocess, sys, shutil
from pathlib import Path
root=Path(__file__).resolve().parents[1]
zig=root/'Tools/python/ziglang/zig.exe'
if not zig.exists():
    subprocess.run([sys.executable,'-m','pip','install','--target',str(root/'Tools/python'),'ziglang==0.13.0'],check=True)
sources=['Middleware/crc/igw_crc.c','System/image_store/image_store.c','Bootloader/boot_policy.c','Application/ota_manager/ota_manager.c','Middleware/firmware_transfer/fw_protocol.c','System/data_model/data_model.c','System/config_store/config_store.c','Middleware/modbus_rtu/modbus_rtu.c','Middleware/modbus_tcp/modbus_tcp.c','Middleware/motor_can_protocol/pd42.c']
includes=['Include','System/image_store','System/data_model','System/config_store','Bootloader','Application/ota_manager','Middleware/crc','Middleware/firmware_transfer','Middleware/modbus_rtu','Middleware/modbus_tcp','Middleware/motor_can_protocol']
output=root/'build/Tests';output.mkdir(parents=True,exist_ok=True)
exe=output/'test_core.exe'
cmd=[str(zig),'cc','-std=c11','-O1','-Wall','-Wextra','-Wno-unused-parameter']+[f'-I{p}' for p in includes]+sources+['Tests/native/test_core.c','-o',str(exe)]
subprocess.run(cmd,cwd=root,check=True)
result=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
match=re.search(r'RESULT checks=(\d+) power_cuts=(\d+)',result.stdout)
report={'passed':result.returncode==0,'checks':int(match[1]) if match else 0,'power_cut_cases':int(match[2]) if match else 0,'scope':'production portable C; simulated flash, no board execution','output':result.stdout}
(output/'native-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
sys.exit(result.returncode)
