"""Package or upload an application. Bootloader.bin is never accepted as an OTA image."""
import argparse,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'PC_Tools/Qt_Gateway_Manager'))
from protocol import package_image,MaintenanceClient,version_string
parser=argparse.ArgumentParser()
sub=parser.add_subparsers(dest='command',required=True)
p=sub.add_parser('package');p.add_argument('binary');p.add_argument('--version',required=True)
p=sub.add_parser('upload');p.add_argument('binary');p.add_argument('--host',required=True);p.add_argument('--port',type=int,default=5001);p.add_argument('--no-reboot',action='store_true')
p=sub.add_parser('status');p.add_argument('--host',required=True);p.add_argument('--port',type=int,default=5001)
args=parser.parse_args()
if args.command=='package':print(package_image(args.binary,args.version))
else:
    client=MaintenanceClient(args.host,args.port)
    try:
        if args.command=='status':print(client.status())
        else:print(client.upload(args.binary,progress=lambda x:print(f'\r{x:.1f}%',end='',flush=True),reboot=not args.no_reboot))
    finally:client.close()
