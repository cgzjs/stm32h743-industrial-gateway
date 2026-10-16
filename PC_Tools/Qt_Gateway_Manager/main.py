"""Qt 5 desktop manager. Network calls run outside the GUI thread."""
import sys,time,queue,csv,threading
from pathlib import Path
from collections import deque
from PyQt5 import QtCore,QtGui,QtWidgets as W
from protocol import ModbusClient,MaintenanceClient,version_string,load_image
ALARMS={1:'温湿度离线',2:'CAN 离线',4:'网线断开',8:'SD 卡异常',16:'电机故障',32:'控制超时停机',64:'Flash 异常'}
class Worker(QtCore.QThread):
    snapshot=QtCore.pyqtSignal(dict);message=QtCore.pyqtSignal(str);configuration=QtCore.pyqtSignal(list)
    def __init__(self,host,modbus_port,ota_port):
        super().__init__();self.host=host;self.modbus_port=modbus_port;self.ota_port=ota_port;self.jobs=queue.Queue();self.shutdown=threading.Event();self.armed=False
    def run(self):
        client=None
        while not self.shutdown.is_set():
            try:
                if client is None:client=ModbusClient(self.host,self.modbus_port);self.message.emit('网关已连接');self.armed=False
                while not self.jobs.empty():
                    action,values,tick=self.jobs.get_nowait()
                    if time.monotonic()-tick>2:raise RuntimeError('指令已过期，请重新操作')
                    if action=='run':client.run(*values);self.armed=True
                    elif action=='move':client.move(*values);self.armed=True
                    elif action=='stop':client.stop();self.armed=False
                    else:
                        m=MaintenanceClient(self.host,self.ota_port)
                        try:
                            c=m.get_config()
                            if action=='set_config':c[:11]=values;m.set_config(c);self.message.emit('参数已保存；IP 更改在重启后生效')
                            else:self.configuration.emit(c)
                        finally:m.close()
                data=client.snapshot()
                if data['ota_busy']:self.armed=False
                if self.armed and data['enabled'] and data['owner']==0:client.heartbeat()
                self.snapshot.emit(data);self.shutdown.wait(.5)
            except Exception as e:
                if client:client.close()
                client=None;self.armed=False;self.message.emit(str(e));self.shutdown.wait(1)
        if client:
            try:client.stop()
            except Exception:pass
            client.close()
    def submit(self,action,values=()):self.jobs.put((action,values,time.monotonic()))
class Uploader(QtCore.QThread):
    progress=QtCore.pyqtSignal(int);message=QtCore.pyqtSignal(str)
    def __init__(self,host,port,path):super().__init__();self.host=host;self.port=port;self.path=path;self.cancelled=threading.Event()
    def run(self):
        try:
            m=MaintenanceClient(self.host,self.port)
            try:
                m.upload(self.path,progress=lambda p:self.progress.emit(round(p)),cancel=self.cancelled.is_set)
                self.message.emit('镜像已校验并请求重启；重新连接后检查运行版本和回滚结果')
            finally:m.close()
        except Exception as e:self.message.emit(str(e))
class Trend(W.QWidget):
    def __init__(self):super().__init__();self.points=deque(maxlen=180);self.setMinimumHeight(190)
    def push(self,d):self.points.append((d['temperature'] if d['valid']&1 else None,d['humidity'] if d['valid']&1 else None));self.update()
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing);p.fillRect(self.rect(),QtGui.QColor('#132334'))
        box=self.rect().adjusted(40,20,-20,-30);p.setPen(QtGui.QColor('#30465b'))
        for value in range(-40,101,20):
            y=box.bottom()-(value+40)*box.height()/140;p.drawLine(QtCore.QPointF(box.left(),y),QtCore.QPointF(box.right(),y));p.drawText(3,int(y+4),str(value))
        p.setPen(QtGui.QColor('#cbd9e5'));p.drawText(45,self.height()-9,'最近 90 秒：红色温度 °C · 蓝色湿度 %')
        for axis,color in enumerate(('#ff826d','#57c9fa')):
            path=QtGui.QPainterPath();started=False
            for i,point in enumerate(self.points):
                value=point[axis]
                if value is None:started=False;continue
                x=box.left()+i*box.width()/179;y=box.bottom()-(max(-40,min(100,value))+40)*box.height()/140
                if not started:path.moveTo(x,y);started=True
                else:path.lineTo(x,y)
            p.setPen(QtGui.QPen(QtGui.QColor(color),2));p.drawPath(path)
