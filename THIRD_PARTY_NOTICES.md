# Third-party notices

The root MIT license covers the original gateway application, protocol, board
adaptation, host tools and documentation. It does not replace third-party
licenses or their existing copyright notices.

## Template-derived files included in this repository

| Files | Origin | Applicable notice |
|---|---|---|
| `Core/Inc/stm32h7xx_hal_conf.h` | ST HAL configuration template, with enabled modules selected | [ST HAL BSD-3-Clause](LICENSES/ST-HAL-BSD-3-Clause.txt) |
| `Core/Src/system_stm32h7xx.c` | ST CMSIS device system template, modified for vector relocation | [ST CMSIS Device Apache-2.0](LICENSES/ST-CMSIS-Device-Apache-2.0.txt) |
| `Core/Inc/ffconf.h` | FatFs/ST configuration template, modified for this target | [FatFs](LICENSES/FatFs.txt), [ST notice](LICENSES/FatFs-ST.txt) |

The notices in these source files are retained. Modified files are listed above;
the original copyright holders do not endorse this project.

## Dependencies fetched separately

`Tools/fetch_dependencies.py` downloads the exact revisions in
`Vendor/dependencies.lock.json`, including their upstream license files.
STM32CubeH7 contains components with different licenses; consult each component's
license rather than assigning the root MIT license to the complete SDK.

LVGL retains its upstream MIT license. PyQt5 uses GPLv3 or a commercial license;
Qt and Zig retain their respective upstream terms. This repository does not bundle
Python, Qt DLLs, toolchains or SDK installers. A bundled executable distribution
needs the notices and applicable terms of its dependencies as well.

## Local reference materials

Motor manuals, extracted vendor examples, ALIENTEK board reference examples,
personal hardware photos and local SDK repositories are excluded from the public
source package. They are not required to build the checked-in implementation once
the pinned firmware dependencies have been fetched.
