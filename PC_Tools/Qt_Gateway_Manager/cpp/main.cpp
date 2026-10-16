#include <QtWidgets>
#include <QtNetwork>
#include <QtEndian>
#include <atomic>
extern "C" {
#include "igw_crc.h"
}
static quint16 be16(const QByteArray &b, int p) {
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + p));
}
static quint32 be32(const QByteArray &b, int p) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + p));
}
static void put16(QByteArray &b, quint16 v) {
    b.append(char(v >> 8));
    b.append(char(v));
}
static void put32(QByteArray &b, quint32 v) {
    for (int shift = 24; shift >= 0; shift -= 8)
        b.append(char(v >> shift));
}
static void fail(const QString &message) {
    throw message;
}
static QByteArray readExact(QTcpSocket &s, int count, int timeout = 1500) {
    QByteArray out;
    QElapsedTimer timer;
    timer.start();
    while (out.size() < count) {
        if (s.bytesAvailable())
            out += s.read(count - out.size());
        else {
            int remaining = timeout - int(timer.elapsed());
            if (remaining <= 0 || !s.waitForReadyRead(remaining))
                fail("TCP response timeout / connection closed");
        }
    }
    return out;
}
static void send(QTcpSocket &s, const QByteArray &b, int timeout = 1500) {
    if (s.write(b) != b.size())
        fail(s.errorString());
    if (s.bytesToWrite() && !s.waitForBytesWritten(timeout))
        fail(s.errorString());
}
static QByteArray frame(quint8 op, quint32 session, quint32 offset, const QByteArray &payload = {}) {
    if (payload.size() > 1024)
        fail("Payload too large");
    QByteArray b;
    put32(b, 0x49475733);
    b.append(char(1));
    b.append(char(op));
    put16(b, 0);
    put32(b, session);
    put32(b, offset);
    put32(b, quint32(payload.size()));
    auto c = igw_crc32_update(0xFFFFFFFF, b.constData(), size_t(b.size()));
    c = igw_crc32_update(c, payload.constData(), size_t(payload.size())) ^ 0xFFFFFFFF;
    put32(b, c);
    return b + payload;
}
class Maintenance {
  public:
    QTcpSocket socket;
    quint32 session = QRandomGenerator::global()->generate() | 1u;
    Maintenance(const QString &host, quint16 port) {
        socket.connectToHost(host, port);
        if (!socket.waitForConnected(2000))
            fail(socket.errorString());
    }
    QByteArray call(quint8 operation, const QByteArray &payload = {}, quint32 offset = 0,
                    quint32 *next = nullptr) {
        send(socket, frame(operation, session, offset, payload));
        auto header = readExact(socket, 24, 90000);
        if (be32(header, 0) != 0x49475733 || uchar(header[4]) != 1 || uchar(header[5]) != operation ||
            be32(header, 8) != session || be32(header, 16) > 1024)
            fail("Invalid IGW3 response");
        auto body = readExact(socket, int(be32(header, 16)), 90000);
        auto c = igw_crc32_update(0xFFFFFFFF, header.constData(), 20);
        c = igw_crc32_update(c, body.constData(), size_t(body.size())) ^ 0xFFFFFFFF;
        if (c != be32(header, 20))
            fail("IGW3 CRC mismatch");
        if (be16(header, 6))
            fail(QString("Gateway status %1").arg(be16(header, 6)));
        if (next)
            *next = be32(header, 12);
        return body;
    }
};
class Backend : public QObject {
    Q_OBJECT
    QTcpSocket *socket = nullptr;
    QTimer *timer = nullptr;
    quint16 transaction = 0;
    bool armed = false, wanted = false;
    QString host;
    quint16 otaPort = 5001, mbPort = 502;
    qint64 lastConnect = 0;
    void attemptConnect() {
        lastConnect = QDateTime::currentMSecsSinceEpoch();
        socket = new QTcpSocket(this);
        socket->connectToHost(host, mbPort);
        if (!socket->waitForConnected(1500)) {
            emit message(socket->errorString());
            delete socket;
            socket = nullptr;
            return;
        }
        armed = false;
        emit message("Connected");
    }
    QByteArray modbus(const QByteArray &pdu) {
        QByteArray b;
        put16(b, ++transaction);
        put16(b, 0);
        put16(b, quint16(pdu.size() + 1));
        b.append(char(1));
        send(*socket, b + pdu);
        auto header = readExact(*socket, 7);
        int length = be16(header, 4);
        if (be16(header, 0) != transaction || be16(header, 2) || uchar(header[6]) != 1 || length < 2 ||
            length > 254)
            fail("Invalid Modbus response");
        auto reply = readExact(*socket, length - 1);
        if (uchar(reply[0]) == (uchar(pdu[0]) | 128))
            fail(QString("Modbus exception %1").arg(uchar(reply[1])));
        if (reply[0] != pdu[0])
            fail("Modbus function mismatch");
        return reply;
    }
    QVector<quint16> read(quint16 address, quint16 count) {
        QByteArray b;
        b.append(char(3));
        put16(b, address);
        put16(b, count);
        auto r = modbus(b);
        if (r.size() != 2 + count * 2 || uchar(r[1]) != count * 2)
            fail("Invalid register count");
        QVector<quint16> values;
        for (int i = 0; i < count; i++)
            values.append(be16(r, 2 + 2 * i));
        return values;
    }
    void write(quint16 address, const QVector<quint16> &values) {
        QByteArray b;
        b.append(char(16));
        put16(b, address);
        put16(b, quint16(values.size()));
        b.append(char(values.size() * 2));
        for (auto v : values)
            put16(b, v);
        auto r = modbus(b);
        if (r.size() != 5 || be16(r, 1) != address || be16(r, 3) != values.size())
            fail("Invalid write acknowledgement");
    }
  public slots:
    void connectGateway(QString hostname, quint16 port, quint16 upgradePort) {
        disconnectGateway();
        host = hostname;
        mbPort = port;
        otaPort = upgradePort;
        wanted = true;
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &Backend::poll);
        timer->start(500);
        attemptConnect();
        poll();
    }
    void disconnectGateway() {
        wanted = false;
        if (timer) {
            timer->stop();
            delete timer;
            timer = nullptr;
        }
        if (socket) {
            try {
                if (armed)
                    write(0x21, {0});
            } catch (const QString &) {
            }
            socket->close();
            delete socket;
            socket = nullptr;
        }
        armed = false;
    }
    void command(int mode, quint16 rpm, bool reverse, quint32 pulses, qint64 issued) {
        if (!socket) {
            emit message("Connect first");
            return;
        }
        if (QDateTime::currentMSecsSinceEpoch() - issued > 2000) {
            emit message("Command expired");
            return;
        }
        try {
            if (!mode) {
                write(0x21, {0});
                armed = false;
            } else if (mode == 1) {
                write(0x20, {rpm, 1, quint16(reverse)});
                armed = true;
            } else {
                write(0x20, {rpm, 1, quint16(reverse), quint16(pulses >> 16), quint16(pulses), 1});
                armed = true;
            }
            poll();
        } catch (const QString &e) {
            armed = false;
            emit message(e);
        }
    }
    void configuration(QVariantList fields = {}) {
        try {
            Maintenance m(host, otaPort);
            auto c = m.call(7);
            if (c.size() != 48)
                fail("Invalid configuration length");
            if (!fields.isEmpty()) {
                if (fields.size() != 11)
                    fail("Invalid configuration fields");
                QByteArray update;
                for (int i = 0; i < 4; i++)
                    update.append(char(fields[i].toUInt()));
                for (int i = 4; i < 10; i++)
                    put16(update, quint16(fields[i].toUInt()));
                put32(update, fields[10].toUInt());
                update += c.mid(20);
                m.call(8, update);
                emit message("Parameters persisted; IP applies after reboot");
                c = update;
            }
            QVariantList values;
            for (int i = 0; i < 4; i++)
                values << uchar(c[i]);
            for (int i = 4; i < 16; i += 2)
                values << be16(c, i);
            values << be32(c, 16);
            emit configReady(values);
        } catch (const QString &e) {
            emit message(e);
        }
    }
    void poll() {
        if (!socket && wanted && QDateTime::currentMSecsSinceEpoch() - lastConnect >= 1000)
            attemptConnect();
        if (!socket)
            return;
        try {
            auto a = read(0, 4), b = read(0x10, 6), c = read(0x20, 3), o = read(0x27, 1), v = read(0x30, 6);
            if (v[4])
                armed = false;
            if (armed && c[1] && o[0] == 0)
                write(0x26, {1});
            QVariantMap d{{"temperature", qint16(a[0]) / 10.0},
                          {"humidity", a[1] / 10.0},
                          {"valid", a[2]},
                          {"alarms", a[3]},
                          {"rpm", qint16(b[0])},
                          {"current", qint16(b[1])},
                          {"state", b[2]},
                          {"fault", b[3]},
                          {"position", qint32((quint32(b[4]) << 16) | b[5])},
                          {"target", c[0]},
                          {"enabled", c[1]},
                          {"owner", o[0]},
                          {"version", (quint32(v[0]) << 16) | v[1]},
                          {"uptime", (quint32(v[2]) << 16) | v[3]},
                          {"ota", v[4]},
                          {"progress", v[5]}};
            emit snapshot(d);
        } catch (const QString &e) {
            armed = false;
            emit message(e);
            socket->close();
            delete socket;
            socket = nullptr;
        }
    }
  signals:
    void snapshot(QVariantMap);
    void message(QString);
    void configReady(QVariantList);
    void disconnected();
};
class Upload : public QThread {
    Q_OBJECT
  public:
    QString host, path;
    quint16 port = 5001;
    std::atomic<bool> cancel{false};
    void run() override {
        try {
            QFile f(path),
                manifest(QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName() + ".json");
            if (!f.open(QIODevice::ReadOnly) || !manifest.open(QIODevice::ReadOnly))
                fail("Select Gateway.bin with Gateway.json");
            auto bytes = f.readAll();
            auto m = QJsonDocument::fromJson(manifest.readAll()).object();
            auto u32 = [](const QJsonValue &v) { return quint32(v.toDouble()); };
            if (bytes.size() < 1044 || bytes.size() > 0x1E0000 ||
                bytes.mid(0x400, 8) != QByteArray("IGWID3\0\0", 8))
                fail("Invalid firmware identity");
            auto le = [&](int offset) {
                return qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(bytes.constData() + offset));
            };
            quint32 sp = le(0), pc = le(4);
            bool ram = (sp > 0x20000000 && sp <= 0x20020000) || (sp > 0x24000000 && sp <= 0x24080000) ||
                       (sp > 0x30000000 && sp <= 0x30048000) || (sp > 0x38000000 && sp <= 0x38010000);
            if (!ram || (sp & 7) || !(pc & 1) || (pc & ~1u) < 0x08020000 ||
                (pc & ~1u) >= 0x08020000 + quint32(bytes.size()) || le(0x408) != 0x743A0001 ||
                le(0x410) != 0x08020000 || m["format"].toString() != "IGW3-manifest-v1" ||
                u32(m["board"]) != 0x743A0001 || u32(m["base"]) != 0x08020000 ||
                le(0x40C) != u32(m["version"]) || u32(m["size"]) != quint32(bytes.size()) ||
                u32(m["crc32"]) != igw_crc32(bytes.constData(), size_t(bytes.size())) ||
                m["sha256"].toString() !=
                    QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()))
                fail("Firmware/manifest mismatch");
            Maintenance client(host, port);
            QByteArray begin;
            for (auto key : {"board", "version", "size", "crc32"})
                put32(begin, u32(m[key]));
            quint32 next = 0;
            client.call(1, begin, 0, &next);
            if (next)
                fail("BEGIN offset mismatch");
            for (int offset = 0; offset < bytes.size(); offset += 1024) {
                if (cancel.load()) {
                    client.call(5);
                    fail("Download cancelled; confirmed image preserved");
                }
                auto block = bytes.mid(offset, 1024);
                client.call(2, block, quint32(offset), &next);
                if (next != quint32(offset + block.size()))
                    fail("DATA offset mismatch");
                emit progress(int(100ull * next / quint32(bytes.size())));
            }
            client.call(3);
            client.call(6);
            emit message("Image committed; reconnect and verify running version / rollback");
        } catch (const QString &e) {
            emit message(e);
        }
    }
  signals:
    void progress(int);
    void message(QString);
};
class Trend : public QWidget {
    QVector<QPointF> points;

