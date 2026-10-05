#include "scripts/ScriptRuntime.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDir>
#include <QRegularExpression>
#include "media/directshow/FilterLoader.h"
#include <cstdarg>
#include <cstdio>
#include <iostream>
interface __declspec(uuid("C8FF17F9-5365-4F32-8AD5-6C550342C2F7")) ProbeUrlSource : IUnknown {
    STDMETHOD(LoadURL)(LPCOLESTR,LPCOLESTR,LPCOLESTR)=0;
};
static void lavError(void*,int level,const char* format,va_list arguments) {
    if(level>16)return;char buffer[4096];vsnprintf(buffer,sizeof(buffer),format,arguments);
    QString message=QString::fromUtf8(buffer);message.replace(QRegularExpression("https?://[^\\s\\\"']+"),"[media URL]");
    std::cerr<<"LAV transport: "<<message.left(512).toStdString();
}
struct Apartment {HRESULT status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(status))CoUninitialize();}};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    Apartment apartment;
    std::cout<<std::unitbuf;
    try {
        bp::HttpService http;
        bp::HostServices services;
        services.http=&http;
        services.log=[](std::string message){std::cerr<<message<<'\n';};
        services.message=[](std::string message,std::string,int,int,bp::Cancel){if(message.starts_with("error code:"))std::cerr<<message.substr(0,message.find('\n'))<<'\n';return 3;};
        bp::ScriptRuntime runtime(services);
        if(argc<2){std::cerr<<"Usage: script_probe media.as [statistics.as]\n";return 2;}
        runtime.load(QString::fromLocal8Bit(argv[1]),argc>2?QString::fromLocal8Bit(argv[2]):QString{});
        if(argc>3) {
            auto session=std::make_shared<bp::HostSession>();session->key={1,1};runtime.initialize(session);
            auto plan=runtime.resolve({1,1},argv[3]);
            std::cout<<"Resolved candidates="<<plan.candidates.size()<<" subtitles="<<plan.subtitles.size()<<" chat="<<plan.chat.has_value()<<"\n";
            if(argc>4 && std::string(argv[4])=="--transport") {
                const QUrl url(QString::fromStdString(plan.defaultUrl));auto policy=http.policyFor(plan.defaultUrl);
                std::cout<<"Default transport="<<url.scheme().toStdString()<<" host="<<url.host().toStdString()
                    <<" UA="<<!policy.userAgent.empty()<<" Referer="<<!policy.referer.empty()<<"\n";
                bp::HttpRequest request;request.url=plan.defaultUrl;request.headers="Range: bytes=0-1023";
                request.noCookie=true;request.limit=64*1024;request.timeoutMs=10000;
                const auto response=http.request(request);
                std::cout<<"Media Range HTTP status="<<response.status<<" bytes="<<response.body.size()
                    <<" success="<<response.error.empty()<<"\n";
                if(argc>5) {
                    bp::FilterLoader filters;const QString directory=QString::fromLocal8Bit(argv[5]);
                    const CLSID sourceId={0xb98d13e7,0x55db,0x4385,{0xa3,0x3d,0x09,0xfd,0x1b,0xa2,0x63,0x38}};
                    auto source=filters.create(QDir(directory).filePath("LAVSplitter.ax"),sourceId);
                    bp::ComPtr<IGraphBuilder> graph;bp::hrCheck(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&graph)),"probe graph");
                    bp::hrCheck(graph->AddFilter(source.Get(),L"LAV Probe Source"),"probe AddFilter");
                    const auto library=GetModuleHandleW(L"avutil-lav-61.dll");
                    using SetLog=void(*)(void(*)(void*,int,const char*,va_list));
                    const auto setLog=library?reinterpret_cast<SetLog>(GetProcAddress(library,"av_log_set_callback")):nullptr;
                    if(setLog)setLog(lavError);
                    bp::ComPtr<ProbeUrlSource> loader;bp::hrCheck(source.As(&loader),"probe URL interface");
                    auto uri=QString::fromStdString(plan.defaultUrl).toStdWString();
                    auto ua=QString::fromStdString(policy.userAgent).toStdWString(),referer=QString::fromStdString(policy.referer).toStdWString();
                    const auto hr=loader->LoadURL(uri.c_str(),ua.c_str(),referer.c_str());
                    if(setLog)setLog(nullptr);
                    std::cout<<"LAV LoadURL HRESULT="<<std::hex<<uint32_t(hr)<<std::dec<<"\n";
                }
            }
        }
        std::cout<<"Both script modules compiled using AngelScript "<<ANGELSCRIPT_VERSION_STRING<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
