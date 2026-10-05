#pragma once
#include "FlvBuffer.h"
#include "network/HttpService.h"
#include <QObject>
#include <QProcess>
#include <QTcpServer>
#include <QTimer>
#include <QMap>

namespace bp {
class LiveTimeshift final : public QObject {
    Q_OBJECT
public:
    struct Playback {QString url;double base=0;};
    explicit LiveTimeshift(QObject* parent=nullptr);
    ~LiveTimeshift();
    void start(QString executable,QString video,QString audio,RequestPolicy videoPolicy,RequestPolicy audioPolicy);
    Playback playback(double seconds);
    double startTime() const{return buffer_.start();}
    double endTime() const{return buffer_.end();}
    qint64 bytes() const{return buffer_.bytes();}
    bool ready() const{return buffer_.ready();}
    bool running() const{return process_.state()!=QProcess::NotRunning;}
signals:
    void available();
    void rangeChanged();
    void failed(QString message);
    void expired();
private:
    struct Client;
    FlvBuffer buffer_;
    QProcess process_;
    QTcpServer server_;
    QTimer notify_;
    QString token_;
    QMap<QString,std::pair<quint64,double>> leases_;
    QList<Client*> clients_;
    bool announced_=false,stopping_=false,invalid_=false;
    void ingest();
    void accept();
    void pump(Client* client);
    void problem(QString message);
};
}
