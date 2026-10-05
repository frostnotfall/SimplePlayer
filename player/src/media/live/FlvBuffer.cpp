#include "FlvBuffer.h"
#include "domain/Models.h"
#include <algorithm>
#include <cmath>

namespace bp {
static quint32 be24(const char* p){return (quint32(quint8(p[0]))<<16)|(quint32(quint8(p[1]))<<8)|quint8(p[2]);}
static quint32 be32(const char* p){return (quint32(quint8(p[0]))<<24)|be24(p+1);}
FlvBuffer::FlvBuffer(qint64 maximumBytes,qint64 maximumMs):maximumBytes_(maximumBytes),maximumMs_(maximumMs){}
void FlvBuffer::feed(const QByteArray& bytes) {
    pending_+=bytes;
    if(header_.isEmpty()) {
        if(pending_.size()<9)return;
        if(!pending_.startsWith("FLV") || pending_[3]!=1)throw Error("LiveCache","直播缓存不是有效 FLV 数据");
        const auto offset=be32(pending_.constData()+5);
        if(offset<9 || offset>1024*1024)throw Error("LiveCache","直播 FLV 头大小无效");
        if(pending_.size()<offset+4)return;
        hasVideo_=(quint8(pending_[4])&1)!=0;
        header_=pending_.left(9);header_[5]=0;header_[6]=0;header_[7]=0;header_[8]=9;header_+=QByteArray(4,0);
        pending_.remove(0,offset+4);
    }
    qsizetype consumed=0;
    while(pending_.size()-consumed>=11) {
        const auto* p=pending_.constData()+consumed;auto length=be24(p+1);
        if(length>16*1024*1024)throw Error("LiveCache","直播包超过缓存大小限制");
        const qsizetype total=15+length;if(pending_.size()-consumed<total)break;
        if(be32(p+11+length)!=length+11)throw Error("LiveCache","直播 FLV 包长度不一致");
        accept(pending_.mid(consumed,total));consumed+=total;
    }
    pending_.remove(0,consumed);
}
QByteArray FlvBuffer::rebase(QByteArray tag,qint64 milliseconds) {
    if(tag.size()<11)return tag;
    const quint32 t=quint32(std::max<qint64>(0,milliseconds));
    tag[4]=char(t>>16);tag[5]=char(t>>8);tag[6]=char(t);tag[7]=char(t>>24);return tag;
}
void FlvBuffer::accept(QByteArray tag) {
    const int type=quint8(tag[0]);const auto length=be24(tag.constData()+1);
    if(type==18){metadata_=rebase(tag,0);return;}
    if((type!=8 && type!=9) || !length)return;
    const auto* data=reinterpret_cast<const quint8*>(tag.constData()+11);
    bool config=false,keyframe=false;
    if(type==9) {
        const bool enhanced=(data[0]&0x80)!=0;
        const int codec=data[0]&15;
        config=enhanced ? codec==0 : (codec==7 || codec==12) && length>1 && data[1]==0;
        keyframe=((data[0]>>4)&7)==1 && !config && (enhanced ? codec==1 || codec==3 : (codec!=7 && codec!=12) || length>1 && data[1]==1);
        if(config)videoConfig_=rebase(tag,0);
    }else {
        config=(data[0]>>4)==10 && length>1 && data[1]==0 || (data[0]>>4)==9 && (data[0]&15)==0;
        if(config)audioConfig_=rebase(tag,0);
        keyframe=!hasVideo_ && !config;
    }
    const quint32 raw=be24(tag.constData()+4)|(quint32(quint8(tag[7]))<<24);
    qint64 time=0;
    if(!config) {
        if(raw<lastRaw_ && lastRaw_-raw>0x80000000u)wrap_+=quint64(1)<<32;
        lastRaw_=raw;const auto expanded=wrap_+raw;
        if(!origin_)origin_=expanded;
        time=expanded>=*origin_?qint64(expanded-*origin_):0;
        endMs_=std::max(endMs_,time);
    }else time=endMs_;
    if(packets_.empty() && !keyframe)return;
    QByteArray prefix;
    if(keyframe)prefix=header_+metadata_+videoConfig_+audioConfig_;
    size_+=tag.size()+prefix.size();packets_.push_back({next_++,time,std::move(tag),std::move(prefix),keyframe});prune();
}
void FlvBuffer::prune() {
    while(!packets_.empty() && (size_>maximumBytes_ || endMs_-packets_.front().ms>maximumMs_)) {
        auto next=std::find_if(packets_.begin()+1,packets_.end(),[](const Packet& p){return p.keyframe;});
        if(next==packets_.end() && size_<=maximumBytes_)break;
        const auto count=next==packets_.end()?packets_.size():size_t(next-packets_.begin());
        for(size_t i=0;i<count;++i){size_-=packets_.front().bytes.size()+packets_.front().prefix.size();packets_.pop_front();}
    }
}
double FlvBuffer::start() const{return packets_.empty()?0:packets_.front().ms/1000.0;}
double FlvBuffer::end() const{return packets_.empty()?0:endMs_/1000.0;}
const FlvBuffer::Packet* FlvBuffer::point(double seconds) const {
    if(packets_.empty() || !std::isfinite(seconds))return nullptr;
    const auto ms=qint64(std::clamp(seconds,start(),end())*1000);const Packet* result=&packets_.front();
    for(const auto& p:packets_)if(p.keyframe && p.ms<=ms)result=&p;return result;
}
const FlvBuffer::Packet* FlvBuffer::packet(quint64 sequence) const {
    if(packets_.empty() || sequence<packets_.front().sequence || sequence>packets_.back().sequence)return nullptr;
    return &packets_[size_t(sequence-packets_.front().sequence)];
}
}
