#include "HostApi.h"
#include "JsonBinding.h"
#include <scriptarray/scriptarray.h>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <zlib.h>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace bp {
void HostSession::join() {
    for(;;) {
        std::vector<std::shared_ptr<ScriptTask>> snapshot;
        {std::lock_guard lock(mutex);for(auto& [id,task]:tasks)snapshot.push_back(task);}
        for(auto& task:snapshot)if(task->worker.joinable())task->worker.join();
        std::lock_guard lock(mutex);
        if(snapshot.size()==tasks.size()){tasks.clear();files.clear();return;}
    }
}
void lineCallback(asIScriptContext* ctx,void*) {
    auto* execution=static_cast<Execution*>(ctx->GetUserData());
    if(execution && (execution->session->cancel->cancelled || std::chrono::steady_clock::now()>execution->deadline))ctx->Abort();
}
int executeContext(asIScriptContext* ctx,Execution& execution) {
    ctx->SetUserData(&execution);checked(ctx->SetLineCallback(asFUNCTION(lineCallback),nullptr,asCALL_CDECL),"line callback");
    return ctx->Execute();
}
static Execution& current(){auto* ctx=asGetActiveContext();if(!ctx || !ctx->GetUserData())throw Error("ScriptRuntime","缺少宿主执行上下文");return *static_cast<Execution*>(ctx->GetUserData());}
static std::string stringArg(asIScriptGeneric* g,int i){return *static_cast<std::string*>(g->GetArgAddress(i));}
static void result(asIScriptGeneric* g,const std::string& s){g->SetReturnObject(const_cast<std::string*>(&s));}
static std::string decompress(const std::string& input) {
    if(input.size()<2)return input;
    const auto* bytes=reinterpret_cast<const unsigned char*>(input.data());
    bool gzip=bytes[0]==0x1f && bytes[1]==0x8b;
    bool zlib=(bytes[0]&15)==8 && (unsigned(bytes[0])*256+bytes[1])%31==0;
    if(!gzip && !zlib)return input;
    z_stream stream{};stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));stream.avail_in=uInt(input.size());
    if(inflateInit2(&stream,47)!=Z_OK)throw Error("Compression","无法初始化解压器");
    struct Cleanup { z_stream* stream; ~Cleanup(){inflateEnd(stream);} } cleanup{&stream};
    std::string out;char buffer[16384];int status=Z_OK;
    while(status==Z_OK) {
        current().session->cancel->check();
        stream.next_out=reinterpret_cast<Bytef*>(buffer);stream.avail_out=sizeof(buffer);status=inflate(&stream,Z_NO_FLUSH);
        out.append(buffer,sizeof(buffer)-stream.avail_out);
        if(out.size()>64*1024*1024)throw Error("Compression","解压长度超限");
    }
    if(status!=Z_STREAM_END)throw Error("Compression","压缩数据损坏");return out;
}
static int startTask(asIScriptFunction* fn,CScriptAny* argument,Execution execution) {
    if(!fn)throw Error("ScriptRuntime","后台回调为空");
    auto session=execution.session;auto task=std::make_shared<ScriptTask>();
    // Construct and publish under the same mutex. close()/join() must never
    // see a default thread while a nested task is still assigning its worker.
    std::lock_guard lock(session->mutex);session->cancel->check();
    if(session->tasks.size()>=256)throw Error("ScriptRuntime","后台任务数量超限");
    task->id=session->nextTask++;
    session->tasks.emplace(task->id,task);
    fn->AddRef();if(argument)argument->AddRef();
    try {task->worker=std::thread([task,fn,argument,execution]() mutable {
        asIScriptContext* ctx=nullptr;
        try {
            execution.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            ctx=execution.engine->CreateContext();checked(ctx->Prepare(fn),"Prepare thread");checked(ctx->SetArgObject(0,argument),"thread argument");
            auto status=executeContext(ctx,execution);
            if(status==asEXECUTION_EXCEPTION)task->error=ctx->GetExceptionString();
            else if(status!=asEXECUTION_FINISHED && status!=asEXECUTION_ABORTED)task->error="后台脚本执行失败";
        }catch(const std::exception& e){task->error=e.what();}
        if(ctx)ctx->Release();if(argument)argument->Release();fn->Release();asThreadCleanup();task->done=true;
    });}catch(...){session->tasks.erase(task->id);if(argument)argument->Release();fn->Release();throw;}
    return task->id;
}
static void hostCall(asIScriptGeneric* g) {
    try {
        auto& execution=current();auto session=execution.session;session->cancel->check();
        auto name=std::string(g->GetFunction()->GetName());
        if(name=="HostUrlGetString") {
            if(!execution.services->http)throw Error("NetworkConnect","当前模式不允许网络请求");
            HttpRequest request;request.url=stringArg(g,0);request.headers=stringArg(g,2);request.body=stringArg(g,3);request.noCookie=g->GetArgByte(4)!=0;
            auto ua=stringArg(g,1);if(!ua.empty())request.headers+="\r\nUser-Agent: "+ua;
            request.method=request.body.empty()?"GET":"POST";request.cancel=session->cancel;request.epoch=session->epoch;
            request.timeoutMs=std::clamp(int(std::chrono::duration_cast<std::chrono::milliseconds>(execution.deadline-std::chrono::steady_clock::now()).count()),1,20000);
            auto response=execution.services->http->request(std::move(request));
            if(response.error=="Cancelled")session->cancel->check();
            if(!response.error.empty()) {
                if(execution.services->log)execution.services->log("HTTP: "+response.error);
                result(g,"");
            } else result(g,response.body);
        } else if(name=="HostSetUrlUserAgentHTTP" || name=="HostSetUrlRefererHTTP") {
            if(execution.services->http) {
                if(name=="HostSetUrlUserAgentHTTP")execution.services->http->setUserAgent(stringArg(g,0),stringArg(g,1));
                else execution.services->http->setReferer(stringArg(g,0),stringArg(g,1));
            }
        } else if(name=="HostUrlEncode")result(g,QUrl::toPercentEncoding(QString::fromUtf8(stringArg(g,0))).toStdString());
        else if(name=="HostUrlDecode")result(g,QByteArray::fromPercentEncoding(QByteArray::fromStdString(stringArg(g,0))).toStdString());
        else if(name=="HostHashMD5")result(g,QCryptographicHash::hash(QByteArray::fromStdString(stringArg(g,0)),QCryptographicHash::Md5).toHex().toStdString());
        else if(name=="HostDecompress")result(g,decompress(stringArg(g,0)));
        else if(name=="HostGetScriptFolder")result(g,execution.scriptFolder);
        else if(name=="HostGetPlayingFileName")result(g,execution.playingPath);
        else if(name=="HostGetTickCount") {
            static auto start=std::chrono::steady_clock::now();g->SetReturnDWord(asDWORD(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()));
        } else if(name=="HostSleep") {
            auto remaining=int(std::chrono::duration_cast<std::chrono::milliseconds>(execution.deadline-std::chrono::steady_clock::now()).count());
            session->cancel->sleep(std::min(std::max(0,remaining),std::max(0,int(g->GetArgDWord(0)))));
            if(std::chrono::steady_clock::now()>execution.deadline)throw Error("ScriptRuntime","脚本等待超时");
        }
        else if(name=="HostIncTimeOut")execution.deadline=std::min(execution.hardDeadline,execution.deadline+std::chrono::milliseconds(std::clamp(int(g->GetArgDWord(0)),0,60000)));
        else if(name=="HostOpenConsole") { /* Console output is redirected to the diagnostics sink. */ }
        else if(name=="HostPrintUTF8") {if(execution.services->log)execution.services->log(stringArg(g,0));}
        else if(name=="HostFileExist")g->SetReturnByte(QFile::exists(QString::fromUtf8(stringArg(g,0))));
        else if(name=="HostFileOpen") {
            auto file=std::make_unique<QFile>(QString::fromUtf8(stringArg(g,0)));
            if(!file->open(QIODevice::ReadOnly)){g->SetReturnQWord(0);return;}
            std::lock_guard lock(session->mutex);auto id=session->nextFile++;session->files[id]=std::move(file);g->SetReturnQWord(id);
        } else if(name=="HostFileRead" || name=="HostFileLength" || name=="HostFileClose") {
            std::lock_guard lock(session->mutex);auto id=g->GetArgQWord(0);auto it=session->files.find(id);
            if(it==session->files.end())throw Error("ScriptRuntime","无效或已关闭的文件句柄");
            if(name=="HostFileClose")session->files.erase(it);
            else if(name=="HostFileLength")g->SetReturnQWord(it->second->size());
            else {auto length=int64_t(g->GetArgQWord(1));if(length<0 || length>64*1024*1024)throw Error("ScriptRuntime","文件读取超限");result(g,it->second->read(length).toStdString());}
        } else if(name=="HostSaveString" || name=="HostLoadString" || name=="HostSaveInteger" || name=="HostLoadInteger") {
            auto key=stringArg(g,0);std::lock_guard lock(session->mutex);
            if(name=="HostSaveString"){session->strings[key]=stringArg(g,1);g->SetReturnByte(true);}
            else if(name=="HostLoadString")result(g,session->strings.contains(key)?session->strings[key]:stringArg(g,1));
            else if(name=="HostSaveInteger"){session->integers[key]=int(g->GetArgDWord(1));g->SetReturnByte(true);}
            else g->SetReturnDWord(session->integers.contains(key)?session->integers[key]:g->GetArgDWord(1));
        } else if(name=="HostExistITag" || name=="HostSetITag") {
            auto tag=*static_cast<int*>(g->GetArgAddress(0));std::lock_guard lock(session->mutex);
            g->SetReturnByte(name=="HostExistITag"?session->tags.contains(tag):session->tags.insert(tag).second);
        } else if(name=="HostGetITag") {
            // Generic opaque identifiers; no site-specific quality ranking.
            std::lock_guard lock(session->mutex);while(session->tags.contains(session->nextTag))++session->nextTag;g->SetReturnDWord(session->nextTag++);
        } else if(name=="HostFormatBitrate") {
            auto bits=int64_t(g->GetArgQWord(0));std::ostringstream s;s.setf(std::ios::fixed);s.precision(1);s<<double(bits)/1000.0<<" kbps";result(g,s.str());
        } else if(name=="HostRegExpRemove" || name=="HostRegExpParse") {
            auto str=QString::fromUtf8(stringArg(g,0));QRegularExpression regex(QString::fromUtf8(stringArg(g,1)));
            if(!regex.isValid())throw Error("ScriptRuntime",regex.errorString().toStdString());
            if(name=="HostRegExpRemove")result(g,str.remove(regex).toUtf8().toStdString());
            else if(g->GetArgCount()==2){auto match=regex.match(str);result(g,(match.hasMatch()?match.captured(match.lastCapturedIndex()>0?1:0):QString{}).toUtf8().toStdString());}
            else {
                auto* arr=static_cast<CScriptArray*>(g->GetArgAddress(2));arr->Resize(0);auto matches=regex.globalMatch(str);
                while(matches.hasNext()) {
                    auto match=matches.next();if(arr->GetSize()>=10000)throw Error("ScriptRuntime","正则结果超限");
                    auto* dic=CScriptDictionary::Create(g->GetEngine());
                    for(int i=0;i<=match.lastCapturedIndex();++i){auto text=match.captured(i).toUtf8().toStdString();dic->Set(std::to_string(i),&text,g->GetEngine()->GetTypeIdByDecl("string"));}
                    arr->InsertLast(dic);dic->Release();
                }g->SetReturnByte(arr->GetSize()!=0);
            }
        } else if(name=="HostCreateThread") {
            auto* function=static_cast<asIScriptFunction*>(g->GetArgAddress(0));auto type=g->GetArgTypeId(1);void* value=g->GetArgAddress(1);
            auto* any=new CScriptAny(g->GetEngine());
            if(type==asTYPEID_INT32){asINT64 v=*static_cast<int*>(value);any->Store(v);}
            else if(type==asTYPEID_BOOL){asINT64 v=*static_cast<bool*>(value);any->Store(v);}
            else if(type==asTYPEID_FLOAT){double v=*static_cast<float*>(value);any->Store(v);}
            else any->Store(value,type);
            try {g->SetReturnDWord(startTask(function,any,execution));}catch(...){any->Release();throw;}any->Release();
        } else if(name=="HostWaitThread") {
            std::shared_ptr<ScriptTask> task;{std::lock_guard lock(session->mutex);auto it=session->tasks.find(int(g->GetArgDWord(0)));if(it!=session->tasks.end())task=it->second;}
            if(!task)throw Error("ScriptRuntime","无效后台任务句柄");
            auto wait=std::clamp(int(g->GetArgDWord(1)),0,1000);for(int i=0;i<wait && !task->done;i+=5)session->cancel->sleep(std::min(5,wait-i));
            if(task->done && !task->error.empty())throw Error("ScriptRuntime",task->error);
            g->SetReturnByte(task->done);
        } else if(name=="HostMessageBox") {
            if(!execution.services->message)throw Error("ScriptRuntime","当前模式不允许交互对话框");
            int choice=execution.services->message(stringArg(g,0),stringArg(g,1),int(g->GetArgDWord(2)),int(g->GetArgDWord(3)),session->cancel);
            auto* fn=static_cast<asIScriptFunction*>(g->GetArgAddress(4));
            if(fn) {
                auto* ctx=g->GetEngine()->CreateContext();checked(ctx->Prepare(fn),"message callback");ctx->SetArgDWord(0,choice);
                auto status=executeContext(ctx,execution);std::string error=status==asEXECUTION_EXCEPTION?ctx->GetExceptionString():"";ctx->Release();
                if(!error.empty())throw Error("ScriptRuntime",error);
            }
        } else if(name=="HostExecuteProgram") {
            // Direct executable + argument list, never a constructed shell command.
            auto executable=QString::fromUtf8(stringArg(g,0));auto args=QProcess::splitCommand(QString::fromUtf8(stringArg(g,1)));
            if(!QProcess::startDetached(executable,args))throw Error("ScriptRuntime","外部程序启动失败");result(g,"");
        } else throw Error("ScriptRuntime","未支持的宿主功能: "+name);
    }catch(const std::exception& e){asGetActiveContext()->SetException(e.what());}
}
static void stringUtility(asIScriptGeneric* g) {
    auto* self=static_cast<std::string*>(g->GetObject());auto name=std::string(g->GetFunction()->GetName());
    if(name=="size")g->SetReturnDWord(asDWORD(self->size()));
    else if(name=="empty")g->SetReturnByte(self->empty());
    else if(name=="find" || name=="rfind") {
        auto pos=name=="find"?self->find(stringArg(g,0),g->GetArgDWord(1)):self->rfind(stringArg(g,0),int(g->GetArgDWord(1))<0?std::string::npos:g->GetArgDWord(1));
        g->SetReturnDWord(pos==std::string::npos?asDWORD(-1):asDWORD(pos));
    } else if(name=="replace") {
        auto from=stringArg(g,0),to=stringArg(g,1);size_t pos=0;int count=0;
        if(!from.empty())while((pos=self->find(from,pos))!=std::string::npos){self->replace(pos,from.size(),to);pos+=to.size();++count;}g->SetReturnDWord(count);
    } else {
        auto str=QString::fromUtf8(*self);
        if(name=="MakeLower")str=str.toLower();else if(name=="MakeUpper")str=str.toUpper();else str=str.trimmed();result(g,str.toUtf8().toStdString());
    }
}
static void stringNumberConstruct(asIScriptGeneric* g) {
    std::string str;auto type=g->GetArgTypeId(0);
    if(type==asTYPEID_INT32)str=std::to_string(int(g->GetArgDWord(0)));
    else if(type==asTYPEID_UINT32)str=std::to_string(g->GetArgDWord(0));
    else if(type==asTYPEID_INT64)str=std::to_string(int64_t(g->GetArgQWord(0)));
    else if(type==asTYPEID_UINT64)str=std::to_string(g->GetArgQWord(0));
    else if(type==asTYPEID_FLOAT)str=std::to_string(g->GetArgFloat(0));else str=std::to_string(g->GetArgDouble(0));
    new(g->GetObject())std::string(std::move(str));
}
static void arrayUtility(asIScriptGeneric* g) {
    auto* arr=static_cast<CScriptArray*>(g->GetObject());auto name=std::string(g->GetFunction()->GetName());
    if(name=="size")g->SetReturnDWord(arr->GetSize());else if(name=="empty")g->SetReturnByte(arr->IsEmpty());
    else if(name=="push_back")arr->InsertLast(g->GetArgAddress(0));else if(name=="pop_back")arr->RemoveLast();
}
static void dictionaryString(asIScriptGeneric* g) {
    auto* v=static_cast<CScriptDictValue*>(g->GetObject());std::string str;
    if(!v->Get(g->GetEngine(),&str,g->GetEngine()->GetTypeIdByDecl("string"))) {
        asINT64 i;double d;if(v->Get(g->GetEngine(),i))str=std::to_string(i);else if(v->Get(g->GetEngine(),d))str=std::to_string(d);
    }result(g,str);
}
static void dictionaryKind(asIScriptGeneric* g) {
    auto type=static_cast<CScriptDictValue*>(g->GetObject())->GetTypeId() & ~asTYPEID_OBJHANDLE & ~asTYPEID_HANDLETOCONST;
    auto name=std::string(g->GetFunction()->GetName());auto* e=g->GetEngine();auto* info=e->GetTypeInfoById(type);
    bool value=name=="isString"?type==e->GetTypeIdByDecl("string"):name=="isInt"?type==asTYPEID_INT64:
        name=="isDouble"?type==asTYPEID_DOUBLE:name=="isBool"?type==asTYPEID_BOOL:name=="isDictionary"?type==e->GetTypeIdByDecl("dictionary"):
        name=="isArray"?info && std::string(info->GetName())=="array":false;g->SetReturnByte(value);
}
void registerHostApi(asIScriptEngine* e) {
    checked(e->RegisterTypedef("intptr","int64"),"intptr");checked(e->RegisterTypedef("uintptr","uint64"),"uintptr");
    checked(e->RegisterFuncdef("void ThreadFunction(any@ param)"),"ThreadFunction");
    checked(e->RegisterFuncdef("void MessageBoxResult(int result)"),"MessageBoxResult");
    const char* declarations[]={
        "string HostUrlGetString(const string &in url, const string &in UserAgent = \"\", const string &in Header = \"\", const string &in PostData = \"\", bool NoCookie = false)",
        "void HostSetUrlUserAgentHTTP(const string &in, const string &in)","void HostSetUrlRefererHTTP(const string &in, const string &in)",
        "string HostUrlEncode(const string &in)","string HostUrlDecode(const string &in)","string HostHashMD5(const string &in)","string HostDecompress(const string &in)",
        "string HostRegExpParse(const string &in, const string &in)","bool HostRegExpParse(const string &in, const string &in, array<dictionary> &inout)","string HostRegExpRemove(const string &in, const string &in)",
        "bool HostFileExist(const string &in)","uintptr HostFileOpen(const string &in)","int64 HostFileLength(uintptr)","string HostFileRead(uintptr, int64)","void HostFileClose(uintptr)",
        "string HostGetScriptFolder()","string HostGetPlayingFileName()","uint HostGetTickCount()","string HostFormatBitrate(int64)",
        "bool HostSaveString(const string &in, const string &in)","string HostLoadString(const string &in, const string &in def = \"\")", "bool HostSaveInteger(const string &in, int)","int HostLoadInteger(const string &in, int def = 0)",
        "int HostGetITag(int, int, bool, bool)","bool HostExistITag(const int &in)","bool HostSetITag(const int &in)",
        "void HostPrintUTF8(const string &in)","void HostOpenConsole()","void HostSleep(int)","void HostIncTimeOut(int)",
        "int HostCreateThread(ThreadFunction@, const ?&in)","bool HostWaitThread(int, int ms = 10)",
        "void HostMessageBox(const string &in, const string &in title = \"\", int iconType = 3, int btnType = 1, MessageBoxResult@ callback = null)",
        "string HostExecuteProgram(const string &in, const string &in)"};
    for(auto decl:declarations)checked(e->RegisterGlobalFunction(decl,asFUNCTION(hostCall),asCALL_GENERIC),decl);
    const char* strings[]={"uint size() const","bool empty() const","int find(const string &in, uint start = 0) const","int rfind(const string &in, int start = -1) const",
        "string Trim(const string &in chars = \"\") const","string MakeLower() const","string MakeUpper() const","int replace(const string &in, const string &in)"};
    for(auto decl:strings)if(!e->GetTypeInfoByDecl("string")->GetMethodByDecl(decl))checked(e->RegisterObjectMethod("string",decl,asFUNCTION(stringUtility),asCALL_GENERIC),decl);
    for(auto type:{"int","uint","int64","uint64","float","double"}) {
        auto decl="void f("+std::string(type)+")";checked(e->RegisterObjectBehaviour("string",asBEHAVE_CONSTRUCT,decl.c_str(),asFUNCTION(stringNumberConstruct),asCALL_GENERIC),decl.c_str());
    }
    for(auto decl:{"uint size() const","bool empty() const","void push_back(const T &in)","void pop_back()"})if(!e->GetTypeInfoByDecl("array<T>")->GetMethodByDecl(decl))checked(e->RegisterObjectMethod("array<T>",decl,asFUNCTION(arrayUtility),asCALL_GENERIC),decl);
    checked(e->RegisterObjectMethod("dictionaryValue","string opConv() const",asFUNCTION(dictionaryString),asCALL_GENERIC),"dictionaryValue string conversion");
    for(auto name:{"isString","isInt","isDouble","isBool","isDictionary","isArray"}) {
        auto decl="bool "+std::string(name)+"() const";checked(e->RegisterObjectMethod("dictionaryValue",decl.c_str(),asFUNCTION(dictionaryKind),asCALL_GENERIC),decl.c_str());
    }
}
Json::Value dictionaryToJson(asIScriptEngine* e,CScriptDictionary* dic,int depth) {
    if(depth>12)throw Error("ResolveContract","字典嵌套超过上限");Json::Value out(Json::objectValue);
    for(auto it=dic->begin();it!=dic->end();++it) {
        auto type=it.GetTypeId();auto base=type & ~asTYPEID_OBJHANDLE & ~asTYPEID_HANDLETOCONST;
        auto key=it.GetKey();
        if(base==e->GetTypeIdByDecl("string")){std::string value;it.GetValue(&value,base);out[key]=value;}
        else if(base==asTYPEID_INT64){asINT64 i=0;it.GetValue(i);out[key]=Json::Int64(i);}
        else if(base==asTYPEID_DOUBLE){double d=0;it.GetValue(d);out[key]=d;}
        else if(base==asTYPEID_BOOL){bool b=false;it.GetValue(&b,base);out[key]=b;}
        else if(base==e->GetTypeIdByDecl("dictionary")) {
            auto* nested=type & asTYPEID_OBJHANDLE?*static_cast<CScriptDictionary* const*>(it.GetAddressOfValue()):static_cast<CScriptDictionary*>(const_cast<void*>(it.GetAddressOfValue()));out[key]=nested?dictionaryToJson(e,nested,depth+1):Json::Value{};
        } else if(auto* info=e->GetTypeInfoById(base);info && std::string(info->GetName())=="array") {
            auto* arr=type & asTYPEID_OBJHANDLE?*static_cast<CScriptArray* const*>(it.GetAddressOfValue()):static_cast<CScriptArray*>(const_cast<void*>(it.GetAddressOfValue()));Json::Value items(Json::arrayValue);
            if(arr && arr->GetSize()>5000)throw Error("ResolveContract","数组长度超限");
            if(arr)for(asUINT i=0;i<arr->GetSize();++i) {
                if(arr->GetElementTypeId()==e->GetTypeIdByDecl("dictionary"))items.append(dictionaryToJson(e,static_cast<CScriptDictionary*>(arr->At(i)),depth+1));
                else if(arr->GetElementTypeId()==e->GetTypeIdByDecl("string"))items.append(*static_cast<std::string*>(arr->At(i)));
            }out[key]=std::move(items);
        } else out[key]=Json::Value{};
    }return out;
}
}
