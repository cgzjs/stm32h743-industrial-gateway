"""Compile the Qt C++ frontend against installed headers/MOC; this does not link an EXE."""
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if not os.environ.get('IGW_QT_ROOT'):
    raise SystemExit('Set IGW_QT_ROOT to a Qt SDK root for this optional object-only check.')
qt = Path(os.environ['IGW_QT_ROOT'])
out = ROOT / 'build/Tests'
out.mkdir(parents=True, exist_ok=True)
zig = ROOT / 'Tools/python/ziglang/zig.exe'
source = ROOT / 'PC_Tools/Qt_Gateway_Manager/cpp/main.cpp'
subprocess.run([str(qt / 'bin/moc.exe'), str(source), '-o', str(out / 'main.moc')], check=True)
command = [str(zig), 'c++', '-c', '-std=c++17', '-Wno-ignored-attributes']
command += [f'-D{x}' for x in ('QT_NO_DEBUG', 'QT_WIDGETS_LIB', 'QT_NETWORK_LIB', 'QT_GUI_LIB', 'QT_CORE_LIB')]
command += [f'-I{qt / "include/qt" / name}' for name in ('', 'QtCore', 'QtGui', 'QtWidgets', 'QtNetwork')]
command += [f'-I{ROOT / "Middleware/crc"}', f'-I{out}', str(source), '-o', str(out / 'qt_frontend.obj')]
subprocess.run(command, cwd=ROOT, check=True)
report = {'object_compilation_passed': True, 'exe_linked': False, 'qt_sdk': str(qt), 'limitation': 'Qt DLL uses MSVC ABI; desktop MSVC toolchain unavailable. Runtime verification uses PyQt frontend.'}
(out / 'cpp-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('PASS Qt C++17 / MOC object compilation; executable link not performed')
