#include "GraphSession.h"
#include "HlsCompatibility.h"
#include "media/subtitles/SubtitleProvider.h"
#include "media/subtitles/TextSubtitleSink.h"
#include "media/network/LavNetworkExtension.h"
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QCryptographicHash>
#include <cmath>
#include <algorithm>

namespace bp {
static const CLSID SourceId={0xb98d13e7,0x55db,0x4385,{0xa3,0x3d,0x09,0xfd,0x1b,0xa2,0x63,0x38}};
static const CLSID VideoId={0xee30215d,0x164f,0x4a92,{0xa4,0xeb,0x9d,0x4c,0x13,0x39,0x0f,0x9f}};
static const CLSID AudioId={0xe8e73b6b,0x4cb3,0x44a4,{0xbe,0x99,0x4f,0x7b,0xcb,0x96,0xe4,0x91}};
static const CLSID RendererId={0x71f080aa,0x8661,0x4093,{0xb1,0x5e,0x4f,0x69,0x03,0xe7,0x7d,0x0a}};
static const CLSID BfrcId={0xfcc4769c,0x1e45,0x4e60,{0x8c,0xbf,0xb1,0x59,0xb2,0x58,0x45,0x87}};
static const CLSID MpcAudioId={0x601d2a2b,0x9cde,0x40bd,{0x86,0x50,0x04,0x85,0xe3,0x52,0x27,0x27}};
interface __declspec(uuid("C8FF17F9-5365-4F32-8AD5-6C550342C2F7")) IURLSourceFilterLAV : IUnknown {
    STDMETHOD(LoadURL)(LPCOLESTR,LPCOLESTR,LPCOLESTR)=0;
};
// Leading vtable of MPC-BE 1.9.1 IMpcAudioRendererFilter, avoiding its CString ABI.
interface __declspec(uuid("495D2C66-D430-439B-9DEE-40F9B7929BBA")) IMpcAudioSetup : IUnknown {
    STDMETHOD(Apply)()=0;
    STDMETHOD(SetWasapiMode)(INT)=0;
    STDMETHOD_(INT,GetWasapiMode)()=0;
};
// Official ILAVVideoStatus ABI, shared by the pinned headers and the installed LAV.
interface __declspec(uuid("1CC2385F-36FA-41B1-9942-5024CE0235DC")) ILavDecoderStatus : IUnknown {
    STDMETHOD_(LPCWSTR,GetActiveDecoderName)()=0;
    STDMETHOD(GetHWAccelActiveDevice)(BSTR*)=0;
};
// Leading IExFilterConfig ABI from MPCVR's FilterInterfaces.h. Redraw is a
// renderer command on this interface, not ISubRenderOptions::SetBool.
interface __declspec(uuid("37CBDF10-D65E-4E5A-8F37-40E0C8EA1695")) IMpcRendererConfig : IUnknown {
    STDMETHOD(Flt_GetBool)(LPCSTR,bool*)=0;
    STDMETHOD(Flt_GetInt)(LPCSTR,int*)=0;
    STDMETHOD(Flt_GetInt64)(LPCSTR,__int64*)=0;
    STDMETHOD(Flt_GetDouble)(LPCSTR,double*)=0;
    STDMETHOD(Flt_GetString)(LPCSTR,LPWSTR*,unsigned*)=0;
    STDMETHOD(Flt_GetBin)(LPCSTR,LPVOID*,unsigned*)=0;
    STDMETHOD(Flt_SetBool)(LPCSTR,bool)=0;
};
static void freeType(AM_MEDIA_TYPE* t){if(!t)return;if(t->cbFormat)CoTaskMemFree(t->pbFormat);if(t->pUnk)t->pUnk->Release();CoTaskMemFree(t);}
static void selectTextTrack(IBaseFilter* source) {
    ComPtr<IAMStreamSelect> streams;if(FAILED(source->QueryInterface(IID_PPV_ARGS(&streams))))return;
    DWORD count=0;if(FAILED(streams->Count(&count)))return;
    for(DWORD i=0;i<count;++i) {
        AM_MEDIA_TYPE* type=nullptr;LCID language=0;WCHAR* name=nullptr;
        auto hr=streams->Info(long(i),&type,nullptr,&language,nullptr,&name,nullptr,nullptr);
        // LAV exposes synthetic "No subtitles" and forced-auto tracks alongside real streams.
        bool suitable=SUCCEEDED(hr) && type && isTextSubtitleType(*type) && language!=LCID(-1)
            && (!name || QString::fromWCharArray(name)!="Forced Subtitles (auto)");
        freeType(type);CoTaskMemFree(name);
        if(suitable){hrCheck(streams->Enable(long(i),AMSTREAMSELECTENABLE_ENABLE),"选择内嵌文本字幕轨道");return;}
    }
}
static ComPtr<IPin> pin(IBaseFilter* filter,PIN_DIRECTION direction,const GUID* media=nullptr) {
    ComPtr<IEnumPins> pins;hrCheck(filter->EnumPins(&pins),"EnumPins");ComPtr<IPin> p;
    while(pins->Next(1,&p,nullptr)==S_OK) {
        PIN_DIRECTION dir;p->QueryDirection(&dir);ComPtr<IPin> peer;
        if(dir==direction && p->ConnectedTo(&peer)!=S_OK) {
            if(!media)return p;
            ComPtr<IEnumMediaTypes> types;p->EnumMediaTypes(&types);AM_MEDIA_TYPE* t=nullptr;
            while(types && types->Next(1,&t,nullptr)==S_OK){bool match=t->majortype==*media;freeType(t);if(match)return p;}
        }p.Reset();
    }return {};
}
void GraphThread::run(){auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(SUCCEEDED(result)){exec();CoUninitialize();}}
GraphSession::GraphSession(HttpService* http):http_(http) {
    timer_.setParent(this);timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,&GraphSession::poll);
}
GraphSession::~GraphSession(){close();}
ComPtr<IBaseFilter> GraphSession::loadSource(const std::string& address,const FilterPaths& paths,const wchar_t* name,bool participate) {
    cancel_->check();auto source=loader_->create(QDir(paths.lav).filePath("LAVSplitter.ax"),SourceId);
    ComPtr<IGraphBuilder> owner=graph_;
    if(!participate)hrCheck(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&owner)),"创建候选探测图");
    hrCheck(owner->AddFilter(source.Get(),name),"添加 LAV Source");
    QUrl url(QString::fromUtf8(address));
    if(url.scheme().startsWith("http")) {
        ComPtr<IURLSourceFilterLAV> loader;hrCheck(source.As(&loader),"LAV URL interface");auto policy=http_->policyFor(address);
        // Stock LAV only supports User-Agent/Referer. Never silently send a global Cookie header.
        if(!http_->cookieHeader(url,http_->snapshot().epoch).empty()) {
            ComPtr<IBpLavNetworkControl> extension;
            if(FAILED(source.As(&extension)))throw Error("NetworkPolicyUnsupported","当前 LAV 缺少请求策略扩展，无法携带该媒体地址所需 Cookie");
            throw Error("NetworkPolicyUnsupported","该 LAV 扩展尚未完成分片与重定向契约验证");
        }
        auto uri=QString::fromUtf8(address).toStdWString(),ua=QString::fromUtf8(policy.userAgent).toStdWString(),referer=QString::fromUtf8(policy.referer).toStdWString();
        hrCheck(loader->LoadURL(uri.c_str(),ua.empty()?nullptr:ua.c_str(),referer.empty()?nullptr:referer.c_str()),"LAV 打开媒体地址");
    } else {
        ComPtr<IFileSourceFilter> loader;hrCheck(source.As(&loader),"LAV File interface");
        auto path=url.isLocalFile()?url.toLocalFile():QString::fromUtf8(address);hrCheck(loader->Load(reinterpret_cast<LPCOLESTR>(path.utf16()),nullptr),"LAV 打开本地媒体");
    }
    cancel_->check();
    if(participate){sources_.push_back(source);ComPtr<IMediaSeeking> seeking;if(SUCCEEDED(source.As(&seeking)))sourceSeeking_.push_back(seeking);}
    else hrCheck(owner->RemoveFilter(source.Get()),"移出候选探测图");
    return source;
}
void GraphSession::connectRole(IBaseFilter* source,const GUID& role,IBaseFilter* decoder,IBaseFilter* output) {
    auto sourcePin=pin(source,PINDIR_OUTPUT,&role);if(!sourcePin)throw Error("PinConnect","所选媒体源缺少必要音视频 pin");
    auto input=pin(decoder,PINDIR_INPUT);if(!input)throw Error("PinConnect","解码器缺少 input pin");
    hrCheck(graph_->ConnectDirect(sourcePin.Get(),input.Get(),nullptr),"源 -> LAV 解码器");
    auto decoded=pin(decoder,PINDIR_OUTPUT),target=pin(output,PINDIR_INPUT);
    if(!decoded || !target)throw Error("PinConnect","解码器或 renderer 缺少 pin");
    hrCheck(graph_->ConnectDirect(decoded.Get(),target.Get(),nullptr),"解码器 -> renderer");
}
void GraphSession::open(PlaybackPlan plan,std::string video,std::string audio,FilterPaths paths,HWND target,Cancel cancel,MediaTime restore,bool paused,double rate) {
    close();key_=plan.key;cancel_=std::move(cancel);ended_=false;
    try {
        cancel_->check();loader_=std::make_unique<FilterLoader>();
        hrCheck(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&graph_)),"创建 DirectShow 图");
        hrCheck(graph_.As(&control_),"IMediaControl");graph_.As(&seeking_);graph_.As(&events_);graph_.As(&audioControl_);
        auto source=loadSource(video,paths,L"LAV Primary Source");bool hasVideo=bool(pin(source.Get(),PINDIR_OUTPUT,&MEDIATYPE_Video));bool hasAudio=bool(pin(source.Get(),PINDIR_OUTPUT,&MEDIATYPE_Audio));
        QVariantMap roles;
        for(auto& c:plan.candidates)if(c.url==video){c.role=hasVideo?(hasAudio?StreamRole::Muxed:StreamRole::Video):hasAudio?StreamRole::Audio:StreamRole::Unknown;roles[QString::fromStdString(c.id)]=int(c.role);}
        emit candidateRoles(key_,roles);
        QVariantMap diagnostics{{"network", "stock LAV: UA/Referer only; segmented policy/cancellable open unverified"},{"renderer","MPC Video Renderer"}};
        if(hasVideo) {
            renderer_=loader_->create(paths.renderer,RendererId);hrCheck(graph_->AddFilter(renderer_.Get(),L"MPC Video Renderer"),"添加 MPCVR");
            hrCheck(renderer_.As(&videoWindow_),"MPCVR IVideoWindow");
            hrCheck(renderer_.As(&basicVideo_),"MPCVR IBasicVideo2");renderer_->QueryInterface(IID_IQualProp,reinterpret_cast<void**>(videoQuality_.GetAddressOf()));videoTarget_=target;
            hrCheck(videoWindow_->put_Owner(OAHWND(target)),"视频窗口 owner");
            // MPCVR creates a visible child itself; put_Visible/put_WindowStyle are E_NOTIMPL.
            videoDecoder_=loader_->create(QDir(paths.lav).filePath("LAVVideo.ax"),VideoId);hrCheck(graph_->AddFilter(videoDecoder_.Get(),L"LAV Video Decoder"),"添加视频解码器");
            if(paths.enableBfrc) {
                ComPtr<IBaseFilter> frc;
                try {
                    frc=loader_->create(paths.bfrc,BfrcId);hrCheck(graph_->AddFilter(frc.Get(),L"Bluesky FRC"),"添加 BFRC");
                    connectRole(source.Get(),MEDIATYPE_Video,videoDecoder_.Get(),frc.Get());
                    auto out=pin(frc.Get(),PINDIR_OUTPUT),in=pin(renderer_.Get(),PINDIR_INPUT);hrCheck(graph_->ConnectDirect(out.Get(),in.Get(),nullptr),"BFRC -> MPCVR");diagnostics["frc"]="BFRC 已连接（实际补帧效果待测量）";
                }catch(const Error&) {
                    if(frc){ComPtr<IEnumPins> ps;frc->EnumPins(&ps);ComPtr<IPin> p;while(ps && ps->Next(1,&p,nullptr)==S_OK){ComPtr<IPin> other;if(p->ConnectedTo(&other)==S_OK){graph_->Disconnect(other.Get());graph_->Disconnect(p.Get());}p.Reset();}graph_->RemoveFilter(frc.Get());}
                    auto decoded=pin(videoDecoder_.Get(),PINDIR_OUTPUT);auto src=pin(source.Get(),PINDIR_OUTPUT,&MEDIATYPE_Video);
                    if(src)connectRole(source.Get(),MEDIATYPE_Video,videoDecoder_.Get(),renderer_.Get());
                    else {auto input=pin(renderer_.Get(),PINDIR_INPUT);hrCheck(graph_->ConnectDirect(decoded.Get(),input.Get(),nullptr),"普通视频回退");}
                    diagnostics["frc"]="BFRC 连接失败，使用普通播放";
                }
            } else {connectRole(source.Get(),MEDIATYPE_Video,videoDecoder_.Get(),renderer_.Get());diagnostics["frc"]="关闭";}
            RECT bounds{};GetClientRect(target,&bounds);resize(bounds.right,bounds.bottom);
            hrCheck(videoWindow_->put_MessageDrain(OAHWND(target)),"视频窗口输入转发");
            subtitles_=new SubtitleProvider([this,key=key_](std::string error){
                QMetaObject::invokeMethod(this,[this,key,message=QString::fromUtf8(error)]{emit subtitleWarning(key,message);});
            });
            subtitles_->attach(renderer_.Get());
            selectTextTrack(source.Get());
            if(auto subtitlePin=pin(source.Get(),PINDIR_OUTPUT,&SubtitleMedia)) {
                TextSubtitleSink* sink=nullptr;auto filter=makeTextSubtitleSink(subtitles_,&sink);
                hrCheck(graph_->AddFilter(filter.Get(),L"Text Subtitle Sink"),"添加内嵌字幕接收器");
                auto input=pin(filter.Get(),PINDIR_INPUT);if(SUCCEEDED(graph_->ConnectDirect(subtitlePin.Get(),input.Get(),nullptr))){embedded_=sink;diagnostics["embeddedSubtitle"]=embeddedLabel(sink);}
                else {graph_->RemoveFilter(filter.Get());diagnostics["embeddedSubtitleUnsupported"]="该内嵌轨道不是首版支持的 UTF-8/ASS/SSA 文本字幕";}
            }
        }
        ComPtr<IBaseFilter> audioSource=source;
        if(hasVideo && !hasAudio) {
            const bool automaticAudio=audio.empty();
            std::map<std::string,ComPtr<IBaseFilter>> probed;
            if(audio.empty()) {
                auto* best=preferredAudioCandidate(plan.candidates);
                if(!best) {
                    // Missing va: inspect actual pins serially, then apply the same bitrate policy.
                    std::map<std::string,StreamRole> inspected;
                    for(auto& c:plan.candidates) {
                        if(c.url==video || c.role==StreamRole::Video || c.role==StreamRole::Muxed)continue;
                        cancel_->check();
                        if(auto existing=inspected.find(c.url);existing!=inspected.end())c.role=existing->second;
                        else try {
                            auto probe=loadSource(c.url,paths,L"LAV Candidate Probe",false);
                            bool v=bool(pin(probe.Get(),PINDIR_OUTPUT,&MEDIATYPE_Video)),a=bool(pin(probe.Get(),PINDIR_OUTPUT,&MEDIATYPE_Audio));
                            c.role=v?(a?StreamRole::Muxed:StreamRole::Video):a?StreamRole::Audio:StreamRole::Unknown;
                            inspected[c.url]=c.role;if(c.role==StreamRole::Audio)probed[c.url]=probe;
                        }catch(const Error& e){if(e.code=="Cancelled" || e.code=="NetworkPolicyUnsupported")throw;inspected[c.url]=StreamRole::Unknown;}
                        roles[QString::fromStdString(c.id)]=int(c.role);
                    }
                    emit candidateRoles(key_,roles);
                    best=preferredAudioCandidate(plan.candidates);
                }
                if(best)audio=best->url;
            }
            if(!audio.empty()){
                if(auto found=probed.find(audio);found!=probed.end()){
                    audioSource=found->second;hrCheck(graph_->AddFilter(audioSource.Get(),L"LAV Audio Source"),"添加已探测音频源");sources_.push_back(audioSource);
                    ComPtr<IMediaSeeking> seeking;if(SUCCEEDED(audioSource.As(&seeking)))sourceSeeking_.push_back(seeking);
                }else audioSource=loadSource(audio,paths,L"LAV Audio Source");
                hasAudio=bool(pin(audioSource.Get(),PINDIR_OUTPUT,&MEDIATYPE_Audio));if(!hasAudio)throw Error("PinConnect","所选音轨没有音频 pin");
                for(auto& c:plan.candidates)if(c.url==audio) {
                    c.role=StreamRole::Audio;roles[QString::fromStdString(c.id)]=int(c.role);
                    diagnostics["selectedAudioId"]=QString::fromStdString(c.id);diagnostics["audioBitrateBps"]=qlonglong(c.bitrateBps);break;
                }
                diagnostics["automaticAudioSelection"]=automaticAudio;
                if(auto* best=preferredAudioCandidate(plan.candidates))diagnostics["highestAudioBitrateBps"]=qlonglong(best->bitrateBps);
                emit candidateRoles(key_,roles);
            }
            else throw Error("MissingAudio","独立视频流缺少可用音轨");
        }
        diagnostics["scriptDefaultEntry"]=video==plan.defaultUrl;
        if(!hasVideo && !hasAudio)throw Error("UnsupportedCodec","媒体源没有可用音视频流");
        if(hasAudio) {
            auto decoder=loader_->create(QDir(paths.lav).filePath("LAVAudio.ax"),AudioId);hrCheck(graph_->AddFilter(decoder.Get(),L"LAV Audio Decoder"),"添加音频解码器");
            ComPtr<IBaseFilter> renderer;
            if(!paths.audio.isEmpty()) {renderer=loader_->create(paths.audio,MpcAudioId);ComPtr<IMpcAudioSetup> setup;hrCheck(renderer.As(&setup),"WASAPI 配置接口");hrCheck(setup->SetWasapiMode(0),"WASAPI shared mode");hrCheck(renderer.As(&audioSeeking_),"保持音调变速接口");diagnostics["audio"]="MPC Audio Renderer / WASAPI shared";}
            else {hrCheck(CoCreateInstance(CLSID_DSoundRender,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&renderer)),"兼容音频输出");diagnostics["audio"]="DirectSound 兼容输出（尚未配置 WASAPI renderer）";}
            hrCheck(graph_->AddFilter(renderer.Get(),L"Audio Output"),"添加音频输出");connectRole(audioSource.Get(),MEDIATYPE_Audio,decoder.Get(),renderer.Get());
            ComPtr<IReferenceClock> clock;if(SUCCEEDED(renderer.As(&clock))){ComPtr<IMediaFilter> filter;graph_.As(&filter);hrCheck(filter->SetSyncSource(clock.Get()),"统一音频时钟");}
        }
        DWORD caps=AM_SEEKING_CanSeekAbsolute;canSeek_=seeking_ && seeking_->CheckCapabilities(&caps)==S_OK;
        LONGLONG duration=0;
        if(!seeking_ || FAILED(seeking_->GetDuration(&duration)) || duration<=0)canSeek_=false;
        diagnostics["sourceDuration"]=duration/10000000.0;
        for(const auto& seeking:sourceSeeking_){DWORD c=AM_SEEKING_CanSeekAbsolute;if(seeking->CheckCapabilities(&c)!=S_OK)canSeek_=false;}
        // The packaged MPC audio renderer uses FFmpeg atempo. DirectSound's
        // rate conversion changes pitch, so never enable variable speed there.
        rateSupported_=bool(seeking_) && (!hasAudio || bool(audioSeeking_));
        if(rateSupported_ && rate!=1.0)setPlaybackRate(rate);
        diagnostics["playbackRateSupported"]=rateSupported_;
        diagnostics["playbackRate"]=playbackRate_;
        diagnostics["pitchPreserved"]=!hasAudio || bool(audioSeeking_);
        if(hasAudio && audioSeeking_)diagnostics["tempoBackend"]="MPC Audio Renderer / FFmpeg atempo";
        if(restore.count()>0 && canSeek_)seek(restore);
        diagnostics["sourceCount"]=int(sources_.size());diagnostics["videoConnected"]=hasVideo;diagnostics["audioConnected"]=hasAudio;
        diagnostics["hlsHttpPersistent"]=false;
        diagnostics["hlsHttpMultiple"]=false;
        diagnostics["guardedHttpOpens"]=qulonglong(guardedHttpOpens());
        if(videoDecoder_){ComPtr<ILavDecoderStatus> status;if(SUCCEEDED(videoDecoder_.As(&status))){
            if(auto name=status->GetActiveDecoderName())diagnostics["activeVideoDecoder"]=QString::fromWCharArray(name);
            BSTR device=nullptr;if(SUCCEEDED(status->GetHWAccelActiveDevice(&device)) && device)diagnostics["activeDecodeDevice"]=QString::fromWCharArray(device);SysFreeString(device);
        }}
        MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);DEVMODEW mode{};mode.dmSize=sizeof(mode);
        if(GetMonitorInfoW(MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST),&monitor) && EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode))diagnostics["displayRefreshHz"]=int(mode.dmDisplayFrequency);
        ComPtr<IMediaFilter> mediaFilter;graph_.As(&mediaFilter);ComPtr<IReferenceClock> selectedClock;
        if(mediaFilter && SUCCEEDED(mediaFilter->GetSyncSource(&selectedClock)))diagnostics["sharedClock"]=bool(selectedClock);
        hrCheck(paused?control_->Pause():control_->Run(),"开始播放");if(!paused)pausedPosition_.reset();timer_.start();emit opened(key_,canSeek_,diagnostics);
    }catch(const Error& e){auto key=key_;close();emit failed(key,QString::fromStdString(e.code),QString::fromUtf8(e.what()));}
    catch(const std::exception& e){auto key=key_;close();emit failed(key,"MediaGraph",QString::fromUtf8(e.what()));}
}
void GraphSession::close() {
    timer_.stop();pausedPosition_.reset();if(control_)control_->Stop();
    if(subtitles_){subtitles_->detach();subtitles_->Release();subtitles_=nullptr;}
    videoQuality_.Reset();basicVideo_.Reset();videoWindow_.Reset();videoTarget_=nullptr;videoGeometry_.clear();
    embedded_=nullptr;sourceSeeking_.clear();sources_.clear();renderer_.Reset();videoDecoder_.Reset();audioSeeking_.Reset();audioControl_.Reset();events_.Reset();seeking_.Reset();control_.Reset();graph_.Reset();loader_.reset();canSeek_=false;rateSupported_=false;playbackRate_=1.0;
}
bool GraphSession::pause(bool paused) {
    if(!control_)return false;
    try {
        hrCheck(paused?control_->Pause():control_->Run(),"暂停/恢复");
        if(!paused)pausedPosition_.reset();return true;
    }catch(const Error& e){auto key=key_;close();emit failed(key,QString::fromStdString(e.code),QString::fromUtf8(e.what()));return false;}
}
bool GraphSession::seek(MediaTime position) {
    if(!canSeek_ || !seeking_){emit seekFinished(key_,false);return false;}
    LONGLONG target=std::max<int64_t>(0,position.count()),old=0;seeking_->GetCurrentPosition(&old);
    OAFilterState state=State_Stopped;if(control_)control_->GetState(0,&state);bool running=state==State_Running;
    if(running && sourceSeeking_.size()>1)control_->Pause();
    bool success=SUCCEEDED(seeking_->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning));
    // DirectShow's aggregate seeking may choose one source. Address each participating source explicitly.
    if(success && sourceSeeking_.size()>1) {
        for(auto& source:sourceSeeking_)if(FAILED(source->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning))){success=false;break;}
    }
    if(!success){seeking_->SetPositions(&old,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning);for(auto& source:sourceSeeking_)source->SetPositions(&old,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning);}
    if(running && sourceSeeking_.size()>1)control_->Run();
    if(success){ended_=false;if(!running)pausedPosition_=target;LONGLONG duration=0;seeking_->GetDuration(&duration);emit this->position(key_,target/10000000.0,duration/10000000.0);}if(subtitles_)subtitles_->clear();emit seekFinished(key_,success);return success;
}
void GraphSession::volume(int percent){if(audioControl_){long gain=percent<=0?-10000:long(2000*std::log10(std::clamp(percent,1,100)/100.0));audioControl_->put_Volume(gain);}}
bool GraphSession::setPlaybackRate(double rate) {
    if(!rateSupported_ || !seeking_ || !std::isfinite(rate) || rate<0.25 || rate>4.0)return false;
    if(rate==playbackRate_)return true;
    OAFilterState state=State_Stopped;
    if(control_ && FAILED(control_->GetState(1000,&state)))return false;
    const bool running=state==State_Running;
    if(running && FAILED(control_->Pause()))return false;
    const double previous=playbackRate_;
    auto apply=[&](double value) {
        // Configure tempo before upstream filters flush/send the new segment.
        if(audioSeeking_ && FAILED(audioSeeking_->SetRate(value)))return false;
        if(FAILED(seeking_->SetRate(value)))return false;
        for(const auto& source:sourceSeeking_)if(FAILED(source->SetRate(value)))return false;
        return true;
    };
    bool success=apply(rate);
    if(!success) {
        // Try every participant during rollback even when one rejects the
        // request. An early return here could leave audio and video at
        // different rates after a partial source update.
        bool restored=!audioSeeking_ || SUCCEEDED(audioSeeking_->SetRate(previous));
        if(FAILED(seeking_->SetRate(previous)))restored=false;
        for(const auto& source:sourceSeeking_)if(FAILED(source->SetRate(previous)))restored=false;
        if(!restored){emit failed(key_,"PlaybackRate","变速失败且无法恢复原速度，请重新打开媒体");return false;}
    }
    else playbackRate_=rate;
    if(subtitles_)subtitles_->clear();
    if(running && FAILED(control_->Run())){emit failed(key_,"PlaybackRate","变速后恢复播放失败");return false;}
    return success;
}
void GraphSession::resize(int width,int height) {
    if(!videoWindow_ || !basicVideo_)return;
    // DirectShow rectangles use physical client pixels. Qt size notifications can
    // arrive before WindowContainer has applied its final native geometry.
    RECT bounds{};if(videoTarget_ && GetClientRect(videoTarget_,&bounds)){width=bounds.right;height=bounds.bottom;}
    if(width<=0 || height<=0)return;
    long nativeWidth=0,nativeHeight=0,aspectX=0,aspectY=0;
    basicVideo_->GetVideoSize(&nativeWidth,&nativeHeight);
    if(FAILED(basicVideo_->GetPreferredAspectRatio(&aspectX,&aspectY)) || aspectX<=0 || aspectY<=0){aspectX=nativeWidth;aspectY=nativeHeight;}
    int fittedWidth=width,fittedHeight=height;
    if(aspectX>0 && aspectY>0) {
        if(int64_t(width)*aspectY>int64_t(height)*aspectX)fittedWidth=std::max(1,int(int64_t(height)*aspectX/aspectY));
        else fittedHeight=std::max(1,int(int64_t(width)*aspectY/aspectX));
    }
    const int left=(width-fittedWidth)/2,top=(height-fittedHeight)/2;
    if(videoGeometry_.value("videoHostWidth").toInt()==width && videoGeometry_.value("videoHostHeight").toInt()==height &&
       videoGeometry_.value("videoDestinationWidth").toInt()==fittedWidth && videoGeometry_.value("videoDestinationHeight").toInt()==fittedHeight &&
       videoGeometry_.value("nativeVideoWidth").toInt()==nativeWidth && videoGeometry_.value("nativeVideoHeight").toInt()==nativeHeight)return;
    hrCheck(videoWindow_->SetWindowPosition(0,0,width,height),"配置视频窗口矩形");
    hrCheck(basicVideo_->SetDestinationPosition(left,top,fittedWidth,fittedHeight),"配置视频帧目标矩形");
    videoGeometry_={{"videoHostWidth",width},{"videoHostHeight",height},{"videoDestinationLeft",left},{"videoDestinationTop",top},
                    {"videoDestinationWidth",fittedWidth},{"videoDestinationHeight",fittedHeight},{"nativeVideoWidth",int(nativeWidth)},{"nativeVideoHeight",int(nativeHeight)}};
}
void GraphSession::setSubtitle(int slot,QByteArray content,int64_t offsetMs,bool danmaku,bool embedded){
    if(embedded_ && selectedEmbeddedSlot(embedded_)==slot && !embedded)selectEmbeddedSlot(embedded_,-1);
    if(subtitles_)subtitles_->setSlot(slot,std::move(content),offsetMs,danmaku);
    if(embedded && embedded_)selectEmbeddedSlot(embedded_,slot);
    // Clearing the subtitle queue does not repaint an already paused frame.
    // Waited-for slot updates above make this redraw use the new bitmap.
    OAFilterState state=State_Stopped;
    if(control_ && SUCCEEDED(control_->GetState(0,&state)) && state==State_Paused){
        ComPtr<IMpcRendererConfig> options;
        if(renderer_ && SUCCEEDED(renderer_.As(&options)))options->Flt_SetBool("cmd_redraw",true);
    }
}
void GraphSession::poll() {
    if(cancel_ && cancel_->cancelled){close();return;}
    if(basicVideo_) {
        try {resize(0,0);}catch(const Error& e){auto key=key_;close();emit failed(key,QString::fromStdString(e.code),QString::fromUtf8(e.what()));return;}
        auto values=videoGeometry_;int frames=0;if(videoQuality_ && SUCCEEDED(videoQuality_->get_FramesDrawn(&frames)))values["rendererFramesDrawn"]=frames;
        emit videoDiagnostics(key_,values);
    }
    if(subtitles_)emit subtitleDiagnostics(key_,subtitles_->requestedFrames(),subtitles_->framesWithText(),subtitles_->slotFramesWithText(0),subtitles_->slotFramesWithText(1));
    if(seeking_){LONGLONG position=0,duration=0;seeking_->GetCurrentPosition(&position);seeking_->GetDuration(&duration);if(pausedPosition_)position=*pausedPosition_;emit this->position(key_,position/10000000.0,duration/10000000.0);}
    long code;LONG_PTR p1,p2;
    while(events_ && events_->GetEvent(&code,&p1,&p2,0)==S_OK) {
        events_->FreeEventParams(code,p1,p2);
        if(code==EC_COMPLETE && canSeek_ && !ended_){ended_=true;emit completed(key_);}
        else if(code==EC_ERRORABORT){emit failed(key_,"MediaGraph",QString("媒体图错误 0x%1").arg(uint32_t(p1),8,16,QChar('0')));close();return;}
    }
}
}
