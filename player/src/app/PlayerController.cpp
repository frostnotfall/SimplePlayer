#include "PlayerController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QColor>
#include <QPointer>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QProcess>
#include <cmath>

namespace bp {
static QVariantMap displayMap(const Json::Value& information) {
    QVariantMap result;
    for(const auto& name:information.getMemberNames())result[QString::fromStdString(name)]=QString::fromUtf8(information[name].asString());
    return result;
}
QVariantMap PlayerController::mediaInformation() const {
    if(!plan_)return {};
    auto result=displayMap(plan_->information);
    result["title"]=QString::fromUtf8(plan_->title);
    result["webUrl"]=QString::fromUtf8(plan_->webUrl.empty()?plan_->originalUrl:plan_->webUrl);
    result["live"]=plan_->live;
    if(!result.contains("duration") && duration_>0 && !plan_->live)result["duration"]=duration_*1000;
    return result;
}
static const QStringList pathSettings={"mediaScript","statisticsScript","lavDirectory","rendererPath","audioRendererPath","bfrcPath","themePath","ffmpegPath"};
static int windowResizeEdges(QWindow* window,QPoint point){
    if(!window || window->visibility()!=QWindow::Windowed)return 0;
    int edges=0;
    if(point.x()<8)edges|=Qt::LeftEdge;else if(point.x()>=window->width()-8)edges|=Qt::RightEdge;
    if(point.y()<8)edges|=Qt::TopEdge;else if(point.y()>=window->height()-8)edges|=Qt::BottomEdge;
    return edges;
}
static Qt::CursorShape resizeCursor(int edges){
    bool horizontal=edges&(Qt::LeftEdge|Qt::RightEdge),vertical=edges&(Qt::TopEdge|Qt::BottomEdge);
    if(horizontal && vertical)return edges==(Qt::LeftEdge|Qt::TopEdge) || edges==(Qt::RightEdge|Qt::BottomEdge)?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor;
    return horizontal?Qt::SizeHorCursor:vertical?Qt::SizeVerCursor:Qt::ArrowCursor;
}
static QString installedFilter(const wchar_t* clsid) {
    wchar_t path[32768];DWORD bytes=sizeof(path);auto key=std::wstring(L"CLSID\\")+clsid+L"\\InprocServer32";
    if(RegGetValueW(HKEY_CLASSES_ROOT,key.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,path,&bytes)==ERROR_SUCCESS)return QString::fromWCharArray(path);return {};
}
static QString locateScript(const QString& relative) {
    auto app=QCoreApplication::applicationDirPath();
    for(auto base:{app+"/extensions/",app+"/../.local/scripts/",app+"/../../.local/scripts/"})if(QFileInfo::exists(base+relative))return QDir::cleanPath(base+relative);
    return app+"/extensions/"+relative;
}
PlayerController::PlayerController(QString dataDirectory,bool validationMode):validationMode_(validationMode) {
    settings_=std::make_unique<Settings>(dataDirectory);
    auto& s=settings_->values;
    if(!s.contains("mediaScript"))s["mediaScript"]=locateScript("Media/PlayParse/MediaPlayParse - Bilibili.as");
    if(!s.contains("statisticsScript"))s["statisticsScript"]=locateScript("Playback/Statistics/PlaybackStatistics - Bilibili.as");
    if(!s.contains("lavDirectory"))s["lavDirectory"]=QFileInfo(installedFilter(L"{B98D13E7-55DB-4385-A33D-09FD1BA26338}")).absolutePath();
    if(!s.contains("rendererPath"))s["rendererPath"]=installedFilter(L"{71F080AA-8661-4093-B15E-4F6903E77D0A}");
    if(!s.contains("audioRendererPath")){auto packaged=QCoreApplication::applicationDirPath()+"/filters/MpcAudioRenderer.ax";s["audioRendererPath"]=QFileInfo::exists(packaged)?packaged:QString{};}
    if(!s.contains("bfrcPath"))s["bfrcPath"]=installedFilter(L"{FCC4769C-1E45-4E60-8CBF-B159B2584587}");
    if(!s.contains("ffmpegPath")){auto packaged=QCoreApplication::applicationDirPath()+"/helpers/ffmpeg.exe";s["ffmpegPath"]=QFileInfo::exists(packaged)?packaged:QStandardPaths::findExecutable("ffmpeg.exe");}
    if(!s.contains("scriptEnabled"))s["scriptEnabled"]=true;
    if(!s.contains("mainSubtitleVisible"))s["mainSubtitleVisible"]=true;
    if(!s.contains("secondarySubtitleVisible"))s["secondarySubtitleVisible"]=true;
    localPlaylist_=s["localPlaylist"].toArray().toVariantList();
    if(!scriptEnabled())playlist_=localPlaylist_;
    if(!s.contains("volume"))s["volume"]=75;
    if(!s.contains("muted"))s["muted"]=false;
    if(s["loopMode"].toString()!="one")s["loopMode"]="none";
    if(!s.contains("playlistItemHeight"))s["playlistItemHeight"]=76;
    for(const auto& key:pathSettings)if(s[key].isString())s[key]=nativePath(s[key].toString());
    // Migrate the old compact sidebar once; subsequent user widths are retained.
    if(s["sidebarLayoutVersion"].toInt()<2 || !s.contains("chatWidth"))s["chatWidth"]=480;
    s["sidebarLayoutVersion"]=2;
    s["chatWidth"]=std::clamp(s["chatWidth"].toInt(480),240,960);
    if(!s.contains("chatVisible"))s["chatVisible"]=true;
    if(!s.contains("theme"))s["theme"]="dark";
    browser_=std::make_unique<BrowserSession>(&http_,settings_->directory+"/webview2");
    connect(browser_.get(),&BrowserSession::accountChanged,this,[this]{lastAccountCheck_=0;refreshAccount();});
    connect(browser_.get(),&BrowserSession::sidebarPageRequested,this,&PlayerController::sidebarPageRequested);
    if(!validationMode_) {
        auto* accountTimer=new QTimer(this);accountTimer->setInterval(60000);
        connect(accountTimer,&QTimer::timeout,this,&PlayerController::refreshAccount);accountTimer->start();
        QTimer::singleShot(0,this,&PlayerController::refreshAccount);
    }
    http_.cookieChanges=[this](std::vector<Cookie> cookies,uint64_t epoch){QMetaObject::invokeMethod(browser_.get(),[this,cookies=std::move(cookies),epoch]() mutable {browser_->applyCookies(std::move(cookies),epoch);});};
    videoHost_=new QWindow;videoHost_->setFlags(Qt::FramelessWindowHint);videoHost_->create();SetClassLongPtrW(HWND(videoHost_->winId()),GCLP_HBRBACKGROUND,LONG_PTR(GetStockObject(BLACK_BRUSH)));
    videoHost_->installEventFilter(this);
    videoHost_->setCursor(Qt::ArrowCursor);
    QCoreApplication::instance()->installNativeEventFilter(this);
    // Native renderers can swallow unpressed mouse movement. Track position
    // within the active player as well, so hover chrome reliably wakes up.
    auto* pointerTimer=new QTimer(this);
    connect(pointerTimer,&QTimer::timeout,this,[this,last=QCursor::pos()]() mutable {
        const auto point=QCursor::pos();if(point==last)return;last=point;
        auto* window=videoHost_->parent();
        if(window && window->isVisible() && window->isActive() &&
           QRect(QPoint{},window->size()).contains(window->mapFromGlobal(point))) {
            emit videoPointerMoved(window->mapFromGlobal(point));
            // QML hover handlers can miss their leave event when the pointer
            // crosses into a native child. Restore the owner cursor there.
            if(QRect(QPoint{},videoHost_->size()).contains(videoHost_->mapFromGlobal(point)) &&
               !(GetAsyncKeyState(VK_LBUTTON)&0x8000)){
                auto cursor=resizeCursor(windowResizeEdges(window,window->mapFromGlobal(point)));
                window->setCursor(cursor);videoHost_->setCursor(cursor);
            }
        }
    });
    pointerTimer->start(50);
    // GraphSession polls the native client rectangle on its STA thread. Avoid
    // duplicate resize calls using Qt geometry that may not yet reach the HWND.
    graphThread_.setObjectName("GraphThread/STA");graphThread_.start();
    graphDispatcher_=new QObject;graphDispatcher_->moveToThread(&graphThread_);connect(&graphThread_,&QThread::finished,graphDispatcher_,&QObject::deleteLater);
    scriptWorker_=new QObject;scriptWorker_->moveToThread(&scriptThread_);connect(&scriptThread_,&QThread::finished,scriptWorker_,&QObject::deleteLater);scriptThread_.setObjectName("ScriptCoordinator");scriptThread_.start();
}
PlayerController::~PlayerController() {
    liveCache_.reset();
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if(accountCancel_)accountCancel_->cancel();
    if(hostSession_)hostSession_->cancel->cancel();if(mediaCancel_)mediaCancel_->cancel();
    for(auto& promise:messages_)try{promise->set_value(3);}catch(...){}messages_.clear();
    QMetaObject::invokeMethod(scriptWorker_,[this]{if(runtime_)runtime_->close();runtime_.reset();},Qt::BlockingQueuedConnection);
    if(pendingGraph_)QMetaObject::invokeMethod(pendingGraph_,[this]{delete pendingGraph_;pendingGraph_=nullptr;},Qt::BlockingQueuedConnection);
    if(activeGraph_)QMetaObject::invokeMethod(activeGraph_,[this]{delete activeGraph_;activeGraph_=nullptr;},Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(graphDispatcher_,[]{},Qt::BlockingQueuedConnection);
    graphThread_.quit();graphThread_.wait();scriptThread_.quit();scriptThread_.wait();
    http_.cookieChanges={};browser_.reset();delete videoHost_;try{settings_->save();}catch(...){}
}
QString PlayerController::state() const {
    switch(state_){case State::Idle:return "就绪";case State::Resolving:return "解析链接";case State::Opening:return "连接媒体";case State::Buffering:return "缓冲";case State::Playing:return "播放中";case State::Paused:return "已暂停";case State::Seeking:return "跳转中";case State::Switching:return "切换中";case State::Reconnecting:return "重新连接";case State::Completed:return "播放完毕";case State::Closing:return "停止中";case State::Error:return "播放失败";}return {};
}
void PlayerController::transition(State next){if(!validTransition(state_,next))throw Error("StateTransition","非法播放状态迁移");state_=next;emit changed();}
bool PlayerController::eventFilter(QObject* watched,QEvent* event) {
    if(watched!=videoHost_)return QObject::eventFilter(watched,event);
    if(event->type()==QEvent::MouseMove){
        auto* window=videoHost_->parent();
        const auto global=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        emit videoPointerMoved(window?window->mapFromGlobal(global):videoHost_->mapFromGlobal(global));
        auto cursor=resizeCursor(windowResizeEdges(window,window?window->mapFromGlobal(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()):QPoint{}));
        videoHost_->setCursor(cursor);if(window)window->setCursor(cursor);
    }
    // The native video child covers QML hit areas; forward its outer edges too.
    if(event->type()==QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
        auto* window=videoHost_->parent();
        if(window && window->windowState()==Qt::WindowNoState) {
            const auto p=window->mapFromGlobal(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
            int edges=windowResizeEdges(window,p);
            if(edges){emit videoResizeRequested(edges);return true;}
        }
    }
    if(event->type()==QEvent::MouseButtonDblClick && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){togglePause();return true;}
    if(event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button()==Qt::MiddleButton){emit videoFullscreenRequested();return true;}
    if(event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button()==Qt::RightButton) {
        const auto global=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        emit videoMenuRequested(videoHost_->parent()?videoHost_->parent()->mapFromGlobal(global):videoHost_->mapFromGlobal(global));return true;
    }
    if(event->type()==QEvent::KeyPress) {
        const auto* key=static_cast<QKeyEvent*>(event);
        if(key->modifiers()==Qt::NoModifier) {
            if(key->key()==Qt::Key_F6 || key->key()==Qt::Key_F9){if(!key->isAutoRepeat())emit sidebarPageRequested(key->key()==Qt::Key_F6?0:1);return true;}
            if(key->key()==Qt::Key_C){adjustPlaybackRate(0.25);return true;}
            if(key->key()==Qt::Key_X){adjustPlaybackRate(-0.25);return true;}
            if(key->key()==Qt::Key_Z){setPlaybackRate(1.0);return true;}
        }
        if(key->key()==Qt::Key_F11){if(!key->isAutoRepeat())emit videoFullscreenRequested();return true;}
        if(key->key()==Qt::Key_Escape){if(!key->isAutoRepeat())emit videoEscapeRequested();return true;}
        if(key->key()==Qt::Key_Space){if(!key->isAutoRepeat())togglePause();return true;}
        if(key->key()==Qt::Key_Left){seek(position_-5);return true;}
        if(key->key()==Qt::Key_Right){seek(position_+5);return true;}
        if(key->key()==Qt::Key_V && key->modifiers().testFlag(Qt::ControlModifier) && key->modifiers().testFlag(Qt::ShiftModifier)){if(!key->isAutoRepeat())emit videoLayoutRequested();return true;}
        if(key->key()==Qt::Key_L && key->modifiers().testFlag(Qt::ControlModifier)){emit videoLinkRequested();return true;}
    }
    return QObject::eventFilter(watched,event);
}
FilterPaths PlayerController::filterPaths() const {const auto& s=settings_->values;return {s["lavDirectory"].toString(),s["rendererPath"].toString(),s["audioRendererPath"].toString(),s["bfrcPath"].toString(),s["bfrcEnabled"].toBool()};}
HostServices PlayerController::hostServices() {
    HostServices services;services.http=&http_;
    services.log=[this](std::string message){QString summary="脚本日志已接收；正文未写入诊断以避免泄露凭据";if(message.starts_with("HTTP: ")){summary=QString::fromUtf8(message).left(512);summary.replace(QRegularExpression("https?://[^\\s]+"),"[URL]");}QMetaObject::invokeMethod(this,[this,summary,message=QString::fromUtf8(message)]{appendDebug(message);diagnostics_["scriptLog"]=summary;emit changed();});};
    services.message=[this](std::string body,std::string title,int,int buttons,Cancel cancel){
        if(validationMode_){cancel->check();if(body.starts_with("error code:")){auto code=QString::fromUtf8(body).split('\n').front();QMetaObject::invokeMethod(this,[this,code]{auto codes=diagnostics_["scriptMessages"].toStringList();codes.push_back(code);diagnostics_["scriptMessages"]=codes;emit changed();});}return 3;}
        auto promise=std::make_shared<std::promise<int>>();auto future=promise->get_future();int id=nextMessage_++;
        QMetaObject::invokeMethod(this,[this,id,promise,body=std::move(body),title=std::move(title),buttons,cancel]{if(cancel->cancelled){promise->set_value(3);return;}messages_[id]=promise;emit askMessage(id,QString::fromUtf8(title),QString::fromUtf8(body),buttons);});
        while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready)cancel->check();cancel->check();return future.get();
    };return services;
}
void PlayerController::answerMessage(int id,int response){if(auto it=messages_.find(id);it!=messages_.end()){it.value()->set_value(response);messages_.erase(it);}}
void PlayerController::open(QString url) { openMedia(std::move(url),true); }
void PlayerController::openMedia(QString url,bool includePlaylist) {
    url=url.trimmed();if(url.isEmpty())return;
    const QUrl input(url);const bool local=input.isLocalFile() || QFileInfo::exists(url);
    if(!scriptEnabled() && !local && !supportedUrl(url.toStdString())){error_="请输入有效媒体 URL 或本地文件路径";emit changed();return;}
    stop();scriptPlan_=scriptEnabled() && !local;error_.clear();title_=url;recoveryAttempts_=0;locatePlaylist_=includePlaylist;
    // A new URL owns a new script list, including an empty result. Keep the
    // existing collection only when replaying one of its logical entries.
    if(scriptPlan_ && includePlaylist){playlist_.clear();notifyPlaylistChanged(true);}
    ++key_.session;++key_.generation;hostSession_=std::make_shared<HostSession>();hostSession_->key=key_;mediaCancel_=std::make_shared<Cancellation>();
    transition(State::Resolving);auto key=key_;
    QUrl parsed(url);if(local || !scriptEnabled()){
        if(local){addLocalFile(parsed.isLocalFile()?parsed.toLocalFile():url);url=QUrl::fromLocalFile(QFileInfo(parsed.isLocalFile()?parsed.toLocalFile():url).absoluteFilePath()).toString();}
        playlist_=localPlaylist_;for(auto& entry:playlist_){auto item=entry.toMap();item["current"]=item["url"].toString()==url;entry=item;}
        notifyPlaylistChanged(locatePlaylist_);
        PlaybackPlan p;p.key=key;p.originalUrl=url.toStdString();p.defaultUrl=p.originalUrl;
        p.title=(local?QFileInfo(QUrl(url).toLocalFile()).fileName():url).toStdString();installPlan(std::move(p));return;
    }
    browser_->syncCookies([this,key,url,includePlaylist]{if(key!=key_)return;hostSession_->epoch=http_.snapshot().epoch;resolve(key,url,includePlaylist);});
}
void PlayerController::appendDebug(QString message) {
    // Keep the interactive log in memory; redact credentials before display.
    message.replace(QRegularExpression("https?://[^\\s]+"),"[URL]");
    message.replace(QRegularExpression("(?i)(cookie|authorization|sessdata|bili_jct|access[_-]?token|refresh[_-]?token)([\\s\"']*[:=][\\s\"']*)[^\\s,;\"']+"),"\\1=[redacted]");
    debugLines_.append(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ")+message.left(4096));
    while(debugLines_.size()>500)debugLines_.removeFirst();emit debugLogChanged();
}
void PlayerController::saveLocalPlaylist(){
    settings_->values["localPlaylist"]=QJsonArray::fromVariantList(localPlaylist_);
    try{settings_->save();}catch(const std::exception& e){error_=QString::fromUtf8(e.what());emit changed();}
}
void PlayerController::addLocalFile(QString path){
    QFileInfo info(path);if(!info.isFile())return;
    const auto url=QUrl::fromLocalFile(info.absoluteFilePath()).toString();
    for(const auto& entry:localPlaylist_)if(entry.toMap()["url"].toString()==url)return;
    localPlaylist_.append(QVariantMap{{"url",url},{"title",info.fileName()},{"current",false}});saveLocalPlaylist();
}
void PlayerController::openFiles(QVariantList paths){
    if(paths.isEmpty())return;
    for(const auto& path:paths){auto url=path.toUrl();addLocalFile(url.isLocalFile()?url.toLocalFile():path.toString());}
    open(paths.front().toString());
}
void PlayerController::removePlaylistItem(int index){
    if(index<0 || index>=playlist_.size())return;
    auto url=playlist_[index].toMap()["url"].toString();playlist_.removeAt(index);
    for(int i=localPlaylist_.size()-1;i>=0;--i)if(localPlaylist_[i].toMap()["url"].toString()==url)localPlaylist_.removeAt(i);
    saveLocalPlaylist();notifyPlaylistChanged();emit changed();
}
void PlayerController::clearPlaylist(){playlist_.clear();localPlaylist_.clear();saveLocalPlaylist();notifyPlaylistChanged();emit changed();}
void PlayerController::notifyPlaylistChanged(bool locateCurrent){emit playlistAboutToChange(locateCurrent);emit playlistChanged();}
qint64 PlayerController::openInNewInstance(QString url){
    url=url.trimmed();if(url.isEmpty())return 0;
    QStringList arguments{"--data-dir",settings_->directory,"--open",url};
    // Validation instances must exit on their own, including children launched
    // through real playlist gestures. Ordinary launches inherit no test flags.
    if(validationMode_)arguments << "--smoke-test" << "--smoke-seconds" << "5";
    if(QCoreApplication::arguments().contains("--direct-network"))arguments << "--direct-network";
    QProcess process;process.setProgram(QCoreApplication::applicationFilePath());process.setArguments(arguments);
    qint64 pid=0;
    if(!process.startDetached(&pid)){error_="无法打开新播放器实例："+process.errorString();emit changed();return 0;}
    emit instanceOpened(pid,url);return pid;
}
void PlayerController::openPlaylistItem(int index) {
    if(index<0 || index>=playlist_.size())return;
    auto saved=playlist_;const auto url=saved[index].toMap().value("url").toString();
    if(url.isEmpty())return;
    // Re-run PlayitemParse for the selected logical page, retaining this list.
    // Re-parsing the entire collection could choose its first entry instead.
    openMedia(url,false);
    playlist_=saved;
    for(int i=0;i<playlist_.size();++i){auto item=playlist_[i].toMap();item["current"]=i==index;playlist_[i]=item;}
    notifyPlaylistChanged();
    emit changed();
}
void PlayerController::resolve(SessionKey key,QString url,bool includePlaylist) {
    ++resolveSerial_;
    diagnostics_["resolveStage"]="脚本编译";emit changed();
    auto session=hostSession_;auto media=settings_->values["mediaScript"].toString();auto stats=settings_->values["statisticsEnabled"].toBool()?settings_->values["statisticsScript"].toString():QString{};
    QMetaObject::invokeMethod(scriptWorker_,[this,key,url,includePlaylist,session,media,stats]{
        try {
            session->cancel->check();if(!runtime_)runtime_=std::make_unique<ScriptRuntime>(hostServices());else runtime_->close();
            runtime_->load(media,stats);runtime_->initialize(session);
            QMetaObject::invokeMethod(this,[this,key]{if(key==key_){diagnostics_["resolveStage"]="脚本解析";emit changed();}});
            auto account=QString::fromUtf8(runtime_->stringFunction("GetWebAccountUrl"));auto config=QString::fromUtf8(runtime_->stringFunction("GetConfigFile"));
            auto plan=runtime_->resolve(key,url.toUtf8().toStdString(),includePlaylist);
            QMetaObject::invokeMethod(this,[this,key,account,config,plan=std::move(plan)]() mutable {if(key!=key_)return;accountUrl_=account;configPath_=config;installPlan(std::move(plan));});
        }catch(const Error& e){QMetaObject::invokeMethod(this,[this,key,code=QString::fromStdString(e.code),text=QString::fromUtf8(e.what())]{fail(key,code,text);});}
        catch(const std::exception& e){QMetaObject::invokeMethod(this,[this,key,text=QString::fromUtf8(e.what())]{fail(key,"ScriptRuntime",text);});}
    });
}
void PlayerController::installPlan(PlaybackPlan plan) {
    if(plan.key!=key_)return;
    plan_=std::move(plan);title_=QString::fromUtf8(plan_->title);qualities_.clear();subtitles_.clear();emit mediaInformationChanged();
    if(!plan_->playlist.empty())playlist_.clear();
    updateChoices();
    for(const auto& p:plan_->playlist) {
        auto thumbnail=QString::fromUtf8(p.thumbnail);if(thumbnail.startsWith("//"))thumbnail.prepend("https:");
        if(!thumbnail.isEmpty() && !supportedUrl(thumbnail.toStdString()))thumbnail.clear();
        auto item=displayMap(p.information);
        item["title"]=QString::fromUtf8(p.title);item["url"]=QString::fromUtf8(p.url);
        item["thumbnail"]=thumbnail;item["author"]=QString::fromUtf8(p.author);item["current"]=p.current;
        playlist_.push_back(item);
    }
    // The item parser may have richer metadata than the collection parser.
    // Fill missing fields of the playing entry without changing list order/count.
    const auto information=displayMap(plan_->information);
    for(auto& entry:playlist_){auto item=entry.toMap();if(item["current"].toBool() || item["url"].toString()==QString::fromUtf8(plan_->originalUrl)){
        for(auto it=information.cbegin();it!=information.cend();++it)if(!item.contains(it.key()) || item[it.key()].toString().isEmpty())item[it.key()]=it.value();
        entry=item;
    }}
    notifyPlaylistChanged(locatePlaylist_);
    subtitles_.push_back(QVariantMap{{"id",""},{"label","关闭"}});
    for(const auto& s:plan_->subtitles)subtitles_.push_back(QVariantMap{{"id",QString::fromStdString(s.id)},{"label",QString::fromUtf8(s.label.empty()?s.language:s.label)}});
    selectedQuality_.clear();selectedAudio_.clear();for(const auto& c:plan_->candidates)if(c.url==plan_->defaultUrl)selectedQuality_=QString::fromStdString(c.id);
    browser_->setChat(scriptEnabled()?plan_->chat:std::nullopt);
    // Select text tracks before opening the graph. Inline fileContent is ready
    // immediately, so the first rendered frame already has both subtitle slots.
    QString main,secondary;
    for(const auto& source:plan_->subtitles){
        const auto id=QString::fromStdString(source.id);
        if(source.label=="弹幕"){if(secondary.isEmpty())secondary=id;}
        else if(main.isEmpty())main=id;
    }
    if(secondary.isEmpty())for(const auto& source:plan_->subtitles){auto id=QString::fromStdString(source.id);if(id!=main){secondary=id;break;}}
    setSubtitle(0,main);setSubtitle(1,secondary);
    if(plan_->imageOnly){fail(key_,"ImageOnly","脚本返回了图片，当前媒体链路没有音视频流");return;}
    transition(State::Opening);buildGraph(selectedQuality_,selectedAudio_);
}
void PlayerController::updateChoices() {
    qualities_.clear();if(!plan_)return;
    for(const auto& c:plan_->candidates){auto label=QString::fromUtf8(c.quality+" "+c.detail+" "+c.resolution+" "+c.format).simplified();
        auto audioLabel=QString::fromUtf8(c.audioName+" "+c.audioCode).simplified();if(audioLabel.isEmpty())audioLabel=label;
        const bool audio=c.role==StreamRole::Audio || c.role==StreamRole::Unknown && (!c.audioName.empty() || !c.audioCode.empty() || c.audioDefault);
        auto detail=QString::fromUtf8(c.detail.empty()?c.quality:c.detail);
        const auto format=QString::fromUtf8(c.format);
        // The pinned script often sets qualityDetail == quality and supplies
        // FPS/container/codec/bitrate separately, as PotPlayer's menu expects.
        if(!format.isEmpty() && !detail.contains(format)) {
            if(c.fps>0 && c.fps<=1000 && !audio)detail+=QString(", %1F").arg(c.fps,0,'g',6);
            detail+=QString(" (%1, %2)").arg(audio?"audio":c.role==StreamRole::Muxed?"video+audio":"video",format);
        }
        qualities_.push_back(QVariantMap{{"id",QString::fromStdString(c.id)},{"quality",QString::fromUtf8(c.quality)},{"qualityDetail",detail},{"rawQualityDetail",QString::fromUtf8(c.detail)},{"label",label},{"audio",audio},{"audioLabel",audioLabel},{"bitrateBps",qlonglong(c.bitrateBps)}});
    }
}
void PlayerController::releaseGraph(GraphSession*& graph){if(!graph)return;auto* released=graph;graph=nullptr;disconnect(released,nullptr,this,nullptr);QMetaObject::invokeMethod(released,[released]{delete released;});}
void PlayerController::buildGraph(QString quality,QString audio,bool switching,bool restoring,QString switchError) {
    if(!plan_)return;releaseGraph(pendingGraph_);
    // MPCVR rejects a second instance with E_ABORT. Destruction and the next
    // open are queued on the same STA, so the old renderer is gone first.
    if(switching)releaseGraph(activeGraph_);
    auto plan=*plan_;auto video=plan.defaultUrl;std::string audioUrl;
    for(const auto& c:plan.candidates){if(QString::fromStdString(c.id)==quality)video=c.url;if(QString::fromStdString(c.id)==audio)audioUrl=c.url;}
    double liveBase=0;
    if(plan.live){
        if(audioUrl.empty())for(const auto& candidate:plan.candidates)if(candidate.url==video && candidate.role==StreamRole::Video){if(auto* best=preferredAudioCandidate(plan.candidates)){audioUrl=best->url;audio=QString::fromStdString(best->id);}break;}
        if(!liveCache_ || video!=liveVideo_ || audioUrl!=liveAudio_){
            liveCache_.reset();canSeek_=false;position_=duration_=0;liveTarget_=-1;emit progressChanged();emit changed();
            const auto executable=settings_->values["ffmpegPath"].toString();
            if(!QFileInfo::exists(executable)){fail(key_,"LiveCache","直播回看需要 FFmpeg，请在设置中选择 FFmpeg 程序");return;}
            if(!http_.cookieHeader(QUrl(QString::fromStdString(video)),http_.snapshot().epoch).empty() || !audioUrl.empty() && !http_.cookieHeader(QUrl(QString::fromStdString(audioUrl)),http_.snapshot().epoch).empty()){
                fail(key_,"LiveCache","直播媒体地址要求 Cookie，当前缓存组件不支持此地址");return;
            }
            liveVideo_=video;liveAudio_=audioUrl;liveCache_=std::make_unique<LiveTimeshift>();auto* cache=liveCache_.get();
            connect(cache,&LiveTimeshift::available,this,[this,quality,audio,switching,restoring,switchError]{buildGraph(quality,audio,switching,restoring,switchError);});
            connect(cache,&LiveTimeshift::rangeChanged,this,[this]{
                if(!liveCache_)return;duration_=liveCache_->endTime();
                diagnostics_["liveCacheStart"]=seekStart();diagnostics_["liveCacheEnd"]=seekEnd();diagnostics_["liveCacheBytes"]=liveCache_->bytes();
                emit progressChanged();
                if(activeGraph_ && canSeek_ && position_<seekStart() && (state_==State::Playing || state_==State::Paused))seek(seekStart());
            });
            connect(cache,&LiveTimeshift::failed,this,[this](QString message){fail(key_,"LiveCache",message);});
            connect(cache,&LiveTimeshift::expired,this,[this]{QTimer::singleShot(0,this,[this]{if(activeGraph_ && canSeek_ && liveCache_ && position_<seekStart())seek(seekStart());});});
            try{cache->start(executable,QString::fromStdString(video),QString::fromStdString(audioUrl),http_.policyFor(video),http_.policyFor(audioUrl));}
            catch(const std::exception& e){fail(key_,"LiveCache",QString::fromUtf8(e.what()));}
            return;
        }
        if(!liveCache_->ready())return;
        try{auto playback=liveCache_->playback(liveTarget_>=0?liveTarget_:liveCache_->endTime());liveBase=playback.base;video=playback.url.toStdString();audioUrl.clear();liveTarget_=-1;}
        catch(const std::exception& e){fail(key_,"LiveCache",QString::fromUtf8(e.what()));return;}
    }
    auto* graph=new GraphSession(&http_);graph->moveToThread(&graphThread_);pendingGraph_=graph;
    auto paths=filterPaths();auto target=HWND(videoHost_->winId());auto cancel=mediaCancel_;auto restore=switching && !plan.live?MediaTime(int64_t(position_*10000000)):MediaTime{};bool paused=switching || wasPaused_;
    connect(graph,&GraphSession::candidateRoles,this,[this,graph](SessionKey key,QVariantMap roles){if(key!=key_ || !plan_ || graph!=pendingGraph_ && graph!=activeGraph_)return;for(auto& c:plan_->candidates){auto id=QString::fromStdString(c.id);if(roles.contains(id))c.role=StreamRole(roles[id].toInt());}updateChoices();emit changed();});
    connect(graph,&GraphSession::opened,this,[this,graph,quality,audio,switching,restoring,switchError,liveBase,source=video](SessionKey key,bool seekable,QVariantMap diag){
        if(key!=key_ || graph!=pendingGraph_)return;releaseGraph(activeGraph_);activeGraph_=graph;pendingGraph_=nullptr;canSeek_=liveCache_?liveCache_->ready():seekable;diagnostics_=diag;selectedQuality_=quality;selectedAudio_=liveCache_?audio:diag.value("selectedAudioId",audio).toString();
        if(liveCache_){position_=liveBase;duration_=liveCache_->endTime();diagnostics_["liveCache"]=true;emit progressChanged();}
        seekOrigin_.reset();
        playbackRate_=diag.value("playbackRate",1.0).toDouble();rateChanging_=false;
        diagnostics_["graphOpenSerial"]=qulonglong(++graphOpenSerial_);diagnostics_["selectedQualityId"]=quality;diagnostics_["resolveSerial"]=qulonglong(resolveSerial_);
        if(diag.contains("embeddedSubtitle")){bool known=false;for(auto entry:subtitles_)if(entry.toMap()["id"]=="embedded:0")known=true;if(!known)subtitles_.push_back(QVariantMap{{"id","embedded:0"},{"label",diag["embeddedSubtitle"]}});}
        if(!switching && slots_[0].id.isEmpty() && diag.contains("embeddedSubtitle"))setSubtitle(0,"embedded:0");
        error_=restoring?QString("切换失败，已恢复原流：%1").arg(switchError):QString{};transition(wasPaused_?State::Paused:State::Playing);applyVolume();applySlots();
        if(switching && !wasPaused_){QMetaObject::invokeMethod(graph,[graph]{graph->pause(false);});}
        if(!switching){queueStatistics("PlaybackOpen");queueStatistics("PlaybackStart");}
        auto history=settings_->values["history"].toArray();history.prepend(QString::fromStdString(plan_->originalUrl));while(history.size()>50)history.removeLast();settings_->values["history"]=history;
        try{settings_->save();}catch(const std::exception& e){error_=QString::fromUtf8(e.what());}emit changed();
        // Direct media URLs have no script live flag. A non-seekable HTTP source
        // without a duration also needs the same application-owned replay cache.
        if(!validationMode_ && !isLive() && !seekable && diag.value("sourceDuration").toDouble()<=0 && QUrl(QString::fromStdString(source)).scheme().startsWith("http")){
            QTimer::singleShot(0,this,[this,graph,key]{if(key!=key_ || activeGraph_!=graph || !plan_ || state_!=State::Playing && state_!=State::Paused)return;plan_->live=true;transition(State::Switching);buildGraph(selectedQuality_,selectedAudio_,true);});
        }
    });
    connect(graph,&GraphSession::failed,this,[this,graph,switching,restoring](SessionKey key,QString code,QString message){
        if(key!=key_ || graph!=pendingGraph_ && graph!=activeGraph_)return;
        if(switching && !restoring && graph==pendingGraph_ && code!="Cancelled") {
            releaseGraph(pendingGraph_);buildGraph(selectedQuality_,selectedAudio_,true,true,message);return;
        }
        fail(key,code,message);
    });
    connect(graph,&GraphSession::position,this,[this,graph,liveBase](SessionKey key,double p,double d){
        if(key!=key_ || graph!=activeGraph_ || state_==State::Seeking)return;
        const double next=liveCache_?liveBase+p:p;
        position_=liveCache_ && state_==State::Playing ? std::max(position_,next) : next;
        const double previousDuration=duration_;
        duration_=liveCache_?liveCache_->endTime():d;emit progressChanged();
        if(plan_ && !plan_->live && !plan_->information.isMember("duration") && duration_!=previousDuration)emit mediaInformationChanged();
        if(int(p)!=lastStatsSecond_ && state_==State::Playing){lastStatsSecond_=int(p);queueStatistics("PlaybackTime");}
    });
    connect(graph,&GraphSession::completed,this,[this,graph](SessionKey key){
        if(key!=key_ || graph!=activeGraph_ || state_!=State::Playing)return;
        transition(State::Completed);queueStatistics("PlaybackComplete");
        if(!isLive() && canSeek_ && settings_->values["loopMode"].toString()=="one")seek(0);
    });
    connect(graph,&GraphSession::subtitleDiagnostics,this,[this,graph](SessionKey key,uint64_t requested,uint64_t visible,uint64_t primary,uint64_t secondary){if(key!=key_ || graph!=activeGraph_)return;diagnostics_["subtitleFrameRequests"]=qulonglong(requested);diagnostics_["subtitleFramesWithText"]=qulonglong(visible);diagnostics_["primarySubtitleFrames"]=qulonglong(primary);diagnostics_["secondarySubtitleFrames"]=qulonglong(secondary);});
    connect(graph,&GraphSession::videoDiagnostics,this,[this,graph](SessionKey key,QVariantMap values){if(key!=key_ || graph!=activeGraph_)return;for(auto i=values.begin();i!=values.end();++i)diagnostics_[i.key()]=i.value();});
    connect(graph,&GraphSession::subtitleWarning,this,[this,graph](SessionKey key,QString message){if(key!=key_ || graph!=activeGraph_ && graph!=pendingGraph_)return;error_=message;diagnostics_["subtitleError"]=message;emit changed();});
    connect(graph,&GraphSession::seekFinished,this,[this,graph](SessionKey key,bool success){
        if(key!=key_ || graph!=activeGraph_ || state_!=State::Seeking)return;
        if(!success && seekOrigin_)position_=*seekOrigin_;
        seekOrigin_.reset();emit progressChanged();
        transition(!success && seekFromEnd_?State::Completed:wasPaused_?State::Paused:State::Playing);
        if(success && seekFromEnd_)queueStatistics("PlaybackStart");
        if(!success){error_="进度跳转失败，已恢复原位置";emit changed();}
        seekFromEnd_=false;
    });
    QMetaObject::invokeMethod(graph,[graph,plan=std::move(plan),video,audioUrl,paths,target,cancel,restore,paused,rate=playbackRate_]{graph->open(plan,video,audioUrl,paths,target,cancel,restore,paused,rate);});
    if(switching)emit changed();
}
void PlayerController::fail(SessionKey key,QString code,QString message) {
    if(key!=key_ || code=="Cancelled")return;
    if(scriptPlan_ && plan_ && (!canSeek_ || plan_->live) && activeGraph_ && code=="MediaGraph" && recoveryAttempts_<3) {
        auto url=QString::fromStdString(plan_->originalUrl);int delay=1000 << recoveryAttempts_++;
        releaseGraph(pendingGraph_);releaseGraph(activeGraph_);liveCache_.reset();if(hostSession_)hostSession_->cancel->cancel();if(mediaCancel_)mediaCancel_->cancel();
        ++key_.generation;auto retryKey=key_;hostSession_=std::make_shared<HostSession>();hostSession_->key=key_;mediaCancel_=std::make_shared<Cancellation>();
        transition(State::Reconnecting);detail_=QString("直播连接中断，%1 秒后重新解析（%2/3）").arg(delay/1000).arg(recoveryAttempts_);emit changed();
        QTimer::singleShot(delay,this,[this,retryKey,url]{if(retryKey!=key_)return;browser_->syncCookies([this,retryKey,url]{if(retryKey!=key_)return;hostSession_->epoch=http_.snapshot().epoch;transition(State::Resolving);resolve(retryKey,url,false);});});return;
    }
    if(scriptPlan_)appendDebug(code+": "+message);
    if(seekOrigin_){position_=*seekOrigin_;seekOrigin_.reset();emit progressChanged();}
    error_=message;diagnostics_["lastErrorCode"]=code;releaseGraph(pendingGraph_);releaseGraph(activeGraph_);canSeek_=false;rateChanging_=false;
    if(liveCache_)QTimer::singleShot(0,this,[this,key]{if(key==key_ && state_==State::Error)liveCache_.reset();});
    if(state_!=State::Idle && state_!=State::Error)transition(State::Error);emit changed();
}
void PlayerController::stop() {
    auto oldPath=plan_?plan_->defaultUrl:std::string{};bool hadMedia=scriptPlan_ && activeGraph_!=nullptr;
    ++key_.generation;if(hostSession_)hostSession_->cancel->cancel();if(mediaCancel_)mediaCancel_->cancel();browser_->closeChat();
    for(auto& promise:messages_)try{promise->set_value(3);}catch(...){}messages_.clear();emit scriptMessagesCancelled();
    releaseGraph(pendingGraph_);releaseGraph(activeGraph_);
    liveCache_.reset();liveTarget_=-1;
    QMetaObject::invokeMethod(scriptWorker_,[this,hadMedia,oldPath]{if(runtime_){if(hadMedia)runtime_->finishStatistics(oldPath);else runtime_->close();}});
    if(state_!=State::Idle){transition(State::Closing);transition(State::Idle);}plan_.reset();emit mediaInformationChanged();qualities_.clear();subtitles_.clear();slots_={};externalSubtitles_.clear();scriptPlan_=false;emit subtitleSelectionChanged();
    wasPaused_=false;seekFromEnd_=false;seekOrigin_.reset();rateChanging_=false;canSeek_=false;position_=duration_=0;lastStatsSecond_=-1;emit changed();emit progressChanged();
}
void PlayerController::togglePause() {
    if(state_==State::Completed){seek(0);return;}
    if(!activeGraph_ || (state_!=State::Playing && state_!=State::Paused))return;
    bool paused=state_==State::Playing;auto* graph=activeGraph_;auto key=key_;
    QMetaObject::invokeMethod(graph,[this,graph,key,paused]{
        if(!graph->pause(paused))return;
        QMetaObject::invokeMethod(this,[this,graph,key,paused]{
            if(key!=key_ || graph!=activeGraph_ || state_!=State::Playing && state_!=State::Paused)return;
            wasPaused_=paused;transition(paused?State::Paused:State::Playing);
            queueStatistics(paused?"PlaybackPause":"PlaybackResume");
        });
    });
}
void PlayerController::seek(double seconds) {
    if(!activeGraph_ || !canSeek_ || !std::isfinite(seconds) || state_!=State::Playing && state_!=State::Paused && state_!=State::Completed)return;
    seekFromEnd_=state_==State::Completed;
    if(seekFromEnd_)wasPaused_=false;
    seekOrigin_=position_;
    position_=std::clamp(seconds,seekStart(),std::max(seekStart(),seekEnd()));emit progressChanged();
    transition(State::Seeking);
    if(liveCache_){liveTarget_=std::clamp(seconds,seekStart(),seekEnd());buildGraph(selectedQuality_,selectedAudio_,true);return;}
    auto* graph=activeGraph_;auto pos=MediaTime(int64_t(std::clamp(seconds,0.0,duration_)*10000000));
    QMetaObject::invokeMethod(graph,[graph,pos,ended=seekFromEnd_]{if(graph->seek(pos) && ended)graph->pause(false);});
}
void PlayerController::returnToLive(){if(!liveCache_ || !canSeek_ || state_!=State::Playing && state_!=State::Paused)return;wasPaused_=false;seek(seekEnd());}
void PlayerController::focusInputWindow(QWindow* window) {
    if(!window || !window->isVisible())return;
    // QML activeFocus alone need not move Windows keyboard focus back from a
    // DirectShow child HWND. The text item remains the Qt focus object.
    window->requestActivate();SetFocus(HWND(window->winId()));
}
void PlayerController::adjustPlaybackRate(double step){if(std::isfinite(step))setPlaybackRate(std::clamp(std::round((playbackRate_+step)*100)/100,0.25,4.0));}
void PlayerController::setPlaybackRate(double rate) {
    if(!canChangePlaybackRate() || !std::isfinite(rate) || rate<0.25 || rate>4.0 || rate==playbackRate_)return;
    auto* graph=activeGraph_;const auto key=key_;rateChanging_=true;emit changed();
    QMetaObject::invokeMethod(graph,[this,graph,key,rate]{
        const bool success=graph->setPlaybackRate(rate);
        QMetaObject::invokeMethod(this,[this,graph,key,rate,success]{
            if(key!=key_ || graph!=activeGraph_)return;
            rateChanging_=false;
            if(success){playbackRate_=rate;diagnostics_["playbackRate"]=rate;}
            else error_="当前媒体无法以该速度播放，已保留原速度";
            emit changed();
        });
    });
}
void PlayerController::applyVolume(){if(activeGraph_){auto* graph=activeGraph_;int gain=settings_->values["muted"].toBool()?0:settings_->values["volume"].toInt(75);QMetaObject::invokeMethod(graph,[graph,gain]{graph->volume(gain);});}}
void PlayerController::setVolume(int percent){settings_->values["volume"]=std::clamp(percent,0,100);settings_->values["muted"]=false;applyVolume();emit settingsChanged();}
void PlayerController::toggleMute(){settings_->values["muted"]=!settings_->values["muted"].toBool();applyVolume();emit settingsChanged();}
QString PlayerController::nativePath(QString path) const {
    if(path.startsWith("file:",Qt::CaseInsensitive))path=QUrl(path).toLocalFile();
    return path.isEmpty()?QString{}:QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path)));
}
void PlayerController::openPath(QString path) {
    QFileInfo info(nativePath(path));
    if(path.isEmpty() || !info.exists()){error_="路径不存在，请先选择或填写有效路径";emit changed();return;}
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir()?info.absoluteFilePath():info.absolutePath()));
}
void PlayerController::selectQuality(QString id){if(!plan_ || pendingGraph_)return;if(state_==State::Error){transition(State::Idle);transition(State::Resolving);transition(State::Opening);buildGraph(id,selectedAudio_);return;}if(state_!=State::Playing && state_!=State::Paused)return;transition(State::Switching);buildGraph(id,selectedAudio_,true);}
void PlayerController::selectAudio(QString id){if(!plan_ || pendingGraph_)return;if(state_==State::Error){selectedAudio_=id;selectQuality(selectedQuality_);return;}if(state_!=State::Playing && state_!=State::Paused)return;transition(State::Switching);buildGraph(selectedQuality_,id,true);}
void PlayerController::saveWindowSize(int width,int height){
    width=std::clamp(width,620,16384);height=std::clamp(height,480,16384);
    if(settings_->values["windowWidth"].toInt()==width && settings_->values["windowHeight"].toInt()==height)return;
    settings_->values["windowWidth"]=width;settings_->values["windowHeight"]=height;
    try{settings_->save();}catch(const std::exception& e){error_=QString::fromUtf8(e.what());emit changed();}
    emit settingsChanged();
}
bool PlayerController::nativeEventFilter(const QByteArray&,void* message,qintptr*) {
    auto* msg=static_cast<MSG*>(message);
    // Do not request a window handle while Qt is creating/reparenting native
    // windows: winId() can re-enter this filter during handle creation.
    if(msg->message!=WM_SIZING && msg->message!=WM_EXITSIZEMOVE)return false;
    auto* window=videoHost_->parent();
    if(!window || msg->hwnd!=HWND(window->winId()))return false;
    if(msg->message==WM_SIZING && !nativeResizeActive_){
        static const int edges[]={0,Qt::LeftEdge,Qt::RightEdge,Qt::TopEdge,Qt::LeftEdge|Qt::TopEdge,Qt::RightEdge|Qt::TopEdge,Qt::BottomEdge,Qt::LeftEdge|Qt::BottomEdge,Qt::RightEdge|Qt::BottomEdge};
        if(msg->wParam>=1 && msg->wParam<=8){nativeResizeActive_=true;emit windowResizeStarted(edges[msg->wParam]);}
    }else if(msg->message==WM_EXITSIZEMOVE){nativeResizeActive_=false;emit windowResizeFinished();}
    return false;
}
void PlayerController::setSetting(QString key,QVariant value) {
    static const QStringList permitted={"mediaScript","statisticsScript","statisticsEnabled","lavDirectory","rendererPath","audioRendererPath","bfrcPath","bfrcEnabled","chatVisible","chatWidth","viewMode","theme","themePath","playlistItemHeight","scriptEnabled","mainSubtitleVisible","secondarySubtitleVisible","ffmpegPath","loopMode"};
    if(!permitted.contains(key))return;if(key=="chatWidth")value=std::clamp(value.toInt(),240,960);
    if(key=="playlistItemHeight")value=std::clamp(value.toInt(),56,180);
    if(pathSettings.contains(key))value=nativePath(value.toString());
    if(key=="viewMode" && value.toString()!="videoOnly" && value.toString()!="videoAndPanel")return;
    if(key=="loopMode" && value.toString()!="none" && value.toString()!="one")return;
    if(settings_->values.value(key)==QJsonValue::fromVariant(value))return;
    if(key=="scriptEnabled" && scriptEnabled()!=value.toBool()){
        if(scriptPlan_)stop();
        if(!value.toBool()){
            if(accountCancel_)accountCancel_->cancel();lastAccountCheck_=0;
            if(hostSession_)hostSession_->cancel->cancel();
            browser_->closeChat();playlist_=localPlaylist_;
            QMetaObject::invokeMethod(scriptWorker_,[this]{if(runtime_)runtime_->close();});
        }
    }
    settings_->values[key]=QJsonValue::fromVariant(value);
    try{settings_->save();}catch(const std::exception& e){error_=QString::fromUtf8(e.what());emit changed();}emit settingsChanged();
    if(key=="theme" || key=="themePath")emit themeChanged();
    if(key=="scriptEnabled"){notifyPlaylistChanged();emit changed();if(scriptEnabled())refreshAccount();}
    if(key=="mainSubtitleVisible" || key=="secondarySubtitleVisible")applySlots(key=="mainSubtitleVisible"?0:1);
    if(key=="bfrcEnabled" && activeGraph_ && !pendingGraph_ && (state_==State::Playing || state_==State::Paused)){transition(State::Switching);buildGraph(selectedQuality_,selectedAudio_,true);}
}
void PlayerController::applySlots(int onlySlot){if(!activeGraph_)return;for(int i=0;i<2;++i){if(onlySlot>=0 && i!=onlySlot)continue;auto* graph=activeGraph_;auto slot=slots_[i];if(!(i==0?mainSubtitleVisible():secondarySubtitleVisible())){slot.content.clear();slot.id.clear();}QMetaObject::invokeMethod(graph,[graph,i,slot]{graph->setSubtitle(i,slot.content,slot.offset,slot.danmaku,slot.id=="embedded:0");});}}
void PlayerController::selectSubtitle(int slot,QString id){if(slot<0 || slot>1)return;setSubtitle(slot,id,slots_[slot].offset/1000.0);}
void PlayerController::setSubtitle(int slot,QString id,double offsetSeconds,bool danmaku) {
    if(slot<0 || slot>1 || !std::isfinite(offsetSeconds) || std::abs(offsetSeconds)>86400)return;
    slots_[slot].offset=int64_t(offsetSeconds*1000);
    bool isDanmaku=false;if(plan_)for(const auto& source:plan_->subtitles)if(QString::fromStdString(source.id)==id)isDanmaku=source.label=="弹幕";
    slots_[slot].danmaku=slot==1 && (danmaku || isDanmaku);
    if(id.isEmpty()){slots_[slot].id.clear();slots_[slot].content.clear();applySlots();emit subtitleSelectionChanged();return;}
    if(slots_[slot].id==id){applySlots();emit subtitleSelectionChanged();return;}
    if(id=="embedded:0"){
        for(int i=0;i<2;++i)if(i!=slot && slots_[i].id==id){slots_[i].id.clear();slots_[i].content.clear();}
        slots_[slot].id=id;slots_[slot].content.clear();applySlots();emit subtitleSelectionChanged();return;
    }
    if(externalSubtitles_.contains(id)){slots_[slot].id=id;slots_[slot].content=externalSubtitles_[id];applySlots();emit subtitleSelectionChanged();return;}
    if(!plan_)return;
    for(const auto& source:plan_->subtitles)if(QString::fromStdString(source.id)==id){
        slots_[slot].id=id;slots_[slot].content=QByteArray::fromStdString(source.content);applySlots();emit subtitleSelectionChanged();
        if(!source.content.empty())return;
        auto key=key_;auto cancel=mediaCancel_;auto epoch=http_.snapshot().epoch;
        QMetaObject::invokeMethod(scriptWorker_,[this,key,source,id,slot,cancel,epoch]{try{
            HttpRequest request;request.url=source.url;request.cancel=cancel;request.epoch=epoch;auto response=http_.request(request);
            if(!response.error.empty() || response.status<200 || response.status>=300)throw Error("SubtitleDecode","远程字幕获取失败");
            auto bytes=QByteArray::fromStdString(response.body);
            QMetaObject::invokeMethod(this,[this,key,id,slot,bytes]{if(key==key_ && slots_[slot].id==id){slots_[slot].content=bytes;applySlots();}});
        }catch(const Error& e){QMetaObject::invokeMethod(this,[this,key,text=QString::fromUtf8(e.what())]{if(key==key_){error_=text;emit changed();}});}});return;
    }
}
void PlayerController::loadSubtitle(int slot,QUrl path){
    if(slot<0 || slot>1)return;QFile file(path.toLocalFile());
    if(!file.open(QIODevice::ReadOnly) || file.size()>32*1024*1024){error_="字幕文件无法读取或超过 32 MB";emit changed();return;}
    auto id="external:"+path.toString();externalSubtitles_[id]=file.readAll();
    bool known=false;for(const auto& entry:subtitles_)if(entry.toMap()["id"].toString()==id)known=true;
    if(!known)subtitles_.append(QVariantMap{{"id",id},{"label",QFileInfo(file).fileName()}});
    selectSubtitle(slot,id);emit changed();
}
void PlayerController::adjustSubtitle(int slot,double seconds,bool danmaku){if(slot<0 || slot>1 || !std::isfinite(seconds) || std::abs(seconds)>86400)return;slots_[slot].offset=int64_t(seconds*1000);slots_[slot].danmaku=slot==1 && danmaku;applySlots();}
void PlayerController::queueStatistics(const char* event) {
    if(!scriptEnabled() || !scriptPlan_ || !settings_->values["statisticsEnabled"].toBool() || !hostSession_ || !plan_)return;
    bool time=std::string(event)=="PlaybackTime";if(time && timeQueued_.exchange(true))return;
    auto session=hostSession_;auto path=plan_->defaultUrl;for(const auto& c:plan_->candidates)if(QString::fromStdString(c.id)==selectedQuality_)path=c.url;
    auto position=int(position_),duration=int(duration_);QMetaObject::invokeMethod(scriptWorker_,[this,session,path,event=std::string(event),position,duration,time]{try{if(runtime_ && runtime_->session()==session && !session->cancel->cancelled)runtime_->statistics(event.c_str(),path,position,duration);}catch(...){}if(time)timeQueued_=false;});
}
void PlayerController::login() {
    if(!scriptEnabled())return;
    if(!accountUrl_.isEmpty()){browser_->login(accountUrl_);return;}
    if(state_!=State::Idle && state_!=State::Error)return;
    auto media=settings_->values["mediaScript"].toString();auto session=std::make_shared<HostSession>();hostSession_=session;
    QMetaObject::invokeMethod(scriptWorker_,[this,media,session]{try{if(!runtime_)runtime_=std::make_unique<ScriptRuntime>(hostServices());runtime_->load(media);runtime_->initialize(session);auto url=QString::fromUtf8(runtime_->stringFunction("GetWebAccountUrl"));auto config=QString::fromUtf8(runtime_->stringFunction("GetConfigFile"));QMetaObject::invokeMethod(this,[this,session,url,config]{if(!scriptEnabled() || hostSession_!=session || session->cancel->cancelled)return;accountUrl_=url;configPath_=config;browser_->login(url);});}catch(const Error& e){QMetaObject::invokeMethod(this,[this,session,text=QString::fromUtf8(e.what())]{if(hostSession_!=session || session->cancel->cancelled)return;error_=text;emit changed();});}});
}
void PlayerController::refreshAccount() {
    if(!scriptEnabled() || validationMode_ || accountCheckPending_ || QDateTime::currentMSecsSinceEpoch()-lastAccountCheck_<10000)return;
    accountCheckPending_=true;lastAccountCheck_=QDateTime::currentMSecsSinceEpoch();
    browser_->syncCookies([this]{
        if(!scriptEnabled()){accountCheckPending_=false;return;}
        auto epoch=http_.snapshot().epoch;accountCancel_=std::make_shared<Cancellation>();auto cancel=accountCancel_;
        QMetaObject::invokeMethod(scriptWorker_,[this,epoch,cancel]{
            bool authenticated=false,known=false;
            try{HttpRequest request;request.url="https://api.bilibili.com/x/web-interface/nav";request.epoch=epoch;request.cancel=cancel;request.timeoutMs=5000;request.limit=256*1024;
                auto response=http_.request(request);auto object=QJsonDocument::fromJson(QByteArray::fromStdString(response.body)).object();
                auto login=object.value("data").toObject().value("isLogin");
                known=response.status==200 && response.error.empty() && ((object.value("code").toInt(-1)==0 && login.isBool()) || object.value("code").toInt()==-101);
                authenticated=known && object.value("code").toInt(-1)==0 && login.toBool();
            }catch(...){}
            QMetaObject::invokeMethod(this,[this,epoch,known,authenticated]{accountCheckPending_=false;if(!scriptEnabled() || epoch!=http_.snapshot().epoch || !known)return;loggedIn_=authenticated;emit accountStatusChanged();});
        });
    });
}
void PlayerController::logout(){stop();loggedIn_=false;emit accountStatusChanged();if(accountCancel_)accountCancel_->cancel();browser_->logout([this]{detail_="已退出登录";emit changed();});}
void PlayerController::openScriptConfig(){if(!configPath_.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(configPath_));else {error_="请先加载解析脚本";emit changed();}}
QVariantMap PlayerController::exportDiagnostics() {
    auto report=QJsonObject::fromVariantMap(diagnostics_);
    report["applicationVersion"]="0.1.0";
    report["scriptEngine"]=ANGELSCRIPT_VERSION_STRING;
    report["state"]=state();
    QSaveFile file(settings_->directory+"/diagnostics.json");
    const auto bytes=QJsonDocument(report).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        return {{"ok",false},{"error","诊断导出失败："+file.errorString()}};
    const auto path=nativePath(file.fileName());
    detail_="诊断已保存到 "+path;
    emit changed();
    return {{"ok",true},{"path",path}};
}
QVariantMap PlayerController::theme() const {
    bool light=settings_->values["theme"]=="light";QVariantMap result{{"background",light?"#f4f5f9":"#0d1018"},{"panel",light?"#ffffff":"#171c28"},{"raised",light?"#e7eaf2":"#222939"},{"text",light?"#202639":"#edf0fa"},{"muted",light?"#68738a":"#98a4bd"},{"accent","#8b7aff"},{"font","Microsoft YaHei UI"},{"viewMode","videoAndPanel"}};
    auto path=settings_->values["themePath"].toString();if(!path.isEmpty()){QFile file(path);if(file.open(QIODevice::ReadOnly) && file.size()<65536){auto values=QJsonDocument::fromJson(file.readAll()).object();for(auto it=result.begin();it!=result.end();++it)if(values[it.key()].isString()){auto text=values[it.key()].toString();if(it.key()=="viewMode"){if(text=="videoOnly" || text=="videoAndPanel")it.value()=text;}else if(it.key()=="font" && text.size()<128 || QColor(text).isValid())it.value()=text;}}}return result;
}
}