  public:
    Trend() {
        setMinimumHeight(170);
    }
    void push(const QVariantMap &d) {
        points.append(d["valid"].toUInt() & 1 ? QPointF(d["temperature"].toDouble(), d["humidity"].toDouble())
                                              : QPointF(qQNaN(), qQNaN()));
        if (points.size() > 180)
            points.removeFirst();
        update();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#132334"));
        QRectF box = rect().adjusted(40, 15, -15, -25);
        p.setPen(QColor("#30465b"));
        for (int v = -40; v <= 100; v += 20) {
            double y = box.bottom() - (v + 40) * box.height() / 140;
            p.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
            p.drawText(2, int(y), QString::number(v));
        }
        for (int axis = 0; axis < 2; axis++) {
            QPainterPath path;
            bool start = false;
            for (int i = 0; i < points.size(); i++) {
                double value = axis ? points[i].y() : points[i].x();
                if (qIsNaN(value)) {
                    start = false;
                    continue;
                }
                double x = box.left() + i * box.width() / 179,
                       y = box.bottom() - (qBound(-40.0, value, 100.0) + 40) * box.height() / 140;
                if (start)
                    path.lineTo(x, y);
                else {
                    path.moveTo(x, y);
                    start = true;
                }
            }
            p.setPen(QPen(QColor(axis ? "#57c9fa" : "#ff826d"), 2));
            p.drawPath(path);
        }
        p.setPen(QColor("#cbd9e5"));
        p.drawText(45, height() - 6, QStringLiteral("最近 90 秒 · 红：温度 °C · 蓝：湿度 %"));
    }
};
class Window : public QMainWindow {
    Q_OBJECT
    QThread thread;
    Backend *backend = new Backend;
    Upload *uploader = nullptr;
    bool connected = false;
    quint16 mbPort, otaPort;
    QLineEdit *host, *ip, *firmware;
    QSpinBox *rpm, *position, *address, *reg, *multiplier;
    QComboBox *baud, *function, *order;
    QCheckBox *reverse, *divide;
    QLabel *values, *state;
    QPlainTextEdit *messages, *alarms;
    QProgressBar *progress;
    QPushButton *connection, *uploadButton;
    Trend *trend;
    QList<QVariantMap> history;
    quint32 lastAlarm = 0xFFFFFFFF;
    void message(const QString &s) {
        messages->appendPlainText(QTime::currentTime().toString("HH:mm:ss ") + s);
    }
    void sendCommand(int mode) {
        auto r = quint16(rpm->value());
        auto p = quint32(position->value());
        auto d = reverse->isChecked();
        auto issued = QDateTime::currentMSecsSinceEpoch();
        QMetaObject::invokeMethod(
            backend, [=] { backend->command(mode, r, d, p, issued); }, Qt::QueuedConnection);
    }

