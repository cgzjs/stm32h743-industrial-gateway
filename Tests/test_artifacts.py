"""Validate generated ARM artifacts and transmit real bins through the native OTA implementation.

ARM instructions are not executed here; boot success/failure is modelled by the C bridge.
"""
import hashlib
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'PC_Tools/Qt_Gateway_Manager'))
from protocol import MaintenanceClient, ProtocolError, load_image, package_image, validate_image
from simulator import Simulator


def rejected(action):
    try:
        action()
    except ProtocolError:
        return
    raise AssertionError('Invalid artifact was accepted')


def main():
    checks = 0
    artifacts = []
    for mode, version in [('Debug', 0x30000), ('Release', 0x30000), ('Upgrade', 0x30001), ('Failure', 0x30002)]:
        path = ROOT / 'build' / mode / 'Gateway.bin'
        data, manifest = load_image(path)
        assert manifest['version'] == version
        assert len(data) == manifest['size']
        rejected(lambda: validate_image(path.with_name('Bootloader.bin').read_bytes()))
        checks += 2
        artifacts.append(dict(mode=mode, **manifest))
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / 'Gateway.bin'
        source = ROOT / 'build/Upgrade/Gateway.bin'
        binary.write_bytes(source.read_bytes())
        rejected(lambda: package_image(binary, '3.0.2'))
        package_image(binary, '3.0.1')
        bad = bytearray(binary.read_bytes())
        bad[-1] ^= 1
        binary.write_bytes(bad)
        rejected(lambda: load_image(binary))
        checks += 2
    server = Simulator(fragment=True)
    server.start()
    try:
        client = MaintenanceClient('127.0.0.1', server.ota_port)
        client.upload(ROOT / 'build/Upgrade/Gateway.bin')
        assert client.status()['version'] == 0x30001
        checks += 1
        with server.lock:
            server.dll.sim_fail_trial(1)
        client.upload(ROOT / 'build/Failure/Gateway.bin')
        assert client.status()['version'] == 0x30001
        checks += 1
        client.close()
    finally:
        server.close()
    report = {'passed': True, 'checks': checks, 'scope': 'actual ARM bin packaging/transfer; native C installation/rollback model, no ARM execution', 'artifacts': artifacts}
    (ROOT / 'build/Tests/artifact-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS {checks} generated-artifact checks, including real Upgrade/Failure bin transfers')


if __name__ == '__main__':
    main()
