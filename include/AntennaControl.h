#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QMap>

// DEC-0140: serialized Hamlib ERP transport; no blocking GUI socket waits.
class HamlibSession : public QObject {
    Q_OBJECT
public:
    explicit HamlibSession(QObject* parent=nullptr);
    ~HamlibSession() override;
    void open(const QString& host, quint16 port);
    void close();
    bool ready() const;
    bool busy() const { return !expected_.isEmpty(); }
    bool request(const QByteArray& command, const QString& expected);
signals:
    void connected();
    void failed(QString reason);
    void response(QString command, int status, QMap<QString,QString> fields);
private:
    void fail(const QString& reason);
    void read();
    QTcpSocket socket_;
    QTimer deadline_;
    QByteArray input_;
    QString expected_;
    QMap<QString,QString> fields_;
    bool echoed_=false;
    int bytes_=0;
};

struct RotorLimits {
    double minAz=0,maxAz=360,minEl=0,maxEl=90;
    bool valid() const;
    bool contains(double az,double el) const;
};

class RotatorController : public QObject {
    Q_OBJECT
public:
    explicit RotatorController(QObject* parent=nullptr);
    void connectTo(const QString& host,quint16 port,const RotorLimits& limits);
    void disconnectFromController();
    bool arm();
    void stop();
    bool moveTo(double az,double el);
    bool armed() const { return armed_; }
    bool fresh() const;
    bool connected() const { return link_.ready(); }
signals:
    void position(double az,double el);
    void state(QString text);
    void armedChanged(bool armed);
    void stopped();
private:
    void poll();
    void disarm();
    HamlibSession link_;
    QTimer poll_;
    QElapsedTimer positionAge_;
    RotorLimits limits_;
    bool armed_=false,stopPending_=false,disconnectPending_=false;
};

class SwrMonitor : public QObject {
    Q_OBJECT
public:
    explicit SwrMonitor(QObject* parent=nullptr);
    void connectTo(const QString& host,quint16 port);
    void disconnectMeter();
signals:
    void reading(double ratio);
    void status(QString text);
private:
    HamlibSession link_;
    QTimer poll_;
};