  public:
    Window(QString hostname, quint16 mb, quint16 ota) : mbPort(mb), otaPort(ota) {
        setWindowTitle("Industrial Gateway V3 · C++ / Qt");
        resize(1030, 750);
        auto root = new QWidget;
        auto layout = new QVBoxLayout(root);
        setCentralWidget(root);
        auto header = new QHBoxLayout;
        auto title = new QLabel(QStringLiteral("工业边缘控制网关"));
        title->setStyleSheet("font-size:24px;font-weight:bold");
        header->addWidget(title);
        header->addStretch();
        host = new QLineEdit(hostname);
        host->setMaximumWidth(180);
        header->addWidget(host);
        connection = new QPushButton(QStringLiteral("连接"));
        header->addWidget(connection);
        layout->addLayout(header);
        auto tabs = new QTabWidget;
        layout->addWidget(tabs);
        auto monitor = new QWidget;
        auto ml = new QVBoxLayout(monitor);
        tabs->addTab(monitor, QStringLiteral("实时监控"));
        values = new QLabel(QStringLiteral("温湿度 —     电机 —"));
        values->setStyleSheet("font-size:21px");
        ml->addWidget(values);
        trend = new Trend;
        ml->addWidget(trend);
        auto control = new QGroupBox(QStringLiteral("电机控制 · 500 ms 心跳，超时 2 s 停机"));
        auto grid = new QGridLayout(control);
        rpm = new QSpinBox;
        rpm->setRange(0, 600);
        rpm->setValue(100);
        position = new QSpinBox;
        position->setRange(0, 2147483647);
        position->setValue(51200);
        reverse = new QCheckBox(QStringLiteral("反向"));
        grid->addWidget(new QLabel("RPM"), 0, 0);
        grid->addWidget(rpm, 0, 1);
        grid->addWidget(reverse, 0, 2);
        auto run = new QPushButton(QStringLiteral("速度运行")),
             stop = new QPushButton(QStringLiteral("停止 / 禁用")),
             move = new QPushButton(QStringLiteral("移动到位置"));
        grid->addWidget(run, 0, 3);
        grid->addWidget(stop, 0, 4);
        stop->setStyleSheet("background:#a43238");
        grid->addWidget(new QLabel(QStringLiteral("绝对位置脉冲")), 1, 0);
        grid->addWidget(position, 1, 1);
        grid->addWidget(move, 1, 3);
        grid->addWidget(new QLabel(QStringLiteral("51200 脉冲 / 圈")), 1, 4);
        ml->addWidget(control);
        state = new QLabel;
        ml->addWidget(state);
        auto exportCsv = new QPushButton(QStringLiteral("导出本次监控 CSV"));
        ml->addWidget(exportCsv);
        auto config = new QWidget;
        auto form = new QFormLayout(config);
        tabs->addTab(config, QStringLiteral("设备参数"));
        ip = new QLineEdit("192.168.10.50");
        form->addRow(QStringLiteral("IP（/24，重启生效）"), ip);
        address = new QSpinBox;
        address->setRange(1, 247);
        address->setValue(1);
        form->addRow(QStringLiteral("RS485 从站地址"), address);
        baud = new QComboBox;
        baud->addItems({"4800", "9600", "19200", "38400", "57600", "115200"});
        baud->setCurrentText("9600");
        form->addRow(QStringLiteral("波特率（8N1）"), baud);
        function = new QComboBox;
        function->addItems({"3", "4"});
        form->addRow(QStringLiteral("功能码"), function);
        reg = new QSpinBox;
        reg->setRange(0, 65534);
        form->addRow(QStringLiteral("零基起始寄存器"), reg);
        order = new QComboBox;
        order->addItems({QStringLiteral("温度、湿度"), QStringLiteral("湿度、温度")});
        form->addRow(QStringLiteral("两个寄存器的顺序"), order);
        multiplier = new QSpinBox;
        multiplier->setRange(1, 10);
        multiplier->setValue(1);
        form->addRow(QStringLiteral("转为 0.1 单位的乘数"), multiplier);
        divide = new QCheckBox(QStringLiteral("改为除数（原始 0.01 单位用 ÷10）"));
        form->addRow(divide);
        connect(divide, &QCheckBox::toggled, this,
                [=](bool enabled) { multiplier->setMaximum(enabled ? 1000 : 10); });
        auto readConfig = new QPushButton(QStringLiteral("读取参数")),
             saveConfig = new QPushButton(QStringLiteral("保存到 Flash"));
        form->addRow(readConfig);
        form->addRow(saveConfig);
        form->addRow(new QLabel(QStringLiteral("传感器寄存器需按卖家协议确认；PC 保存保留触摸校准。")));
        auto update = new QWidget;
        auto ol = new QVBoxLayout(update);
        tabs->addTab(update, QStringLiteral("固件升级"));
        ol->addWidget(new QLabel(QStringLiteral("应用 Gateway.bin，旁边必须有 Gateway.json")));
        firmware = new QLineEdit;
        firmware->setReadOnly(true);
        ol->addWidget(firmware);
        auto select = new QPushButton(QStringLiteral("选择固件"));
        ol->addWidget(select);
        uploadButton = new QPushButton(QStringLiteral("上传、校验并重启"));
        ol->addWidget(uploadButton);
        progress = new QProgressBar;
        ol->addWidget(progress);
        auto cancel = new QPushButton(QStringLiteral("中断下载"));
        ol->addWidget(cancel);
        ol->addStretch();
        alarms = new QPlainTextEdit;
        alarms->setReadOnly(true);
        tabs->addTab(alarms, QStringLiteral("故障记录"));
        messages = new QPlainTextEdit;
        messages->setReadOnly(true);
        messages->setMaximumHeight(95);
        messages->document()->setMaximumBlockCount(300);
        layout->addWidget(messages);
        setStyleSheet(
            "QWidget{background:#1b2e40;color:#e4edf4;font-family:'Microsoft "
            "YaHei';}QLineEdit,QSpinBox,QComboBox,QPlainTextEdit{background:#102131;border:1px solid "
            "#3f5a70;padding:5px;}QPushButton{background:#235e86;border:0;border-radius:4px;padding:9px;}"
            "QTabBar::tab{padding:10px 18px;}QTabBar::tab:selected{background:#235e86;}QGroupBox{border:1px "
            "solid #3f5a70;margin-top:12px;padding-top:12px;}");
        backend->moveToThread(&thread);
        connect(&thread, &QThread::finished, backend, &QObject::deleteLater);
        connect(backend, &Backend::message, this, [=](QString s) { message(s); });
        connect(backend, &Backend::disconnected, this, [=] {
            connected = false;
            connection->setText(QStringLiteral("连接"));
        });
        connect(backend, &Backend::snapshot, this, [=](QVariantMap d) {
            d["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            history.append(d);
            if (history.size() > 10000)
                history.removeFirst();
            trend->push(d);
            auto valid = d["valid"].toUInt();
            QString sensor = valid & 1 ? QString("%1 °C   %2 %RH")
                                             .arg(d["temperature"].toDouble(), 0, 'f', 1)
                                             .arg(d["humidity"].toDouble(), 0, 'f', 1)
                                       : QStringLiteral("温湿度离线");
            values->setText(
                sensor +
                QString("     |     %1 RPM   %2 mA").arg(d["rpm"].toInt()).arg(d["current"].toInt()));
            auto version = d["version"].toUInt();
            state->setText(QStringLiteral("版本 %1.%2.%3 · 运行 %4 s · 位置 %5 · 状态 %6 · 使能 %7 · SD %8")
                               .arg(version >> 16)
                               .arg((version >> 8) & 255)
                               .arg(version & 255)
                               .arg(d["uptime"].toUInt())
                               .arg(d["position"].toInt())
                               .arg(d["state"].toUInt())
                               .arg(d["enabled"].toUInt())
                               .arg(valid & 8 ? "OK" : "ERROR"));
            auto mask = d["alarms"].toUInt();
            if (mask != lastAlarm) {
                lastAlarm = mask;
                QStringList labels;
                const QString names[] = {QStringLiteral("温湿度离线"), QStringLiteral("CAN 离线"),
                                         QStringLiteral("网线断开"),   QStringLiteral("SD 异常"),
                                         QStringLiteral("电机故障"),   QStringLiteral("控制超时"),
                                         QStringLiteral("Flash 异常")};
                for (int bit = 0; bit < 7; bit++)
                    if (mask & (1u << bit))
                        labels << names[bit];
                alarms->appendPlainText(
                    QTime::currentTime().toString("HH:mm:ss ") +
                    QString("0x%1 ").arg(mask, 2, 16, QChar('0')) +
                    (labels.isEmpty() ? QStringLiteral("故障已清除") : labels.join(" / ")));
            }
        });
        connect(backend, &Backend::configReady, this, [=](QVariantList c) {
            ip->setText(QString("%1.%2.%3.%4")
                            .arg(c[0].toUInt())
                            .arg(c[1].toUInt())
                            .arg(c[2].toUInt())
                            .arg(c[3].toUInt()));
            address->setValue(c[4].toInt());
            function->setCurrentText(c[5].toString());
            reg->setValue(c[6].toInt());
            order->setCurrentIndex(c[7].toInt());
            divide->setChecked((c[9].toUInt() & 0x8000) != 0);
            multiplier->setValue(c[9].toUInt() & 0x7FFF);
            baud->setCurrentText(c[10].toString());
        });
        connect(connection, &QPushButton::clicked, this, [=] {
            if (connected) {
                QMetaObject::invokeMethod(backend, "disconnectGateway", Qt::QueuedConnection);
                connected = false;
                connection->setText(QStringLiteral("连接"));
            } else {
                QString h = host->text().trimmed();
                QMetaObject::invokeMethod(
                    backend, [=] { backend->connectGateway(h, mbPort, otaPort); }, Qt::QueuedConnection);
                connected = true;
                connection->setText(QStringLiteral("断开"));
            }
        });
        connect(run, &QPushButton::clicked, this, [=] { sendCommand(1); });
        connect(stop, &QPushButton::clicked, this, [=] { sendCommand(0); });
        connect(move, &QPushButton::clicked, this, [=] { sendCommand(2); });
        connect(readConfig, &QPushButton::clicked, this, [=] {
            QMetaObject::invokeMethod(backend, [=] { backend->configuration(); }, Qt::QueuedConnection);
        });
        connect(saveConfig, &QPushButton::clicked, this, [=] {
            auto parts = ip->text().split('.');
            if (parts.size() != 4) {
                message("Invalid IP");
                return;
            }
            QVariantList c;
            for (auto p : parts) {
                bool ok;
                auto value = p.toUInt(&ok);
                if (!ok || value > 255) {
                    message("Invalid IP");
                    return;
                }
                c << value;
            }
            c << address->value() << function->currentText().toUInt() << reg->value() << order->currentIndex()
              << (1 - order->currentIndex()) << (multiplier->value() | (divide->isChecked() ? 0x8000 : 0))
              << baud->currentText().toUInt();
            QMetaObject::invokeMethod(backend, [=] { backend->configuration(c); }, Qt::QueuedConnection);
        });
        connect(select, &QPushButton::clicked, this, [=] {
            auto path =
                QFileDialog::getOpenFileName(this, QStringLiteral("选择固件"), {}, "Application (*.bin)");
            if (!path.isEmpty())
                firmware->setText(path);
        });
        connect(uploadButton, &QPushButton::clicked, this, [=] {
            if (firmware->text().isEmpty()) {
                message("Select firmware first");
                return;
            }
            sendCommand(0);
            uploader = new Upload;
            uploader->host = host->text().trimmed();
            uploader->port = otaPort;
            uploader->path = firmware->text();
            connect(uploader, &Upload::progress, progress, &QProgressBar::setValue);
            connect(uploader, &Upload::message, this, [=](QString s) { message(s); });
            connect(uploader, &QThread::finished, this, [=] {
                uploadButton->setEnabled(true);
                uploader->deleteLater();
                uploader = nullptr;
            });
            uploadButton->setEnabled(false);
            uploader->start();
        });
        connect(cancel, &QPushButton::clicked, this, [=] {
            if (uploader)
                uploader->cancel = true;
        });
        connect(exportCsv, &QPushButton::clicked, this, [=] {
            if (history.isEmpty())
                return;
            auto path = QFileDialog::getSaveFileName(this, "Export CSV", "gateway.csv", "CSV (*.csv)");
            if (path.isEmpty())
                return;
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                message(f.errorString());
                return;
            }
            QStringList keys = history.first().keys();
            f.write(keys.join(',').toUtf8() + "\r\n");
            for (auto row : history) {
                QStringList values;
                for (auto key : keys)
                    values << row[key].toString();
                f.write(values.join(',').toUtf8() + "\r\n");
            }
            message("CSV exported");
        });
        thread.start();
    }
    void closeEvent(QCloseEvent *event) override {
        if (uploader && uploader->isRunning()) {
            uploader->cancel = true;
            message("Cancelling upload; close again after completion");
            event->ignore();
            return;
        }
        QMetaObject::invokeMethod(backend, "disconnectGateway", Qt::QueuedConnection);
        thread.quit();
        if (!thread.wait(2200)) {
            message("Waiting for network call");
            event->ignore();
        } else
            event->accept();
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"host", "Gateway IP", "ip", "192.168.10.50"});
    parser.addOption({"modbus-port", "Modbus TCP port", "port", "502"});
    parser.addOption({"ota-port", "OTA TCP port", "port", "5001"});
    parser.process(app);
    Window window(parser.value("host"), quint16(parser.value("modbus-port").toUInt()),
                  quint16(parser.value("ota-port").toUInt()));
    window.show();
    return app.exec();
}
#include "main.moc"
