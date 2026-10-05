#include <QtTest>
#include "domain/Models.h"
#include "scripts/ScriptRuntime.h"
#include "storage/Settings.h"
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QtConcurrent>
using namespace bp;
class PlayerTests : public QObject {
    Q_OBJECT
private slots:
    void domainBoundaries() {
        QVERIFY(domainMatches("a.bilibili.com",".bilibili.com"));
        QVERIFY(!domainMatches("evilbilibili.com","bilibili.com"));
        QVERIFY(!domainMatches("bilibili.com.evil.test","bilibili.com"));
        QVERIFY(!domainMatches("a.bilibili.com","bilibili.com",true));
        QVERIFY(cookiePathMatches("/foo/bar","/foo"));QVERIFY(!cookiePathMatches("/foobar","/foo"));
    }
    void preserveScriptDefault() {
        Json::Value meta(Json::objectValue), choices(Json::arrayValue);
        Json::Value first;first["url"]="https://cdn.test/first";first["quality"]="高清";choices.append(first);
        auto plan=adaptResult({1,1},"https://site.test/watch","https://cdn.test/default",meta,choices);
        QCOMPARE(plan.defaultUrl,"https://cdn.test/default");QCOMPARE(plan.candidates.front().url,"https://cdn.test/first");
        meta["chatScript"]="throw new Error('must not run without chatUrl')";
        QVERIFY(!adaptResult({1,1},"https://site.test/watch","https://cdn.test/default",meta,choices).chat);
    }
    void invalidResults() {
        Json::Value m(Json::objectValue),q(Json::arrayValue);m["errorMessage"]="需要权限";
        QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test","https://cdn.test/x",m,q),Error);
        m.removeMember("errorMessage");m["chatUrl"]="javascript:alert(1)";
        QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test","https://cdn.test/x",m,q),Error);
    }
    void streamRolesAndAudioBitrate() {
        Json::Value meta(Json::objectValue),choices(Json::arrayValue);
        auto add=[&](const char* va,int rate,bool defaultHint=false) {
            Json::Value c(Json::objectValue);c["url"]="https://cdn.test/"+std::to_string(choices.size());
            c["va"]=va;c["bitrateVal"]=rate;c["audioIsDefault"]=defaultHint;choices.append(c);
        };
        add("va",9000000);add("v",8000000);add("a",64000,true);add("a",320000);add("a",320000);
        auto plan=adaptResult({},"https://site.test/watch",choices[1]["url"].asString(),meta,choices);
        QCOMPARE(plan.defaultUrl,plan.candidates[1].url);
        QVERIFY(plan.candidates[0].role==StreamRole::Muxed);QVERIFY(plan.candidates[1].role==StreamRole::Video);
        QVERIFY(plan.candidates[2].role==StreamRole::Audio);
        QCOMPARE(preferredAudioCandidate(plan.candidates)->id,plan.candidates[3].id);
        choices[4]["bitrateVal"]=0;choices[4]["bitrate"]="1.5 Mbps";
        plan=adaptResult({},"https://site.test/watch",plan.defaultUrl,meta,choices);
        QCOMPARE(preferredAudioCandidate(plan.candidates)->bitrateBps,int64_t(1500000));
        choices[4]["bitrateVal"]=128000;choices[4]["bitrate"]="9 Mbps";
        plan=adaptResult({},"https://site.test/watch",plan.defaultUrl,meta,choices);
        QCOMPARE(preferredAudioCandidate(plan.candidates)->id,plan.candidates[3].id);
        choices[3]["bitrateVal"]=Json::Int64(5000000000);
        plan=adaptResult({},"https://site.test/watch",plan.defaultUrl,meta,choices);
        QCOMPARE(preferredAudioCandidate(plan.candidates)->bitrateBps,int64_t(5000000000));
        choices[3]["bitrateVal"]=-1;QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test/watch",plan.defaultUrl,meta,choices),Error);
        choices[3]["bitrateVal"]="320000";QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test/watch",plan.defaultUrl,meta,choices),Error);
        choices.clear();add("v",9000000);add("va",8000000);
        plan=adaptResult({},"https://site.test/watch",choices[0]["url"].asString(),meta,choices);
        QVERIFY(!preferredAudioCandidate(plan.candidates));
        choices[0]["va"]="invalid";QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test/watch",plan.defaultUrl,meta,choices),Error);
    }
    void scriptStreamHints() {
        ScriptRuntime runtime({});runtime.compileFixture(
            "bool PlayitemCheck(const string &in url){return true;}\n"
            "string PlayitemParse(const string &in url,dictionary &inout meta,array<dictionary> &inout choices){\n"
            "dictionary high={{\"url\",\"https://cdn.test/high\"},{\"va\",\"a\"},{\"bitrateVal\",320000}};\n"
            "dictionary low={{\"url\",\"https://cdn.test/low\"},{\"va\",\"a\"},{\"bitrateVal\",64000},{\"audioIsDefault\",true}};\n"
            "dictionary video={{\"url\",\"https://cdn.test/video\"},{\"va\",\"v\"},{\"bitrateVal\",9000000}};\n"
            "choices.insertLast(low);choices.insertLast(high);choices.insertLast(video);\n"
            "return \"https://cdn.test/video\";}\n");
        const auto plan=runtime.resolve({1,1},"https://fixture.test/watch");
        QCOMPARE(plan.defaultUrl,"https://cdn.test/video");
        QCOMPARE(preferredAudioCandidate(plan.candidates)->url,"https://cdn.test/high");
        QCOMPARE(preferredAudioCandidate(plan.candidates)->bitrateBps,int64_t(320000));
    }
    void subtitleMetadataNameAndInlineContent() {
        ScriptRuntime runtime({});runtime.compileFixture("\n"
"            bool PlayitemCheck(const string &in url){return true;}\n"
"            string PlayitemParse(const string &in url,dictionary &inout meta,array<dictionary> &inout choices){\n"
"                array<dictionary> tracks;\n"
"                dictionary danmaku={{\"name\",\"弹幕\"},{\"fileContent\",\"1\\n00:00:00,000 --> 00:01:00,000\\nDanmaku\\n\"}};\n"
"                dictionary main={{\"name\",\"简体中文\"},{\"langTranslated\",\"Ignored language label\"},{\"fileContent\",\"1\\n00:00:00,000 --> 00:01:00,000\\nCaption\\n\"}};\n"
"                tracks.insertLast(danmaku);tracks.insertLast(main);meta[\"subtitle\"]=tracks;\n"
"                return \"https://cdn.test/video\";\n"
"            }\n"
"        \n");
        const auto plan=runtime.resolve({1,1},"https://fixture.test/subtitles");
        QCOMPARE(plan.subtitles.size(),size_t(2));
        QCOMPARE(plan.subtitles[0].label,std::string("弹幕"));
        QCOMPARE(plan.subtitles[1].label,std::string("简体中文"));
        QVERIFY(plan.subtitles[1].content.find("Caption")!=std::string::npos);
        QVERIFY(plan.subtitles[1].url.empty());
    }
    void scriptDebugLogsCompileAndCalls() {
        QStringList logs;HostServices services;services.log=[&](std::string line){logs.append(QString::fromUtf8(line));};
        ScriptRuntime runtime(services);runtime.compileFixture("\n"
"            bool PlayitemCheck(const string &in url){return true;}\n"
"            string PlayitemParse(const string &in url,dictionary &inout meta,array<dictionary> &inout choices){HostPrintUTF8(\"Fixture debug log\");return \"https://cdn.test/video\";}\n"
"        \n");
        runtime.resolve({1,1},"https://fixture.test/subtitles");
        QVERIFY(logs.join('\n').contains("调用 PlayitemParse"));
        QVERIFY(logs.join('\n').contains("Fixture debug log"));
        QVERIFY_EXCEPTION_THROWN(runtime.compileFixture("invalid syntax"),Error);
        QVERIFY(!runtime.diagnostics.isEmpty());
        QVERIFY(logs.join('\n').contains(runtime.diagnostics.back()));
    }
    void playlistCurrentCompatibility() {
        Json::Value meta(Json::objectValue),choices(Json::arrayValue),list(Json::arrayValue);
        for(const auto& current:{Json::Value(false),Json::Value(true),Json::Value(0),Json::Value(1),Json::Value("0"),Json::Value("1")}) {
            Json::Value item(Json::objectValue);item["title"]="选中的分 P";item["url"]="https://site.test/part";item["current"]=current;list.clear();list.append(item);
            const auto plan=adaptResult({},"https://site.test/watch","https://cdn.test/media",meta,choices,list);
            const bool expected=current.isString()?current.asString()=="1":current.asBool();
            QCOMPARE(plan.playlist.front().current,expected);if(expected)QCOMPARE(plan.title,"选中的分 P");
        }
        list[0].removeMember("current");QVERIFY(!adaptResult({},"https://site.test/watch","https://cdn.test/media",meta,choices,list).playlist.front().current);
        for(const auto& invalid:{Json::Value("true"),Json::Value("2"),Json::Value(2)}) {
            list[0]["current"]=invalid;QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test/watch","https://cdn.test/media",meta,choices,list),Error);
        }
        Json::Value audio(Json::objectValue);audio["url"]="https://cdn.test/audio";audio["audioIsDefault"]="1";choices.append(audio);
        QVERIFY_EXCEPTION_THROWN(adaptResult({},"https://site.test/watch","https://cdn.test/media",meta,choices),Error);
    }
    void scriptPlaylistCurrentSelection() {
        ScriptRuntime runtime({});runtime.compileFixture(R"(
            bool PlayitemCheck(const string &in url){return url=="https://fixture.test/first" || url=="https://fixture.test/second" || url=="https://fixture.test/empty";}
            bool PlaylistCheck(const string &in url){return url=="https://fixture.test/list" || PlayitemCheck(url);}
            array<dictionary> PlaylistParse(const string &in url){
                array<dictionary> items;
                if(url=="https://fixture.test/empty")return items;
                dictionary first={{"url","https://fixture.test/first"},{"title","First"},{"current","0"}};
                dictionary second={{"url","https://fixture.test/second"},{"title","Second"},{"current","1"}};
                items.insertLast(first);items.insertLast(second);return items;
            }
            string PlayitemParse(const string &in url,dictionary &inout meta,array<dictionary> &inout choices){return url+"/media";}
        )");
        const auto plan=runtime.resolve({1,1},"https://fixture.test/list");
        QCOMPARE(plan.defaultUrl,"https://fixture.test/second/media");QCOMPARE(plan.title,"Second");
        QCOMPARE(plan.playlist.size(),size_t(2));QVERIFY(!plan.playlist[0].current);QVERIFY(plan.playlist[1].current);
        const auto direct=runtime.resolve({1,2},"https://fixture.test/first");
        QCOMPARE(direct.defaultUrl,"https://fixture.test/first/media");QCOMPARE(direct.playlist.size(),size_t(2));
        QVERIFY(runtime.resolve({1,3},"https://fixture.test/empty").playlist.empty());
        QVERIFY(runtime.resolve({1,4},"https://fixture.test/first",false).playlist.empty());
    }
    void timestampsAndState() {
        QCOMPARE(subtitleMillis(MediaTime(100000000),MediaTime(15000000)),8500);
        QVERIFY(validTransition(State::Playing,State::Seeking));QVERIFY(!validTransition(State::Idle,State::Playing));
        QVERIFY(!validTransition(State::Paused,State::Completed));
        QVERIFY(validTransition(State::Paused,State::Reconnecting));
        QCOMPARE(redactUrl("https://u:p@cdn.test/video?token=secret#frag"),"https://cdn.test/video");
    }
    void atomicSettings() {
        QTemporaryDir temp;QVERIFY(temp.isValid());Settings a(temp.path());a.values["volume"]=75;a.save();Settings b(temp.path());QCOMPARE(b.values["volume"].toInt(),75);
    }
    void scriptOwnershipAndThreads() {
        ScriptRuntime runtime({});
        runtime.compileFixture(R"(
            void run() {
                JsonReader reader; JsonValue root;
                if(!reader.parse('{"node":{"n":3},"list":[1,2]}',root)) return;
                JsonValue copy=root["node"]; root["node"]["n"]=7;
                if(copy["n"].asInt()!=3 || root["node"]["n"].asInt()!=7) { HostFileClose(999); }
                dictionary@ task=dictionary(); task.set("value","before");
                int id=HostCreateThread(function(any@ p) {
                    dictionary@ t; p.retrieve(@t); t.set("value","after"); HostSaveString("worker","ok");
                },@task);
                while(!HostWaitThread(id,10)) HostIncTimeOut(10);
                string text;task.get("value",text);
                if(text!="after" || HostLoadString("worker")!="ok") HostFileClose(999);
                string binary="a"; binary.resize(3);binary[1]=0;binary[2]=98;
                if(binary.length()!=3 || HostDecompress(binary)!=binary) HostFileClose(999);
                string s="a a";s.replace("a","b");if(s!="b b")HostFileClose(999);
            }
        )");
        runtime.runFixture("void run()");
    }
    void staleHandles() {
        ScriptRuntime runtime({});runtime.compileFixture("void run(){HostFileClose(100);}");
        QVERIFY_EXCEPTION_THROWN(runtime.runFixture("void run()"),Error);
    }
    void displayMetadataFields() {
        Json::Value meta(Json::objectValue),choices(Json::arrayValue),list(Json::arrayValue),item(Json::objectValue);
        meta["author"]="@测试作者";meta["duration"]="93123";meta["date"]="2026-10-05 12:30:00";
        meta["viewCount"]=Json::Int64(32001);meta["likeCount"]="864";meta["dislikeCount"]=0;
        meta["webUrl"]="https://site.test/watch";meta["content"]="完整简介\n<b>普通文字</b>";
        meta["authorization"]="must-not-be-exposed";meta["chatScript"]="private script";
        item=meta;item["title"]="列表标题";item["url"]="https://site.test/watch";item["duration"]=Json::Int64(93123);list.append(item);
        const auto plan=adaptResult({},"https://site.test/watch","https://cdn.test/media",meta,choices,list);
        QCOMPARE(plan.information["viewCount"].asInt64(),Json::Int64(32001));QCOMPARE(plan.information["dislikeCount"].asInt(),0);
        QCOMPARE(plan.information["content"].asString(),meta["content"].asString());
        QCOMPARE(plan.playlist[0].information["duration"].asInt64(),Json::Int64(93123));
        QCOMPARE(plan.playlist[0].information["date"].asString(),meta["date"].asString());
        QVERIFY(!plan.information.isMember("authorization"));QVERIFY(!plan.information.isMember("chatScript"));
        QVERIFY(!plan.playlist[0].information.isMember("authorization"));
        meta["viewCount"]=Json::Value(Json::objectValue);
        QVERIFY(!adaptResult({},"https://site.test/watch","https://cdn.test/media",meta,choices).information.isMember("viewCount"));
    }
    void invalidJsonOutputPathBecomesScriptException() {
        ScriptRuntime runtime({});runtime.compileFixture(R"(
            void run(){JsonReader reader;JsonValue root;reader.parse("1",root);reader.parse("{}",root["child"]);}
        )");
        QVERIFY_EXCEPTION_THROWN(runtime.runFixture("void run()"),Error);
    }
    void nestedTasksCancelAndJoin() {
        for(int i=0;i<40;++i) {
            ScriptRuntime runtime({});runtime.compileFixture(R"(
                void leaf(any@ p){HostSleep(50);}
                void parent(any@ p){for(int i=0;i<32;++i)HostCreateThread(leaf,0);}
                void run(){for(int i=0;i<8;++i)HostCreateThread(parent,0);}
            )");
            runtime.runFixture("void run()");auto session=runtime.session();
            runtime.close();QVERIFY(session->cancel->cancelled);
            std::lock_guard lock(session->mutex);QVERIFY(session->tasks.empty());
        }
    }
    void httpPoliciesAndCancellation() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        auto port=server.serverPort();
        connect(&server,&QTcpServer::newConnection,&server,[&]{
            while(auto* socket=server.nextPendingConnection()){
                auto bytes=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket,bytes]{
                    *bytes+=socket->readAll();if(!bytes->contains("\r\n\r\n"))return;
                    auto request=*bytes;bytes->clear();auto path=request.split(' ').value(1);
                    QByteArray response="HTTP/1.1 200 OK\r\nConnection: close\r\n";
                    if(path=="/redirect")response="HTTP/1.1 302 Found\r\nConnection: close\r\nLocation: http://localhost:"+QByteArray::number(port)+"/echo\r\n";
                    if(path=="/set")response+="Set-Cookie: returned=value; Path=/\r\n";
                    if(path=="/folder/set")response+="Set-Cookie: nested=value; SameSite=Strict\r\n";
                    if(path=="/slow"){QPointer<QTcpSocket> weak=socket;QTimer::singleShot(1500,socket,[weak]{if(weak){weak->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");weak->disconnectFromHost();}});return;}
                    auto body=path=="/echo"?request:QByteArray("ok");response+="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;socket->write(response);socket->disconnectFromHost();
                });
            }
        });
        HttpService http;CookieSnapshot snapshot;snapshot.epoch=7;snapshot.revision=1;snapshot.cookies.push_back({"login","dummy","127.0.0.1","/",false,true,true,0});http.publishCookies(snapshot);
        auto run=[&](HttpRequest request){auto future=QtConcurrent::run([&http,request]{return http.request(request);});for(int i=0;i<150 && !future.isFinished();++i)QTest::qWait(20);if(!future.isFinished())throw Error("TestTimeout","HTTP fixture stalled");return future.result();};
        HttpRequest r;r.url="http://127.0.0.1:"+std::to_string(port)+"/echo";r.epoch=7;
        auto normal=run(r);QCOMPARE(normal.status,200);QVERIFY(QByteArray::fromStdString(normal.body).toLower().contains("cookie: login=dummy"));
        r.noCookie=true;r.headers="Cookie: forced=dummy";auto isolated=run(r);QVERIFY(!QByteArray::fromStdString(isolated.body).toLower().contains("cookie:"));
        r.noCookie=false;r.url="http://127.0.0.1:"+std::to_string(port)+"/redirect";r.headers="Cookie: forced=dummy\r\nAuthorization: dummy";
        auto redirect=run(r);QCOMPARE(redirect.status,200);QVERIFY(!QByteArray::fromStdString(redirect.body).toLower().contains("cookie:"));QVERIFY(!QByteArray::fromStdString(redirect.body).toLower().contains("authorization:"));
        r.headers.clear();r.url="http://127.0.0.1:"+std::to_string(port)+"/set";QCOMPARE(run(r).status,200);QVERIFY(http.cookieHeader(QUrl(QString::fromStdString(r.url)),7).find("returned=value")!=std::string::npos);
        r.url="http://127.0.0.1:"+std::to_string(port)+"/folder/set";QCOMPARE(run(r).status,200);
        const auto saved=http.snapshot();auto nested=std::find_if(saved.cookies.begin(),saved.cookies.end(),[](const Cookie& c){return c.name=="nested";});
        QVERIFY(nested!=saved.cookies.end());QCOMPARE(nested->path,"/folder");QVERIFY(nested->sameSite==Cookie::SameSite::Strict);
        QVERIFY(http.cookieHeader(QUrl(QString("http://127.0.0.1:%1/outside").arg(port)),7).find("nested=value")==std::string::npos);
        QVERIFY(!http.publishCookiesIfUnchanged({7,0,{}},saved.revision-1));QCOMPARE(http.snapshot().cookies.size(),saved.cookies.size());
        r.url="http://127.0.0.1:"+std::to_string(port)+"/slow";r.cancel=std::make_shared<Cancellation>();QTimer::singleShot(50,&server,[cancel=r.cancel]{cancel->cancel();});QCOMPARE(run(r).error,"Cancelled");
        r.cancel=std::make_shared<Cancellation>();r.timeoutMs=80;QCOMPARE(run(r).error,"Timeout");
        http.publishCookies({8,0,{}});QVERIFY(http.cookieHeader(QUrl(QString::fromStdString(r.url)),7).empty());
    }
};
QTEST_GUILESS_MAIN(PlayerTests)
#include "PlayerTests.moc"