class Window(W.QMainWindow):
    def __init__(self,host='192.168.10.50',mp=502,op=5001):
        super().__init__();self.setWindowTitle('Industrial Gateway V3 · STM32H743');self.resize(1030,750);self.worker=None;self.uploader=None;self.mp=mp;self.op=op;self.history=deque(maxlen=10000);self.last_alarm=None
        root=W.QWidget();layout=W.QVBoxLayout(root);self.setCentralWidget(root)
        row=W.QHBoxLayout();self.title_label=W.QLabel('工业边缘控制网关');self.title_label.setStyleSheet('font-size:24px;font-weight:bold');row.addWidget(self.title_label);row.addStretch();self.host=W.QLineEdit(host);self.host.setMaximumWidth(180);row.addWidget(self.host)
        self.connect_button=W.QPushButton('连接');self.connect_button.clicked.connect(self.connect_gateway);row.addWidget(self.connect_button);layout.addLayout(row)
        self.connection=W.QLabel('未连接');layout.addWidget(self.connection)
        tabs=W.QTabWidget();layout.addWidget(tabs);monitor=W.QWidget();ml=W.QVBoxLayout(monitor);tabs.addTab(monitor,'实时监控')
        self.values=W.QLabel('温湿度 —     电机 —');self.values.setStyleSheet('font-size:21px');ml.addWidget(self.values);self.trend=Trend();ml.addWidget(self.trend)
        controls=W.QGroupBox('电机控制 · 心跳周期 500 ms，超时 2 s 停机');cl=W.QGridLayout(controls);self.rpm=W.QSpinBox();self.rpm.setRange(0,600);self.rpm.setValue(100);self.reverse=W.QCheckBox('反向');self.position=W.QSpinBox();self.position.setRange(0,2147483647);self.position.setValue(51200)
        cl.addWidget(W.QLabel('目标 RPM'),0,0);cl.addWidget(self.rpm,0,1);cl.addWidget(self.reverse,0,2);run=W.QPushButton('速度运行');run.clicked.connect(lambda:self.job('run',(self.rpm.value(),self.reverse.isChecked())));cl.addWidget(run,0,3)
        stop=W.QPushButton('停止 / 禁用');stop.setStyleSheet('background:#a43238');stop.clicked.connect(lambda:self.job('stop'));cl.addWidget(stop,0,4)
        cl.addWidget(W.QLabel('绝对位置脉冲'),1,0);cl.addWidget(self.position,1,1);move=W.QPushButton('移动到位置');move.clicked.connect(lambda:self.job('move',(self.rpm.value(),self.reverse.isChecked(),self.position.value())));cl.addWidget(move,1,3);cl.addWidget(W.QLabel('51200 脉冲 / 圈'),1,4);ml.addWidget(controls)
        self.state=W.QLabel('');ml.addWidget(self.state);export=W.QPushButton('导出本次监控 CSV');export.clicked.connect(self.export_csv);ml.addWidget(export)
        config=W.QWidget();fl=W.QFormLayout(config);tabs.addTab(config,'设备参数');self.ip=W.QLineEdit('192.168.10.50');fl.addRow('设备 IP（/24，重启生效）',self.ip)
        self.address=W.QSpinBox();self.address.setRange(1,247);self.address.setValue(1);fl.addRow('RS485 从站地址',self.address)
        self.baud=W.QComboBox();self.baud.addItems(['4800','9600','19200','38400','57600','115200']);self.baud.setCurrentText('9600');fl.addRow('波特率（8N1）',self.baud)
        self.function=W.QComboBox();self.function.addItems(['3','4']);fl.addRow('读功能码',self.function)
        self.register=W.QSpinBox();self.register.setRange(0,65534);fl.addRow('起始寄存器（零基地址）',self.register)
        self.order=W.QComboBox();self.order.addItems(['温度在前，湿度在后','湿度在前，温度在后']);fl.addRow('两个连续寄存器顺序',self.order)
        self.multiplier=W.QSpinBox();self.multiplier.setRange(1,10);self.multiplier.setValue(1);fl.addRow('原始值转为 0.1 单位的乘数',self.multiplier)
        self.divide=W.QCheckBox('改为除数（原始 0.01 单位用 ÷10）');self.divide.toggled.connect(lambda v:self.multiplier.setMaximum(1000 if v else 10));fl.addRow(self.divide)
        read=W.QPushButton('读取参数');read.clicked.connect(lambda:self.job('get_config'));fl.addRow(read);save=W.QPushButton('保存参数到 Flash');save.clicked.connect(self.save_config);fl.addRow(save)
        note=W.QLabel('SHT30 变送器的寄存器与倍率需按卖家协议设置。\n触摸校准参数由屏幕三点校准写入，PC 保存保留校准值。');note.setWordWrap(True);fl.addRow(note)
        ota=W.QWidget();ol=W.QVBoxLayout(ota);tabs.addTab(ota,'固件升级');self.firmware=W.QLineEdit();self.firmware.setReadOnly(True);ol.addWidget(W.QLabel('选择应用 Gateway.bin，旁边须有构建工具生成的 Gateway.json'));ol.addWidget(self.firmware)
        select=W.QPushButton('选择固件');select.clicked.connect(self.select_firmware);ol.addWidget(select);self.upload_button=W.QPushButton('上传、校验并重启');self.upload_button.clicked.connect(self.upload);ol.addWidget(self.upload_button);self.progress=W.QProgressBar();ol.addWidget(self.progress)
        cancel=W.QPushButton('中断下载');cancel.clicked.connect(lambda:self.uploader.cancelled.set() if self.uploader else None);ol.addWidget(cancel);ol.addWidget(W.QLabel('升级下载保留已确认槽；安装中断后重新安装；新版本未确认启动时回滚。'));ol.addStretch()
        alarms=W.QWidget();al=W.QVBoxLayout(alarms);tabs.addTab(alarms,'故障记录');self.alarm_log=W.QPlainTextEdit();self.alarm_log.setReadOnly(True);al.addWidget(self.alarm_log)
        self.messages=W.QPlainTextEdit();self.messages.setReadOnly(True);self.messages.setMaximumHeight(95);self.messages.document().setMaximumBlockCount(300);layout.addWidget(self.messages)
        self.setStyleSheet('QMainWindow,QWidget{background:#1b2e40;color:#e4edf4;font-family:Microsoft YaHei;}QLineEdit,QSpinBox,QComboBox,QPlainTextEdit{background:#102131;border:1px solid #3f5a70;padding:5px;}QPushButton{background:#235e86;border:0;border-radius:4px;padding:9px;}QPushButton:disabled{background:#364957;}QTabBar::tab{padding:10px 18px;}QTabBar::tab:selected{background:#235e86;}QGroupBox{border:1px solid #3f5a70;margin-top:12px;padding-top:12px;}')
    def message(self,text):self.messages.appendPlainText(time.strftime('%H:%M:%S')+' '+text);self.connection.setText(text)
    def connect_gateway(self):
        if self.worker:
            self.worker.shutdown.set();self.worker.wait(2500)
            if self.worker.isRunning():self.message('正在关闭连接');return
            self.worker=None;self.connect_button.setText('连接');return
        self.worker=Worker(self.host.text().strip(),self.mp,self.op);self.worker.snapshot.connect(self.show_data);self.worker.message.connect(self.message);self.worker.configuration.connect(self.show_config);self.worker.start();self.connect_button.setText('断开')
    def job(self,action,values=()):
        if self.worker:self.worker.submit(action,values)
        else:self.message('请先连接网关')
    def show_data(self,d):
        self.connection.setText('已连接 · '+self.host.text());self.history.append(dict(d,timestamp=time.strftime('%Y-%m-%d %H:%M:%S')));self.trend.push(d)
        sensor=f"{d['temperature']:.1f} °C   {d['humidity']:.1f} %RH" if d['valid']&1 else '温湿度离线'
        motor=f"{d['rpm']} RPM   {d['current']} mA" if d['valid']&2 else '电机离线'
        self.values.setText(sensor+'     |     '+motor)
        self.state.setText(f"版本 {version_string(d['version'])} · 运行 {d['uptime']} s · 位置 {d['position']} · 状态 {d['state']} · 使能 {int(d['enabled'])} · {'本地' if d['owner'] else '远程'} · SD {'正常' if d['valid']&8 else '异常'}")
        if d['alarms']!=self.last_alarm:
            self.last_alarm=d['alarms'];names='、'.join(name for bit,name in ALARMS.items() if d['alarms']&bit) or '故障已清除';self.alarm_log.appendPlainText(time.strftime('%H:%M:%S')+f" 0x{d['alarms']:02X} "+names)
    def show_config(self,c):
        self.ip.setText('.'.join(map(str,c[:4])));self.address.setValue(c[4]);self.function.setCurrentText(str(c[5]));self.register.setValue(c[6]);self.order.setCurrentIndex(c[7]);self.divide.setChecked(bool(c[9]&0x8000));self.multiplier.setValue(c[9]&0x7FFF);self.baud.setCurrentText(str(c[10]));self.message('参数读取完成')
    def save_config(self):
        try:
            ip=list(map(int,self.ip.text().split('.')))
            if len(ip)!=4 or any(x<0 or x>255 for x in ip):raise ValueError('IP 地址格式错误')
            scale=self.multiplier.value()|(0x8000 if self.divide.isChecked() else 0)
            values=ip+[self.address.value(),int(self.function.currentText()),self.register.value(),self.order.currentIndex(),1-self.order.currentIndex(),scale,int(self.baud.currentText())];self.job('set_config',values)
        except Exception as e:self.message(str(e))
    def select_firmware(self):
        path,_=W.QFileDialog.getOpenFileName(self,'选择固件','','Application (*.bin)')
        if path:
            try:data,m=load_image(path);self.firmware.setText(path);self.message(f"已检查：{version_string(m['version'])} / {len(data)} bytes")
            except Exception as e:self.message(str(e))
    def upload(self):
        if not self.firmware.text():self.message('请先选择固件');return
        if self.worker:self.worker.armed=False;self.job('stop')
        self.uploader=Uploader(self.host.text().strip(),self.op,self.firmware.text());self.uploader.progress.connect(self.progress.setValue);self.uploader.message.connect(self.message);self.uploader.finished.connect(lambda:self.upload_button.setEnabled(True));self.upload_button.setEnabled(False);self.uploader.start()
    def export_csv(self):
        if not self.history:return
        path,_=W.QFileDialog.getSaveFileName(self,'导出 CSV','gateway.csv','CSV (*.csv)')
        if path:
            with open(path,'w',newline='',encoding='utf-8-sig') as f:writer=csv.DictWriter(f,fieldnames=self.history[0].keys());writer.writeheader();writer.writerows(self.history)
            self.message('已导出 '+path)
    def closeEvent(self,event):
        if self.uploader and self.uploader.isRunning():self.uploader.cancelled.set();self.message('正在中断上传，完成后可关闭');event.ignore();return
        if self.worker:self.worker.shutdown.set();self.worker.wait(2500)
        if self.worker and self.worker.isRunning():event.ignore()
        else:event.accept()
