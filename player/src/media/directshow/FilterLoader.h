#pragma once
#include <Windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include <QString>
#include <vector>
#include <memory>
namespace bp {
using Microsoft::WRL::ComPtr;
void hrCheck(HRESULT result,const std::string& stage);
struct FilterPaths { QString lav,renderer,audio,bfrc; bool enableBfrc=false; };
class FilterLoader {
public:
    ~FilterLoader();
    ComPtr<IBaseFilter> create(const QString& path,REFCLSID clsid);
private:
    std::vector<HMODULE> modules_;
};
}
