# 依赖与可复现构建

## 固件依赖

```powershell
python Tools/fetch_dependencies.py
python Tools/fetch_dependencies.py --check
```

提交由 `Vendor/dependencies.lock.json` 固定；脚本获取 STM32CubeH7 v1.12.1、其 HAL/CMSIS Device 对应提交以及 LVGL v8.3.11。Cube 根仓库稀疏检出 CMSIS Core、FreeRTOS、LwIP、FatFs；HAL 和 CMSIS Device 按独立仓库的固定提交获取。源码保留上游许可证。

Git 与网络是首次获取的前提。已存在的仓库如果版本不符或有已跟踪文件改动，脚本报错，不会自动重置本地更改。下载不包含电机手册、原厂示例或其他板卡参考仓库。网络中断留下的未完成目录可先移到工程外再重新获取，避免覆盖仍需保留的文件。

## Windows 工具

- Python 3.10+；PyQt 依赖用 `python -m pip install -r PC_Tools/Qt_Gateway_Manager/requirements.txt` 安装。
- CMake 3.22+、Ninja、ARM GNU；历史构建使用 GCC 14.3.1。
- STM32CubeIDE，历史版本 1.19.0：提供集成 CubeMX / Programmer / OpenOCD。普通 CMake 固件构建不需要启动 IDE。
- 原生 C 测试首次自动将 Zig 0.13.0 安装到忽略的 `Tools/python`，模拟器与测试不需要 STM32 SDK。

环境变量：

| 变量 | 内容 |
|---|---|
| `ARM_GCC_DIR` | `arm-none-eabi-gcc.exe` 等工具所在 bin 目录；或直接将其加 PATH |
| `IGW_PYTHON` | Python 可执行文件；脚本也会查找工程 `.venv` 与 PATH |
| `CUBE_IDE_DIR` | 含 plugins 目录的 STM32CubeIDE 根目录 |
| `IGW_QT_ROOT` | 可选的 Qt C++ 对象编译检查所使用的 SDK 根目录 |

可复制 `Tools/local.env.example.ps1` 为 `Tools/local.env.ps1` 后配置本机路径。不要提交该文件。PowerShell 辅助脚本加载它，直接运行 Python/CMake 时需自行在当前终端设置相应变量。

VS Code 的 CMake 扩展需要在其进程中能够找到 CMake/Ninja/编译器；安装工具并配置环境变量后重新打开 VS Code。F5 的附加配置会询问 GDB 路径，只连接已烧录的 Debug 固件；实际烧录使用单独任务。

## 代码来源与许可

见根目录 `THIRD_PARTY_NOTICES.md` 和 `LICENSES/`。PD42S1 手册、原厂样例和本地板卡参考只用于硬件/协议核对，没有纳入公开包。

不要直接运行历史配置生成器覆盖已修改的启动与链接文件；当前受支持的入口为根目录 CMake 和 `Tools/cubemx.ps1`。更换 HAL / FreeRTOS / LwIP / LVGL 版本后，需重新验证适配层与硬件。
