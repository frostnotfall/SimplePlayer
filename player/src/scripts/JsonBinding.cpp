#include "JsonBinding.h"
#include "domain/Models.h"
#include <scriptarray/scriptarray.h>
#include <map>
#include <variant>

namespace bp {
void checked(int r,const char* declaration){if(r<0)throw Error("ScriptRegistration",std::string(declaration)+": "+std::to_string(r));}
struct JsonRoot { Json::Value value; std::recursive_mutex mutex; };
struct ScriptJson {
    std::shared_ptr<JsonRoot> root=std::make_shared<JsonRoot>();
    std::vector<std::variant<std::string,unsigned>> path;
    std::map<std::string,std::unique_ptr<ScriptJson>> children;
    ScriptJson()=default;
    ScriptJson(const ScriptJson& other) { std::lock_guard lock(other.root->mutex); root->value=other.node(); }
    Json::Value& node() const {
        auto* n=&root->value;
        for(const auto& key:path)if(auto* str=std::get_if<std::string>(&key))n=&(*n)[*str];else n=&(*n)[std::get<unsigned>(key)];
        return *n;
    }
    ScriptJson& operator=(const ScriptJson& other) {
        if(this==&other)return *this;
        Json::Value snapshot;{std::lock_guard lock(other.root->mutex);snapshot=other.node();}
        {std::lock_guard lock(root->mutex);node()=std::move(snapshot);}return *this;
    }
    ScriptJson* child(std::variant<std::string,unsigned> key) {
        std::lock_guard lock(root->mutex);
        auto id=std::holds_alternative<std::string>(key)?"s:"+std::get<std::string>(key):"i:"+std::to_string(std::get<unsigned>(key));
        if(!children.contains(id)) {auto c=std::make_unique<ScriptJson>();c->root=root;c->path=path;c->path.push_back(key);children[id]=std::move(c);}
        return children[id].get();
    }
};
static void jsonConstruct(asIScriptGeneric* g){new(g->GetObject())ScriptJson;}
static void jsonCopy(asIScriptGeneric* g){new(g->GetObject())ScriptJson(*static_cast<ScriptJson*>(g->GetArgAddress(0)));}
static void jsonDestroy(asIScriptGeneric* g){static_cast<ScriptJson*>(g->GetObject())->~ScriptJson();}
static void jsonMethod(asIScriptGeneric* g) {
    auto* self=static_cast<ScriptJson*>(g->GetObject());auto name=std::string(g->GetFunction()->GetName());
    try {
        std::lock_guard lock(self->root->mutex);auto& v=self->node();
        if(name=="opIndex") {
            if(g->GetArgTypeId(0)==asTYPEID_INT32) {
                auto i=int(g->GetArgDWord(0));if(i<0 || i>1000000)throw Error("ScriptRuntime","JsonValue index 超限");
                g->SetReturnAddress(self->child(unsigned(i)));
            } else g->SetReturnAddress(self->child(*static_cast<std::string*>(g->GetArgAddress(0))));
        } else if(name=="opAssign") {
            auto type=g->GetArgTypeId(0);
            if(type==asTYPEID_INT32)v=int(g->GetArgDWord(0));
            else if(type==asTYPEID_BOOL)v=bool(g->GetArgByte(0));
            else if(type==g->GetEngine()->GetTypeIdByDecl("string"))v=*static_cast<std::string*>(g->GetArgAddress(0));
            else {auto* other=static_cast<ScriptJson*>(g->GetArgAddress(0));*self=*other;}
            g->SetReturnAddress(self);
        } else if(name=="asString") {auto str=v.isNull()?std::string{}:v.asString();g->SetReturnObject(&str);}
        else if(name=="asInt")g->SetReturnDWord(v.isNull()?0:v.asInt());
        else if(name=="asUInt")g->SetReturnDWord(v.isNull()?0:v.asUInt());
        else if(name=="asInt64")g->SetReturnQWord(v.isNull()?0:v.asInt64());
        else if(name=="asUInt64")g->SetReturnQWord(v.isNull()?0:v.asUInt64());
        else if(name=="asBool")g->SetReturnByte(!v.isNull() && v.asBool());
        else if(name=="asFloat")g->SetReturnFloat(v.isNull()?0:v.asFloat());
        else if(name=="asDouble")g->SetReturnDouble(v.isNull()?0:v.asDouble());
        else if(name=="size")g->SetReturnDWord(v.size());
        else if(name=="getKeys") {
            auto keys=v.isObject()?v.getMemberNames():Json::Value::Members{};
            auto* arr=CScriptArray::Create(g->GetEngine()->GetTypeInfoByDecl("array<string>"),asUINT(keys.size()));
            for(asUINT i=0;i<arr->GetSize();++i)*static_cast<std::string*>(arr->At(i))=keys[i];g->SetReturnAddress(arr);
        } else {
            bool result=name=="isNull"?v.isNull():name=="isBool"?v.isBool():name=="isInt"?v.isInt():name=="isUInt"?v.isUInt():
                name=="isInt64"?v.isInt64():name=="isUInt64"?v.isUInt64():name=="isFloat" || name=="isDouble"?v.isDouble():
                name=="isNumeric"?v.isNumeric():name=="isString"?v.isString():name=="isArray"?v.isArray():name=="isObject"?v.isObject():
                name=="canString"?v.isString() || v.isNumeric() || v.isBool():false;
            g->SetReturnByte(result);
        }
    } catch(const std::exception& e){asGetActiveContext()->SetException(e.what());}
}
static void readerConstruct(asIScriptGeneric* g){new(g->GetObject())int(0);}
static void readerParse(asIScriptGeneric* g) {
    try {
    auto& str=*static_cast<std::string*>(g->GetArgAddress(0));auto* out=static_cast<ScriptJson*>(g->GetArgAddress(1));
    Json::CharReaderBuilder builder; builder["allowComments"]=true; builder["collectComments"]=false;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());std::string errors;
    std::lock_guard lock(out->root->mutex);
    g->SetReturnByte(reader->parse(str.data(),str.data()+str.size(),&out->node(),&errors));
    }catch(const std::exception& e){g->SetReturnByte(false);asGetActiveContext()->SetException(e.what());}
}
void registerJson(asIScriptEngine* e) {
    checked(e->RegisterObjectType("JsonValue",sizeof(ScriptJson),asOBJ_VALUE|asGetTypeTraits<ScriptJson>()),"JsonValue");
    checked(e->RegisterObjectBehaviour("JsonValue",asBEHAVE_CONSTRUCT,"void f()",asFUNCTION(jsonConstruct),asCALL_GENERIC),"JsonValue()");
    checked(e->RegisterObjectBehaviour("JsonValue",asBEHAVE_CONSTRUCT,"void f(const JsonValue &in)",asFUNCTION(jsonCopy),asCALL_GENERIC),"JsonValue(copy)");
    checked(e->RegisterObjectBehaviour("JsonValue",asBEHAVE_DESTRUCT,"void f()",asFUNCTION(jsonDestroy),asCALL_GENERIC),"~JsonValue");
    const char* methods[]={"JsonValue &opAssign(const JsonValue &in)","JsonValue &opAssign(int)","JsonValue &opAssign(bool)","JsonValue &opAssign(const string &in)",
        "JsonValue &opIndex(int)","JsonValue &opIndex(const string &in)","bool isNull() const","bool isBool() const","bool isInt() const","bool isUInt() const",
        "bool isInt64() const","bool isUInt64() const","bool isFloat() const","bool isDouble() const","bool isNumeric() const","bool isString() const",
        "bool isArray() const","bool isObject() const","bool canString() const","int asInt() const","uint asUInt() const","int64 asInt64() const",
        "uint64 asUInt64() const","float asFloat() const","double asDouble() const","bool asBool() const","string asString() const","int size() const","array<string>@ getKeys() const"};
    for(auto decl:methods)checked(e->RegisterObjectMethod("JsonValue",decl,asFUNCTION(jsonMethod),asCALL_GENERIC),decl);
    checked(e->RegisterObjectType("JsonReader",sizeof(int),asOBJ_VALUE|asOBJ_POD|asOBJ_APP_PRIMITIVE),"JsonReader");
    checked(e->RegisterObjectBehaviour("JsonReader",asBEHAVE_CONSTRUCT,"void f()",asFUNCTION(readerConstruct),asCALL_GENERIC),"JsonReader()");
    checked(e->RegisterObjectMethod("JsonReader","bool parse(const string &in, JsonValue &out)",asFUNCTION(readerParse),asCALL_GENERIC),"JsonReader.parse");
}
}
