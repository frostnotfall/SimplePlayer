#pragma once
#include <Windows.h>
#include <Unknwn.h>
#include <cstdint>
// Project ABI, not an upstream LAV interface. Must be implemented by the matching LAV/FFmpeg patch.
// All callbacks are free-threaded, process-local, and valid until Close completes.
namespace bp {
struct LavRequestHeaders { uint32_t size,version; const char* headersUtf8; void* allocation; };
using EvaluateRequest=HRESULT(__stdcall*)(void*,const char* urlUtf8,LavRequestHeaders*);
using ReleaseHeaders=void(__stdcall*)(void*,LavRequestHeaders*);
using ResponseCookies=void(__stdcall*)(void*,const char* urlUtf8,const char* setCookieUtf8);
struct LavNetworkOptions {
    uint32_t size=sizeof(LavNetworkOptions), version=1;
    int64_t readTimeoutUs=15000000;
    uint32_t reconnectLimit=2;
    uint32_t reserved=0;
    void* context=nullptr;
    EvaluateRequest evaluate=nullptr;
    ReleaseHeaders release=nullptr;
    ResponseCookies cookies=nullptr;
};
interface __declspec(uuid("82B37A25-2D3F-4E4D-B2A0-BEC87ED7637E")) IBpLavNetworkControl : IUnknown {
    STDMETHOD(SetNetworkOptions)(const LavNetworkOptions*)=0;
    // Explicitly safe to invoke from a cancelling thread while LoadURL blocks. No graph/COM work here.
    STDMETHOD(CancelPendingOpen)()=0;
    STDMETHOD(GetLastNetworkError)(int32_t* httpStatus,int32_t* transportCode)=0;
};
}
