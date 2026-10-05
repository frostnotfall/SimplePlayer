#include "FilterLoader.h"
#include "HlsCompatibility.h"
#include "domain/Models.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <sstream>
namespace bp {
void hrCheck(HRESULT result,const std::string& stage) {
    if(FAILED(result)){std::ostringstream message;message<<stage<<" (HRESULT 0x"<<std::hex<<uint32_t(result)<<")";throw Error("MediaGraph",message.str());}
}
FilterLoader::~FilterLoader(){for(auto it=modules_.rbegin();it!=modules_.rend();++it)FreeLibrary(*it);}
ComPtr<IBaseFilter> FilterLoader::create(const QString& path,REFCLSID clsid) {
    if(!QFileInfo(path).isAbsolute() || !QFileInfo::exists(path))throw Error("FilterMissing","滤镜文件不存在: "+QDir::toNativeSeparators(path).toStdString());
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw Error("FilterMissing","无法读取滤镜文件");
    auto dosBytes=file.read(sizeof(IMAGE_DOS_HEADER));
    if(dosBytes.size()!=sizeof(IMAGE_DOS_HEADER))throw Error("FilterMissing","无效 PE 文件");
    IMAGE_DOS_HEADER dos;memcpy(&dos,dosBytes.data(),sizeof(dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>file.size()-sizeof(IMAGE_NT_HEADERS64))throw Error("FilterMissing","无效 PE 头");
    if(!file.seek(dos.e_lfanew))throw Error("FilterMissing","无法读取 PE 头");
    auto ntBytes=file.read(sizeof(IMAGE_NT_HEADERS64));if(ntBytes.size()!=sizeof(IMAGE_NT_HEADERS64))throw Error("FilterMissing","PE 头被截断");
    IMAGE_NT_HEADERS64 nt;memcpy(&nt,ntBytes.data(),sizeof(nt));
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)throw Error("FilterMissing","只支持 x64 滤镜");
    auto module=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)throw Error("FilterMissing","滤镜或其依赖加载失败: "+QDir::toNativeSeparators(path).toStdString()+" / "+std::to_string(GetLastError()));
    modules_.push_back(module);
    if(QFileInfo(path).fileName().compare("LAVSplitter.ax",Qt::CaseInsensitive)==0 && !installHlsCompatibility(module))
        throw Error("HlsCompatibility",std::string("当前 LAV 无法设置 HLS 兼容选项: ")+hlsCompatibilityStatus());
    using GetClassObject=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,void**);
    auto factoryFn=reinterpret_cast<GetClassObject>(GetProcAddress(module,"DllGetClassObject"));
    if(!factoryFn)throw Error("FilterMissing","滤镜没有导出 DllGetClassObject");
    ComPtr<IClassFactory> factory;hrCheck(factoryFn(clsid,IID_PPV_ARGS(&factory)),"DllGetClassObject");
    ComPtr<IBaseFilter> filter;hrCheck(factory->CreateInstance(nullptr,IID_PPV_ARGS(&filter)),"CreateInstance "+QFileInfo(path).fileName().toStdString());return filter;
}
}
