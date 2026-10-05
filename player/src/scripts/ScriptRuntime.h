#pragma once
#include "HostApi.h"
#include <QStringList>
namespace bp {
class ScriptRuntime {
public:
    explicit ScriptRuntime(HostServices services);
    ~ScriptRuntime();
    void load(QString mediaPath,QString statisticsPath = {});
    void initialize(std::shared_ptr<HostSession> session);
    PlaybackPlan resolve(SessionKey key,std::string url,bool includePlaylist=true);
    std::string stringFunction(const char* name);
    void statistics(const char* event,const std::string& path,int position=0,int duration=0);
    void close();
    void finishStatistics(const std::string& path);
    void debug(std::string message){if(services_.log)services_.log(std::move(message));}
    QStringList diagnostics;
    asIScriptEngine* engine() const {return engine_;}
    std::shared_ptr<HostSession> session() const{return session_;}
    void compileFixture(const std::string& code);
    void runFixture(const char* declaration);
private:
    asIScriptEngine* engine_=nullptr;
    asIScriptModule* media_=nullptr;
    asIScriptModule* statistics_=nullptr;
    HostServices services_;
    QString mediaPath_,statisticsPath_;
    std::shared_ptr<HostSession> session_;
    void compile(asIScriptModule*& module,const char* name,const QString& path);
    int call(asIScriptContext* context,asIScriptModule* module);
    bool checkFunction(const char* name,const std::string& url);
};
}
