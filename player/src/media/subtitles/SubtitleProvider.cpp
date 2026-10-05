#include "SubtitleProvider.h"
#include "domain/Models.h"
#include "media/directshow/FilterLoader.h"
#include <ass/ass.h>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <future>

namespace bp {
QByteArray toAss(const QByteArray& bytes,int slot) {
    if(bytes.size()>32*1024*1024)throw Error("SubtitleDecode","字幕超过大小上限");
    QString input;
    if(bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff"))throw Error("SubtitleDecode","请先将 UTF-16 字幕转换为 UTF-8");
    input=QString::fromUtf8(bytes);input.remove(QChar(0xfeff));
    if(input.contains("[Script Info]"))return input.toUtf8();
    auto ass=QString("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Microsoft YaHei,48,&H00FFFFFF,&H0000FFFF,&H00101010,&H80000000,0,0,0,0,100,100,0,0,1,2,0,%1,30,30,42,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n").arg(slot==0?2:8);
    QRegularExpression cue(R"((?:(\d+):)?(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(?:(\d+):)?(\d{2}):(\d{2})[,.](\d{3})[^\n]*\n([\s\S]*?)(?=\r?\n\s*\r?\n|$))");
    auto matches=cue.globalMatch(input);int count=0;
    auto time=[](const QRegularExpressionMatch& m,int first) {
        return (m.captured(first).toLongLong()*3600+m.captured(first+1).toLongLong()*60+m.captured(first+2).toLongLong())*1000+m.captured(first+3).toLongLong();
    };
    auto format=[](int64_t ms){return QString("%1:%2:%3.%4").arg(ms/3600000).arg(ms/60000%60,2,10,QChar('0')).arg(ms/1000%60,2,10,QChar('0')).arg(ms/10%100,2,10,QChar('0'));};
    while(matches.hasNext()) {
        auto m=matches.next();auto start=time(m,1),end=time(m,5);if(end<=start)continue;
        auto text=m.captured(9).trimmed();text.remove(QRegularExpression("<[^>]*>"));text.replace("{","\\{");text.replace("}","\\}");text.replace("\r","");text.replace("\n","\\N");
        ass+=QString("Dialogue: 0,%1,%2,Default,,0,0,0,,%3\n").arg(format(start),format(end),text);if(++count>200000)throw Error("SubtitleDecode","字幕事件超过上限");
    }
    if(!count)throw Error("SubtitleDecode","不支持或没有有效字幕的文本文件");return ass.toUtf8();
}
class SubtitleFrame final : public ISubRenderFrame {
    std::atomic_ulong refs_{1};
public:
    RECT output{};QImage image;POINT position{};uint64_t id=0;
    STDMETHOD(QueryInterface)(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown || iid==__uuidof(ISubRenderFrame)){*out=static_cast<ISubRenderFrame*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHOD_(ULONG,AddRef)() override{return ++refs_;}
    STDMETHOD_(ULONG,Release)() override{auto n=--refs_;if(!n)delete this;return n;}
    STDMETHOD(GetOutputRect)(RECT* r) override{if(!r)return E_POINTER;*r=output;return S_OK;}
    STDMETHOD(GetClipRect)(RECT* r) override{return GetOutputRect(r);}
    STDMETHOD(GetBitmapCount)(int* count) override{if(!count)return E_POINTER;*count=image.isNull()?0:1;return S_OK;}
    STDMETHOD(GetBitmap)(int index,ULONGLONG* bitmapId,POINT* pos,SIZE* size,LPCVOID* pixels,int* pitch) override {
        if(index || image.isNull())return E_INVALIDARG;
        if(bitmapId)*bitmapId=id;if(pos)*pos=position;if(size)*size={image.width(),image.height()};if(pixels)*pixels=image.constBits();if(pitch)*pitch=int(image.bytesPerLine());return S_OK;
    }
};
struct SubtitleProvider::Impl {
    ASS_Library* library=nullptr;
    std::array<ASS_Renderer*,2> renderers{};
    std::array<ASS_Track*,2> tracks{};
    std::array<int64_t,2> offsets{};
    uint64_t frameId=0;
    uint64_t readOrder=0;
    Microsoft::WRL::ComPtr<SubtitleFrame> cached;
    Impl() {
        library=ass_library_init();if(!library)throw Error("SubtitleDecode","libass 初始化失败");
        ass_set_message_cb(library,[](int,const char*,va_list,void*){},nullptr);
        try {for(auto& renderer:renderers){renderer=ass_renderer_init(library);if(!renderer)throw Error("SubtitleDecode","libass renderer 初始化失败");ass_set_fonts(renderer,nullptr,"Microsoft YaHei",ASS_FONTPROVIDER_AUTODETECT,nullptr,1);ass_set_cache_limits(renderer,200,64);}}
        catch(...){for(auto* renderer:renderers)if(renderer)ass_renderer_done(renderer);ass_library_done(library);throw;}
    }
    ~Impl(){for(auto* t:tracks)if(t)ass_free_track(t);for(auto* r:renderers)if(r)ass_renderer_done(r);if(library)ass_library_done(library);}
    void set(int slot,QByteArray bytes,int64_t offset) {
        if(slot<0 || slot>1)return;ASS_Track* replacement=nullptr;
        if(!bytes.isEmpty()){auto ass=toAss(bytes,slot);replacement=ass_read_memory(library,ass.data(),size_t(ass.size()),nullptr);if(!replacement)throw Error("SubtitleDecode","ASS 解析失败");}
        if(tracks[slot])ass_free_track(tracks[slot]);tracks[slot]=replacement;offsets[slot]=offset;cached.Reset();
    }
    SubtitleFrame* render(REFERENCE_TIME time,RECT output,std::array<bool,2>& visible) {
        int width=output.right-output.left,height=output.bottom-output.top;
        if(width<=0 || height<=0 || width>8192 || height>8192)return nullptr;
        std::array<ASS_Image*,2> images{};QRect area;
        bool changed=!cached || !EqualRect(&cached->output,&output);
        for(int i=0;i<2;++i)if(tracks[i]) {
            ass_set_frame_size(renderers[i],width,height);int change;
            images[i]=ass_render_frame(renderers[i],tracks[i],time/10000-offsets[i],&change);
            changed=changed || change!=0;
            for(auto* image=images[i];image;image=image->next)if(image->w && image->h){visible[i]=true;area=area.united(QRect(image->dst_x,image->dst_y,image->w,image->h));}
        }
        if(!changed){cached->AddRef();return cached.Get();}
        area=area.intersected(QRect(0,0,width,height));auto* frame=new SubtitleFrame;frame->output=output;frame->id=++frameId;cached=frame;
        if(area.isEmpty())return frame;
        frame->position={output.left+area.x(),output.top+area.y()};frame->image=QImage(area.size(),QImage::Format_ARGB32_Premultiplied);frame->image.fill(Qt::transparent);
        for(auto* head:images)for(auto* image=head;image;image=image->next) {
            auto clip=QRect(image->dst_x,image->dst_y,image->w,image->h).intersected(area);
            int r=(image->color>>24)&255,g=(image->color>>16)&255,b=(image->color>>8)&255,a=255-(image->color&255);
            for(int y=clip.top();y<=clip.bottom();++y) {
                auto* dest=reinterpret_cast<QRgb*>(frame->image.scanLine(y-area.y()));
                for(int x=clip.left();x<=clip.right();++x) {
                    int alpha=image->bitmap[(y-image->dst_y)*image->stride+(x-image->dst_x)]*a/255, inverse=255-alpha;
                    auto old=dest[x-area.x()];dest[x-area.x()]=qRgba(r*alpha/255+qRed(old)*inverse/255,g*alpha/255+qGreen(old)*inverse/255,b*alpha/255+qBlue(old)*inverse/255,alpha+qAlpha(old)*inverse/255);
                }
            }
        }return frame;
    }
};
SubtitleProvider::SubtitleProvider(std::function<void(std::string)> errors):onError(std::move(errors)) {
    worker_=std::thread([this]{
        try {impl_=std::make_unique<Impl>();}catch(const std::exception& e){if(onError)onError(e.what());}
        for(;;) {
            std::function<void()> job;{std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return stopping_ || !jobs_.empty();});if(stopping_ && jobs_.empty())break;job=std::move(jobs_.front());jobs_.pop_front();}
            try{job();}catch(const std::exception& e){if(onError)onError(e.what());}
        }impl_.reset();
    });
}
SubtitleProvider::~SubtitleProvider(){{std::lock_guard lock(mutex_);stopping_=true;jobs_.clear();}ready_.notify_all();if(worker_.joinable())worker_.join();}
void SubtitleProvider::enqueue(std::function<void()> job){{std::lock_guard lock(mutex_);if(!stopping_)jobs_.push_back(std::move(job));}ready_.notify_one();}
void SubtitleProvider::attach(IBaseFilter* renderer) {
    Microsoft::WRL::ComPtr<ISubRenderConsumer> consumer;hrCheck(renderer->QueryInterface(IID_PPV_ARGS(&consumer)),"MPCVR ISubRenderConsumer");
    attachConsumer(consumer.Get());
}
void SubtitleProvider::attachConsumer(ISubRenderConsumer* consumer) {
    {std::lock_guard lock(mutex_);consumer_=consumer;}hrCheck(consumer->Connect(this),"MPCVR subtitle connect");
}
void SubtitleProvider::detach(){Microsoft::WRL::ComPtr<ISubRenderConsumer> consumer;{std::lock_guard lock(mutex_);consumer=consumer_;consumer_.Reset();++epoch_;}if(consumer)consumer->Disconnect();}
HRESULT SubtitleProvider::Disconnect(){std::lock_guard lock(mutex_);consumer_.Reset();++epoch_;return S_OK;}
void SubtitleProvider::clear() {
    ++epoch_;Microsoft::WRL::ComPtr<ISubRenderConsumer> consumer;{std::lock_guard lock(mutex_);consumer=consumer_;}
    Microsoft::WRL::ComPtr<ISubRenderConsumer2> newer;if(consumer && SUCCEEDED(consumer.As(&newer)))newer->Clear(0);
}
void SubtitleProvider::setSlot(int slot,QByteArray bytes,int64_t offset,bool) {
    auto task=std::make_shared<std::packaged_task<void()>>([this,slot,bytes=std::move(bytes),offset]{if(impl_)impl_->set(slot,bytes,offset);});
    auto done=task->get_future();enqueue([task]{(*task)();});
    try{done.get();clear();}catch(const std::exception& e){if(onError)onError(e.what());}
}
void SubtitleProvider::beginEmbedded(int slot,QByteArray header,bool ass) {
    enqueue([this,slot,header=std::move(header),ass]{if(!impl_ || slot<0 || slot>1)return;auto data=header;if(!ass || !data.contains("[Script Info]")){data=toAss("1\n00:00:00,000 --> 00:00:01,000\nplaceholder\n\n",slot);data=data.left(data.indexOf("Dialogue:"));}impl_->set(slot,data,impl_->offsets[slot]);});clear();
}
void SubtitleProvider::resetEmbedded(int slot){enqueue([this,slot]{if(impl_ && slot>=0 && slot<2 && impl_->tracks[slot])ass_flush_events(impl_->tracks[slot]);});clear();}
void SubtitleProvider::pushEmbedded(int slot,int64_t start,int64_t stop,QByteArray bytes,bool ass) {
    {std::lock_guard lock(mutex_);if(jobs_.size()>512)return;}
    enqueue([this,slot,start,stop,bytes=std::move(bytes),ass]() mutable {if(!impl_ || slot<0 || slot>1 || !impl_->tracks[slot] || stop<=start)return;
        if(!ass){QString text=QString::fromUtf8(bytes);text.remove(QChar(0));text.replace("{","\\{");text.replace("}","\\}");text.replace("\r","");text.replace("\n","\\N");bytes=QByteArray::number(impl_->readOrder++)+",0,Default,,0,0,0,,"+text.toUtf8();}
        ass_process_chunk(impl_->tracks[slot],bytes.data(),int(bytes.size()),start/10000,(stop-start)/10000);
    });
}
HRESULT SubtitleProvider::RequestFrame(REFERENCE_TIME start,REFERENCE_TIME stop,LPVOID context) {
    Microsoft::WRL::ComPtr<ISubRenderConsumer> consumer;uint64_t epoch;
    {std::lock_guard lock(mutex_);consumer=consumer_;epoch=epoch_;if(jobs_.size()>512)return E_PENDING;}
    if(!consumer)return VFW_E_WRONG_STATE;
    RECT rect{};if(FAILED(consumer->GetRect("videoOutputRect",&rect)))return E_FAIL;
    ++requested_;
    // MPCVR's compatibility consumer waits only one video frame after this
    // method returns. Complete rasterization before returning, and deliver on
    // the requesting thread so slow full-screen ASS frames cannot arrive late.
    auto task=std::make_shared<std::packaged_task<SubtitleFrame*()>>([this,epoch,start,rect]{
        if(epoch!=epoch_)return static_cast<SubtitleFrame*>(nullptr);
        std::array<bool,2> presence{};auto* frame=impl_?impl_->render(start,rect,presence):nullptr;for(int i=0;i<2;++i)if(presence[i])++slotVisible_[i];
        if(frame && !frame->image.isNull())++visible_;
        return frame;
    });
    auto result=task->get_future();enqueue([task]{(*task)();});
    try {
        Microsoft::WRL::ComPtr<SubtitleFrame> frame;frame.Attach(result.get());
        if(epoch!=epoch_)return VFW_E_WRONG_STATE;
        return consumer->DeliverFrame(start,stop,context,frame.Get());
    }catch(const std::exception& e){if(onError)onError(e.what());return E_FAIL;}
}
HRESULT SubtitleProvider::QueryInterface(REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown || iid==__uuidof(ISubRenderProvider) || iid==__uuidof(ISubRenderOptions)){*out=static_cast<ISubRenderProvider*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
HRESULT SubtitleProvider::GetBool(LPCSTR name,bool* value){if(!name || !value)return E_POINTER;if(!_stricmp(name,"combineBitmaps")){*value=true;return S_OK;}if(!_stricmp(name,"isBitmap") || !_stricmp(name,"isMovable")){*value=false;return S_OK;}return E_INVALIDARG;}
HRESULT SubtitleProvider::SetBool(LPCSTR name,bool){return name && !_stricmp(name,"combineBitmaps")?S_OK:E_INVALIDARG;}
HRESULT SubtitleProvider::GetString(LPCSTR name,LPWSTR* value,int* chars) {
    if(!name || !value || !chars)return E_POINTER;const wchar_t* text=nullptr;
    if(!_stricmp(name,"name"))text=L"SimplePlayer Dual Subtitles";else if(!_stricmp(name,"version"))text=L"0.1";else if(!_stricmp(name,"yuvMatrix"))text=L"None";else if(!_stricmp(name,"outputLevels"))text=L"PC";
    if(!text)return E_INVALIDARG;*chars=int(wcslen(text));*value=static_cast<LPWSTR>(LocalAlloc(LMEM_FIXED,(*chars+1)*sizeof(wchar_t)));if(!*value)return E_OUTOFMEMORY;memcpy(*value,text,(*chars+1)*sizeof(wchar_t));return S_OK;
}
}
