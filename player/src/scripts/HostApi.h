#pragma once
#include "domain/Models.h"
#include "network/HttpService.h"
#include <angelscript.h>
#include <scriptany/scriptany.h>
#include <scriptdictionary/scriptdictionary.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <QFile>
#include <functional>
namespace bp {
struct ScriptTask {
    int id;
    std::thread worker;
    std::atomic_bool done{false};
    std::string error;
    ~ScriptTask(){if(worker.joinable())worker.join();}
};
struct HostSession {
    SessionKey key;
    Cancel cancel=std::make_shared<Cancellation>();
    uint64_t epoch=1;
    std::string playingPath;
    std::mutex mutex;
    std::unordered_map<std::string,std::string> strings;
    std::unordered_map<std::string,int> integers;
    std::unordered_set<int> tags;
    std::unordered_map<int,std::shared_ptr<ScriptTask>> tasks;
    std::unordered_map<uint64_t,std::unique_ptr<QFile>> files;
    int nextTask=1, nextTag=100000;
    uint64_t nextFile=1;
    void join();
};
struct HostServices {
    HttpService* http=nullptr;
    std::function<void(std::string)> log;
    std::function<int(std::string,std::string,int,int,Cancel)> message;
};
struct Execution {
    asIScriptEngine* engine;
    std::shared_ptr<HostSession> session;
    HostServices* services;
    std::string scriptFolder;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
    std::chrono::steady_clock::time_point hardDeadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    std::string playingPath;
};
void registerHostApi(asIScriptEngine* engine);
void lineCallback(asIScriptContext* context,void*);
int executeContext(asIScriptContext* context,Execution& execution);
Json::Value dictionaryToJson(asIScriptEngine* engine,CScriptDictionary* dictionary,int depth=0);
}
