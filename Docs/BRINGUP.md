# 上板、烧录与 V3 验收

## 硬件配置与接口

代码目标是正点原子阿波罗 STM32H743IIT6、25 MHz HSE、2 MB 内部 Flash、W25Q256 32 MB 外部 NOR、LAN8720 RMII。运行时会核对内部 Flash 容量和 NOR JEDEC 容量字节 `0x19`；不同型号需要先调整驱动和分区，不能仅改宏绕过检查。

| 接口 | 工程引脚/参数 | 连接说明 |
|---|---|---|
| 调试串口 USART1 | PA9 TX / PA10 RX，115200 8N1 | 查看 LCD ID 等诊断 |
| RS485 USART3 | PB10 TX / PB11 RX；DMA1 Stream0 RX、Stream1 TX | 断开收发器 TTL 侧原 PA2/PA3 跳线，再改接此处；PB10 接收发器 DI，PB11 接 RO |
| RS485 方向 | PCF8574 P6 | 原板方向控制保持；PH4/PH5 软件 I2C |
| CAN1 | PA11 RX / PA12 TX，500 kbit/s | 使用 CAN 收发器，CANH/CANL 接电机，检查两端 120Ω 与公共参考地 |
| Ethernet RMII | PA1 REFCLK / PA2 MDIO / PA7 CRS_DV；PC1 MDC / PC4 RXD0 / PC5 RXD1；PG11 TX_EN / PG13 TXD0 / PG14 TXD1 | LAN8720，PCF8574 P7 PHY reset；确认板上 50 MHz 时钟来源与跳线 |
| QSPI | PB2 CLK / PB6 NCS / PF8 IO0 / PF9 IO1 / PF7 IO2 / PF6 IO3 | 程序采用单线专用 4 字节地址指令；无 memory mapped 模式 |
| SDMMC1 | PC8..PC11 D0..D3 / PC12 CLK / PD2 CMD | 4 bit、IDMA；SD 预先格式化为 FAT，程序不会自动格式化 |
| LCD FMC | PD7 NE1 / PD4 RD / PD5 WR / PD13 A18；PD0/1/8/9/10/14/15、PE7..15 数据 | 16 bit；命令 `0x6007FFFE`、数据 `0x60080000`；PB5 背光 |
| XPT2046 | PH6 CLK / PI3 MOSI / PG3 MISO / PI8 CS / PH7 PEN | 软件 SPI，3 点仿射校准 |

屏幕使用16位并口和XPT2046触摸控制器。程序读取LCD ID，识别0x9341后初始化ILI9341；识别信息通过USART1输出。若读到全 0/全 1，先检查供电、读时序、CS/RS 与跳线；不要直接套用 9341 初始化。

RS485 默认 slave=1、9600、8N1、FC3、从 0 开始读取两个连续寄存器，第 0 个有符号温度、第 1 个湿度，默认已是 ×10。按设备协议配置地址、波特率、校验位、寄存器、顺序、负温度编码和缩放；Qt 参数页可改地址/波特率/FC3或4/起始地址/顺序/乘除换算。若设备要求奇偶校验或不连续寄存器，需修改 UART/传感器轮询实现，当前界面未提供该选项。

## 首次正常烧录

### 2026-10-15 实物照片补充

电机插头标签确认 CAN / PLC 版；温湿度变送器供电标签为 **DC5–36V**。电机驱动器与变送器可以共用24.0V直流电源，正负极分别并联，电源需要满足电机手册24V/3A建议并为传感器留余量。开发板先沿用原配电源；若也共用24V电源，应通过降压支路按原配供电要求接DC_IN1，不要把24V接到5V/3.3V/GPIO。

按照片中当前插头装配方向，线色如下；上电前以端子丝印和线序连续性复核，不能按红正黑负套用：

| 设备 | 端子 | 当前线色 |
|---|---|---|
| 电机 | V+ / GND | 黑 / 白 |
| 电机 | T/A/H（CAN_H） / R/B/L（CAN_L） | 黄 / 橙 |
| 电机 | Lim | 红，非电源正极 |
| 温湿度 | + / − | 棕 / 红 |
| 温湿度 | A / B | 橙 / 黄 |

电机CAN与传感器RS485分别接板载对应收发器，不能因线色相同就把两条总线短接。外部电源负极、传感器负极、电机GND与开发板GND共参考；电机的大电流电源线直接返回电源，不从开发板排针取电。

