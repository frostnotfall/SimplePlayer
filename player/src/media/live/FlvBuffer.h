#pragma once
#include <QByteArray>
#include <deque>
#include <optional>

namespace bp {
// Compressed packets only; a replay begins at a video keyframe with the codec
// headers that were current at that keyframe. All methods run on one thread.
class FlvBuffer {
public:
    struct Packet { quint64 sequence=0; qint64 ms=0; QByteArray bytes,prefix; bool keyframe=false; };
    explicit FlvBuffer(qint64 maximumBytes=128*1024*1024,qint64 maximumMs=600000);
    void feed(const QByteArray& bytes);
    double start() const;
    double end() const;
    qint64 bytes() const {return size_;}
    bool ready() const {return !packets_.empty() && end()-start()>=0.25;}
    const Packet* point(double seconds) const;
    const Packet* packet(quint64 sequence) const;
    static QByteArray rebase(QByteArray tag,qint64 milliseconds);
private:
    QByteArray pending_,header_,metadata_,videoConfig_,audioConfig_;
    std::deque<Packet> packets_;
    quint64 next_=0,wrap_=0;
    quint32 lastRaw_=0;
    std::optional<quint64> origin_;
    qint64 size_=0,maximumBytes_,maximumMs_,endMs_=0;
    bool hasVideo_=true;
    void accept(QByteArray tag);
    void prune();
};
}
