# 触摸校准误点后的恢复

现有固件可从屏幕 `System → Touch` 重新校准。若坐标偏移大到无法操作该按钮，可通过现有ST-Link重新进入校准页，不需要联网或重烧固件。

1. 给开发板正常供电，ST-Link插回电脑，连接SWDIO、SWCLK、GND；无需NRST。带VTref检测的仿真器仍需其电压参考连接。
2. 关闭其他占用ST-Link的调试会话，电机保持停止。
3. VS Code运行任务 **Gateway: Recalibrate touch (ST-Link)**；或在PowerShell执行：

```powershell
cd <你的工程目录>
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\recalibrate_touch.ps1
```

脚本先只读上传内部Flash，与本地BIN逐字节对应的SHA256核对，选取匹配ELF；不匹配则停止。然后使用软件复位、临时硬件断点在UI任务入口暂停，仅将RAM中`configuration.touch_calibrated`设0，再继续运行。没有Flash擦除、固件下载或配置扇区清空命令。其他参数保留。校准成功后的正常保存仍由固件存储任务执行。

4. 看到校准页后，用塑料触控笔点当前显示的 **＋中心**，不要点屏幕的物理边角。每个点轻按稳后完全松开，再点下一个；当前固件共三个校准点。
5. 校准完成回到主界面后等待几秒，让存储任务保存，再正常重启确认。

如果没有完成新校准就断电，RAM临时标志会消失，下次仍加载之前的校准值，可再次运行恢复脚本。不要为了触摸校准全片擦除或重新执行首次烧录；OTA备份可能恢复原应用，而校准参数本来就保存在外部Flash。

`-CheckOnly`用于核查本地符号并生成GDB命令。恢复脚本使用OpenOCD软件复位与硬件断点控制执行流程，日志保存在`build/TouchRecovery`。
