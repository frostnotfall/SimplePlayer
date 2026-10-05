#include "ScriptRuntime.h"
#include "JsonBinding.h"
#include <scriptstdstring/scriptstdstring.h>
#include <scriptarray/scriptarray.h>
#include <scriptdictionary/scriptdictionary.h>
#include <scriptany/scriptany.h>
#include <scriptmath/scriptmath.h>
#include <datetime/datetime.h>
#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace bp {
static void messageCallback(const asSMessageInfo* info,ScriptRuntime* runtime) {
    runtime->diagnostics.push_back(QString("%1:%2:%3 [%4] %5").arg(QString::fromUtf8(info->section)).arg(info->row).arg(info->col).arg(info->type).arg(QString::fromUtf8(info->message)));
    runtime->debug(runtime->diagnostics.back().toUtf8().toStdString());
}
struct ContextOwner {asIScriptContext* p;~ContextOwner(){p->Release();}asIScriptContext* operator->(){return p;}};
ScriptRuntime::ScriptRuntime(HostServices services):services_(std::move(services)) {
    engine_=asCreateScriptEngine();if(!engine_)throw Error("ScriptRuntime","无法创建 AngelScript 引擎");
    auto release=[](asIScriptEngine* engine){engine->ShutDownAndRelease();};
    std::unique_ptr<asIScriptEngine,decltype(release)> constructionGuard(engine_,release);
    checked(engine_->SetMessageCallback(asFUNCTION(messageCallback),this,asCALL_CDECL),"message callback");
    checked(engine_->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES,1),"reference profile");
    checked(engine_->SetEngineProperty(asEP_USE_CHARACTER_LITERALS,0),"byte-string literals");
    checked(engine_->SetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD,0),"deferred globals");
    RegisterStdString(engine_);RegisterScriptArray(engine_,true);RegisterStdStringUtils(engine_);
    RegisterScriptDictionary(engine_);RegisterScriptAny(engine_);RegisterScriptDateTime(engine_);RegisterScriptMath(engine_);
    try {registerJson(engine_);registerHostApi(engine_);} catch(const Error& error){throw Error(error.code,std::string(error.what())+"\n"+diagnostics.join('\n').toStdString());}
    constructionGuard.release();
}
ScriptRuntime::~ScriptRuntime(){close();if(engine_)engine_->ShutDownAndRelease();asThreadCleanup();}
void ScriptRuntime::compile(asIScriptModule*& module,const char* name,const QString& path) {
    debug("编译 "+QDir::toNativeSeparators(path).toUtf8().toStdString());
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw Error("ScriptMissing","无法读取脚本: "+path.toStdString());
    if(file.size()>2*1024*1024)throw Error("ScriptCompile","脚本超过长度上限");
    auto source=file.readAll();module=engine_->GetModule(name,asGM_ALWAYS_CREATE);
    checked(module->AddScriptSection(path.toUtf8().constData(),source.constData(),size_t(source.size())),"AddScriptSection");
    if(module->Build()<0)throw Error("ScriptCompile",diagnostics.join('\n').toStdString());
}
void ScriptRuntime::load(QString mediaPath,QString statisticsPath) {
    close();diagnostics.clear();mediaPath_=std::move(mediaPath);statisticsPath_=std::move(statisticsPath);
    compile(media_,"MediaPlayParse",mediaPath_);
    if(!statisticsPath_.isEmpty())compile(statistics_,"PlaybackStatistics",statisticsPath_);
}
int ScriptRuntime::call(asIScriptContext* ctx,asIScriptModule* module) {
    if(!session_)throw Error("ScriptRuntime","没有活动脚本会话");
    auto path=module==statistics_?statisticsPath_:mediaPath_;
    Execution execution{engine_,session_,&services_,QFileInfo(path).absolutePath().toUtf8().toStdString()+"/"};
    execution.playingPath=session_->playingPath;
    if(std::string(ctx->GetFunction()->GetName())=="PlaybackClose")execution.deadline=execution.hardDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    const auto name=std::string(ctx->GetFunction()->GetName());
    if(name!="PlaybackTime")debug("调用 "+name);
    auto status=executeContext(ctx,execution);
    if(status==asEXECUTION_EXCEPTION)debug("异常 "+name+": "+ctx->GetExceptionString()+"，行 "+std::to_string(ctx->GetExceptionLineNumber()));
    else if(name!="PlaybackTime")debug("结束 "+name+"，状态 "+std::to_string(status));
    if(status==asEXECUTION_ABORTED){session_->cancel->check();throw Error("ScriptRuntime","脚本执行预算超时");}
    if(status==asEXECUTION_EXCEPTION)throw Error("ScriptRuntime",std::string(ctx->GetExceptionString())+" at "+std::to_string(ctx->GetExceptionLineNumber()));
    if(status!=asEXECUTION_FINISHED)throw Error("ScriptRuntime","脚本未正常结束: "+std::to_string(status));return status;
}
void ScriptRuntime::initialize(std::shared_ptr<HostSession> session) {
    if(session_)close();session_=std::move(session);
    for(auto* module:{media_,statistics_})if(module) {
        ContextOwner ctx{engine_->CreateContext()};
        auto path=module==statistics_?statisticsPath_:mediaPath_;
        Execution execution{engine_,session_,&services_,QFileInfo(path).absolutePath().toUtf8().toStdString()+"/"};
        ctx->SetUserData(&execution);ctx->SetLineCallback(asFUNCTION(lineCallback),nullptr,asCALL_CDECL);
        if(module->ResetGlobalVars(ctx.p)<0)throw Error("ScriptRuntime",ctx->GetExceptionString()?ctx->GetExceptionString():"脚本全局初始化失败");
    }
    if(auto* fn=media_->GetFunctionByDecl("void OnInitialize()")) {
        ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),"OnInitialize");call(ctx.p,media_);
    }
    if(statistics_)if(auto* fn=statistics_->GetFunctionByDecl("void OnInitialize()")) {
        ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),"statistics OnInitialize");call(ctx.p,statistics_);
    }
}
std::string ScriptRuntime::stringFunction(const char* name) {
    auto decl="string "+std::string(name)+"()";auto* fn=media_->GetFunctionByDecl(decl.c_str());if(!fn)return {};
    ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),decl.c_str());call(ctx.p,media_);
    return *static_cast<std::string*>(ctx->GetReturnObject());
}
bool ScriptRuntime::checkFunction(const char* name,const std::string& url) {
    auto decl="bool "+std::string(name)+"(const string &in)";auto* fn=media_->GetFunctionByDecl(decl.c_str());if(!fn)return false;
    ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),decl.c_str());ctx->SetArgObject(0,const_cast<std::string*>(&url));call(ctx.p,media_);return ctx->GetReturnByte()!=0;
}
PlaybackPlan ScriptRuntime::resolve(SessionKey key,std::string url,bool includePlaylist) {
    session_->cancel->check();Json::Value playlist(Json::arrayValue);
    bool single=checkFunction("PlayitemCheck",url);
    if(includePlaylist && checkFunction("PlaylistCheck",url)) {
        auto* fn=media_->GetFunctionByDecl("array<dictionary> PlaylistParse(const string &in)");
        if(!fn)throw Error("ScriptRuntime","缺少 PlaylistParse 入口");
        ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),"PlaylistParse");ctx->SetArgObject(0,&url);call(ctx.p,media_);
        auto* arr=static_cast<CScriptArray*>(ctx->GetReturnObject());if(arr->GetSize()>5000)throw Error("ResolveContract","播放列表超限");
        for(asUINT i=0;i<arr->GetSize();++i)playlist.append(dictionaryToJson(engine_,static_cast<CScriptDictionary*>(arr->At(i))));
        if(!single) {
            std::string target;
            for(const auto& item:playlist)if(playlistItemCurrent(item)){target=item["url"].asString();break;}
            if(target.empty() && !playlist.empty())target=playlist[0]["url"].asString();
            if(target.empty())throw Error("ResolveDenied","脚本未返回可播放的列表项");url=target;single=checkFunction("PlayitemCheck",url);
        }
    }
    if(!single)throw Error("ResolveDenied","脚本不支持此链接");
    auto* fn=media_->GetFunctionByDecl("string PlayitemParse(const string &in, dictionary &inout, array<dictionary> &inout)");
    if(!fn)throw Error("ScriptRuntime","缺少 PlayitemParse 入口");
    auto* meta=CScriptDictionary::Create(engine_);auto* quality=CScriptArray::Create(engine_->GetTypeInfoByDecl("array<dictionary>"));
    try {
        ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),"PlayitemParse");ctx->SetArgObject(0,&url);ctx->SetArgObject(1,meta);ctx->SetArgObject(2,quality);call(ctx.p,media_);
        auto entry=*static_cast<std::string*>(ctx->GetReturnObject());auto metadata=dictionaryToJson(engine_,meta);Json::Value choices(Json::arrayValue);
        for(asUINT i=0;i<quality->GetSize();++i)choices.append(dictionaryToJson(engine_,static_cast<CScriptDictionary*>(quality->At(i))));
        auto plan=adaptResult(key,url,entry,metadata,choices,playlist);meta->Release();quality->Release();return plan;
    }catch(...){meta->Release();quality->Release();throw;}
}
void ScriptRuntime::statistics(const char* event,const std::string& path,int position,int duration) {
    if(!statistics_)return;session_->playingPath=path;
    auto decl="void "+std::string(event)+"(const string &in";
    if(std::string(event)=="PlaybackStart")decl+=", int";
    if(std::string(event)=="PlaybackTime")decl+=", int, int";decl+=")";
    auto* fn=statistics_->GetFunctionByDecl(decl.c_str());if(!fn)return;
    ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),"statistics");ctx->SetArgObject(0,const_cast<std::string*>(&path));
    if(fn->GetParamCount()>1)ctx->SetArgDWord(1,position);if(fn->GetParamCount()>2)ctx->SetArgDWord(2,duration);call(ctx.p,statistics_);
}
void ScriptRuntime::close() {
    if(!session_)return;session_->cancel->cancel();session_->join();session_.reset();
}
void ScriptRuntime::finishStatistics(const std::string& path) {
    if(!session_)return;session_->cancel->cancel();session_->join();
    auto closing=std::make_shared<HostSession>();closing->key=session_->key;closing->epoch=session_->epoch;
    {std::lock_guard lock(session_->mutex);closing->strings=session_->strings;closing->integers=session_->integers;closing->tags=session_->tags;}
    session_=std::move(closing);
    try{statistics("PlaybackClose",path);}catch(...){}
    close();
}
void ScriptRuntime::compileFixture(const std::string& source) {
    close();media_=engine_->GetModule("fixture",asGM_ALWAYS_CREATE);checked(media_->AddScriptSection("fixture",source.data(),source.size()),"fixture");
    if(media_->Build()<0)throw Error("ScriptCompile",diagnostics.join('\n').toStdString());
    session_=std::make_shared<HostSession>();
}
void ScriptRuntime::runFixture(const char* declaration) {
    auto* fn=media_->GetFunctionByDecl(declaration);if(!fn)throw Error("ScriptRuntime","测试入口不存在");
    ContextOwner ctx{engine_->CreateContext()};checked(ctx->Prepare(fn),"fixture");call(ctx.p,media_);
}
}
