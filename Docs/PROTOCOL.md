# 数据接口与设备协议

## Modbus TCP，端口 502

支持 FC3/FC4 读取、FC6/FC16 写入。以下是 PDU 的零基地址；部分工具显示的 40001 编号需自行换算。MBAP protocol=0，客户端使用 unit=1。16 位数据大端；32 位数据高寄存器在前。不存在的地址返回异常 02，非法参数或运行条件不满足异常 03，命令队列拒绝异常 06。

| 地址 | 内容 | 类型/单位 |
|---|---|---|
| 0000 | 温度 | int16，0.1℃ |
| 0001 | 湿度 | uint16，0.1%RH |
| 0002 | 有效位 | bit0 sensor、bit1 CAN、bit2 Ethernet、bit3 SD |
| 0003 | 报警掩码 | sensor=1、CAN=2、network=4、SD=8、motor=16、lease=32、Flash=64 |
| 0010 | 实际转速 | int16，rpm |
| 0011 | 电流 | int16，mA |
| 0012/0013 | 电机状态/故障码 | 状态 0 空闲、1 完成、2 运行、3 过载、4 堵转、5 欠压；3..5 同时报故障 |
| 0014..0015 | 实际位置 | int32 脉冲，高字/低字 |
| 0020/0021/0022 | 目标速度/使能/方向 | 0..600 rpm、0禁用/1使能、0/1方向，可写 |
| 0023..0025 | 绝对位置高字/低字/触发 | 仅用于六寄存器 FC16 命令，不能读取 |
| 0026 | 控制心跳 | 仅写值1，不能读取 |
| 0027 | 控制来源 | 0远程/1触摸屏 |
| 0030..0031 | 编译版本 | uint32，3.0.1=`0x00030001` |
| 0032..0033 | 运行时间 | uint32 秒 |
| 0034/0035 | OTA忙/进度 | 0/1、0..1000 千分比 |

速度运行：FC16 从 0020 写 `[rpm,1,direction]`。禁用：写 0021=0。绝对位置：FC16 从 0020 一次写 `[rpm,1,direction,pulses_hi,pulses_lo,1]`，脉冲数是非负幅度、方向单独表示。51200 脉冲/圈。心跳每 500 ms 写 0026=1；读寄存器不会续租。远程只能续远程控制，本地只能续本地控制；重连不自动重新下发运动。

TCP 口每次处理一个活动客户，空闲/不完整请求有超时。PC 和 Modbus Poll 同时监测时应考虑该限制。数据失效后寄存器仍保留最近值，必须结合有效位使用。

## 维护与升级 TCP，端口 5001

所有多字节网络字段大端。24 字节头：

| 字节偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 4 | magic=`0x49475733`，ASCII IGW3 |
| 4 | 1 | 协议版本=1 |
| 5 | 1 | operation |
| 6 | 2 | status，请求=0；应答见下 |
| 8 | 4 | session，BEGIN 的随机非零会话 |
| 12 | 4 | offset，应答为当前累计接收长度 |
| 16 | 4 | payload length，最大1024 |
| 20 | 4 | IEEE CRC32（前20字节 + payload，初值FFFFFFFF、最终异或FFFFFFFF） |

| op | 请求 payload | 行为/应答 |
|---|---|---|
| 1 BEGIN | 4×uint32：boardID、version、size、imageCRC | boardID=743A0001；版本大于 active；擦除 inactive 槽；可等待较长时间 |
| 2 DATA | <=1024 字节镜像数据 | 同 session，offset 必须等于累计值；持久化并读回后 ACK |
| 3 END | 空 | 长度/CRC/向量/编译身份/槽读回校验，提交 pending，不立即重启 |
| 4 STATUS | 空 | 6×uint32：boardID、运行版本、active、pending、offset、size；NONE=FFFFFFFF |
| 5 ABORT | 空 | 取消未结束下载，保留已提交的 pending；不能撤销已接受升级 |
| 6 REBOOT | 空 | 无下载且电机禁用时 ACK 后复位 |
| 7 GET CONFIG | 空 | 48 字节配置 |
| 8 SET CONFIG | 48 字节配置 | 电机禁用、没有下载/pending，Flash 持久化后 ACK；IP 重启生效 |

status：0 OK、1 参数错、2忙、3存储IO、4会话/顺序错、5 CRC错、6镜像错。CRC 或头错误的连接会被关闭。断开未完成下载后需要重新 BEGIN，从偏移0下载；没有断点续传。Qt 上传取消会 ABORT，已 END 的 pending 不会被悄悄移除。

配置 payload：IP4字节；随后6个uint16依次为传感器地址、功能码、起始寄存器、温度索引、湿度索引、换算编码；uint32波特率；6个int32触摸仿射Q16系数；uint32已校准标志。总48字节。温湿度索引为0或1且不同；換算编码1..10表示乘数，`0x8000|divisor` 表示除数1..1000，四舍五入，负值半值向远离0舍入。Qt 改传感器参数时保留触摸校准数据。

## PD42S1 CAN

根据用户提供的手册与示例：Classic CAN 500 kbit/s，扩展 ID `0x1001`（节点1），接收过滤 `0x1000..0x100F`。数据帧是 `C5 node opcode payload checksum 5C`，checksum 为之前全部字节的8位累加和。超过8字节拆成连续 CAN 帧，**使用同一ID，不增加CANopen或CAN FD字段**；各段发送完成后再发下一段，命令之间至少2ms，接收段间超过5ms丢弃残帧。

FA 的 payload=0启用/1禁用。F1 速度 payload=方向、加速度、IEEE754 float32 BE rpm。F2 绝对位置 payload=方向、加速度、uint16 BE rpm、uint32 BE pulses。反馈 opcode23电流、29速度均int16；2A位置int32（完整应答10字节）；2C状态byte。应答byte3=1为成功，错误应答不会当作有效测量。

600 rpm 示例：`C5 01 F1 00 64 44 16 00 00 75 5C`。实现见 `Middleware/motor_can_protocol/pd42.c`，任务层再做速度、状态、租约、命令年龄与OTA互锁。

## 部署与校验

Modbus TCP与维护TCP服务部署于局域网。CRC用于传输与存储错误检测，SHA256用于PC端固件包匹配校验，编译身份用于核对板卡、版本和应用地址。
