#include "TextSubtitleSink.h"
#include <streams.h>
#include <cmath>
#include <cstring>
namespace bp {
class TextSubtitleSink final : public CBaseFilter,public IAMFilterMiscFlags {
public:
    explicit TextSubtitleSink(SubtitleProvider* provider);
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID,void**) override;
    STDMETHODIMP_(ULONG) GetMiscFlags() override{return AM_FILTER_MISC_FLAGS_IS_RENDERER;}
    int GetPinCount() override{return 1;}
    CBasePin* GetPin(int index) override;
    void selectSlot(int slot);
    int selectedSlot() const{return slot_;}
    QString label() const;
private:
    class Input;
    CCritSec lock_;
    std::unique_ptr<Input> input_;
    Microsoft::WRL::ComPtr<SubtitleProvider> provider_;
    std::atomic_int slot_{-1};
    QByteArray privateData_;
    QString label_="内嵌文本字幕";
    bool ass_=false;
    struct Sample {int64_t start,stop;QByteArray bytes;};
    std::deque<Sample> samples_;
    void begin();
    ~TextSubtitleSink();
};

static const GUID SinkId={0x2319c1b8,0x4517,0x486c,{0xa7,0xf8,0xa9,0x32,0x1a,0x69,0xd7,0x34}};
static const GUID Utf8Id={0x87c0b230,0x03a8,0x4fdf,{0x80,0x10,0xb2,0x7a,0x58,0x48,0x20,0x0d}};
static const GUID AssId={0x326444f7,0x686f,0x47ff,{0xa4,0xb2,0xc8,0xc9,0x63,0x07,0xb4,0xc2}};
static const GUID SsaId={0x3020560f,0x255a,0x4ddc,{0x80,0x6e,0x6c,0x5c,0xc6,0xdc,0xd7,0x0a}};
static const GUID Ass2Id={0x370689e7,0xb226,0x4f67,{0x97,0x8d,0xf1,0x0b,0xc1,0xa9,0xc6,0xae}};
bool isTextSubtitleType(const AM_MEDIA_TYPE& type) {
    return type.majortype==MEDIATYPE_Text || (type.majortype==SubtitleMedia &&
        (type.subtype==Utf8Id || type.subtype==AssId || type.subtype==Ass2Id || type.subtype==SsaId));
}
class TextSubtitleSink::Input final : public CBaseInputPin {
    TextSubtitleSink* owner_;
    std::atomic_int64_t segment_{0};
    std::atomic<double> rate_{1};
public:
    Input(TextSubtitleSink* owner,HRESULT* hr):CBaseInputPin(L"Text Subtitle Input",owner,&owner->lock_,hr,L"Text"),owner_(owner){}
    HRESULT CheckMediaType(const CMediaType* type) override {
        return isTextSubtitleType(*type)?S_OK:VFW_E_TYPE_NOT_ACCEPTED;
    }
    HRESULT SetMediaType(const CMediaType* type) override {
        auto hr=CBaseInputPin::SetMediaType(type);if(FAILED(hr))return hr;
        CAutoLock lock(&owner_->lock_);owner_->ass_=type->subtype==AssId || type->subtype==Ass2Id || type->subtype==SsaId;owner_->privateData_.clear();
        // FORMAT_SubtitleInfo: DWORD offset, ISO language[4], UTF-16 name[256].
        if(type->cbFormat>=520 && type->pbFormat){DWORD offset=0;memcpy(&offset,type->pbFormat,4);if(offset>=520 && offset<=type->cbFormat)owner_->privateData_=QByteArray(reinterpret_cast<const char*>(type->pbFormat+offset),int(type->cbFormat-offset));
            QByteArray lang(reinterpret_cast<const char*>(type->pbFormat+4),3);owner_->label_="内嵌文本字幕 · "+QString::fromUtf8(lang);}
        owner_->begin();return S_OK;
    }
    STDMETHODIMP NewSegment(REFERENCE_TIME start,REFERENCE_TIME stop,double rate) override {
        if(!std::isfinite(rate) || rate<=0)return E_INVALIDARG;segment_=start;rate_=rate;{CAutoLock lock(&owner_->lock_);owner_->samples_.clear();}owner_->begin();return CBaseInputPin::NewSegment(start,stop,rate);
    }
    STDMETHODIMP BeginFlush() override{auto hr=CBaseInputPin::BeginFlush();{CAutoLock lock(&owner_->lock_);owner_->samples_.clear();}if(owner_->slot_>=0)owner_->provider_->resetEmbedded(owner_->slot_);return hr;}
    STDMETHODIMP EndOfStream() override{owner_->NotifyEvent(EC_COMPLETE,S_OK,reinterpret_cast<LONG_PTR>(owner_));return S_OK;}
    STDMETHODIMP Receive(IMediaSample* sample) override {
        auto hr=CBaseInputPin::Receive(sample);if(FAILED(hr) || hr==S_FALSE)return hr;
        REFERENCE_TIME start=0,stop=0;if(FAILED(sample->GetTime(&start,&stop)) || stop<=start)return S_OK;
        BYTE* bytes=nullptr;if(FAILED(sample->GetPointer(&bytes)))return E_FAIL;auto length=sample->GetActualDataLength();if(length<0 || length>1024*1024)return E_INVALIDARG;
        auto base=segment_.load();auto speed=rate_.load();Sample copied{base+int64_t(start*speed),base+int64_t(stop*speed),QByteArray(reinterpret_cast<const char*>(bytes),length)};
        CAutoLock lock(&owner_->lock_);owner_->samples_.push_back(copied);if(owner_->samples_.size()>512)owner_->samples_.pop_front();int slot=owner_->slot_;if(slot>=0)owner_->provider_->pushEmbedded(slot,copied.start,copied.stop,copied.bytes,owner_->ass_);return S_OK;
    }
};
TextSubtitleSink::TextSubtitleSink(SubtitleProvider* provider):CBaseFilter(L"Bilibili Text Subtitle Sink",nullptr,&lock_,SinkId),provider_(provider){HRESULT hr=S_OK;input_=std::make_unique<Input>(this,&hr);if(FAILED(hr))throw std::runtime_error("Subtitle input creation failed");}
TextSubtitleSink::~TextSubtitleSink()=default;
STDMETHODIMP TextSubtitleSink::NonDelegatingQueryInterface(REFIID iid,void** out){if(iid==IID_IAMFilterMiscFlags)return GetInterface(static_cast<IAMFilterMiscFlags*>(this),out);return CBaseFilter::NonDelegatingQueryInterface(iid,out);}
CBasePin* TextSubtitleSink::GetPin(int index){return index==0?input_.get():nullptr;}
void TextSubtitleSink::begin(){CAutoLock lock(&lock_);if(slot_>=0){provider_->beginEmbedded(slot_,privateData_,ass_);for(const auto& sample:samples_)provider_->pushEmbedded(slot_,sample.start,sample.stop,sample.bytes,ass_);}}
void TextSubtitleSink::selectSlot(int slot){slot_=slot>=0 && slot<2?slot:-1;begin();}
QString TextSubtitleSink::label() const{return label_;}
Microsoft::WRL::ComPtr<IBaseFilter> makeTextSubtitleSink(SubtitleProvider* provider,TextSubtitleSink** sink){auto* created=new TextSubtitleSink(provider);Microsoft::WRL::ComPtr<IBaseFilter> filter;created->QueryInterface(IID_PPV_ARGS(&filter));*sink=created;return filter;}
void selectEmbeddedSlot(TextSubtitleSink* sink,int slot){sink->selectSlot(slot);}
int selectedEmbeddedSlot(TextSubtitleSink* sink){return sink->selectedSlot();}
QString embeddedLabel(TextSubtitleSink* sink){return sink->label();}
}