照片中USB/CAN选择区为P9，位于USART1选择区上方，两帽分别连接CAN_TX↔PA12、CAN_RX↔PA11，不选择D+/D−。以最新CAN近照方向，上排CAN_RX/CAN_TX、中排PA11/PA12、下排D−/D+，两帽竖跨上中排。蓝色CAN端子左CANL、右CANH。照片中标PHONE、LINE_IN的绿色接口是音频接口，不是CAN/RS485电源端子。网口旁另一蓝色端子为RS485；本项目实物已核对为左B、右A。不同批次仍以端子旁A/B丝印为准。

P7/P8改接：断电后拔掉P7、P8共四只跳线帽，使PA2/PA3脱离232/485、PB10/PB11脱离COM3/GPS。接两根杜邦线：**P8中排PB10(TX)→P7下排485_RX；P8中排PB11(RX)←P7下排485_TX**。照片里P8中排左PB11、右PB10；P7下排左485_RX、右485_TX，即两线交叉连接。485_RX是收发器输入命名，对应MCU TX，不能按同名TX接TX。P7中排PA2/PA3不再插帽，避免Ethernet MDIO的PA2与其他驱动输出相连。

电机九线中当前CAN控制只使用V+黑、GND白、CAN_H黄、CAN_L橙四根。其余COM灰、En紫、Step蓝、Dir绿、Lim红分别绝缘悬空，不互连。电机本体到驱动器的四针绕组插头保持原连接。

ST-Link板端：TF卡槽下方黄色20针JTAG插座也支持SWD。按标准ARM20接口定义，板端1=3.3V参考、4=GND、7=TMS/SWDIO(PA13)、9=TCK/SWCLK(PA14)、15=NRST。按PCB针1标记数，不能按照片上下猜方向；ST-Link侧按其信号名接，仿真器自身针序取决型号。独立供电时VTref/VAPP接板3.3V用于电压检测，只有3.3V电源输出而无VTref的简易仿真器先不并接供电输出。首次烧录脚本默认NORMAL连接+软件复位，不强制NRST；若已接好NRST，可加 `-ResetMode Hardware` 使用硬件under-reset。参考ST官方UM1075表4：https://www.st.com/resource/en/user_manual/um1075-stsw-link007.pdf 。

先让电机处于静止、无机械负载的联调状态，完成上述 RS485 改接，接好 ST-Link 的 SWDIO/SWCLK/GND 与板卡供电；NRST有条件时可接，带VTref/VAPP检测的仿真器还需连接目标电压参考。初次出厂使用 3.0.0 正常固件：

```powershell
Set-Location <你的工程目录>
.\Tools\build.ps1 -Mode Release
.\Tools\program_factory.ps1 -Mode Release
```

脚本调用 CubeProgrammer：Bootloader 写到 `0x08000000`，Gateway 写到 `0x08020000`，分别读回校验，再复位。此脚本适用于外部 NOR 尚无有效 IGW3 启动日志的板卡。

VS Code 中也可按 `Ctrl+Shift+P` → `Tasks: Run Task / 任务: 运行任务` → **Gateway: Flash factory (Release)**。该任务先构建Release，再校验应用manifest/SHA256，然后执行上述双镜像烧录，成功时终端显示 `Factory programming complete: both images verified and target reset.`。左侧F5的 `Gateway: attach ST-Link` 只附加已烧录的程序，不会下载固件。STM32扩展的“项目设置/板卡设备”页不是这份自定义CMake工程的首次烧录入口。

