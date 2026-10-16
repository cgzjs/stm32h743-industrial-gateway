# 自动化测试报告

测试内容包括协议解析、数据模型、控制互锁、镜像管理、参数持久化、TCP通信和上位机界面。

## 测试结果

| 项目 | 结果 | 测试方式与输出 |
|---|---|---|
| ARM固件 | Debug / Release / Upgrade / Failure 编译链接通过 | Cortex-M7 GCC14.3.1；生成ELF、BIN、HEX、manifest |
| 生产C模块 | 5457项检查通过 | 原生C执行；`build/Tests/native-report.json` |
| 断电恢复 | 1158个场景通过 | NOR写入/擦除中断模型，重建启动状态 |
| TCP集成 | 10项通过 | Loopback TCP、3字节分片响应、生产C模块；`transport-report.json` |
| 固件产物 | 12项通过 | 四版本身份、CRC、SHA256、版本及升级包传输；`artifact-report.json` |
| Qt C++ | MOC与对象编译通过 | Qt5.15.2头文件；`cpp-report.json` |
| PyQt界面 | 演示启动、连接、采集与退出通过 | 离屏渲染；`qt-demo.png` |
| CubeMX | IOC生成外设参考源码 | USART、ETH、FDCAN、FMC、QSPI、SDMMC |
| 脚本 | Python与PowerShell语法检查通过 | compile / PowerShell Parser |

## 断电恢复场景

| 场景 | 中断案例数 |
|---|---:|
| 出厂镜像备份 | 275 |
| 下载与提交 | 275 |
| 安装与试运行 | 295 |
| 启动失败回滚 | 295 |
| 启动确认 | 9 |
| 参数日志 | 9 |

检查项目包括CRC与提交标志、完整旧版/新版恢复、未确认启动回滚、参数一致性、日志I/O错误处理、寄存器越界、负温度解析、PD42校验与分帧、控制来源租约和传感器倍率换算。

## 固件资源占用

以下为2026-10-13构建记录，单位字节。

| 固件 | Flash BIN | AXI RAM链接占用 | DMA区 |
|---|---:|---:|---:|
| Debug Bootloader | 16784 | 20664 | 0 |
| Release Bootloader | 14896 | 20664 | 0 |
| Debug Gateway 3.0.0 | 340208 | 311264 | 15296 |
| Release Gateway 3.0.0 | 292900 | 311256 | 15296 |
| Upgrade Gateway 3.0.1 | 292900 | 311256 | 15296 |
| Failure Gateway 3.0.2 | 25564 | 185944 | 0 |

内部Flash分配为Bootloader 128KiB、Application 1920KiB；AXI RAM为512KiB，DMA区为256KiB。RAM链接占用包含静态堆、主栈和预留区。Failure在调度器启动前挂起，供回滚流程测试使用。

## 复现命令

```powershell
python Tools/test_native.py
python Tests/test_transport.py
# 分别构建Debug / Release / Upgrade / Failure后执行：
python Tests/test_artifacts.py
$env:QT_QPA_PLATFORM='offscreen'
python PC_Tools/Qt_Gateway_Manager/main.py --demo --smoke-screenshot build/Tests/qt-demo.png
```

可选Qt C++对象编译检查：设置`IGW_QT_ROOT`后执行`python Tools/test_cpp.py`。桌面C++应用使用Qt SDK配套的编译器，通过`PC_Tools/Qt_Gateway_Manager/cpp`中的CMake工程构建。

## GitHub发布包检查

2026-10-16在独立发布目录完成固定提交依赖下载、Release固件构建、原生C检查、TCP集成和PyQt演示。发布检查覆盖文档链接、JSON、个人路径、常见凭证模式、Git暂存文件、ZIP完整性与逐文件SHA256。

GitHub Actions执行原生C、TCP和PyQt演示测试，并保存报告和界面截图。烧录与设备联调步骤见[BRINGUP](BRINGUP.md)。
