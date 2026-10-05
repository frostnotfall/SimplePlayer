#pragma once
#include "FilterLoader.h"
#include "domain/Models.h"
#include "network/HttpService.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <d3d9.h>
#include <vmr9.h>
namespace bp {
class SubtitleProvider;
class TextSubtitleSink;
class GraphThread : public QThread {
    void run() override;
};
class GraphSession : public QObject {
    Q_OBJECT
public:
    explicit GraphSession(HttpService* http);
    ~GraphSession();
    void open(PlaybackPlan plan,std::string video,std::string audio,FilterPaths paths,HWND target,Cancel cancel,MediaTime restore={},bool paused=false,double rate=1.0);
    void close();
    bool pause(bool paused);
    bool seek(MediaTime position);
    bool setPlaybackRate(double rate);
    void volume(int percent);
    void resize(int width,int height);
    void setSubtitle(int slot,QByteArray content,int64_t offsetMs,bool danmaku,bool embedded=false);
signals:
    void opened(bp::SessionKey key,bool canSeek,QVariantMap diagnostics);
    void failed(bp::SessionKey key,QString code,QString message);
    void position(bp::SessionKey key,double seconds,double duration);
    void completed(bp::SessionKey key);
    void seekFinished(bp::SessionKey key,bool success);
    void subtitleDiagnostics(bp::SessionKey key,uint64_t requested,uint64_t visible,uint64_t primary,uint64_t secondary);
    void subtitleWarning(bp::SessionKey key,QString message);
    void candidateRoles(bp::SessionKey key,QVariantMap roles);
    void videoDiagnostics(bp::SessionKey key,QVariantMap values);
private:
    HttpService* http_;
    std::unique_ptr<FilterLoader> loader_;
    ComPtr<IGraphBuilder> graph_;
    ComPtr<IMediaControl> control_;
    ComPtr<IMediaSeeking> seeking_;
    ComPtr<IMediaEvent> events_;
    ComPtr<IBasicAudio> audioControl_;
    ComPtr<IMediaSeeking> audioSeeking_;
    ComPtr<IVideoWindow> videoWindow_;
    ComPtr<IBasicVideo2> basicVideo_;
    ComPtr<IQualProp> videoQuality_;
    HWND videoTarget_=nullptr;
    QVariantMap videoGeometry_;
    ComPtr<IBaseFilter> renderer_,videoDecoder_;
    std::vector<ComPtr<IBaseFilter>> sources_;
    std::vector<ComPtr<IMediaSeeking>> sourceSeeking_;
    SubtitleProvider* subtitles_=nullptr;
    TextSubtitleSink* embedded_=nullptr;
    SessionKey key_;
    Cancel cancel_;
    bool canSeek_=false,ended_=false;
    bool rateSupported_=false;
    double playbackRate_=1.0;
    std::optional<int64_t> pausedPosition_;
    QTimer timer_;
    void poll();
    ComPtr<IBaseFilter> loadSource(const std::string& url,const FilterPaths& paths,const wchar_t* name,bool participate=true);
    void connectRole(IBaseFilter* source,const GUID& role,IBaseFilter* decoder,IBaseFilter* output);
};
}
Q_DECLARE_METATYPE(bp::SessionKey)