def main():
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--host',default='192.168.10.50');parser.add_argument('--modbus-port',type=int,default=502);parser.add_argument('--ota-port',type=int,default=5001);parser.add_argument('--demo',action='store_true');parser.add_argument('--smoke-screenshot');args=parser.parse_args()
    demo=None
    if args.demo:
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'Tests'))
        from simulator import Simulator
        demo=Simulator();demo.start();args.host='127.0.0.1';args.modbus_port=demo.modbus_port;args.ota_port=demo.ota_port
    app=W.QApplication(sys.argv[:1])
    if not QtGui.QFontDatabase().families():
        font=Path('C:/Windows/Fonts/msyh.ttc')
        if font.exists():QtGui.QFontDatabase.addApplicationFont(str(font))
    app.setFont(QtGui.QFont('Microsoft YaHei',10))
    window=Window(args.host,args.modbus_port,args.ota_port);window.show()
    if args.demo:
        window.setWindowTitle('Industrial Gateway V3 · 演示模式 / 模拟数据')
        window.title_label.setText('工业边缘控制网关 · 演示数据')
        window.connect_gateway()
    if args.smoke_screenshot:
        def screenshot():window.grab().save(args.smoke_screenshot);window.close();app.quit()
        QtCore.QTimer.singleShot(3500,screenshot)
    result=app.exec_()
    if demo:demo.close()
    return result
if __name__=='__main__':sys.exit(main())
