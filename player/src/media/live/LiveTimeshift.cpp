#include "LiveTimeshift.h"
#include <QTcpSocket>
#include <QUrl>
#include <QUuid>
#include <windows.h>

namespace bp {
struct LiveTimeshift::Client : QObject {
    QTcpSocket* socket=nullptr;QByteArray request;
    quint64 next=0;double base=0;bool started=false,pumping=false;
};
LiveTimeshift::LiveTimeshift(QObject* parent):QObject(parent) {
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
    token_=QUuid::createUuid().toString(QUuid::Id128);
    connect(&server_,&QTcpServer::newConnection,this,&LiveTimeshift::accept);
    connect(&process_,&QProcess::readyReadStandardOutput,this,&LiveTimeshift::ingest);
    // Never persist subprocess stderr: upstream messages can contain signed URLs.
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{process_.readAllStandardError();});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(!stopping_ && error==QProcess::FailedToStart)problem("直播缓存组件无法启动，请检查 FFmpeg 路径");});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        ingest();const auto clients=clients_;for(auto* client:clients)pump(client);
        if(!stopping_ && code!=0)problem(QString("直播缓存连接或封装失败（%1）").arg(code));
        emit rangeChanged();
    });
    notify_.setInterval(100);connect(&notify_,&QTimer::timeout,this,[this]{
        if(!announced_ && buffer_.ready()){announced_=true;emit available();}
        emit rangeChanged();
    });
}
LiveTimeshift::~LiveTimeshift() {
    stopping_=true;notify_.stop();server_.close();
    const auto clients=clients_;for(auto* client:clients)client->socket->abort();
    if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(1500);}
}
void LiveTimeshift::start(QString executable,QString video,QString audio,RequestPolicy videoPolicy,RequestPolicy audioPolicy) {
    if(!server_.listen(QHostAddress::LocalHost,0))throw Error("LiveCache","无法创建本地直播缓存读取端口");
    QStringList args={"-hide_banner","-loglevel","error","-nostdin","-rw_timeout","15000000"};
    auto input=[&](QString url,const RequestPolicy& policy){
        if(QUrl(url).scheme().startsWith("http")){
            args<<"-user_agent"<<QString::fromStdString(policy.userAgent.empty()?"SimplePlayer":policy.userAgent);
            if(!policy.referer.empty())args<<"-referer"<<QString::fromStdString(policy.referer);
        }
        args<<"-analyzeduration"<<"1000000"<<"-probesize"<<"1048576"<<"-i"<<url;
    };
    input(video,videoPolicy);if(!audio.isEmpty())input(audio,audioPolicy);
    args<<"-map"<<"0:v:0?"<<"-map"<<(audio.isEmpty()?"0:a:0?":"1:a:0?")<<"-c"<<"copy"<<"-flvflags"<<"no_duration_filesize"<<"-flush_packets"<<"1"<<"-f"<<"flv"<<"pipe:1";
    process_.start(executable,args);notify_.start();
    QTimer::singleShot(20000,this,[this]{if(!announced_ && !stopping_)problem("直播缓存尚未收到可解码关键帧");});
}
LiveTimeshift::Playback LiveTimeshift::playback(double seconds) {
    auto* point=buffer_.point(seconds);if(!point)throw Error("LiveCache","直播尚无可播放缓存");
    auto id=QUuid::createUuid().toString(QUuid::Id128);const double base=point->ms/1000.0;
    if(leases_.size()>=16)leases_.erase(leases_.begin());leases_[id]={point->sequence,base};
    return {QString("http://127.0.0.1:%1/%2/%3.flv").arg(server_.serverPort()).arg(token_,id),base};
}
void LiveTimeshift::problem(QString message) {if(invalid_ || stopping_)return;invalid_=true;emit failed(std::move(message));}
void LiveTimeshift::ingest() {
    if(stopping_ || invalid_){process_.readAllStandardOutput();return;}
    try{buffer_.feed(process_.readAllStandardOutput());const auto clients=clients_;for(auto* client:clients)pump(client);}
    catch(const std::exception& e){problem(QString::fromUtf8(e.what()));}
}
void LiveTimeshift::accept() {
    while(server_.hasPendingConnections()) {
        auto* socket=server_.nextPendingConnection();
        if(clients_.size()>=8){socket->abort();socket->deleteLater();continue;}
        auto* client=new Client;client->setParent(this);client->socket=socket;socket->setParent(client);clients_.push_back(client);
        connect(socket,&QTcpSocket::disconnected,client,[this,client]{clients_.removeOne(client);client->deleteLater();});
        connect(socket,&QTcpSocket::bytesWritten,client,[this,client]{pump(client);});
        QTimer::singleShot(10000,client,[client]{if(!client->started)client->socket->disconnectFromHost();});
        connect(socket,&QTcpSocket::readyRead,client,[this,client]{
            if(client->started){client->socket->readAll();return;}
            client->request+=client->socket->readAll();
            if(client->request.size()>8192){client->socket->abort();return;}
            if(!client->request.contains("\r\n\r\n"))return;
            const auto words=client->request.split('\n').front().trimmed().split(' ');
            auto parts=words.size()==3?QString::fromLatin1(words[1]).split('/') : QStringList{};
            const auto id=parts.size()==3 && parts[1]==token_ && parts[2].endsWith(".flv")?parts[2].chopped(4):QString{};
            if(words.size()!=3 || words[0]!="GET" || !leases_.contains(id)){
                client->socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");client->socket->disconnectFromHost();return;
            }
            auto lease=leases_[id];auto* first=buffer_.packet(lease.first);
            if(!first){client->socket->write("HTTP/1.1 410 Gone\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");client->socket->disconnectFromHost();return;}
            client->started=true;client->next=lease.first;client->base=lease.second;
            client->socket->write("HTTP/1.1 200 OK\r\nContent-Type: video/x-flv\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");client->socket->write(first->prefix);pump(client);
        });
    }
}
void LiveTimeshift::pump(Client* client) {
    if(!client->started || client->pumping || stopping_ || client->socket->state()!=QAbstractSocket::ConnectedState)return;
    client->pumping=true;
    while(client->socket->bytesToWrite()<512*1024) {
        auto* packet=buffer_.packet(client->next);
        if(!packet) {
            auto* first=buffer_.point(buffer_.start());
            if(first && client->next<first->sequence){client->socket->abort();emit expired();}
            else if(!running())client->socket->disconnectFromHost();
            break;
        }
        client->socket->write(FlvBuffer::rebase(packet->bytes,packet->ms-qint64(client->base*1000)));++client->next;
    }
    client->pumping=false;
}
}
