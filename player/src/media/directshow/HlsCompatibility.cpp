#include "HlsCompatibility.h"
#include <atomic>
#include <cstring>
#include <mutex>

namespace bp {
struct AVFormatContext;
struct AVInputFormat;
struct AVDictionary;
using OpenInput=int(__cdecl*)(AVFormatContext**,const char*,const AVInputFormat*,AVDictionary**);
using DictionarySet=int(__cdecl*)(AVDictionary**,const char*,const char*,int);
using DictionaryFree=void(__cdecl*)(AVDictionary**);
static OpenInput originalOpen=nullptr;
static DictionarySet dictionarySet=nullptr;
static DictionaryFree dictionaryFree=nullptr;
static std::mutex installMutex;
static std::atomic<unsigned long long> httpOpens{0};
static const char* status="Not installed";
const char* hlsCompatibilityStatus(){return status;}
unsigned long long guardedHttpOpens(){return httpOpens.load();}

static int __cdecl safeOpen(AVFormatContext** context,const char* url,const AVInputFormat* format,AVDictionary** options) {
    AVDictionary* local=nullptr;
    auto** opts=options?options:&local;
    if(url && (!_strnicmp(url,"http://",7) || !_strnicmp(url,"https://",8))) {
        // WER RVA 0x7b098 maps to hls.c:1771 av_assert0(v->input),
        // reachable only with http_persistent. Ordinary segment opens provide
        // recoverable errors instead of aborting the entire application.
        if(dictionarySet(opts,"http_persistent","0",0)<0 || dictionarySet(opts,"http_multiple","0",0)<0){
            if(!options)dictionaryFree(&local);
            return -12; // AVERROR(ENOMEM), never throw through a C ABI.
        }
        ++httpOpens;
    }
    int result=originalOpen(context,url,format,opts);
    if(!options)dictionaryFree(&local);
    return result;
}
bool installHlsCompatibility(HMODULE splitter) {
    std::lock_guard lock(installMutex);
    status="No avformat import";
    auto* base=reinterpret_cast<unsigned char*>(splitter);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!rva)return false;
    for(auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+rva);descriptor->Name;++descriptor) {
        auto* dll=reinterpret_cast<const char*>(base+descriptor->Name);
        if(_strnicmp(dll,"avformat-lav-",13))continue;
        status="No avformat_open_input import";
        if(!descriptor->OriginalFirstThunk)return false;
        auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+descriptor->OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+descriptor->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            auto* import=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(std::strcmp(reinterpret_cast<const char*>(import->Name),"avformat_open_input"))continue;
            auto current=reinterpret_cast<OpenInput>(slots->u1.Function);
            if(current==safeOpen)return true;
            if(originalOpen && originalOpen!=current)return false;
            HMODULE avformat=nullptr;
            // Resolve the loaded module by its actual imported function.
            // LAV's dependent module identity need not match the PE DLL name.
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(current),&avformat);
            status="No matching avutil module";
            HMODULE utility=nullptr;
            // LAV names its libavutil import with the matching major version.
            auto* fmtBase=reinterpret_cast<unsigned char*>(avformat);
            if(!fmtBase)return false;
            auto* fmtDos=reinterpret_cast<IMAGE_DOS_HEADER*>(fmtBase);
            auto* fmtNt=reinterpret_cast<IMAGE_NT_HEADERS64*>(fmtBase+fmtDos->e_lfanew);
            auto fmtImports=fmtNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            for(auto* d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(fmtBase+fmtImports);fmtImports && d->Name;++d) {
                auto* dependency=reinterpret_cast<const char*>(fmtBase+d->Name);
                if(!_strnicmp(dependency,"avutil-lav-",11)){
                    auto* first=reinterpret_cast<IMAGE_THUNK_DATA64*>(fmtBase+d->FirstThunk);
                    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(first->u1.Function),&utility);break;
                }
            }
            if(!utility)return false;
            auto set=reinterpret_cast<DictionarySet>(GetProcAddress(utility,"av_dict_set"));
            auto free=reinterpret_cast<DictionaryFree>(GetProcAddress(utility,"av_dict_free"));
            status="No dictionary exports";
            if(!set || !free)return false;
            DWORD old=0;
            status="IAT protection failed";
            if(!VirtualProtect(&slots->u1.Function,sizeof(slots->u1.Function),PAGE_READWRITE,&old))return false;
            originalOpen=current;dictionarySet=set;dictionaryFree=free;
            // Keep these function addresses valid across graph destruction.
            HMODULE retained;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(current),&retained);
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(set),&retained);
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function),reinterpret_cast<void*>(safeOpen));
            DWORD ignored;VirtualProtect(&slots->u1.Function,sizeof(slots->u1.Function),old,&ignored);
            status="Installed";
            return true;
        }
    }
    return false;
}
}
