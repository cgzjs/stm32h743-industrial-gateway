"""Create a new, checked source directory and ZIP suitable for GitHub upload.

No existing directory is overwritten. No GitHub authentication or upload occurs.
"""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = {'.github', '.vscode', 'Application', 'Bootloader', 'BSP', 'cmake',
               'Core', 'Docs', 'Include', 'Middleware', 'PC_Tools', 'System',
               'Tests', 'Tools', 'LICENSES'}
TOP_FILES = {'.gitignore', '.gitattributes', '.clang-format', 'CMakeLists.txt',
             'CMakePresets.json', 'Industrial_Gateway.ioc', 'README.md',
             'LICENSE', 'THIRD_PARTY_NOTICES.md',
             '\u6253\u5f00\u4e0a\u4f4d\u673a.cmd',
             '\u6253\u5f00\u4e0a\u4f4d\u673a\u6f14\u793a.cmd'}
EXCLUDED = {'Tools/local.env.ps1', 'Tools/cubemx.script',
            'Tools/generate_ioc.py', 'Tools/setup_project.py'}
BINARY_SUFFIXES = {'.png', '.jpg', '.jpeg', '.ico'}
GENERATED_SUFFIXES = {'.pyc', '.pyo', '.exe', '.dll', '.obj', '.o', '.elf',
                      '.bin', '.hex', '.map', '.log', '.zip', '.pid'}


def files():
    selected = []
    # Explicit roots avoid traversing SDK and toolchain directories at all.
    for name in sorted(TOP_FILES):
        path = ROOT / name
        if path.is_file():
            selected.append(path)
    selected.append(ROOT / 'Vendor/dependencies.lock.json')
    for name in sorted(DIRECTORIES):
        folder = ROOT / name
        if not folder.is_dir():
            continue
        for path in sorted(folder.rglob('*')):
            if not path.is_file():
                continue
            relative = path.relative_to(ROOT)
            if any(part in {'.git', '__pycache__', '.venv'} for part in relative.parts):
                continue
            if relative.parts[:2] == ('Tools', 'python'):
                continue
            if relative.as_posix() in EXCLUDED or path.suffix.lower() in GENERATED_SUFFIXES:
                continue
            if '.local.' in path.name:
                continue
            if path.name.startswith('.env') and path.name != '.env.example':
                continue
            if path.suffix.lower() in {'.pem', '.key', '.pfx', '.p12'}:
                continue
            selected.append(path)
    return selected


def public_bytes(path):
    relative = path.relative_to(ROOT).as_posix()
    if relative == '.vscode/settings.json':
        return (json.dumps({
            'cmake.useCMakePresets': 'always', 'cmake.configureOnOpen': False,
            'C_Cpp.default.compileCommands': '${workspaceFolder}/build/Debug/compile_commands.json',
            'files.associations': {'*.ioc': 'properties', '*.h': 'c'},
            'search.exclude': {'Vendor': True, 'build': True, 'Tools/python': True, 'CubeGenerated': True},
        }, indent=2) + '\n').encode()
    if relative == '.vscode/launch.json':
        config = json.loads(path.read_text(encoding='utf-8'))
        for item in config['configurations']:
            item['miDebuggerPath'] = '${input:armGdb}'
        config['inputs'] = [{'id': 'armGdb', 'type': 'promptString',
                             'description': 'ARM GDB executable (full path, or a name on PATH)',
                             'default': 'arm-none-eabi-gdb.exe'}]
        return (json.dumps(config, indent=2) + '\n').encode()
    if relative == '.vscode/tasks.json':
        config = json.loads(path.read_text(encoding='utf-8'))
        for item in config['tasks']:
            if 'python' in item.get('command', '').lower():
                original_args = item.get('args', [])
                item['command'] = 'powershell'
                item['type'] = 'process'
                if item['label'] == 'Gateway: Qt demo':
                    item['args'] = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                    '${workspaceFolder}/Tools/open_manager.ps1', '-Demo']
                else:
                    item['args'] = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                    '${workspaceFolder}/Tools/python_task.ps1', '-Script', *original_args]
        return (json.dumps(config, indent=2) + '\n').encode()
    if relative == 'Industrial_Gateway.ioc':
        text = path.read_text(encoding='utf-8')
        text = re.sub(r'(?m)^ProjectManager.CustomerFirmwarePackage=.*$',
                      'ProjectManager.CustomerFirmwarePackage=Vendor/STM32CubeH7', text)
        return text.encode()
    return path.read_bytes()


def validate(payload):
    required = {'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CMakeLists.txt',
                'Vendor/dependencies.lock.json', 'Tools/fetch_dependencies.py',
                'Docs/GITHUB.md', 'Docs/images/pc-demo.png'}
    missing = required - payload.keys()
    if missing:
        raise ValueError(f'Missing public files: {sorted(missing)}')
    for name, data in payload.items():
        if len(data) >= 100_000_000:
            raise ValueError(f'File too large for normal GitHub source upload: {name}')
        if Path(name).suffix.lower() in BINARY_SUFFIXES:
            continue
        text = data.decode('utf-8-sig')
        if re.search(r'(?i)C:[/\\]Users[/\\]lty|D:[/\\]anaconda|D:[/\\]stm32|D:[/\\]CubeIDE', text):
            raise ValueError(f'Personal workstation path remains: {name}')
        if re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\bghp_[A-Za-z0-9]{36}\b|\bgithub_pat_[A-Za-z0-9_]{40,}', text):
            raise ValueError(f'Possible credential in {name}; inspect before publishing')
        if name.endswith('.json'):
            json.loads(text)
        if name.endswith('.md'):
            for link in re.findall(r'\]\(([^)\s]+)\)', text):
                if '://' in link or link.startswith(('#', 'mailto:')):
                    continue
                target = (Path(name).parent / link.split('#')[0]).as_posix()
                if target not in payload:
                    raise ValueError(f'Broken local document link in {name}: {link}')


def collect_payload():
    payload = {}
    for path in files():
        name = path.relative_to(ROOT).as_posix()
        data = public_bytes(path)
        if path.suffix.lower() not in BINARY_SUFFIXES:
            text = data.decode('utf-8-sig').replace('\r\n', '\n')
            text = '\n'.join(line.rstrip() for line in text.splitlines()).rstrip() + '\n'
            if path.suffix.lower() in {'.ps1', '.cmd'}:
                text = text.replace('\n', '\r\n')
            data = text.encode('utf-8')
        payload[name] = data
    validate(payload)
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT.parent / 'Industrial_Gateway_GitHub')
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    payload = collect_payload()
    size = sum(map(len, payload.values()))
    print(f'PASS public source checks: {len(payload)} files, {size:,} bytes')
    if args.check_only:
        return
    output = args.output.resolve()
    archive = output.with_suffix('.zip')
    checksum = Path(str(archive) + '.sha256')
    if output.exists() or archive.exists() or checksum.exists():
        raise FileExistsError('Output already exists. Choose a new --output; existing files are never overwritten.')
    output.mkdir(parents=True)
    manifest = {'format': 1, 'scope': 'Public source only; no SDK, firmware images or local references',
                'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(payload.items())}}
    payload['PUBLISH_MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    for name, data in payload.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as zipped:
        for name, data in sorted(payload.items()):
            zipped.writestr(f'{output.name}/{name}', data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum.write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    print(f'Source: {output}\nZIP: {archive}\nSHA256: {digest}')


if __name__ == '__main__':
    main()