若终端因PowerShell执行策略拦截，可执行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\program_factory.ps1 -Mode Release`；只对这一进程放开，不修改系统执行策略。STM32CubeProgrammer通过Hot Plug可读取芯片ID、内核、Flash容量和目标电压。烧录前关闭其他占用ST-Link的调试会话。若旧固件关闭SWD或持续复位，普通软件连接可能失败，此时需要复位按键配合连接，或补NRST线使用硬件under-reset。

启动约 10 s 持续任务健康且 Ethernet 栈初始化成功后，正常固件会把内部应用备份到 A 槽，提交出厂日志；在此之前禁止运动与升级。物理网线、传感器、电机或 SD 离线会报警，但不单独阻止固件确认，避免外部设备离线导致反复回滚。试运行固件 45 s 内未能确认会停止喂狗，随后进入回滚。

PC 网卡设为例如 `192.168.10.2/24`，网关默认 `192.168.10.50/24`，不要与现有设备 IP 冲突。Qt 输入该 IP；Modbus TCP 502、维护 TCP 5001。参数修改后 IP 在重启后生效，传感器参数下一轮生效。首次触摸启动依次点击三个十字并松开，配置由存储任务持久化。

检查温湿度有效位、电机实际状态、SD READY、曲线和报警。SD 日志 `DATA0000.CSV` 等，每文件约 8 MB 轮换，`ALARMS.CSV` 记录报警状态变化，时间是启动秒数；未提供实时时钟日期。Qt 可导出本次会话 CSV。

首次触摸校准误点、无法进入System/Touch时，见 [触摸恢复](TOUCH_RECOVERY.md)：可用ST-Link临时清除RAM校准标志，重新显示校准页，无需联网或重新烧录。

## 正常远程升级与失败回滚

```powershell
.\Tools\build.ps1 -Mode Upgrade
python Tools/firmware.py status --host 192.168.10.50
python Tools/firmware.py upload build/Upgrade/Gateway.bin --host 192.168.10.50
```

也可在 Qt 固件升级页选择同一 `Gateway.bin`。下载前自动禁用电机并等待确认；CAN 禁用未应答时会拒绝升级。下载校验结束后请求重启，Bootloader 安装 3.0.1，试运行成功后约 10 s 确认。读取版本和 pending 状态，pending 应变为 `0xFFFFFFFF`。

已有正常已确认固件后，再上传 `build/Failure/Gateway.bin`。它编译为 3.0.2，故意在调度器前挂起，继承的 IWDG 应触发复位，Bootloader 应恢复上一已确认版本 3.0.1。IWDG 在 LSI=32 kHz 时约 16.4 s，实际受 LSI 偏差影响。不要把 Failure 首次烧录到无备份板卡。

| 联调项目 | 操作 | 通过条件 |
|---|---|---|
| V1 闭环 | Qt 读温湿度、100 rpm 启停、低速位置控制 | 数据正确、CAN 反馈正确、位置方向符合手册 |
| V2 断线恢复 | 分别断 RS485/CAN/网线与拔 SD，再接回 | 明确报警；恢复采集；网络重连不自动运动 |
| V3 正常升级 | 3.0.0 → 3.0.1 | 版本更新、pending 清除、可再次重启 |
| 下载中断 | 上传中断网/断电，重新连接并重新上传 | 旧固件继续启动；新下载从 0 开始 |
| 安装断电 | 完整上传后复位，在内部 Flash 擦写期间切断板卡电源，再上电；重复不同时间点 | 重装完整候选或恢复完整旧版，无跳入残缺镜像 |
| 新固件失败 | 正常版本确认后上传 Failure | IWDG 复位，回滚上一确认版本 |
| 确认/参数写入断电 | 在提交附近断电，多次重启 | 仅接受 CRC+commit 完整记录，配置是完整旧值或新值 |

租约超时后，网关通过CAN发送禁用指令。联调时同时检查CAN反馈、电机状态及设备的独立急停线路。

## 已有启动日志时通过 SWD 换回出厂固件

Bootloader 会维护外部 NOR 的已确认镜像，直接 SWD 覆盖内部 Gateway 可能被它恢复成旧版。需要换回本工程出厂版本时使用显式恢复流程：

```powershell
.\Tools\build.ps1 -Mode Debug
.\Tools\recover_factory.ps1 -Mode Debug -RecoverJournal
```

脚本通过 OpenOCD/GDB 在 `boot_factory_gate`（IWDG 尚未启动）暂停，覆盖正常应用，写入恢复请求，执行 Bootloader 清除两个启动日志，再正常进入应用并重新建立 A 槽备份。配置扇区与其他槽内容保留。需要板上 Bootloader 与所选 ELF 对应，Bootloader 已损坏时先用 CubeProgrammer 重写它。

VS Code 的附加调试会暂停已启动的看门狗系统，长时间暂停可能复位；调试实际 Flash 写入和确认时应考虑这一点。双镜像都无效时 Bootloader 保持 SWD 可用、闪灯，不执行错误向量。
