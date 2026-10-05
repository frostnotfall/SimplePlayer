#include <QtTest>
#include "media/subtitles/SubtitleProvider.h"
#include "domain/Models.h"
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
class Consumer final : public ISubRenderConsumer2 {
    std::atomic_ulong refs_{1};
public:
    ComPtr<ISubRenderProvider> provider;
    ComPtr<ISubRenderFrame> frame;
    std::mutex mutex;
    std::atomic_int delivered{0};
    RECT output{0,0,960,540};
    LPVOID context=nullptr;
    STDMETHOD(QueryInterface)(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown || iid==__uuidof(ISubRenderConsumer) || iid==__uuidof(ISubRenderConsumer2)){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHOD_(ULONG,AddRef)() override{return ++refs_;}
    STDMETHOD_(ULONG,Release)() override{auto n=--refs_;if(!n)delete this;return n;}
    STDMETHOD(GetMerit)(ULONG* merit) override{if(!merit)return E_POINTER;*merit=0x40000;return S_OK;}
    STDMETHOD(Connect)(ISubRenderProvider* p) override{provider=p;return p->SetBool("combineBitmaps",true);}
    STDMETHOD(Disconnect)() override{provider.Reset();return S_OK;}
    STDMETHOD(DeliverFrame)(REFERENCE_TIME,REFERENCE_TIME,LPVOID ctx,ISubRenderFrame* value) override{std::lock_guard lock(mutex);frame=value;context=ctx;++delivered;return S_OK;}
    STDMETHOD(Clear)(REFERENCE_TIME) override{std::lock_guard lock(mutex);frame.Reset();return S_OK;}
    STDMETHOD(GetRect)(LPCSTR field,RECT* rect) override{if(!rect)return E_POINTER;if(strcmp(field,"videoOutputRect"))return E_INVALIDARG;std::lock_guard lock(mutex);*rect=output;return S_OK;}
    STDMETHOD(GetBool)(LPCSTR,bool*) override{return E_NOTIMPL;}
    STDMETHOD(GetInt)(LPCSTR,int*) override{return E_NOTIMPL;}
    STDMETHOD(GetSize)(LPCSTR,SIZE*) override{return E_NOTIMPL;}
    STDMETHOD(GetUlonglong)(LPCSTR,ULONGLONG*) override{return E_NOTIMPL;}
    STDMETHOD(GetDouble)(LPCSTR,double*) override{return E_NOTIMPL;}
    STDMETHOD(GetString)(LPCSTR,LPWSTR*,int*) override{return E_NOTIMPL;}
    STDMETHOD(GetBin)(LPCSTR,LPVOID*,int*) override{return E_NOTIMPL;}
    STDMETHOD(SetBool)(LPCSTR,bool) override{return E_NOTIMPL;}
    STDMETHOD(SetInt)(LPCSTR,int) override{return E_NOTIMPL;}
    STDMETHOD(SetSize)(LPCSTR,SIZE) override{return E_NOTIMPL;}
    STDMETHOD(SetRect)(LPCSTR,RECT) override{return E_NOTIMPL;}
    STDMETHOD(SetUlonglong)(LPCSTR,ULONGLONG) override{return E_NOTIMPL;}
    STDMETHOD(SetDouble)(LPCSTR,double) override{return E_NOTIMPL;}
    STDMETHOD(SetString)(LPCSTR,LPWSTR,int) override{return E_NOTIMPL;}
    STDMETHOD(SetBin)(LPCSTR,LPVOID,int) override{return E_NOTIMPL;}
};
class SubtitleTests : public QObject {
    Q_OBJECT
private slots:
    void synchronousDeliveryAndResize(){
        ComPtr<bp::SubtitleProvider> provider;provider.Attach(new bp::SubtitleProvider);
        ComPtr<Consumer> consumer;consumer.Attach(new Consumer);provider->attachConsumer(consumer.Get());
        QByteArray cue="1\n00:00:00,000 --> 00:01:00,000\nPersistent caption\n\n";
        provider->setSlot(0,cue,0,false);provider->setSlot(1,cue,0,false);
        ULONGLONG firstId=0;ComPtr<ISubRenderFrame> retained;
        for(int i=0;i<12;++i){
            const bool full=i%2;{std::lock_guard lock(consumer->mutex);consumer->output=full?RECT{0,0,3840,2160}:RECT{0,0,960,540};}
            QCOMPARE(provider->RequestFrame(10000000+i*333333,10333333+i*333333,nullptr),S_OK);
            // A consumer with a one-frame deadline must receive the completed
            // bitmap before RequestFrame returns, at both viewport sizes.
            QCOMPARE(consumer->delivered.load(),i+1);
            std::lock_guard lock(consumer->mutex);QVERIFY(consumer->frame);
            RECT rect{};consumer->frame->GetOutputRect(&rect);QCOMPARE(rect.right,full?3840:960);QCOMPARE(rect.bottom,full?2160:540);
            int count=0;consumer->frame->GetBitmapCount(&count);QCOMPARE(count,1);
            if(i==0){retained=consumer->frame;retained->GetBitmap(0,&firstId,nullptr,nullptr,nullptr,nullptr);}
        }
        ULONGLONG id=0;retained->GetBitmap(0,&id,nullptr,nullptr,nullptr,nullptr);QCOMPARE(id,firstId);
        provider->detach();
    }
    void unchangedFramesReuseBitmap(){
        ComPtr<bp::SubtitleProvider> provider;provider.Attach(new bp::SubtitleProvider);
        ComPtr<Consumer> consumer;consumer.Attach(new Consumer);provider->attachConsumer(consumer.Get());
        provider->setSlot(0,"1\n00:00:00,000 --> 00:01:00,000\nStable caption\n\n",0,false);
        ULONGLONG first=0,second=0;
        QCOMPARE(provider->RequestFrame(10000000,10333333,nullptr),S_OK);consumer->frame->GetBitmap(0,&first,nullptr,nullptr,nullptr,nullptr);
        QCOMPARE(provider->RequestFrame(10333333,10666666,nullptr),S_OK);consumer->frame->GetBitmap(0,&second,nullptr,nullptr,nullptr,nullptr);QCOMPARE(first,second);
        provider->setSlot(0,{},0,false);QCOMPARE(provider->RequestFrame(10666666,10999999,nullptr),S_OK);
        int count=-1;consumer->frame->GetBitmapCount(&count);QCOMPARE(count,0);
        provider->setSlot(0,"1\n00:00:00,000 --> 00:01:00,000\nStable caption\n\n",0,false);
        QCOMPARE(provider->RequestFrame(10999999,11333333,nullptr),S_OK);consumer->frame->GetBitmapCount(&count);QCOMPARE(count,1);provider->detach();
    }
    void dualSlotsAndOptionalBitmapOutputs(){
        ComPtr<bp::SubtitleProvider> provider;provider.Attach(new bp::SubtitleProvider);
        ComPtr<Consumer> consumer;consumer.Attach(new Consumer);provider->attachConsumer(consumer.Get());
        QByteArray cue="1\n00:00:01,000 --> 00:00:05,000\n双字幕测试 / Caption\n\n";
        provider->setSlot(0,cue,1000,false);provider->setSlot(1,cue,1000,false);
        QCOMPARE(provider->RequestFrame(15000000,15333333,reinterpret_cast<void*>(1)),S_OK);
        QTRY_COMPARE_WITH_TIMEOUT(consumer->delivered.load(),1,5000);
        {std::lock_guard lock(consumer->mutex);QVERIFY(consumer->frame);int count=-1;QCOMPARE(consumer->frame->GetBitmapCount(&count),S_OK);QCOMPARE(count,0);}
        QCOMPARE(provider->RequestFrame(30000000,30333333,reinterpret_cast<void*>(2)),S_OK);
        QTRY_COMPARE_WITH_TIMEOUT(consumer->delivered.load(),2,5000);
        ComPtr<ISubRenderFrame> retained;
        {std::lock_guard lock(consumer->mutex);retained=consumer->frame;QCOMPARE(consumer->context,reinterpret_cast<void*>(2));}
        int count=0;QCOMPARE(retained->GetBitmapCount(&count),S_OK);QCOMPARE(count,1);
        ULONGLONG id=0;QCOMPARE(retained->GetBitmap(0,&id,nullptr,nullptr,nullptr,nullptr),S_OK);QVERIFY(id>0);
        POINT point{};SIZE size{};LPCVOID bytes=nullptr;int pitch=0;QCOMPARE(retained->GetBitmap(0,nullptr,&point,&size,&bytes,&pitch),S_OK);
        QVERIFY(bytes);QVERIFY(pitch>=size.cx*4);QVERIFY(point.y<100);QVERIFY(point.y+size.cy>440);
        provider->clear();provider->detach();QCOMPARE(retained->GetBitmap(0,&id,nullptr,nullptr,nullptr,nullptr),S_OK);
    }
    void decodeFormats(){
        QVERIFY(bp::toAss("WEBVTT\n\n00:01.000 --> 00:02.000\nCaption\n\n",0).contains("Dialogue:"));
        QVERIFY_EXCEPTION_THROWN(bp::toAss("not a subtitle",0),bp::Error);
    }
};
QTEST_GUILESS_MAIN(SubtitleTests)
#include "SubtitleTests.moc"
