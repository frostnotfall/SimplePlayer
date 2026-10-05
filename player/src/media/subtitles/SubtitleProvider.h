#pragma once
#include <Windows.h>
#include <dshow.h>
#include <SubRenderIntf.h>
#include <wrl/client.h>
#include <QByteArray>
#include <atomic>
#include <array>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <functional>
namespace bp {
class SubtitleProvider final : public ISubRenderProvider {
public:
    explicit SubtitleProvider(std::function<void(std::string)> errors={});
    void attach(IBaseFilter* renderer);
    void attachConsumer(ISubRenderConsumer* consumer);
    void detach();
    void clear();
    void setSlot(int slot,QByteArray content,int64_t offsetMs,bool danmaku);
    void beginEmbedded(int slot,QByteArray codecPrivate,bool ass);
    void pushEmbedded(int slot,int64_t start,int64_t stop,QByteArray bytes,bool ass);
    void resetEmbedded(int slot);
    uint64_t requestedFrames() const{return requested_;}
    uint64_t framesWithText() const{return visible_;}
    uint64_t slotFramesWithText(int slot) const{return slot>=0 && slot<2?slotVisible_[slot].load():0;}
    STDMETHOD(QueryInterface)(REFIID,void**) override;
    STDMETHOD_(ULONG,AddRef)() override{return ++references_;}
    STDMETHOD_(ULONG,Release)() override{auto n=--references_;if(!n)delete this;return n;}
    STDMETHOD(RequestFrame)(REFERENCE_TIME,REFERENCE_TIME,LPVOID) override;
    STDMETHOD(Disconnect)() override;
    STDMETHOD(GetBool)(LPCSTR,bool*) override;
    STDMETHOD(GetInt)(LPCSTR,int*) override{return E_NOTIMPL;}
    STDMETHOD(GetSize)(LPCSTR,SIZE*) override{return E_NOTIMPL;}
    STDMETHOD(GetRect)(LPCSTR,RECT*) override{return E_NOTIMPL;}
    STDMETHOD(GetUlonglong)(LPCSTR,ULONGLONG*) override{return E_NOTIMPL;}
    STDMETHOD(GetDouble)(LPCSTR,double*) override{return E_NOTIMPL;}
    STDMETHOD(GetString)(LPCSTR,LPWSTR*,int*) override;
    STDMETHOD(GetBin)(LPCSTR,LPVOID*,int*) override{return E_NOTIMPL;}
    STDMETHOD(SetBool)(LPCSTR,bool) override;
    STDMETHOD(SetInt)(LPCSTR,int) override{return E_NOTIMPL;}
    STDMETHOD(SetSize)(LPCSTR,SIZE) override{return E_NOTIMPL;}
    STDMETHOD(SetRect)(LPCSTR,RECT) override{return E_NOTIMPL;}
    STDMETHOD(SetUlonglong)(LPCSTR,ULONGLONG) override{return E_NOTIMPL;}
    STDMETHOD(SetDouble)(LPCSTR,double) override{return E_NOTIMPL;}
    STDMETHOD(SetString)(LPCSTR,LPWSTR,int) override{return E_NOTIMPL;}
    STDMETHOD(SetBin)(LPCSTR,LPVOID,int) override{return E_NOTIMPL;}
    std::function<void(std::string)> onError;
private:
    ~SubtitleProvider();
    std::atomic_ulong references_{1};
    std::atomic_uint64_t epoch_{1};
    std::atomic_uint64_t requested_{0},visible_{0};
    std::array<std::atomic_uint64_t,2> slotVisible_{};
    std::mutex mutex_;
    Microsoft::WRL::ComPtr<ISubRenderConsumer> consumer_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    std::thread worker_;
    bool stopping_=false;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void enqueue(std::function<void()> job);
};
QByteArray toAss(const QByteArray& input,int slot);
}
