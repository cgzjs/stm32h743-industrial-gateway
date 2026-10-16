"""Fetch only the firmware dependencies, at the commits recorded in the lock file."""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'Vendor/dependencies.lock.json').read_text(encoding='utf-8'))
CUBE_PATHS = ['Drivers/CMSIS/Include', 'Middlewares/Third_Party/FreeRTOS',
              'Middlewares/Third_Party/LwIP', 'Middlewares/Third_Party/FatFs']


def git(*args, cwd=None):
    return subprocess.check_output(['git', *args], cwd=cwd, text=True).strip()


def fetch(name, destination, sparse=None, check=False):
    entry = LOCK[name]
    commit = entry['commit']
    if (destination / '.git').exists():
        actual = git('rev-parse', 'HEAD', cwd=destination)
        if actual != commit:
            raise RuntimeError(f'{name}: expected {commit}, found {actual}; existing checkout was not changed')
        if git('status', '--porcelain', '--untracked-files=no', cwd=destination):
            raise RuntimeError(f'{name}: tracked files have local changes; refusing to replace them')
        print(f'OK {name}: {actual}', flush=True)
        return
    if check:
        raise RuntimeError(f'{name}: dependency is missing; run python Tools/fetch_dependencies.py')
    if destination.exists() and any(destination.iterdir()):
        raise RuntimeError(f'{destination}: nonempty directory is not a Git checkout')
    destination.mkdir(parents=True, exist_ok=True)
    git('init', str(destination))
    git('remote', 'add', 'origin', entry['url'], cwd=destination)
    if sparse:
        git('sparse-checkout', 'init', '--cone', cwd=destination)
        git('sparse-checkout', 'set', *sparse, cwd=destination)
    # Fetch by full commit, without downloading the repository's history.
    git('fetch', '--depth=1', '--filter=blob:none', 'origin', commit, cwd=destination)
    git('checkout', '--detach', 'FETCH_HEAD', cwd=destination)
    if git('rev-parse', 'HEAD', cwd=destination) != commit:
        raise RuntimeError(f'{name}: commit verification failed')
    print(f'Fetched {name}: {commit}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify existing checkouts without downloading')
    args = parser.parse_args()
    vendor = ROOT / 'Vendor'
    cube = vendor / 'STM32CubeH7'
    fetch('STM32CubeH7', cube, CUBE_PATHS, args.check)
    fetch('STM32H7xx_HAL_Driver', cube / 'Drivers/STM32H7xx_HAL_Driver', check=args.check)
    fetch('cmsis_device_h7', cube / 'Drivers/CMSIS/Device/ST/STM32H7xx', check=args.check)
    fetch('lvgl', vendor / 'lvgl', ['src'], args.check)
    required = [cube / 'Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h',
                cube / 'Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h743xx.h',
                cube / 'Middlewares/Third_Party/FreeRTOS/Source/tasks.c',
                cube / 'Middlewares/Third_Party/LwIP/src/core/init.c',
                cube / 'Middlewares/Third_Party/FatFs/src/ff.c', vendor / 'lvgl/lvgl.h']
    for path in required:
        if not path.is_file():
            raise RuntimeError(f'Missing dependency file: {path.relative_to(ROOT)}')
    print('All pinned firmware dependencies are ready.')


if __name__ == '__main__':
    main()
